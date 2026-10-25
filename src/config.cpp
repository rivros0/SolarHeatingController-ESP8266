#include "config.h"
Config cfg;
bool storageReady=false,configReady=false;
static void encode(JsonObject o,const Config& c,bool secrets) {
  o["version"]=2;
#define FIELD(n) o[#n]=c.n
  FIELD(ssid); FIELD(mqttHost); FIELD(mqttUser); FIELD(adminUser); FIELD(discovery);
  FIELD(irFingerprint); FIELD(protocol); FIELD(powerKind); FIELD(fan); FIELD(model); FIELD(setpoint);
  FIELD(mqttPort); FIELD(mqttKeepalive); FIELD(sampleMs); FIELD(historyMs); FIELD(forceOnMs);
  FIELD(filterAlpha); FIELD(receiver); FIELD(irVerified); FIELD(automationEnabled);
  if(secrets) { FIELD(wifiPassword); FIELD(mqttPassword); FIELD(adminPassword); }
#undef FIELD
  JsonObject topics=o.createNestedObject("topics");
  for(int i=0;i<9;i++) topics[metricNames[i]]=c.topics[i];
  JsonObject e=o.createNestedObject("energy");
#define E(n) e[#n]=c.energy.n
  E(tempOn); E(tempOff); E(reserveW); E(climateW); E(dischargeW); E(minVoltage); E(maxDischargeA);
  E(stableMs); E(dropMs); E(restartMs); E(minRunMs); E(staleMs); E(negativeCharge); E(negativeCurrentCharge);
#undef E
  JsonObject d=o.createNestedObject("detector");
#define D(n) d[#n]=c.detector.n
  D(baselineMs); D(startupMs); D(observationMs); D(deltaW); D(maxNoiseW);
#undef D
  o["modeOnBoot"]="force_off";
}
void configJson(JsonObject o,bool secrets) { encode(o,cfg,secrets); }
static bool bounded(float v,float lo,float hi) { return isfinite(v)&&v>=lo&&v<=hi; }
bool parseConfig(JsonObjectConst o,Config& c,String& error) {
  // v1 used the same flat schema but had no explicit opt-in. Migration disables automation.
  int version=o["version"]|0;
  if(version!=1 && version!=2) { error="Versione configurazione non supportata"; return false; }
#define READ_FIELD(n) if(o.containsKey(#n)) { if(!o[#n].is<decltype(c.n)>()) { error="Tipo non valido: " #n; return false; } c.n=o[#n].as<decltype(c.n)>(); }
  READ_FIELD(ssid); READ_FIELD(wifiPassword); READ_FIELD(mqttHost); READ_FIELD(mqttUser); READ_FIELD(mqttPassword); READ_FIELD(adminUser); READ_FIELD(adminPassword);
  READ_FIELD(irFingerprint); READ_FIELD(discovery); READ_FIELD(protocol); READ_FIELD(powerKind); READ_FIELD(fan); READ_FIELD(model); READ_FIELD(setpoint); READ_FIELD(mqttPort); READ_FIELD(mqttKeepalive);
  READ_FIELD(sampleMs); READ_FIELD(historyMs); READ_FIELD(forceOnMs); READ_FIELD(filterAlpha); READ_FIELD(receiver); READ_FIELD(irVerified); READ_FIELD(automationEnabled);
#undef READ_FIELD
  if(version==1) c.automationEnabled=false;
  for(const char* key:{"topics","energy","detector"}) if(o.containsKey(key)&&!o[key].is<JsonObjectConst>()) { error=String("Oggetto richiesto: ")+key; return false; }
  JsonObjectConst t=o["topics"];
  for(int i=0;i<9;i++) if(t.containsKey(metricNames[i])) { if(!t[metricNames[i]].is<String>()) { error="Topic deve essere una stringa"; return false; } c.topics[i]=t[metricNames[i]].as<String>(); }
  JsonObjectConst e=o["energy"];
#define E(n) if(e.containsKey(#n)) { if(!e[#n].is<decltype(c.energy.n)>()) { error="Tipo non valido: energy." #n; return false; } c.energy.n=e[#n].as<decltype(c.energy.n)>(); }
  E(tempOn); E(tempOff); E(reserveW); E(climateW); E(dischargeW); E(minVoltage); E(maxDischargeA);
  E(stableMs); E(dropMs); E(restartMs); E(minRunMs); E(staleMs); E(negativeCharge); E(negativeCurrentCharge);
#undef E
  JsonObjectConst d=o["detector"];
#define D(n) if(d.containsKey(#n)) { if(!d[#n].is<decltype(c.detector.n)>()) { error="Tipo non valido: detector." #n; return false; } c.detector.n=d[#n].as<decltype(c.detector.n)>(); }
  D(baselineMs); D(startupMs); D(observationMs); D(deltaW); D(maxNoiseW);
#undef D
  auto& a=c.energy; auto& b=c.detector;
  bool good=c.model>=1 && c.model<=2 && c.ssid.length()<=32 && c.wifiPassword.length()<=63 && c.mqttHost.length()<=128 &&
    c.mqttUser.length()<=64 && c.mqttPassword.length()<=128 && c.adminUser.length()>0 && c.adminUser.length()<=32 &&
    c.adminPassword.length()>=12 && c.adminPassword.length()<=63 && c.discovery.length()<=128 &&
    c.mqttPort>0 && c.mqttKeepalive>=5 && c.mqttKeepalive<=300 &&
    c.sampleMs>=1000 && c.sampleMs<=30000 && c.historyMs>=10000 && c.historyMs<=3600000 &&
    c.forceOnMs>=60000 && c.forceOnMs<=86400000 && bounded(c.filterAlpha,0.01,1) &&
    bounded(c.setpoint,16,30) && (c.fan=="auto"||c.fan=="low"||c.fan=="medium"||c.fan=="high") &&
    (c.protocol=="RAW"||c.protocol=="KELON"||c.protocol=="WHIRLPOOL_AC") &&
    (c.powerKind=="unverified"||c.powerKind=="toggle"||c.powerKind=="discrete") &&
    !(c.protocol!="RAW"&&c.powerKind=="discrete") &&
    bounded(a.tempOn,5,29) && bounded(a.tempOff,a.tempOn+0.5,35) && c.setpoint>=a.tempOff &&
    bounded(a.reserveW,0,20000) && bounded(a.climateW,100,10000) && bounded(a.dischargeW,0,5000) &&
    bounded(a.minVoltage,0,200) && bounded(a.maxDischargeA,0,500) &&
    a.staleMs>=10000 && a.staleMs<=300000 && c.sampleMs*2<=a.staleMs &&
    a.stableMs>=10000 && a.stableMs<=7200000 && a.dropMs>=10000 && a.dropMs<=3600000 &&
    a.restartMs>=60000 && a.restartMs<=7200000 && a.minRunMs<=7200000 &&
    b.baselineMs>=10000 && b.baselineMs<=600000 && b.startupMs<=900000 &&
    b.observationMs>=10000 && b.observationMs<=600000 && bounded(b.deltaW,50,10000) && bounded(b.maxNoiseW,1,5000);
  for(int i=0;i<9;i++) {
    good &= c.topics[i].length()<=128 && c.topics[i].indexOf('#')<0 && c.topics[i].indexOf('+')<0;
    for(int j=0;j<i;j++) if(c.topics[i].length() && c.topics[i]==c.topics[j]) good=false;
  }
  if(c.automationEnabled) good &= c.topics[0].length() && c.topics[1].length() && c.topics[2].length();
  if(a.minVoltage>0) good &= c.topics[3].length()>0;
  if(a.maxDischargeA>0) good &= c.topics[4].length()>0;
  if(!good) { error="Configurazione non valida: verificare limiti, topic univoci e semantica IR (vedere docs/API.md)"; return false; }
  return true;
}
bool saveConfig(const Config& c) {
  if(!storageReady) return false;
  DynamicJsonDocument doc(6144); encode(doc.to<JsonObject>(),c,true);
  File f=LittleFS.open("/config.new","w"); if(!f) return false;
  size_t expected=measureJson(doc),written=serializeJson(doc,f); f.flush(); f.close();
  if(written!=expected) { LittleFS.remove("/config.new"); return false; }
  if(LittleFS.exists("/config.json")) {
    LittleFS.remove("/config.bak");
    if(!LittleFS.rename("/config.json","/config.bak")) return false;
  }
  return LittleFS.rename("/config.new","/config.json");
}
void loadConfig() {
  storageReady=LittleFS.begin(); // Never format automatically: preserve config on mount failure.
  if(!storageReady) return;
  bool exists=LittleFS.exists("/config.json")||LittleFS.exists("/config.bak");
  for(const char* path:{"/config.json","/config.bak"}) {
    File f=LittleFS.open(path,"r"); if(!f) continue;
    if(f.size()>6144) { f.close(); continue; }
    DynamicJsonDocument doc(6144); auto err=deserializeJson(doc,f); f.close(); String why;
    Config candidate;
    if(!err && parseConfig(doc.as<JsonObjectConst>(),candidate,why)) { cfg=candidate; configReady=true; return; }
  }
  if(exists) { Serial.println(F("Configurazione corrotta/futura: recuperare via USB. Nessuna sovrascrittura.")); return; }
  char password[25]; snprintf(password,sizeof(password),"%08lx%08lx%08lx",os_random(),os_random(),os_random());
  cfg.adminPassword=password;
  configReady=saveConfig(cfg);
  if(configReady) { Serial.println(F("PRIMO AVVIO - utente admin, password web/AP:")); Serial.println(cfg.adminPassword); }
}
