#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <AsyncMqttClient.h>
#include <Adafruit_SHT31.h>
#include <Wire.h>
#include <Updater.h>
#include <memory>
#include "config.h"
#include "ir_manager.h"
#include "github_ota.h"
#include <time.h>
#include "environment_manager.h"
using namespace solar;
static const char* VERSION="0.3.1";
ESP8266WebServer web(80);
AsyncMqttClient mqtt;
Adafruit_SHT31 sht;
EnergyManager manager;
EnergyData energy;
EnvironmentManager environment;
PowerDetector detector;
Mode mode=Mode::ForceOff;
String reason="Avvio sicuro: Forza OFF, stato fisico sconosciuto",inverterStatus,csrf,lastActionReason="Nessun comando";
String lastRequestedMode="force_off";
float availableW=0;
bool sensorPresent=false,rebootNeeded=false,updateActive=false,updateOk=false,recoveryAP=false;
uint32_t forceAt=0,rebootAt=0,lastSensor=0,lastSecond=0,lastHistory=0,lastMqttAttempt=0;
uint32_t lastMqttAt=0,retainedIgnored=0,mqttRejected=0;

// Diagnostica seriale: controllo non bloccante e riepilogo ogni 30 secondi.
static const uint32_t SERIAL_POLL_MS = 1000UL;
static const uint32_t SERIAL_STATUS_MS = 30000UL;
uint32_t serialLastPollAt=0,serialLastStatusAt=0;
bool serialNetworkStateKnown=false,serialLastMqttConnected=false;
int serialLastWiFiStatus=-1;
struct Event { uint32_t at=0; String message; } events[16];
uint8_t eventHead=0,eventCount=0;
struct TopicSeen { String topic,value; uint32_t at=0; bool retained=false; } seen[12];
uint8_t seenHead=0;
struct History { uint32_t at; float pv,load,battery,t,h; uint8_t flags; } history[120];
uint8_t historyHead=0,historyCount=0;
struct Learning {
  String stage="idle"; uint32_t at=0,startup=0; float sum=0,squares=0;
  uint32_t count=0; float offMean=0,onMean=0,offAfter=0,noise=0;
  void resetWindow(uint32_t now) { at=now; sum=squares=0; count=0; }
} learning;
Sample* metric(int i) {
  switch(i) {case 0:return &energy.pv;case 1:return &energy.load;case 2:return &energy.battery;
    case 3:return &energy.voltage;case 4:return &energy.current;case 5:return &energy.soc;
    case 6:return &energy.importW;case 7:return &energy.exportW;default:return nullptr;}
}
// ============================================================================
// DIAGNOSTICA SERIALE
// I messaggi ROM precedenti a setup() sono a 74880 baud; il firmware usa 115200.
// Nessuna password Wi-Fi, amministratore o MQTT viene stampata nei riepiloghi.
// ============================================================================
const char* serialWiFiStatusName(int status) {
  switch(status) {
    case WL_CONNECTED: return "CONNECTED";
    case WL_NO_SSID_AVAIL: return "NO_SSID";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    case WL_IDLE_STATUS: return "IDLE";
    default: return "OTHER";
  }
}

const char* serialModeName() {
  switch(mode) {
    case Mode::Auto: return "AUTO";
    case Mode::ForceOn: return "FORCE_ON";
    case Mode::ForceOff: return "FORCE_OFF";
    case Mode::Diagnostic: return "DIAGNOSTIC";
    default: return "UNKNOWN";
  }
}

void serialPrintSample(const char* label,const Sample& sample,uint32_t now,const char* unit) {
  Serial.print(label);
  Serial.print('=');
  if(sample.fresh(now,cfg.energy.staleMs)) {
    Serial.print(sample.value,1);
    Serial.print(unit);
  } else {
    Serial.print(F("n/d"));
  }
}

void serialPrintStartup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(F("======================================================"));
  Serial.println(F(" SOLAR HEATING CONTROLLER - DIAGNOSTICA SERIALE"));
  Serial.println(F("======================================================"));
  Serial.print(F("[BOOT] Versione: ")); Serial.println(VERSION);
  Serial.print(F("[BOOT] Reset: ")); Serial.println(ESP.getResetReason());
  Serial.print(F("[BOOT] Dettaglio reset: ")); Serial.println(ESP.getResetInfo());
  Serial.print(F("[BOOT] Flash rilevata: "));
  Serial.print(ESP.getFlashChipRealSize()/1024UL); Serial.println(F(" KiB"));
  Serial.print(F("[BOOT] Firmware: "));
  Serial.print(ESP.getSketchSize()); Serial.println(F(" byte"));
  Serial.print(F("[BOOT] Heap disponibile: "));
  Serial.print(ESP.getFreeHeap()); Serial.println(F(" byte"));
  Serial.println(F("[BOOT] Caricamento configurazione LittleFS..."));
  Serial.flush();
}

