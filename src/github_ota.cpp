#include "github_ota.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <LittleFS.h>
#include <Updater.h>
#include <time.h>
#include <cstring>
#include <cctype>
#include "github_ca_roots.h"

// ============================================================================
// SOLAR HEATING - GITHUB OTA SICURO
//
// - Canale firmware-stable distinto dalla release del contenitore CAD.
// - Solo download HTTPS con verifica completa della catena TLS.
// - Versioni semanticamente crescenti; board/layout/size vincolati.
// - Prima SHA256 su file temporaneo LittleFS, POI scrittura via Update.
// - In caso di errore HTTP/hash il firmware in esecuzione resta intatto.
// - La pagina OTA manuale /update resta indipendente e disponibile.
// - Dashboard OTA: aggiorna solo tre file HTML/JS/CSS con SHA256 verificato.
// - Non scarica mai una nuova immagine LittleFS, preservando credenziali
//   Wi-Fi/MQTT, configurazione energetica e codici IR acquisiti.
// ============================================================================
namespace {
const char* MANIFEST =
  "https://raw.githubusercontent.com/rivros0/SolarHeatingController-ESP8266/main/firmware/stable.json";
const char* RELEASE_URL =
  "https://github.com/rivros0/SolarHeatingController-ESP8266/releases/download/firmware-stable/";
const char* TEMP_FIRMWARE = "/github-update.tmp";
const char* FLASH_LAYOUT = "4m1m";

// Elenco fisso: un manifest remoto non puo' decidere quali file modificare.
// I nomi web-* coincidono con gli asset pubblicati nella release stabile.
const char* WEB_NAMES[GITHUB_WEB_ASSET_COUNT] = {
  "index.html",
  "app.js",
  "style.css"
};
const char* WEB_LOCAL[GITHUB_WEB_ASSET_COUNT] = {
  "/index.html",
  "/app.js",
  "/style.css"
};
const char* WEB_TEMP[GITHUB_WEB_ASSET_COUNT] = {
  "/.ota-index.tmp",
  "/.ota-app.tmp",
  "/.ota-style.tmp"
};

static const size_t MAX_WEB_FILE_BYTES = 131072;
static const size_t MAX_WEB_TOTAL_BYTES = 196608;
static const size_t WEB_FS_MARGIN_BYTES = 16384;

bool networkReady(String& error) {
  if(WiFi.status()!=WL_CONNECTED) {
    error="Wi-Fi non connesso";
    return false;
  }
  // L'ESP8266 non ha RTC: TLS deve conoscere la data per validare le CA.
  time_t now=time(nullptr);
  if(now<1735689600) {
    error="Ora non sincronizzata tramite NTP: controllare DNS/gateway";
    return false;
  }
  return true;
}

bool parseVersion(const String& version, unsigned& a, unsigned& b, unsigned& c) {
  char extra=0;
  int n=sscanf(version.c_str(),"%u.%u.%u%c",&a,&b,&c,&extra);
  return n==3 && a<1000 && b<1000 && c<1000;
}

bool newer(const String& remote,const String& local) {
  unsigned ra=0,rb=0,rc=0,la=0,lb=0,lc=0;
  if(!parseVersion(remote,ra,rb,rc) || !parseVersion(local,la,lb,lc)) return false;
  if(ra!=la) return ra>la;
  if(rb!=lb) return rb>lb;
  return rc>lc;
}

bool validSha256(const String& value) {
  if(value.length()!=64) return false;
  for(unsigned i=0;i<value.length();i++) {
    if(!isxdigit(static_cast<unsigned char>(value[i]))) return false;
  }
  return true;
}

bool newHttpClient(BearSSL::WiFiClientSecure& client,
                   BearSSL::X509List& roots,
                   HTTPClient& http,
                   const String& url,
                   String& error) {
  client.setTrustAnchors(&roots);
  client.setX509Time(time(nullptr));
  client.setTimeout(15000);
  client.setBufferSizes(1024,512);
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setRedirectLimit(4);
  if(!http.begin(client,url)) {
    error="Impossibile inizializzare HTTPS";
    return false;
  }
  return true;
}

String firmwareUrl() {
  String url=String(RELEASE_URL)+"firmware-"+githubFirmwareBoard()+".bin";
  return url;
}

void removeTemp() {
  if(LittleFS.exists(TEMP_FIRMWARE)) {
    LittleFS.remove(TEMP_FIRMWARE);
  }
}

// ---------------------------------------------------------------------------
// SHA256 in streaming su file locali: non carica la dashboard in RAM.
// Il confronto del contenuto reale consente di recuperare automaticamente
// un update interrotto e rilevare web app datate dopo uploadfs.
// ---------------------------------------------------------------------------
String hexDigest(const uint8_t* digest) {
  char hex[65];
  for(size_t index = 0; index < 32; index++) {
    snprintf(hex + index * 2, 3, "%02x", digest[index]);
  }
  hex[64] = 0;
  return String(hex);
}

bool webFileMatches(size_t index, size_t expectedSize, const String& sha) {
  File input = LittleFS.open(WEB_LOCAL[index], "r");
  if(!input) {
    return false;
  }

  if(input.size() != expectedSize) {
    input.close();
    return false;
  }

  BearSSL::HashSHA256 hash;
  hash.begin();

  uint8_t buffer[512];
  size_t remaining = expectedSize;
  while(remaining > 0) {
    size_t readCount = remaining;
    if(readCount > sizeof(buffer)) {
      readCount = sizeof(buffer);
    }

    const int received = input.read(buffer, readCount);
    if(received <= 0) {
      input.close();
      return false;
    }

    hash.add(buffer, received);
    remaining -= size_t(received);
    yield();
  }

  input.close();
  hash.end();
  const uint8_t* digest = static_cast<const uint8_t*>(hash.hash());
  return sha.equalsIgnoreCase(hexDigest(digest));
}

// ---------------------------------------------------------------------------
// Scarica un asset web interamente in un file temporaneo, controllando
// dimensioni e SHA256. In caso di errore, il file pubblicato non cambia.
// TLS rimane verificato come per l'OTA del firmware.
// ---------------------------------------------------------------------------
bool downloadVerifiedWebAsset(size_t index,
                              const GithubWebAssetInfo& asset,
                              String& error) {
  const String url = String(RELEASE_URL) + "web-" + WEB_NAMES[index];

  BearSSL::X509List roots(GH_TLS_CA_PEM);
  if(roots.getCount() == 0) {
    error = "Certificati HTTPS CA assenti";
    return false;
  }

  BearSSL::WiFiClientSecure client;
  HTTPClient http;
  if(!newHttpClient(client, roots, http, url, error)) {
    return false;
  }

  int status = http.GET();
  if(status != HTTP_CODE_OK) {
    error = "Download asset web non disponibile: " +
      String(WEB_NAMES[index]) + " HTTP " + String(status);
    http.end();
    return false;
  }

  const int reported = http.getSize();
  if(reported < 0 || size_t(reported) != asset.size) {
    error = "Dimensione asset web difforme: " + String(WEB_NAMES[index]);
    http.end();
    return false;
  }

  File stage = LittleFS.open(WEB_TEMP[index], "w");
  if(!stage) {
    error = "Impossibile creare il file temporaneo: " +
      String(WEB_NAMES[index]);
    http.end();
    return false;
  }

  BearSSL::HashSHA256 hash;
  hash.begin();
  WiFiClient* stream = http.getStreamPtr();

  uint8_t buffer[512];
  size_t received = 0;
  unsigned long started = millis();
  unsigned long lastActivity = started;
  bool writeSucceeded = true;

  while(received < asset.size) {
    const unsigned long now = millis();
    if(now - started > 120000UL || now - lastActivity > 15000UL) {
      error = "Timeout durante il download: " + String(WEB_NAMES[index]);
      writeSucceeded = false;
      break;
    }

    size_t available = stream->available();
    if(available == 0) {
      delay(10);
      continue;
    }

    size_t count = available;
    if(count > sizeof(buffer)) {
      count = sizeof(buffer);
    }
    if(count > asset.size - received) {
      count = asset.size - received;
    }

    const int bytesRead = stream->readBytes(buffer, count);
    if(bytesRead <= 0) {
      error = "Lettura HTTPS fallita: " + String(WEB_NAMES[index]);
      writeSucceeded = false;
      break;
    }

    if(stage.write(buffer, size_t(bytesRead)) != size_t(bytesRead)) {
      error = "Scrittura LittleFS fallita: " + String(WEB_NAMES[index]);
      writeSucceeded = false;
      break;
    }

    hash.add(buffer, bytesRead);
    received += size_t(bytesRead);
    lastActivity = millis();
    yield();
  }

  stage.flush();
  stage.close();
  http.end();

  if(!writeSucceeded || received != asset.size) {
    LittleFS.remove(WEB_TEMP[index]);
    return false;
  }

  hash.end();
  const uint8_t* digest = static_cast<const uint8_t*>(hash.hash());
  if(!asset.sha256.equalsIgnoreCase(hexDigest(digest))) {
    LittleFS.remove(WEB_TEMP[index]);
    error = "SHA256 errato: " + String(WEB_NAMES[index]);
    return false;
  }

  return true;
}

}

