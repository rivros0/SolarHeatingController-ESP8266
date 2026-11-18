// ============================================================================
// TEST DELL'ASSE X CON ORARIO LOCALE (NODE.JS, SENZA HARDWARE)
//
// La dashboard non usa librerie di grafici esterne. Questo test esegue le
// funzioni di conversione dei tempi e verifica che le etichette siano
// distribuite correttamente anche su display di larghezza ridotta.
// ============================================================================
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { runInNewContext } from "node:vm";

const source = readFileSync("data/app.js", "utf8");
const begin = source.indexOf("function axisTimestampMs(");
const end = source.indexOf("// GRAFICO ENERGETICO", begin);

assert.ok(begin >= 0, "Conversione temporale assente.");
assert.ok(end > begin, "Sezione delle funzioni temporali incompleta.");

// Le funzioni sono pure rispetto a DOM e rete; testiamo solo la sezione
// responsabile di secondi dall'avvio, formattazione e posizioni dell'asse.
const sourceBlock = source.slice(begin, end);
const referenceTime = new Date(2026, 10, 18, 21, 0, 0).getTime();

const sandbox = {
  historyTimeAnchor: {
    wallMs: referenceTime,
    uptimeSec: 7200
  }
};

runInNewContext(
  sourceBlock +
  "\n globalThis.testAxis = {" +
  " axisTimestampMs, formatChartTime, drawChartTimeAxis };",
  sandbox
);

const axis = sandbox.testAxis;

assert.equal(
  axis.axisTimestampMs(3600),
  referenceTime - 3600000,
  "La conversione deve usare la differenza tra uptime e campione."
);

assert.equal(
  axis.formatChartTime(3600, 3600),
  "20:00",
  "Su intervalli ordinari sono necessari orari HH:mm."
);

assert.equal(
  axis.formatChartTime(7190, 10),
  "20:59:50",
  "Su intervalli brevi devono comparire i secondi."
);

// Simulazione minima del contesto Canvas, senza richiedere un browser.
function recordingContext() {
  const labels = [];

  return {
    labels,
    save() {},
    restore() {},
    beginPath() {},
    moveTo() {},
    lineTo() {},
    stroke() {},
    fillText(text, x, y) {
      labels.push({ text, x, y });
    }
  };
}

const rows = [{ s: 3600 }, { s: 5400 }, { s: 7200 }];
const desktop = recordingContext();
axis.drawChartTimeAxis(desktop, rows, 40, 740, 20, 150, 175);

assert.equal(desktop.labels.length, 5, "Desktop: cinque etichette.");
assert.equal(desktop.labels[0].text, "20:00", "Etichetta iniziale.");
assert.equal(desktop.labels[4].text, "21:00", "Etichetta finale.");
assert.equal(desktop.labels[0].x, 40, "Primo tick sul bordo sinistro.");
assert.equal(desktop.labels[4].x, 740, "Ultimo tick sul bordo destro.");

const mobile = recordingContext();
axis.drawChartTimeAxis(mobile, rows, 40, 280, 20, 150, 175);
assert.equal(mobile.labels.length, 2, "Mobile: evitare sovrapposizioni.");
assert.equal(mobile.labels[0].text, "20:00", "Inizio conservato su mobile.");
assert.equal(mobile.labels[1].text, "21:00", "Fine conservata su mobile.");

sandbox.historyTimeAnchor = null;
assert.equal(
  axis.formatChartTime(3600, 3600),
  "T+01:00:00",
  "Senza ancora temporale deve comparire il tempo dall'avvio."
);

const html = readFileSync("data/index.html", "utf8");
assert.ok(
  html.includes("orario locale ricostruito"),
  "La dashboard deve indicare che gli orari sono ricostruiti."
);

console.log("Asse temporale: conversione, etichette desktop/mobile e fallback OK.");
