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
#include "environment_manager.h"
using namespace solar;
static const char* VERSION="0.1.0";
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
struct Event { uint32_t at=0; String message; } events[16];
uint8_t eventHead=0,eventCount=0;
struct TopicSeen { String topic,value; uint32_t at=0; bool retained=false; } seen[12];
uint8_t seenHead=0;
struct History { uint32_t at; float pv,load,battery,t,h; uint8_t flags; } history[60];
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
void logEvent(const String& text) {
  events[eventHead]={millis(),text.substring(0,160)};
  eventHead=(eventHead+1)%16; if(eventCount<16) eventCount++;
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
  o["version"]=VERSION; o["csrf"]=csrf; o["uptime"]=now/1000;
  o["wifi"]=WiFi.status()==WL_CONNECTED; o["mqtt"]=mqtt.connected();
  o["heap"]=ESP.getFreeHeap(); o["maxBlock"]=ESP.getMaxFreeBlockSize();
  o["otaSpace"]=ESP.getFreeSketchSpace(); o["storage"]=storageReady;
  o["resetReason"]=ESP.getResetReason(); o["lastMqttAgeMs"]=elapsed(now,lastMqttAt);
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
      auto& h=history[(historyHead+60-historyCount+i)%60]; StaticJsonDocument<256> doc;
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
  Serial.begin(115200); Serial.println(); loadConfig();
  if(!configReady) { Serial.println(F("STOP sicuro: LittleFS/config non disponibili. Flash USB, nessun comando IR.")); return; }
  char token[25]; snprintf(token,sizeof(token),"%08lx%08lx%08lx",os_random(),os_random(),os_random()); csrf=token;
  File savedMode=LittleFS.open("/mode.txt","r");
  if(savedMode && savedMode.size()<32) lastRequestedMode=savedMode.readString();
  if(savedMode) savedMode.close();
  Wire.begin(4,5); sensorPresent=sht.begin(0x44);
  infrared.begin();
  WiFi.persistent(false); WiFi.mode(WIFI_AP_STA); WiFi.setAutoReconnect(true);
  if(cfg.ssid.length()) WiFi.begin(cfg.ssid.c_str(),cfg.wifiPassword.c_str());
  // Recovery AP is available on first setup, and after 30s without station Wi-Fi.
  if(!cfg.ssid.length()) recoveryAP=WiFi.softAP(("SolarHeat-"+String(ESP.getChipId(),HEX)).c_str(),cfg.adminPassword.c_str());
  mqtt.setServer(cfg.mqttHost.c_str(),cfg.mqttPort); mqtt.setKeepAlive(cfg.mqttKeepalive);
  if(cfg.mqttUser.length()) mqtt.setCredentials(cfg.mqttUser.c_str(),cfg.mqttPassword.c_str());
  mqtt.onConnect([](bool) {
    clearTelemetry(); logEvent("MQTT connesso: attesa dati live");
    for(auto& topic:cfg.topics) if(topic.length()) mqtt.subscribe(topic.c_str(),0);
    if(cfg.discovery.length()) mqtt.subscribe(cfg.discovery.c_str(),0);
  });
  mqtt.onDisconnect([](AsyncMqttClientDisconnectReason) { clearTelemetry(); logEvent("MQTT disconnesso"); });
  mqtt.onMessage(receiveMqtt);
  setupRoutes(); logEvent(String("Firmware ")+VERSION+" pronto; stato fisico sconosciuto");
}
void loop() {
  if(!configReady) { delay(20); return; }
  uint32_t now=millis(); web.handleClient(); infrared.poll();
  if(rebootAt && int32_t(now-rebootAt)>=0) ESP.restart();
  if(updateActive) { yield(); return; }
  if(now>30000 && WiFi.status()!=WL_CONNECTED && !recoveryAP)
    recoveryAP=WiFi.softAP(("SolarHeat-"+String(ESP.getChipId(),HEX)).c_str(),cfg.adminPassword.c_str());
  if(WiFi.status()==WL_CONNECTED && recoveryAP) { WiFi.softAPdisconnect(true); recoveryAP=false; }
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
    auto d=manager.evaluate(now,cfg.energy,energy,environment.temperature,mode,infrared.known&&infrared.on,
                            mqtt.connected()&&gridOk,ready,expired);
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
    historyHead=(historyHead+1)%60; if(historyCount<60) historyCount++;
  }
  yield();
}