void serialPrintStatus(uint32_t now) {
  Serial.print(F("[STATUS] uptime=")); Serial.print(now/1000UL);
  Serial.print(F("s heap=")); Serial.print(ESP.getFreeHeap());
  Serial.print(F("B fs="));
  if(storageReady) {
    Serial.print(F("OK"));
  } else {
    Serial.print(F("KO"));
  }
  Serial.print(F(" config="));
  if(configReady) {
    Serial.print(F("OK"));
  } else {
    Serial.print(F("KO"));
  }
  Serial.print(F(" mode=")); Serial.println(serialModeName());

  Serial.print(F("[WIFI] stato="));
  Serial.print(serialWiFiStatusName(WiFi.status()));
  if(WiFi.status()==WL_CONNECTED) {
    Serial.print(F(" ip=")); Serial.print(WiFi.localIP());
    Serial.print(F(" rssi=")); Serial.print(WiFi.RSSI());
    Serial.print(F("dBm"));
  }
  if(recoveryAP) {
    Serial.print(F(" recovery_ap=")); Serial.print(WiFi.softAPIP());
  }
  Serial.println();

  Serial.print(F("[MQTT] "));
  if(mqtt.connected()) {
    Serial.println(F("connesso"));
  } else {
    Serial.println(F("disconnesso"));
  }

  Serial.print(F("[SENSOR] SHT31="));
  if(sensorPresent) {
    Serial.print(F("rilevato "));
  } else {
    Serial.print(F("non rilevato "));
  }
  serialPrintSample("temperatura",environment.temperature,now,"C");
  Serial.print(' ');
  serialPrintSample("umidita",environment.humidity,now,"%");
  Serial.println();

  // Stato della nuova assistenza UR: informativo, non equivale a DRY.
  Serial.print(F("[HUMIDITY] assist="));
  if(cfg.energy.humidityAssist) {
    Serial.print(F("ON"));
  } else {
    Serial.print(F("OFF"));
  }
  Serial.print(F(" soglia_on="));
  Serial.print(cfg.energy.humidityOn,1);
  Serial.print(F("% soglia_off="));
  Serial.print(cfg.energy.humidityOff,1);
  Serial.print(F("% richiesta="));
  if(manager.humidityDemand()) {
    Serial.print(F("SI"));
  } else {
    Serial.print(F("NO"));
  }
  Serial.print(F(" ciclo_solo_UR="));
  if(manager.humidityOnlyRun()) {
    Serial.println(F("SI"));
  } else {
    Serial.println(F("NO"));
  }

  Serial.print(F("[ENERGY] "));
  serialPrintSample("PV",energy.pv,now,"W");
  Serial.print(' ');
  serialPrintSample("carichi",energy.load,now,"W");
  Serial.print(' ');
  serialPrintSample("batteria_raw",energy.battery,now,"W");
  Serial.print(' ');
  serialPrintSample("V_batteria",energy.voltage,now,"V");
  Serial.println();

  Serial.print(F("[IR] stato fisico="));
  if(!infrared.known) {
    Serial.println(F("sconosciuto"));
  } else if(infrared.on) {
    Serial.println(F("acceso (ultimo stato noto)"));
  } else {
    Serial.println(F("spento (ultimo stato noto)"));
  }
}

void serialDiagnosticsTick(uint32_t now) {
  // Una lettura al secondo evita di appesantire il controllo energetico.
  if(now-serialLastPollAt<SERIAL_POLL_MS) return;
  serialLastPollAt=now;

  int wifiStatus=WiFi.status();
  bool mqttConnected=mqtt.connected();
  if(!serialNetworkStateKnown || wifiStatus!=serialLastWiFiStatus) {
    Serial.print(F("[WIFI] Cambio stato: "));
    Serial.println(serialWiFiStatusName(wifiStatus));
    if(wifiStatus==WL_CONNECTED) {
      Serial.print(F("[WIFI] IP locale: "));
      Serial.println(WiFi.localIP());
    }
    serialLastWiFiStatus=wifiStatus;
  }
  if(!serialNetworkStateKnown || mqttConnected!=serialLastMqttConnected) {
    Serial.print(F("[MQTT] Cambio stato: "));
    if(mqttConnected) {
      Serial.println(F("connesso"));
    } else {
      Serial.println(F("disconnesso"));
    }
    serialLastMqttConnected=mqttConnected;
  }
  serialNetworkStateKnown=true;

  if(now-serialLastStatusAt>=SERIAL_STATUS_MS) {
    serialLastStatusAt=now;
    serialPrintStatus(now);
  }
}

