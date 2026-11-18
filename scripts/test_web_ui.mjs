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

console.log(
  "Test UI superato: " + requiredIds.length +
  " controlli preservati e " + tabIds.length + " schede disponibili."
);
