// ============================================================================
// REGRESSIONI MQTT 0.3.6
//
// Protegge la gestione frammenti PUBLISH, le conferme SUBACK, la modalita'
// di diagnosi wildcard opzionale e la visibilita' dei risultati nella UI.
// ============================================================================

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const main = readFileSync("src/main.cpp", "utf8");
const defaults = readFileSync("src/config.cpp", "utf8");
const app = readFileSync("data/app.js", "utf8");
const index = readFileSync("data/index.html", "utf8");
const diagnose = readFileSync("scripts/diagnose_mqtt.py", "utf8");

assert.match(main, /static const char\* VERSION="0\.3\.[0-9]+"/,
  "I test MQTT devono restare validi dopo i normali aggiornamenti firmware.");

for(const expected of [
  "mqtt.onSubscribe(mqttSubscriptionAcknowledged)",
  "mqttSubscriptions[i].packetId=packetId",
  "mqttSubscriptions[i].status=2",
  "mqttSubscriptions[i].status=3",
  'doc["status"]="confermata"',
  'doc["status"]="rifiutata"',
  'o["mqttReceived"]=mqttReceived',
  'o["mqttMatched"]=mqttMatched',
  'o["mqttAccepted"]=mqttAccepted',
  'o["mqttFragmented"]=mqttFragmented',
  'o["mqttOversized"]=mqttOversized',
  'o["mqttInvalid"]=mqttInvalid'
]) {
  assert.ok(main.includes(expected), "Diagnostica MQTT incompleta: " + expected);
}

// Il parser deve completare il PUBLISH prima di convertire il payload.
assert.ok(
  main.includes("if(mqttValueBytes<total) return;"),
  "Il payload non deve essere elaborato prima di essere completo."
);
assert.ok(
  main.includes("mqttValueBuffer[total]=0;"),
  "Il payload completo deve essere terminato prima di strtof."
);
assert.ok(
  main.includes("if(len>0) memcpy(mqttValueBuffer+index,payload,len);"),
  "I frammenti devono essere collocati nel buffer alla posizione index."
);
assert.ok(
  !main.includes("if(index||len!=total||total==0||total>127)"),
  "Il controllo precedente scartava tutti i PUBLISH frammentati."
);
assert.ok(
  main.includes("if(properties.retain) return;"),
  "I messaggi retained non devono autorizzare automazioni."
);
assert.ok(
  main.includes("if(topicIndex<0)"),
  "I topic diagnostici sconosciuti non devono essere interpretati."
);
assert.ok(
  main.includes('web.sendContent("],\\\"subscriptions\\\":[");'),
  "L'API diagnostica deve restituire l'array subscriptions JSON."
);

// Il profilo noto disattiva soltanto il wildcard, non le sottoscrizioni
// energetiche e nemmeno la possibilita' di riattivarlo manualmente.
assert.ok(defaults.includes('target.discovery = "";'));
assert.ok(app.includes('updated.discovery = "";'));
assert.ok(main.includes("if(topic->length()==0) continue;"));
assert.ok(index.includes('id="mqttSubscriptions"'));
assert.ok(index.includes('id="mqttSummary"'));
assert.ok(app.includes('subscription.status === "confermata"'));
assert.ok(diagnose.includes("def show_subscriptions("));

console.log("MQTT 0.3.6: sottoscrizioni, buffer e diagnostica verificati.");