void logEvent(const String& text) {
  // Manteniamo il registro circolare della dashboard e duplichiamo sulla USB.
  events[eventHead]={millis(),text.substring(0,160)};
  eventHead=(eventHead+1)%16;
  if(eventCount<16) eventCount++;
  Serial.print(F("[EVENT +"));
  Serial.print(millis()/1000UL);
  Serial.print(F("s] "));
  Serial.println(text);
}
bool authenticated() {
  if(!configReady) { web.send(503,"text/plain","Configurazione non disponibile: recupero USB necessario"); return false; }
  if(!web.authenticate(cfg.adminUser.c_str(),cfg.adminPassword.c_str())) { web.requestAuthentication(); return false; }
  return true;
}
bool mutation() {
  if(!authenticated()) return false;
  if(web.header("X-CSRF-Token")!=csrf) { web.send(403,"text/plain","Token CSRF non valido"); return false; }
  return true;
}
void jsonReply(JsonDocument& doc,int status=200) {
  if(doc.overflowed()) { web.send(503,"text/plain","Memoria JSON insufficiente"); return; }
  String out; serializeJson(doc,out); web.sendHeader("Cache-Control","no-store"); web.send(status,"application/json",out);
}
bool body(JsonDocument& doc) {
  if(web.arg("plain").length()>6144) { web.send(413,"text/plain","Corpo troppo grande"); return false; }
  if(deserializeJson(doc,web.arg("plain"))||!doc.is<JsonObject>()) { web.send(400,"text/plain","JSON non valido"); return false; }
  return true;
}
void clearTelemetry() {
  for(int i=0;i<8;i++) metric(i)->valid=false;
  inverterStatus=""; detector.invalidate(); manager.resetTimers();
}
void receiveMqtt(char* topic,char* payload,AsyncMqttClientMessageProperties properties,size_t len,size_t index,size_t total) {
  // Only bounded, single-packet scalar messages. Fragmented values are rejected
  // rather than interpreted partially. Retained payloads are NEVER live samples.
  if(index||len!=total||total==0||total>127) { mqttRejected++; return; }
  char value[128]; memcpy(value,payload,len); value[len]=0;
  for(size_t i=0;i<len;i++) if(value[i]=='\0') { mqttRejected++; return; }
  int slot=-1;
  for(int i=0;i<12;i++) if(seen[i].topic==topic) { slot=i; break; }
  if(slot<0) { slot=seenHead; seenHead=(seenHead+1)%12; }
  seen[slot]={String(topic).substring(0,128),String(value).substring(0,80),millis(),properties.retain};
  if(properties.retain) { retainedIgnored++; return; }
  for(int i=0;i<9;i++) {
    if(!cfg.topics[i].length()||cfg.topics[i]!=topic) continue;
    lastMqttAt=millis();
    if(i==8) { inverterStatus=value; return; }
    char* end=nullptr; float v=strtof(value,&end);
    while(end && (*end==' '||*end=='\r'||*end=='\n'||*end=='\t')) end++;
    bool valid=end!=value && end && *end==0 && isfinite(v);
    float lo=(i==2||i==4)?-100000:0,hi=(i==5?100:(i==3?200:100000));
    if(i==4) { lo=-1000; hi=1000; }
    valid &= v>=lo && v<=hi;
    *metric(i)={v,millis(),valid};
    if(!valid) mqttRejected++;
    return;
  }
}
bool command(bool on,const String& why,bool diagnostic=false,bool physical=false) {
  if(on && !environment.temperature.fresh(millis(),cfg.energy.staleMs)) { logEvent("ON rifiutato: sensore non valido"); return false; }
  if(!infrared.send(on,diagnostic,physical)) { logEvent(infrared.lastError); return false; }
  detector.command(on,millis()); manager.changed(on,millis()); reason=why; lastActionReason=why;
  logEvent(String(on?"IR ON: ":"IR OFF: ")+why); return true;
}
void stateRoute() {
  if(!authenticated()) return;
  DynamicJsonDocument doc(4096); JsonObject o=doc.to<JsonObject>(); uint32_t now=millis();
  o["version"]=VERSION; o["firmwareBoard"]=githubFirmwareBoard(); o["csrf"]=csrf; o["uptime"]=now/1000;
  o["wifi"]=WiFi.status()==WL_CONNECTED; o["mqtt"]=mqtt.connected();
  o["localIp"]=WiFi.localIP().toString(); o["staticIpEnabled"]=cfg.staticIp.length()>0;
  o["heap"]=ESP.getFreeHeap(); o["maxBlock"]=ESP.getMaxFreeBlockSize();
  o["otaSpace"]=ESP.getFreeSketchSpace(); o["storage"]=storageReady;
  o["resetReason"]=ESP.getResetReason(); o["lastMqttAgeMs"]=elapsed(now,lastMqttAt);
  o["humidityAssist"]=cfg.energy.humidityAssist;
  o["humidityDemand"]=manager.humidityDemand();
  o["humidityOnlyRun"]=manager.humidityOnlyRun();
  o["mode"]=mode==Mode::Auto?"auto":mode==Mode::ForceOn?"force_on":mode==Mode::ForceOff?"force_off":"diagnostic";
  o["commanded"]=infrared.known?(infrared.on?"on":"off"):"unknown";
  o["estimated"]=detector.estimated; o["confidence"]=detector.confidence;
  o["estimatedClimateW"]=detector.delta; o["baselineReady"]=detector.baselineReady;
  o["availableW"]=availableW; o["reason"]=reason;
  o["lastActionReason"]=lastActionReason; o["lastCommand"]=infrared.lastCommand; o["lastCommandAtMs"]=infrared.lastAt;
  o["irReady"]=infrared.automaticReady(); o["automationEnabled"]=cfg.automationEnabled;
  o["retainedIgnored"]=retainedIgnored; o["mqttRejected"]=mqttRejected;
  o["lastRequestedMode"]=lastRequestedMode; o["setpoint"]=cfg.setpoint; o["rebootNeeded"]=rebootNeeded;
  JsonObject m=o.createNestedObject("energy");
  for(int i=0;i<8;i++) {
    JsonObject v=m.createNestedObject(metricNames[i]); auto s=metric(i);
    if(s->fresh(now,cfg.energy.staleMs)) v["value"]=s->value; else v["value"]=nullptr;
    v["valid"]=s->fresh(now,cfg.energy.staleMs); v["ageMs"]=elapsed(now,s->at);
  }
  o["inverter"]=inverterStatus;
  if(environment.temperature.fresh(now,cfg.energy.staleMs)) o["temperature"]=environment.temperature.value;
  else o["temperature"]=nullptr;
  if(environment.humidity.fresh(now,cfg.energy.staleMs)) o["humidity"]=environment.humidity.value;
  else o["humidity"]=nullptr;
  JsonObject l=o.createNestedObject("learning"); l["stage"]=learning.stage;
  l["offW"]=learning.offMean; l["onW"]=learning.onMean; l["offAfterW"]=learning.offAfter;
  l["startupMs"]=learning.startup; l["noiseW"]=learning.noise;
  jsonReply(doc);
}
void learningSample(uint32_t now) {
  if(learning.stage!="baseline" && learning.stage!="running" && learning.stage!="stopping") return;
  if(!mqtt.connected()||!energy.load.fresh(now,cfg.energy.staleMs)) {
    learning.stage="error"; logEvent("Apprendimento annullato: dati carico assenti"); return;
  }
  float v=energy.load.value; uint32_t age=elapsed(now,learning.at);
  if(learning.stage=="running" && !learning.startup && v-learning.offMean>=cfg.detector.deltaW) learning.startup=age;
  if(learning.stage!="baseline" && age<cfg.detector.startupMs) return;
  learning.sum+=v; learning.squares+=v*v; learning.count++;
  uint32_t duration=learning.stage=="baseline"?cfg.detector.baselineMs:cfg.detector.startupMs+cfg.detector.observationMs;
  if(age<duration) return;
  float mean=learning.sum/learning.count;
  float noise=sqrtf(fmaxf(0,learning.squares/learning.count-mean*mean));
  learning.noise=fmaxf(learning.noise,noise);
  if(learning.stage=="baseline") { learning.offMean=mean; learning.stage="await_on"; }
  else if(learning.stage=="running") { learning.onMean=mean; learning.stage="await_off"; }
  else { learning.offAfter=mean; learning.stage="ready"; }
  logEvent("Apprendimento: "+learning.stage);
}
void setupRoutes() {
  web.collectHeaders("X-CSRF-Token");
  web.on("/api/state",HTTP_GET,stateRoute);
  web.on("/api/config",HTTP_GET,[] {
    if(!authenticated()) return;
    DynamicJsonDocument doc(6144); configJson(doc.to<JsonObject>(),false); jsonReply(doc);
  });
  web.on("/api/config",HTTP_POST,[] {
    if(!mutation()) return;
    if(infrared.known&&infrared.on) { web.send(409,"text/plain","Spegnere prima di modificare la configurazione"); return; }
    DynamicJsonDocument doc(6144); if(!body(doc)) return;
    Config next=cfg; String error;
    if(!parseConfig(doc.as<JsonObjectConst>(),next,error)) { web.send(400,"text/plain",error); return; }
    if(next.irVerified && (next.protocol!="RAW" || next.powerKind!="discrete" || !infrared.slotsDistinct())) { web.send(400,"text/plain","Verifica IR richiede due codici RAW distinti acquisiti"); return; }
    next.irFingerprint=next.irVerified?infrared.fingerprint():0;
    if(!saveConfig(next)) { web.send(507,"text/plain","Salvataggio fallito"); return; }
    // All changes take effect together after reboot; avoid mixed old/new network state.
    rebootNeeded=true; logEvent("Configurazione salvata; riavviare per applicare"); web.send(200,"application/json","{\"rebootNeeded\":true}");
  });
  web.on("/api/mode",HTTP_POST,[] {
    if(!mutation()) return;
    StaticJsonDocument<256> doc; if(!body(doc)) return;
    String requested=doc["mode"]|"";
    if(requested=="force_off") {
      mode=Mode::ForceOff;
      if(!infrared.known||infrared.on) {
        if(!command(false,"Richiesta Forza OFF")) { web.send(409,"text/plain",infrared.lastError); return; }
      }
    } else if(requested=="auto"||requested=="force_on") {
      if(!infrared.automaticReady()||!infrared.known || rebootNeeded || (requested=="auto"&&!cfg.automationEnabled)) {
        web.send(409,"text/plain","Verificare IR discreti, confermare stato fisico, abilitazione e riavvio"); return;
      }
      mode=requested=="auto"?Mode::Auto:Mode::ForceOn; forceAt=millis();
    } else if(requested=="diagnostic") mode=Mode::Diagnostic;
    else { web.send(400,"text/plain","Modalita non valida"); return; }
    manager.resetTimers(); if(mode!=Mode::Diagnostic) learning=Learning{}; lastRequestedMode=requested;
    File modeFile=LittleFS.open("/mode.txt","w"); if(modeFile) { modeFile.print(requested); modeFile.close(); }
    logEvent("Modalita: "+requested); web.send(200,"application/json","{}");
  });
  web.on("/api/ir",HTTP_GET,[] {
    if(!authenticated()) return;
    if(ESP.getMaxFreeBlockSize()<11000) { web.send(503,"text/plain","Memoria insufficiente per esportare cattura"); return; }
    DynamicJsonDocument doc(9216); infrared.captureJson(doc.to<JsonObject>()); jsonReply(doc);
  });
  web.on("/api/ir",HTTP_POST,[] {
    if(!mutation()) return;
    if(mode!=Mode::Diagnostic) { web.send(409,"text/plain","Attivare diagnostica"); return; }
    StaticJsonDocument<384> doc; if(!body(doc)) return;
    String action=doc["action"]|""; bool confirmed=doc["confirmed" ]|false;
    if(action=="confirm_state" && confirmed && doc["on"].is<bool>()) {
      infrared.known=true; infrared.on=doc["on"].as<bool>(); manager.changed(infrared.on,millis());
      logEvent("Stato fisico dichiarato dall'operatore"); web.send(200,"application/json","{}"); return;
    }
    if((action=="capture_on"||action=="capture_off") && confirmed) {
      if(rebootNeeded) { web.send(409,"text/plain","Applicare prima la configurazione salvata"); return; }
      if(infrared.known&&infrared.on) { web.send(409,"text/plain","Spegnere prima di salvare codici"); return; }
      if(!infrared.saveCapture(action=="capture_on")) { web.send(400,"text/plain","Nessuna acquisizione valida o errore flash"); return; }
      logEvent("Codice acquisito; verifica IR e automazione disabilitate"); web.send(200,"application/json","{}"); return;
    }
    if((action=="on"||action=="off") && confirmed) {
      if(!doc["actualOn"].is<bool>() || (cfg.powerKind!="discrete" && doc["actualOn"].as<bool>()==(action=="on"))) {
        web.send(400,"text/plain","Dichiarare lo stato fisico opposto al comando"); return;
      }
      infrared.known=true; infrared.on=doc["actualOn"].as<bool>();
      if(!command(action=="on","Test IR confermato",true,true)) { web.send(409,"text/plain",infrared.lastError); return; }
      web.send(200,"application/json","{}"); return;
    }
    web.send(400,"text/plain","Azione/conferma mancante");
  });
  web.on("/api/learn",HTTP_POST,[] {
    if(!mutation()) return;
    if(mode!=Mode::Diagnostic) { web.send(409,"text/plain","Attivare diagnostica"); return; }
    StaticJsonDocument<256> doc; if(!body(doc)) return; String action=doc["action"]|"";
    if(action=="cancel") { learning=Learning{}; web.send(200,"application/json","{}"); return; }
    if(action=="baseline" && infrared.known&&!infrared.on && mqtt.connected() && energy.load.fresh(millis(),cfg.energy.staleMs)) {
      learning=Learning{}; learning.stage="baseline"; learning.resetWindow(millis());
    } else if(action=="on" && learning.stage=="await_on" && (doc["confirmed"]|false)) {
      if(!command(true,"Apprendimento",true,true)) { web.send(409,"text/plain",infrared.lastError); return; }
      learning.stage="running"; learning.resetWindow(millis());
    } else if(action=="off" && learning.stage=="await_off" && (doc["confirmed"]|false)) {
      if(!command(false,"Apprendimento",true,true)) { web.send(409,"text/plain",infrared.lastError); return; }
      learning.stage="stopping"; learning.resetWindow(millis());
    } else if(action=="save" && learning.stage=="ready" && !rebootNeeded) {
      float rise=learning.onMean-learning.offMean,fall=learning.onMean-learning.offAfter;
      if(rise<100||fall<100||learning.noise>fminf(rise,fall)*0.5f) { web.send(409,"text/plain","Apprendimento ambiguo: ripetere con carichi stabili"); return; }
      Config next=cfg;
      next.energy.climateW=fmaxf(100,rise); next.detector.deltaW=fmaxf(50,fminf(rise,fall)*0.5f);
      next.detector.maxNoiseW=fmaxf(20,learning.noise*1.5f);
      if(learning.startup) next.detector.startupMs=learning.startup;
      if(!saveConfig(next)) { web.send(507,"text/plain","Errore flash"); return; }
      rebootNeeded=true; learning.stage="saved";
    } else { web.send(409,"text/plain","Fase non valida: verificare stato fisico e dati"); return; }
    web.send(200,"application/json","{}");
  });
  web.on("/api/diagnostics",HTTP_GET,[] {
    if(!authenticated()) return;
    web.setContentLength(CONTENT_LENGTH_UNKNOWN); web.sendHeader("Cache-Control","no-store");
    web.send(200,"application/json",""); web.sendContent("{\"events\":[");
    for(int i=0;i<eventCount;i++) {
      auto& event=events[(eventHead+16-eventCount+i)%16]; StaticJsonDocument<384> doc;
      doc["atMs"]=event.at; doc["message"]=event.message;
      String row; serializeJson(doc,row); if(i) web.sendContent(","); web.sendContent(row); yield();
    }
    web.sendContent("],\"topics\":["); bool first=true;
    for(auto& s:seen) if(s.topic.length()) {
      StaticJsonDocument<512> doc; doc["topic"]=s.topic; doc["value"]=s.value;
      doc["retained"]=s.retained; doc["ageMs"]=elapsed(millis(),s.at);
      String row; serializeJson(doc,row); if(!first) web.sendContent(","); first=false; web.sendContent(row); yield();
    }
    web.sendContent("]}"); web.sendContent("");
  });
  web.on("/api/history",HTTP_GET,[] {
    if(!authenticated()) return;
    web.setContentLength(CONTENT_LENGTH_UNKNOWN); web.sendHeader("Cache-Control","no-store"); web.send(200,"application/json",""); web.sendContent("[");
    for(int i=0;i<historyCount;i++) {
      auto& h=history[(historyHead+120-historyCount+i)%120]; StaticJsonDocument<256> doc;
      doc["s"]=h.at;
      if(h.flags&1) { doc["pv"]=h.pv; doc["load"]=h.load; doc["battery"]=h.battery; }
      else { doc["pv"]=nullptr; doc["load"]=nullptr; doc["battery"]=nullptr; }
      if(h.flags&2) { doc["t"]=h.t; doc["h"]=h.h; } else { doc["t"]=nullptr; doc["h"]=nullptr; }
      doc["on"]=bool(h.flags&4); doc["known"]=bool(h.flags&8);
      String row; serializeJson(doc,row); if(i) web.sendContent(","); web.sendContent(row); yield();
    }
    web.sendContent("]"); web.sendContent("");
  });
  web.on("/api/reboot",HTTP_POST,[] {
    if(!mutation()) return;
    if(!infrared.known||infrared.on) { web.send(409,"text/plain","Confermare climatizzatore fisicamente spento in diagnostica"); return; }
    rebootAt=millis()+1500; web.send(200,"application/json","{}");
  });
  // ======================================================================
  // CONFERMA DELLO STATO FISICO OFF PRIMA DI UN AGGIORNAMENTO
  //
  // La pagina /update presenta un popup di conferma all'operatore.
  // Questo endpoint registra SOLO la dichiarazione dello stato fisico:
  // non invia mai comandi IR al climatizzatore.
  //
  // Manteniamo le verifiche indipendenti gia' previste nelle due rotte OTA.
  // La modalita' ForceOff blocca avvii automatici durante la manutenzione,
  // anche se il download o il caricamento manuale falliscono.
  // ======================================================================
  web.on("/api/update-confirm-off",HTTP_POST,[] {
    if(!mutation()) return;
    if(updateActive) {
      web.send(409,"text/plain","Aggiornamento gia' in corso.");
      return;
    }
    StaticJsonDocument<160> doc;
    if(!body(doc)) return;
    if(!doc["confirmed"].is<bool>() || !doc["confirmed"].as<bool>()) {
      web.send(400,"text/plain","Conferma fisica dello spegnimento obbligatoria.");
      return;
    }

    mode=Mode::ForceOff;
    lastRequestedMode="force_off";
    learning=Learning{};
    infrared.known=true;
    infrared.on=false;
    manager.changed(false,millis());
    logEvent("OTA: climatizzatore fisicamente spento confermato dall'operatore; nessun IR inviato");

    StaticJsonDocument<128> result;
    result["commanded"]="off";
    result["mode"]="force_off";
    jsonReply(result);
  });

  // Endpoint espressamente richiesti dall'amministratore. Nessun firmware
  // viene scaricato/installato automaticamente all'avvio o durante il polling.
  web.on("/api/github-update",HTTP_GET,[] {
    if(!authenticated()) return;
    GithubFirmwareInfo info;
    String error;
    DynamicJsonDocument response(640);
    bool checked=githubFirmwareCheck(VERSION,info,error);
    response["checked"]=checked;
    response["board"]=githubFirmwareBoard();
    response["installed"]=VERSION;
    if(checked) {
      response["version"]=info.version;
      response["size"]=info.size;
      response["available"]=info.available;
    } else {
      response["error"]=error;
      response["available"]=false;
    }
    jsonReply(response);
  });

  web.on("/api/github-update",HTTP_POST,[] {
    if(!mutation()) return;
    // Difesa indipendente dal frontend: il popup da solo non basta.
    // La conferma deve essere stata registrata nello stato del controller.
    if(!infrared.known || infrared.on) {
      web.send(409,"text/plain","Confermare che il climatizzatore sia fisicamente spento.");
      return;
    }
    if(updateActive) {
      web.send(409,"text/plain","Aggiornamento gia' in corso.");
      return;
    }
    mode=Mode::ForceOff;
    GithubFirmwareInfo info;
    String error;
    if(!githubFirmwareCheck(VERSION,info,error)) {
      web.send(502,"text/plain",error);
      return;
    }
    if(!info.available) {
      web.send(409,"text/plain","Nessuna versione successiva disponibile.");
      return;
    }
    updateActive=true;
    logEvent(String("GitHub OTA iniziato: ")+info.version);
    bool installed=githubFirmwareInstall(info,error);
    updateActive=false;
    if(!installed) {
      logEvent(String("GitHub OTA fallito: ")+error);
      web.send(502,"text/plain",error);
      return;
    }
    logEvent(String("GitHub OTA completato: ")+info.version);
    DynamicJsonDocument response(256);
    response["installed"]=true;
    response["version"]=info.version;
    jsonReply(response);
    rebootAt=millis()+1500;
  });

  web.on("/update",HTTP_GET,[] {
    if(!authenticated()) return;
    File f=LittleFS.open("/update.html","r");
    if(f) { web.streamFile(f,"text/html"); f.close(); } else web.send(503,"text/plain","Caricare LittleFS via USB (uploadfs)");
  });
  web.on("/update",HTTP_POST,[] {
    if(!mutation()) return;
    if(updateOk) { web.send(200,"text/plain","Firmware verificato. Riavvio in corso; controllare la versione al rientro."); rebootAt=millis()+1500; }
    else { web.send(400,"text/plain",String("Aggiornamento fallito/rifiutato: ")+Update.getErrorString()+". Verificare stato spento e file .bin."); }
  },[] {
    HTTPUpload& upload=web.upload();
    if(upload.status==UPLOAD_FILE_START) {
      updateOk=false; updateActive=false;
      if(!configReady || !web.authenticate(cfg.adminUser.c_str(),cfg.adminPassword.c_str()) || web.header("X-CSRF-Token")!=csrf ||
        !infrared.known || infrared.on || !upload.filename.endsWith(".bin")) return;
      mode=Mode::ForceOff;
      uint32_t space=(ESP.getFreeSketchSpace()-0x1000)&0xFFFFF000;
      updateActive=Update.begin(space,U_FLASH); logEvent(updateActive?"OTA iniziato":"OTA: spazio/inizializzazione falliti");
    } else if(upload.status==UPLOAD_FILE_WRITE && updateActive) {
      if(Update.write(upload.buf,upload.currentSize)!=upload.currentSize) { Update.end(); updateActive=false; logEvent("OTA scrittura fallita"); }
    } else if(upload.status==UPLOAD_FILE_END && updateActive) {
      updateOk=Update.end(true); updateActive=false; logEvent(updateOk?String("OTA completato"):String("OTA fallito: ")+Update.getErrorString());
    } else if(upload.status==UPLOAD_FILE_ABORTED) { if(updateActive) Update.end(); updateActive=false; logEvent("OTA interrotto"); }
    yield();
  });
  web.onNotFound([] {
    if(!authenticated()) return;
    String path=web.uri(); if(path=="/") path="/index.html";
    if(path!="/index.html"&&path!="/app.js"&&path!="/style.css") { web.send(404,"text/plain","Non trovato"); return; }
    File f=LittleFS.open(path,"r"); if(!f) { web.send(503,"text/plain","Asset mancanti: eseguire pio run -t uploadfs via USB"); return; }
    web.sendHeader("Cache-Control","no-cache");
    web.streamFile(f,path.endsWith(".js")?"application/javascript":path.endsWith(".css")?"text/css":"text/html"); f.close();
  });
  web.begin();
}
void setup() {
  serialPrintStartup();
  loadConfig();
  Serial.print(F("[FS] LittleFS: "));
  if(storageReady) {
    Serial.println(F("montato"));
  } else {
    Serial.println(F("ERRORE montaggio"));
  }
  Serial.print(F("[CONFIG] Configurazione: "));
  if(configReady) {
    Serial.println(F("valida"));
  } else {
    Serial.println(F("NON DISPONIBILE"));
  }
  if(!configReady) {
    Serial.println(F("[ERROR] STOP sicuro: LittleFS/config non disponibili; nessun comando IR."));
    serialPrintStatus(millis());
    serialLastStatusAt=millis();
    Serial.flush();
    return;
  }
  char token[25]; snprintf(token,sizeof(token),"%08lx%08lx%08lx",os_random(),os_random(),os_random()); csrf=token;
  File savedMode=LittleFS.open("/mode.txt","r");
  if(savedMode && savedMode.size()<32) lastRequestedMode=savedMode.readString();
  if(savedMode) savedMode.close();
  Serial.println(F("[INIT] Sensore I2C SHT31..."));
  Wire.begin(4,5);
  sensorPresent=sht.begin(0x44);
  if(sensorPresent) {
    Serial.println(F("[SENSOR] SHT31 rilevato"));
  } else {
    Serial.println(F("[SENSOR] SHT31 non rilevato; automazione protetta"));
  }
  Serial.println(F("[INIT] Trasmettitore/ricevitore IR..."));
  infrared.begin();
  Serial.println(F("[INIT] Interfaccia Wi-Fi..."));
  WiFi.persistent(false); WiFi.mode(WIFI_AP_STA); WiFi.setAutoReconnect(true);
  if(cfg.ssid.length()) {
    // Imposta l'eventuale IP statico PRIMA della connessione alla rete Wi-Fi.
    // In assenza di staticIp il comportamento resta DHCP come in precedenza.
    if(cfg.staticIp.length()>0) {
      IPAddress local,gateway,subnet,dns1,dns2;
      bool valid=local.fromString(cfg.staticIp) &&
        gateway.fromString(cfg.staticGateway) && subnet.fromString(cfg.staticSubnet);
      if(cfg.staticDns1.length()>0) {
        valid=valid && dns1.fromString(cfg.staticDns1);
      } else {
        dns1=gateway;
      }
      if(cfg.staticDns2.length()>0) {
        valid=valid && dns2.fromString(cfg.staticDns2);
      }
      if(valid && WiFi.config(local,gateway,subnet,dns1,dns2)) {
        logEvent(String("Wi-Fi statico configurato: ")+local.toString());
      } else {
        logEvent("IP statico non applicabile: Wi-Fi non avviato");
        Serial.println(F("ERRORE: Wi-Fi statico invalido o WiFi.config fallita."));
        return;
      }
    }
    Serial.println(F("[WIFI] Connessione alla rete configurata richiesta"));
    WiFi.begin(cfg.ssid.c_str(),cfg.wifiPassword.c_str());
    // NTP: necessario alla verifica temporale dei certificati HTTPS.
    configTime(0,0,"pool.ntp.org","time.google.com");
  }
  // Recovery AP is available on first setup, and after 30s without station Wi-Fi.
  if(!cfg.ssid.length()) {
    recoveryAP=WiFi.softAP(("SolarHeat-"+String(ESP.getChipId(),HEX)).c_str(),cfg.adminPassword.c_str());
    if(recoveryAP) {
      Serial.print(F("[WIFI] AP iniziale attivo: "));
      Serial.println(WiFi.softAPIP());
    } else {
      Serial.println(F("[WIFI][ERROR] AP iniziale non disponibile"));
    }
  }
  mqtt.setServer(cfg.mqttHost.c_str(),cfg.mqttPort); mqtt.setKeepAlive(cfg.mqttKeepalive);
  if(cfg.mqttUser.length()) mqtt.setCredentials(cfg.mqttUser.c_str(),cfg.mqttPassword.c_str());
  mqtt.onConnect([](bool) {
    clearTelemetry(); logEvent("MQTT connesso: attesa dati live");
    for(auto& topic:cfg.topics) if(topic.length()) mqtt.subscribe(topic.c_str(),0);
    if(cfg.discovery.length()) mqtt.subscribe(cfg.discovery.c_str(),0);
  });
  mqtt.onDisconnect([](AsyncMqttClientDisconnectReason) { clearTelemetry(); logEvent("MQTT disconnesso"); });
  mqtt.onMessage(receiveMqtt);
  setupRoutes();
  logEvent(String("Firmware ")+VERSION+" pronto; stato fisico sconosciuto");
  serialPrintStatus(millis());
  serialLastStatusAt=millis();
  Serial.println(F("[BOOT] Setup terminato; monitoraggio periodico attivo"));
  Serial.flush();
}
void loop() {
  uint32_t now=millis();
  serialDiagnosticsTick(now);
  if(!configReady) {
    delay(20);
    return;
  }
  web.handleClient();
  infrared.poll();
  if(rebootAt && int32_t(now-rebootAt)>=0) ESP.restart();
  if(updateActive) { yield(); return; }
  if(now>30000 && WiFi.status()!=WL_CONNECTED && !recoveryAP) {
    recoveryAP=WiFi.softAP(("SolarHeat-"+String(ESP.getChipId(),HEX)).c_str(),cfg.adminPassword.c_str());
    if(recoveryAP) {
      logEvent("AP di recupero attivato: 192.168.4.1");
    }
  }
  if(WiFi.status()==WL_CONNECTED && recoveryAP) {
    WiFi.softAPdisconnect(true);
    recoveryAP=false;
    logEvent("AP di recupero disattivato: Wi-Fi connesso");
  }
  if(WiFi.status()==WL_CONNECTED && !mqtt.connected() && cfg.mqttHost.length() && elapsed(now,lastMqttAttempt)>=10000) {
    lastMqttAttempt=now; mqtt.connect();
  }
  if(elapsed(now,lastSensor)>=cfg.sampleMs) {
    lastSensor=now;
    if(!sensorPresent) sensorPresent=sht.begin(0x44);
    float t=sensorPresent?sht.readTemperature():NAN,h=sensorPresent?sht.readHumidity():NAN;
    bool before=environment.temperature.valid;
    environment.update(t,h,now,cfg.filterAlpha);
    if(!environment.temperature.valid) { sensorPresent=false; if(before) logEvent("SHT31 non valido: automazione arrestata"); }
  }
  if(elapsed(now,lastSecond)>=1000) {
    lastSecond=now;
    if(energy.load.fresh(now,cfg.energy.staleMs)&&mqtt.connected()) detector.sample(energy.load.value,now,cfg.detector);
    else detector.invalidate();
    learningSample(now);
    bool expired=mode==Mode::ForceOn&&elapsed(now,forceAt)>=cfg.forceOnMs;
    // Configured optional grid channels are safety inputs: absence/staleness blocks decisions.
    bool gridOk=(!cfg.topics[6].length()||energy.importW.fresh(now,cfg.energy.staleMs)) &&
                (!cfg.topics[7].length()||energy.exportW.fresh(now,cfg.energy.staleMs));
    bool ready=infrared.automaticReady()&&infrared.known&&!rebootNeeded;
    // UR SHT31: campione indipendente usato solo quando l'assistenza e' attiva.
    // Le decisioni HEAT rimangono vincolate a sicurezza, surplus e limiti termici.
    auto d=manager.evaluate(now,cfg.energy,energy,environment.temperature,mode,infrared.known&&infrared.on,
                            mqtt.connected()&&gridOk,ready,expired,environment.humidity);
    availableW=d.available;
    if(!infrared.known) reason="Stato sconosciuto: confermare lo stato fisico in diagnostica";
    else reason=d.reason;
    const bool safeUnknownOff=!infrared.known && infrared.automaticReady() && mode!=Mode::Diagnostic &&
      (d.critical || mode==Mode::ForceOff || (environment.temperature.valid && environment.temperature.value>=cfg.energy.tempOff));
    if(safeUnknownOff) command(false,"OFF discreto preventivo: stato sconosciuto");
    else if(d.action==Action::Off && infrared.automaticReady()) command(false,d.reason);
    else if(d.action==Action::Off) reason="Protezione richiesta: OFF toggle non sicuro; intervenire con telecomando";
    else if(d.action==Action::On && ready && (mode!=Mode::Auto||cfg.automationEnabled)) command(true,d.reason);
    if(expired) { mode=Mode::ForceOff; logEvent("Forza ON scaduto: Forza OFF"); }
  }
  if(elapsed(now,lastHistory)>=cfg.historyMs) {
    lastHistory=now;
    bool e=mqtt.connected()&&energy.pv.fresh(now,cfg.energy.staleMs)&&energy.load.fresh(now,cfg.energy.staleMs)&&energy.battery.fresh(now,cfg.energy.staleMs);
    bool t=environment.temperature.fresh(now,cfg.energy.staleMs);
    history[historyHead]={now/1000,energy.pv.value,energy.load.value,energy.battery.value*(cfg.energy.negativeCharge?-1:1),environment.temperature.value,environment.humidity.value,
      uint8_t((e?1:0)|(t?2:0)|(infrared.on?4:0)|(infrared.known?8:0))};
    historyHead=(historyHead+1)%120; if(historyCount<120) historyCount++;
  }
  yield();
}
