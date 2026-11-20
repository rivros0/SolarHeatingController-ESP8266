// ============================================================================
// TEST: CONFERME OFF CONTESTUALI IN TUTTA LA DASHBOARD
//
// Testa i tre casi della funzione popup senza un browser reale:
// - Annulla: nessuna chiamata POST, nessuna operazione.
// - OK: stato registrato tramite endpoint autenticato dall'helper api().
// - Risposta non valida: operazione rifiutata, mai considerata confermata.
// ============================================================================

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { runInNewContext } from "node:vm";

const app = readFileSync("data/app.js", "utf8");
const firmware = readFileSync("src/main.cpp", "utf8");
const update = readFileSync("data/update.html", "utf8");
const dashboard = readFileSync("data/index.html", "utf8");

const begin = app.indexOf("async function confirmPhysicalOffForAction(");
const end = app.indexOf("// Il valore false segnala", begin);
assert.ok(begin >= 0 && end > begin, "Funzione di conferma globale assente.");

const source = app.slice(begin, end);

function createCase(accepted, response) {
  const requests = [];
  const messages = [];
  const popups = [];
  const state = { commanded: "unknown" };

  const context = {
    state,
    notice(message) {
      messages.push(message);
    },
    window: {
      confirm(message) {
        popups.push(message);
        return accepted;
      }
    },
    async api(path, body) {
      requests.push({ path, body });
      return response;
    }
  };

  const action = runInNewContext(
    source + "\n confirmPhysicalOffForAction;",
    context
  );

  return { action, requests, messages, popups, state };
}

// L'annullamento non aggiorna lo stato fisico e non invia alcuna richiesta.
const canceled = createCase(false, { commanded: "off", confirmed: true });
assert.equal(await canceled.action("Riavvio"), false);
assert.equal(canceled.requests.length, 0);
assert.equal(canceled.state.commanded, "unknown");
assert.equal(canceled.popups.length, 1);
assert.ok(canceled.popups[0].includes("Riavvio"));

// Conferma esplicita: deve transitare per l'endpoint di attestazione.
const approved = createCase(true, { commanded: "off", confirmed: true });
assert.equal(await approved.action("Salva configurazione"), true);
assert.equal(approved.popups.length, 1);
assert.equal(approved.requests.length, 1);
assert.equal(approved.requests[0].path, "/api/confirm-physical-off");
assert.equal(approved.requests[0].body.confirmed, true);
assert.equal(approved.state.commanded, "off");
assert.ok(approved.popups[0].includes("FISICAMENTE SPENTO"));

// Risposte incoerenti non devono consentire un'operazione successiva.
const invalid = createCase(true, { commanded: "unknown", confirmed: false });
await assert.rejects(
  async function() {
    await invalid.action("Salva codici IR");
  },
  /non ha registrato/
);
assert.equal(invalid.state.commanded, "unknown");

// Copertura di tutti i flussi esistenti: nessuna scheda obbligatoria.
const requiredContexts = [
  "Salvare la configurazione del controller",
  "Riavviare il controller e applicare la configurazione",
  "Attivare la modalità ",
  "Salvare il codice infrarosso ",
  "Misurare il consumo di riferimento",
  "Salvare l'apprendimento"
];

for(const context of requiredContexts) {
  assert.ok(app.includes(context), "Operazione priva di popup: " + context);
}

assert.ok(
  app.includes('await api("/api/confirm-physical-off", {confirmed: true})'),
  "Conferma OFF globale non registrata dal dispositivo."
);
assert.ok(
  firmware.includes('web.on("/api/confirm-physical-off",HTTP_POST'),
  "L'endpoint di conferma OFF manca nel firmware."
);
assert.ok(
  firmware.includes('if(!document["confirmed"].is<bool>()'),
  "Il backend deve rifiutare le conferme non esplicite."
);
assert.ok(
  update.includes("confirmDeviceOffForUpdate(state)"),
  "La pagina OTA deve mantenere il popup esistente."
);
assert.ok(
  dashboard.includes("popup direttamente durante l'operazione"),
  "La pagina deve spiegare il nuovo comportamento all'utente."
);

console.log(
  "Popup OFF: annullamento, consenso, errore, contesti UI e controllo backend OK."
);