const char* githubFirmwareBoard() {
#if defined(SOLAR_BOARD_NODEMCU)
  return "nodemcuv2";
#elif defined(SOLAR_BOARD_D1_MINI)
  return "d1_mini";
#else
  // Evita aggiornamenti con firmware non identificabili.
  return "unknown";
#endif
}

bool githubFirmwareCheck(const char* installedVersion,
                         GithubFirmwareInfo& result,
                         String& error) {
  result=GithubFirmwareInfo();
  if(!networkReady(error)) return false;
  if(strcmp(githubFirmwareBoard(),"unknown")==0) {
    error="Scheda non identificata nella compilazione firmware";
    return false;
  }

  BearSSL::X509List roots(GH_TLS_CA_PEM);
  if(roots.getCount()==0) {
    error="Archivio CA HTTPS non disponibile";
    return false;
  }

  BearSSL::WiFiClientSecure client;
  HTTPClient http;
  if(!newHttpClient(client,roots,http,String(MANIFEST),error)) return false;
  http.addHeader("Cache-Control","no-cache");
  int status=http.GET();
  if(status!=HTTP_CODE_OK) {
    error="Manifest GitHub non disponibile. Codice HTTP: "+String(status);
    http.end();
    return false;
  }
  int length=http.getSize();
  if(length<1 || length>4096) {
    error="Dimensione manifest non valida";
    http.end();
    return false;
  }

  DynamicJsonDocument doc(3072);
  DeserializationError parseError=deserializeJson(doc,http.getStream(),DeserializationOption::NestingLimit(6));
  http.end();
  if(parseError || !doc.is<JsonObject>()) {
    error="Manifest firmware JSON non valido";
    return false;
  }

  String version=doc["version"].as<String>();
  String layout=doc["layout"].as<String>();
  JsonVariantConst board=doc["boards"][githubFirmwareBoard()];
  String sha=board["sha256"].as<String>();
  size_t size=board["size"] | 0UL;
  if(layout!=FLASH_LAYOUT || !validSha256(sha) || size<32768 ||
     size>1044464UL ) {
    error="Versione, layout, board o checksum non validi nel manifest";
    return false;
  }

  unsigned x=0,y=0,z=0;
  if(!parseVersion(version,x,y,z)) {
    error="Formato versione non valido";
    return false;
  }
  result.version=version;
  result.sha256=sha;
  result.size=size;
  result.available=newer(version,String(installedVersion));
  return true;
}

