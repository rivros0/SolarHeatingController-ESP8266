// ============================================================================
// REGRESSIONE DELLA DISTRIBUZIONE OTA DI FIRMWARE E DASHBOARD
//
// Questo test non sostituisce le prove hardware o HTTPS: controlla le
// invarianti strutturali dei percorsi e la sintassi JavaScript della pagina
// incorporata nel firmware. Non richiede toolchain ESP8266.
// ============================================================================
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { Script } from "node:vm";
import { createHash } from "node:crypto";

const cpp = readFileSync("src/github_ota.cpp", "utf8");
const main = readFileSync("src/main.cpp", "utf8");
const header = readFileSync("include/github_ota.h", "utf8");
const update = readFileSync("data/update.html", "utf8");
const workflow = readFileSync(".github/workflows/firmware-release.yml", "utf8");

const webFiles = [
  "index.html",
  "app.js",
  "style.css"
];

for(const filename of webFiles) {
  const payload = readFileSync("data/" + filename);

  // La dashboard deve contenere almeno un byte e rispettare il massimo
  // previsto dal controllo del manifest integrato nel firmware.
  assert.ok(payload.length > 0, "Asset web vuoto: " + filename);
  assert.ok(payload.length <= 131072, "Asset web troppo grande: " + filename);

  const digest = createHash("sha256").update(payload).digest("hex");
  assert.equal(digest.length, 64, "SHA256 non valido: " + filename);

  assert.ok(
    cpp.includes('"' + filename + '"'),
    "File web assente dalla whitelist OTA: " + filename
  );
  assert.ok(
    workflow.includes("web-" + filename),
    "File web non pubblicato nella release: " + filename
  );
  assert.ok(
    workflow.includes('release["web"][filename]'),
    "Manifest web SHA256 assente dal workflow."
  );
}

assert.ok(
  cpp.includes("bool githubWebInstall("),
  "Installazione selettiva dashboard assente."
);
assert.ok(
  cpp.includes("webFileMatches("),
  "Il firmware deve confrontare il checksum dei file locali."
);
assert.ok(
  cpp.includes("downloadVerifiedWebAsset("),
  "Il firmware deve verificare il download prima dell'installazione."
);
assert.ok(
  cpp.includes("LittleFS.rename(WEB_TEMP[index], WEB_LOCAL[index])"),
  "Un file web verificato deve essere applicato mediante rename."
);
assert.ok(
  cpp.includes("githubWebCleanup()"),
  "Il firmware deve poter ripulire i file temporanei OTA."
);
assert.ok(
  !cpp.includes("LittleFS.format("),
  "Gli aggiornamenti OTA non devono formattare LittleFS."
);
assert.ok(
  !cpp.includes("Update.begin(firmware.size,U_FS)"),
  "OTA web non deve riscrivere la partizione LittleFS."
);
assert.ok(
  header.includes("bool webAvailable = false"),
  "Il manifest deve distinguere firmware e dashboard."
);
assert.ok(
  main.includes('response["firmwareAvailable"]'),
  "GET OTA deve esporre la disponibilita' firmware."
);
assert.ok(
  main.includes('response["webAvailable"]'),
  "GET OTA deve esporre la disponibilita' web."
);
assert.ok(
  main.includes("if(info.webAvailable)"),
  "POST OTA deve aggiornare la web UI anche a firmware invariato."
);
assert.ok(
  main.includes("if(info.available)"),
  "POST OTA deve conservare la selezione del firmware."
);
assert.ok(
  main.includes('if(!infrared.known || infrared.on)'),
  "Conferma fisica OFF obbligatoria lato firmware."
);

// Estraiamo lo script della pagina /update e lo compiliamo con Node.js.
// Rileva errori di sintassi anche quando il firmware ESP8266 compila.
const match = update.match(/<script>([\s\S]*?)<\/script>/);
assert.ok(match, "Script della pagina di aggiornamento non disponibile.");
new Script(match[1], { filename: "data/update.html" });

assert.ok(
  update.includes("info.webAvailable"),
  "La pagina OTA deve indicare quando e' disponibile una dashboard nuova."
);
assert.ok(
  update.includes("info.webInstalled"),
  "La pagina OTA deve confermare l'installazione effettiva della dashboard."
);
assert.ok(
  update.includes("confirmDeviceOffForUpdate(state)"),
  "Il popup di conferma fisica OFF deve rimanere operativo."
);

console.log(
  "OTA web: whitelist asset, verifica SHA256, pubblicazione release, " +
  "protezione configurazioni e sintassi pagina OK."
);
