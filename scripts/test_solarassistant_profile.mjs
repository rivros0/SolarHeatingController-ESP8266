// ============================================================================
// REGRESSIONI: PROFILO MQTT SOLARASSISTANT DOMESTICO
//
// Verifica le sette associazioni osservate sul broker reale e preserva
// credenziali, impostazioni IR, altri parametri e automazione disabilitata.
// ============================================================================

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { runInNewContext } from "node:vm";

const js = readFileSync("data/app.js", "utf8");
const cpp = readFileSync("src/config.cpp", "utf8");
const html = readFileSync("data/index.html", "utf8");

const actualTopics = {
  pv: "solar_assistant/inverter_1/pv_power/state",
  load: "solar_assistant/inverter_1/load_power/state",
  battery: "solar_assistant/total/battery_power/state",
  voltage: "solar_assistant/battery_1/voltage/state",
  current: "solar_assistant/battery_1/current/state",
  soc: "solar_assistant/total/battery_state_of_charge/state",
  import: "",
  export: "",
  inverter: "solar_assistant/inverter_1/device_mode/state"
};

// La funzione di preparazione del profilo e' indipendente da DOM e rete.
const begin = js.indexOf("function knownSolarAssistantConfig(");
const end = js.indexOf('$("applySolarAssistantTopics").onclick', begin);

assert.ok(begin >= 0, "Applicazione del profilo MQTT mancante.");
assert.ok(end > begin, "Funzione JS del profilo MQTT incompleta.");

const code = js.slice(begin, end);
const createProfile = runInNewContext(
  code + "\n knownSolarAssistantConfig;",
  { structuredClone }
);

const existing = {
  mqttHost: "vecchio-broker",
  mqttPort: 1884,
  discovery: "un-altro-topic/#",
  mqttUser: "utente-preesistente",
  mqttPassword: "segreto-non-deve-cambiare",
  ssid: "rete-esistente",
  wifiPassword: "credenziale-wifi",
  adminPassword: "password-web-conservata",
  staticIp: "192.168.1.242",
  receiver: true,
  irVerified: true,
  automationEnabled: true,
  setpoint: 24,
  energy: {
    negativeCharge: true,
    negativeCurrentCharge: true,
    reserveW: 650,
    humidityAssist: true
  },
  topics: {
    pv: "vecchio/pv",
    import: "vecchio/import",
    export: "vecchio/export"
  }
};

const updated = createProfile(existing);

assert.equal(updated.mqttHost, "192.168.1.240");
assert.equal(updated.mqttPort, 1883);
// Il filtro wildcard e' opzionale e provoca traffico inutile sui NodeMCU.
// Il profilo domestico lo disattiva, ma il campo resta configurabile.
assert.equal(updated.discovery, "");

for(const [key, expected] of Object.entries(actualTopics)) {
  assert.equal(updated.topics[key], expected, "Topic errato per " + key);
}

assert.equal(updated.energy.negativeCharge, false);
assert.equal(updated.energy.negativeCurrentCharge, false);
assert.equal(updated.automationEnabled, false);

for(const key of [
  "mqttUser", "mqttPassword", "ssid", "wifiPassword",
  "adminPassword", "staticIp", "receiver", "irVerified", "setpoint"
]) {
  assert.equal(updated[key], existing[key], "Parametro alterato: " + key);
}

assert.equal(updated.energy.reserveW, 650);
assert.equal(updated.energy.humidityAssist, true);
assert.equal(existing.mqttHost, "vecchio-broker");
assert.equal(existing.automationEnabled, true);

for(const topic of Object.values(actualTopics)) {
  if(topic.length === 0) {
    continue;
  }
  assert.ok(cpp.includes('"' + topic + '"'), "Default C++ mancante: " + topic);
}

// Un upgrade non deve sovrascrivere le impostazioni gia' salvate.
const loadedExistingConfig = cpp.indexOf(
  "if(!err && parseConfig(doc.as<JsonObjectConst>(),candidate,why))"
);
const defaultsCall = cpp.indexOf("setKnownSolarAssistantDefaults(cfg);");
assert.ok(loadedExistingConfig >= 0);
assert.ok(defaultsCall > loadedExistingConfig);
assert.ok(
  cpp.slice(loadedExistingConfig, defaultsCall).includes("return;"),
  "Le configurazioni esistenti devono prevalere sui nuovi default."
);

assert.ok(
  html.includes('id="applySolarAssistantTopics"'),
  "Pulsante del profilo assente dalla dashboard."
);
assert.ok(
  js.includes('confirmPhysicalOffForAction('),
  "La conferma fisica OFF deve restare obbligatoria."
);
assert.ok(
  js.includes('await api("/api/config", next)'),
  "Il profilo deve essere effettivamente salvato sul dispositivo."
);
assert.ok(
  js.includes("Premi «Riavvia e applica»"),
  "Occorre esplicitare il riavvio per ri-sottoscrivere i topic."
);

console.log("Profilo MQTT: topic verificati, credenziali conservate e migrazione sicura OK.");
