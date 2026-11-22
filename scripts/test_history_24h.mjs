// ============================================================================
// TEST STORICO 24H/1440 CAMPIONI - SENZA HARDWARE
//
// Verifica il formato compatto, la rotazione oraria, le condizioni di
// registrazione NTP, i limiti dell'archivio e il rendering a 1/6/12/24 ore.
// ============================================================================

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { runInNewContext } from "node:vm";

const header = readFileSync("include/history_store.h", "utf8");
const store = readFileSync("src/history_store.cpp", "utf8");
const main = readFileSync("src/main.cpp", "utf8");
const ui = readFileSync("data/app.js", "utf8");
const html = readFileSync("data/index.html", "utf8");

for(const phrase of [
  "HISTORY_WINDOW_MINUTES = 1440UL",
  "HISTORY_SLOT_COUNT = 26UL",
  "static_assert(sizeof(HistoryRecord)==22",
  "sample.minuteUtc*60UL",
  "historyChecksum(sample)",
  "HISTORY_MIN_EPOCH",
  "historyStoreAppend",
  "historyStoreSendJson",
  "LittleFS.open(path,\"a\")",
  'server.sendContent(buffer)',
  'server.sendContent("]")'
]) {
  assert.ok(store.includes(phrase), "Archivio persistente incompleto: " + phrase);
}

assert.ok(main.includes('static const char* VERSION="0.3.7"'));
assert.ok(main.includes('historyStoreSendJson(web'));
assert.ok(main.includes('historyStoreAppend(sample)'));
assert.ok(main.includes('cfg.historyMs<60000UL'));
assert.ok(main.includes('historyClockSynchronized(epoch)'));
assert.ok(!main.includes("history[120]"), "Non aumentare RAM di 1.440 campioni.");
assert.ok(!main.includes("historyHead"), "Rimuovere il vecchio ring buffer RAM.");

// Il periodo di 24h interessa 25 distinti file orari al massimo;
// modulo 26, nessun file necessario viene riutilizzato nella finestra.
for(let hour=100000;hour<100000+100;hour++) {
  const slots = new Set();
  for(let offset=0;offset<=24;offset++) {
    slots.add((hour-offset)%26);
  }
  assert.equal(slots.size,25,"Collisione dei file all'interno delle 24h.");
}

const begin = ui.indexOf("function historyVisibleRows(");
const end = ui.indexOf("function renderHistoryCharts(", begin);
assert.ok(begin>=0 && end>begin,"Filtro della finestra temporale assente.");
const script = [
  "const HISTORY_VIEW_HOURS=[1,6,12,24];",
  ui.slice(begin,end),
  "globalThis.testHistoryVisibleRows=historyVisibleRows;"
].join("\n");

const sandbox={};
runInNewContext(script,sandbox);
const visible=sandbox.testHistoryVisibleRows;

// Un record ogni minuto su 24h esatte, incluso il campione corrente.
const now=1795406400;
const samples=[];
for(let i=1439;i>=0;i--) {
  samples.push({
    s:now-i*60,
    pv:i,
    load:i+20,
    battery:-i,
    t:20.2,
    h:48.5,
    known:true,
    on:false
  });
}

const day=visible(samples,24,now);
assert.equal(day.length,1442,"1440 campioni + due estremi grafici.");
assert.equal(day[0].s,now-86400,"La finestra inizia esattamente 24h prima.");
assert.equal(day[day.length-1].s,now,"La finestra termina ora.");
assert.equal(day[0].pv,null,"Gli estremi non devono inventare misure.");
assert.equal(day[1].pv,1439);
assert.equal(day[day.length-2].pv,0);

for(const hours of [1,6,12,24]) {
  const view=visible(samples,hours,now);
  const points=view.filter(function(row) {
    return Number.isFinite(row.pv);
  });
  assert.equal(points.length,hours*60,
    "Punti errati per finestra di " + hours + " ore.");
}

const empty=visible([],24,now);
assert.equal(empty.length,2,"Anche senza misure, asse stabile di 24h.");
assert.equal(empty[0].s,now-86400);
assert.equal(empty[1].s,now);

assert.ok(html.includes('id="historyRange"'));
assert.ok(html.includes('value="24" selected'));
assert.ok(html.includes('id="historyStorageStatus"'));
assert.ok(ui.includes('Date.now()-historyLastFetchAt>=60000'),
  "Non scaricare 1440 campioni ogni 15 secondi.");
assert.ok(ui.includes('row.s-previousSecond>125'),
  "Gli intervalli privi di campioni non devono essere interpolati.");

console.log("Storico 24h: 1440 campioni/minuto, rotazione, finestre e limiti RAM OK.");