bool githubFirmwareInstall(const GithubFirmwareInfo& firmware,
                           String& error) {
  if(!firmware.available || !validSha256(firmware.sha256)) {
    error="Versione non installabile: controllare nuovamente GitHub";
    return false;
  }
  if(!networkReady(error)) return false;

  uint32_t maxSketch=(ESP.getFreeSketchSpace()-0x1000)&0xFFFFF000;
  if(firmware.size>maxSketch) {
    error="Firmware troppo grande per la partizione OTA";
    return false;
  }

  FSInfo info;
  if(!LittleFS.info(info)) {
    error="Filesystem LittleFS non disponibile";
    return false;
  }
  removeTemp();
  if(firmware.size+16384>info.totalBytes-info.usedBytes) {
    error="Spazio LittleFS insufficiente per la verifica preliminare; usare OTA manuale";
    return false;
  }

  BearSSL::X509List roots(GH_TLS_CA_PEM);
  if(roots.getCount()==0) {
    error="CA TLS mancanti";
    return false;
  }
  BearSSL::WiFiClientSecure client;
  HTTPClient http;
  if(!newHttpClient(client,roots,http,firmwareUrl(),error)) return false;
  int status=http.GET();
  if(status!=HTTP_CODE_OK) {
    error="Impossibile scaricare firmware. Codice HTTP: "+String(status);
    http.end();
    return false;
  }
  int receivedSize=http.getSize();
  if(receivedSize<0 || size_t(receivedSize)!=firmware.size) {
    error="Dimensione binaria non corrispondente al manifest";
    http.end();
    return false;
  }

  File stage=LittleFS.open(TEMP_FIRMWARE,"w");
  if(!stage) {
    error="Impossibile creare file temporaneo firmware";
    http.end();
    return false;
  }

  BearSSL::HashSHA256 hash;
  hash.begin();
  WiFiClient* stream=http.getStreamPtr();
  uint8_t buffer[512];
  size_t received=0;
  unsigned long lastActivity=millis();
  unsigned long start=millis();
  bool ok=true;

  while(received<firmware.size) {
    if(millis()-start>180000UL || millis()-lastActivity>15000UL) {
      error="Timeout scaricamento firmware";
      ok=false;
      break;
    }

    size_t available=stream->available();
    if(available==0) {
      delay(10);
      continue;
    }
    size_t count=available;
    if(count>sizeof(buffer)) count=sizeof(buffer);
    if(count>firmware.size-received) count=firmware.size-received;
    int read=stream->readBytes(buffer,count);
    if(read<=0 || stage.write(buffer,read)!=size_t(read)) {
      error="Scrittura temporanea LittleFS fallita";
      ok=false;
      break;
    }
    hash.add(buffer,read);
    received+=read;
    lastActivity=millis();
    yield();
  }
  stage.flush();
  stage.close();
  http.end();

  if(!ok || received!=firmware.size) {
    removeTemp();
    return false;
  }

  hash.end();
  const uint8_t* digest=static_cast<const uint8_t*>(hash.hash());
  char hex[65];
  for(int i=0;i<32;i++) {
    snprintf(hex+i*2,3,"%02x",digest[i]);
  }
  hex[64]=0;
  if(!firmware.sha256.equalsIgnoreCase(String(hex))) {
    removeTemp();
    error="Checksum SHA256 NON valido: firmware rifiutato";
    return false;
  }

  stage=LittleFS.open(TEMP_FIRMWARE,"r");
  if(!stage) {
    error="Riapertura file firmware fallita";
    removeTemp();
    return false;
  }
  if(!Update.begin(firmware.size,U_FLASH)) {
    error="Update.begin fallito: "+Update.getErrorString();
    stage.close();
    removeTemp();
    return false;
  }

  // Il nuovo firmware si avvia solo se TUTTE le scritture riescono e
  // Update.end() completa la verifica del formato ESP8266.
  size_t written=0;
  bool writeOk=true;
  while(written<firmware.size) {
    int count=stage.read(buffer,sizeof(buffer));
    if(count<=0 || Update.write(buffer,count)!=size_t(count)) {
      error="Scrittura flash OTA incompleta";
      writeOk=false;
      break;
    }
    written+=count;
    yield();
  }
  stage.close();
  removeTemp();
  if(!writeOk || written!=firmware.size) {
    // Nessuna chiamata ad Update.end(true): non si accetta un'immagine parziale.
    return false;
  }
  if(!Update.end(false)) {
    error="Finalizzazione OTA fallita: "+Update.getErrorString();
    return false;
  }
  return true;
}
