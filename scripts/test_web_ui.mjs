// ============================================================================
// REGRESSIONE STRUTTURALE DELLA DASHBOARD A SCHEDE
//
// Nessuna libreria esterna richiesta: si controlla che l'interfaccia conservi
// gli ID necessari a tutte le funzioni preesistenti e che ciascuna scheda abbia
// un pulsante accessibile e un pannello corrispondente.
// ============================================================================
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const html = readFileSync("data/index.html", "utf8");
const js = readFileSync("data/app.js", "utf8");
const css = readFileSync("data/style.css", "utf8");
const updateHtml = readFileSync("data/update.html", "utf8");
const firmware = readFileSync("src/main.cpp", "utf8");

// Tutti gli ID utilizzati dalla dashboard prima della divisione in schede.
const requiredIds = [
  "connection", "notice", "pv", "load", "battery", "batteryDetail",
  "available", "mode", "temperature", "humidity", "target",
  "humidityHint", "climate", "reason", "energyChart", "envChart",
  "export", "settings", "fields", "clearMqtt", "reboot",
  "confirmOff", "confirmOn", "actualState", "irOn", "irOff",
  "capture", "saveOn", "saveOff", "irCapture", "learnState",
  "refreshDiagnostics", "exportLogs", "topics", "events", "system",
  "version"
];

// Nuovi elementi dedicati all'acquisizione IR: l'aggiunta non deve
// compromettere gli ID originali (verificati separatamente).
const receiverIds = ["irReceiverStatus", "irCaptureSummary"];

const tabIds = [
  "overview",
  "charts",
  "settings",
  "infrared",
  "diagnostics"
];

const foundIds = [];
const idMatcher = /\bid="([^"]+)"/g;
let match;

while((match = idMatcher.exec(html)) !== null) {
  foundIds.push(match[1]);
}

const distinctIds = new Set(foundIds);
assert.equal(distinctIds.size, foundIds.length, "Gli ID HTML devono essere univoci.");

for(const id of requiredIds) {
  assert.ok(distinctIds.has(id), "Controllo originale mancante: " + id);
}

for(const id of receiverIds) {
  assert.ok(distinctIds.has(id), "Indicatore ricevitore IR mancante: " + id);
}

assert.ok(
  html.includes("G</strong>") && html.includes("R</strong>") && html.includes("Y</strong>"),
  "La scheda IR deve riportare le etichette G/R/Y del modulo."
);
assert.ok(
  js.includes("async function readIrCapture()"),
  "Il riepilogo della cattura IR deve restare disponibile."
);

for(const tabId of tabIds) {
  const buttonId = "tab-button-" + tabId;
  const panelId = "tab-" + tabId;

  assert.ok(distinctIds.has(buttonId), "Pulsante scheda assente: " + buttonId);
  assert.ok(distinctIds.has(panelId), "Pannello scheda assente: " + panelId);
  assert.ok(
    html.includes('data-tab="' + tabId + '"'),
    "Attributo navigazione non trovato: " + tabId
  );
  assert.ok(
    html.includes('aria-controls="' + panelId + '"'),
    "Associazione ARIA mancante: " + panelId
  );
  assert.ok(
    html.includes('aria-labelledby="' + buttonId + '"'),
    "Nome accessibile pannello mancante: " + panelId
  );
}

for(const requiredFunction of [
  "function activateTab(",
  "function initializeTabs(",
  "function renderHistoryCharts(",
  "function drawEnvironmentChart(",
  "function fields()"
]) {
  assert.ok(js.includes(requiredFunction), "Funzione JavaScript mancante: " + requiredFunction);
}

assert.ok(
  css.includes(".tab-panel[hidden]"),
  "La regola per nascondere i pannelli non attivi e' necessaria."
);

assert.ok(
  js.includes('button.addEventListener("keydown", moveTabByKeyboard)'),
  "La navigazione da tastiera deve restare disponibile."
);



// ============================================================================
// OTA: entrambe le strade di aggiornamento devono chiedere e registrare OFF.
// La condizione rimane verificata anche lato firmware, prima della scrittura.
// ============================================================================
assert.ok(
  updateHtml.includes("async function confirmDeviceOffForUpdate("),
  "Manca la conferma di sicurezza sulla pagina OTA."
);
assert.ok(
  updateHtml.includes('fetch("/api/update-confirm-off"'),
  "La conferma non viene inviata al dispositivo."
);
assert.ok(
  updateHtml.includes("const confirmed=await confirmDeviceOffForUpdate(state);"),
  "Le azioni OTA devono essere condizionate alla conferma OFF."
);
const confirmCallCount = updateHtml.split(
  "const confirmed=await confirmDeviceOffForUpdate(state);"
).length - 1;
assert.equal(
  confirmCallCount,
  2,
  "Sia GitHub OTA sia il caricamento .bin devono richiedere conferma."
);
assert.ok(
  firmware.includes('web.on("/api/update-confirm-off",HTTP_POST'),
  "Endpoint di conferma fisica OFF assente nel firmware."
);
assert.ok(
  firmware.includes('if(!infrared.known || infrared.on)'),
  "Il firmware deve mantenere il controllo dello stato fisico prima del GitHub OTA."
);

console.log(
  "Test UI superato: " + requiredIds.length +
  " controlli preservati e " + tabIds.length + " schede disponibili."
);
