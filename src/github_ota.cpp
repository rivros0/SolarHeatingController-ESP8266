#include "github_ota.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <StreamString.h>
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

bool fileMatchesSha256(const char* path, size_t expectedSize, const String& sha) {
  File input = LittleFS.open(path, "r");
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

bool webFileMatches(size_t index, size_t expectedSize, const String& sha) {
  return fileMatchesSha256(WEB_LOCAL[index], expectedSize, sha);
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

  // HTTPClient trasferisce e decodifica il body completo (Content-Length
  // o chunked) prima della verifica. Non leggere il WiFiClient grezzo:
  // eventuali delimitatori HTTP provocherebbero falsi checksum.
  const int transferred = http.writeToStream(&stage);
  stage.flush();
  stage.close();
  http.end();

  if(transferred < 0 || size_t(transferred) != asset.size) {
    LittleFS.remove(WEB_TEMP[index]);
    error = "Download web incompleto: " + String(WEB_NAMES[index]) +
      " (HTTP " + String(transferred) + " byte, attesi " +
      String(asset.size) + ")";
    return false;
  }

  // Il file verificato e' quello realmente salvato nella flash,
  // non un hash calcolato su dati eventualmente persi in scrittura.
  if(!fileMatchesSha256(WEB_TEMP[index], asset.size, asset.sha256)) {
    LittleFS.remove(WEB_TEMP[index]);
    error = "SHA256 o dimensione errata: " + String(WEB_NAMES[index]);
    return false;
  }

  return true;
}

}

bool githubAnythingAvailable(const GithubFirmwareInfo& info) {
  return info.available || info.webAvailable;
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
  if(length<1 || length>6144) {
    error="Dimensione manifest non valida";
    http.end();
    return false;
  }

  // ========================================================================
  // LETTURA MANIFEST ROBUSTA
  //
  // IMPORTANTE: http.getStream() espone il WiFiClient grezzo, non il body
  // decodificato dall'HTTPClient. ArduinoJson puo' quindi ricevere byte
  // parziali o chunk HTTP e restituire IncompleteInput/InvalidInput.
  //
  // HTTPClient::writeToStream gestisce Content-Length, chunked transfer,
  // timeout e connessioni interrotte. Interpretiamo JSON SOLO dopo avere
  // verificato la ricezione di tutti i byte promessi dal server.
  // ========================================================================
  StreamString payload;
  if(!payload.reserve(size_t(length) + 1)) {
    error="Memoria insufficiente per il manifest GitHub";
    http.end();
    return false;
  }

  const int downloaded=http.writeToStream(&payload);
  http.end();

  if(downloaded < 0) {
    error="Ricezione HTTP manifest fallita (codice "+String(downloaded)+")";
    Serial.println(String("[OTA] ") + error);
    return false;
  }
  if(downloaded != length || payload.length() != size_t(length)) {
    error="Manifest HTTP incompleto: attesi "+String(length)+
          " byte, ricevuti "+String(payload.length());
    Serial.println(String("[OTA] ") + error);
    return false;
  }

  DynamicJsonDocument doc(6144);
  DeserializationError parseError=deserializeJson(
    doc, payload.c_str(), DeserializationOption::NestingLimit(6)
  );
  if(parseError || !doc.is<JsonObject>()) {
    error="Manifest firmware JSON non valido (ArduinoJson: "+
          String(parseError.c_str())+", byte="+String(payload.length())+")";
    Serial.println(String("[OTA] ") + error);
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

  // Manifest legacy: il firmware e' aggiornabile anche senza sezione web.
  // Quando presente, la sezione web deve essere completa e valida.
  JsonVariantConst webManifest = doc["web"];
  if(!webManifest.isNull()) {
    if(!webManifest.is<JsonObjectConst>()) {
      error = "Manifest web non valido";
      return false;
    }

    result.webSupported = true;
    size_t totalSize = 0;

    for(size_t index = 0; index < GITHUB_WEB_ASSET_COUNT; index++) {
      JsonVariantConst asset = webManifest[WEB_NAMES[index]];
      if(!asset.is<JsonObjectConst>()) {
        error = "Asset web mancante nel manifest: " + String(WEB_NAMES[index]);
        return false;
      }

      GithubWebAssetInfo& entry = result.web[index];
      entry.sha256 = asset["sha256"].as<String>();
      entry.size = asset["size"] | 0UL;

      if(!validSha256(entry.sha256) ||
         entry.size == 0 ||
         entry.size > MAX_WEB_FILE_BYTES ||
         totalSize + entry.size > MAX_WEB_TOTAL_BYTES) {
        error = "Asset web non valido: " + String(WEB_NAMES[index]);
        return false;
      }

      totalSize += entry.size;
      entry.needsUpdate = !webFileMatches(index, entry.size, entry.sha256);
      if(entry.needsUpdate) {
        result.webAvailable = true;
      }
      yield();
    }
  }

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

  // Il body e' letto dal layer HTTPClient anziche' dal TLS socket grezzo:
  // questo gestisce correttamente i blocchi del protocollo HTTP.
  const int downloaded = http.writeToStream(&stage);
  stage.flush();
  stage.close();
  http.end();

  if(downloaded < 0 || size_t(downloaded) != firmware.size) {
    removeTemp();
    error = "Download firmware incompleto: attesi " + String(firmware.size) +
            " byte, ricevuti/codice HTTP " + String(downloaded);
    return false;
  }

  // Calcola SHA256 sul file realmente scritto in LittleFS prima di avviare
  // l'Updater. Il firmware non verra' mai programmato se la verifica fallisce.
  if(!fileMatchesSha256(TEMP_FIRMWARE, firmware.size, firmware.sha256)) {
    removeTemp();
    error = "SHA256 o dimensione firmware non valida: aggiornamento rifiutato";
    return false;
  }

  uint8_t buffer[512];
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


// ============================================================================
// AGGIORNAMENTO WEB SELETTIVO
//
// Scarica soltanto i tre asset pubblici dichiarati nel manifest. Tutti
// vengono verificati prima della promozione su LittleFS; il filesystem non
// viene mai formattato. Una perdita di corrente durante la promozione puo'
// lasciare asset di due revisioni, ma il prossimo controllo checksum rileva
// i singoli file ancora da aggiornare e consente di completare il ripristino.
// ============================================================================

void githubWebCleanup() {
  for(size_t index = 0; index < GITHUB_WEB_ASSET_COUNT; index++) {
    if(LittleFS.exists(WEB_TEMP[index])) {
      LittleFS.remove(WEB_TEMP[index]);
    }
  }
}

bool githubWebInstall(const GithubFirmwareInfo& firmware, String& error) {
  if(!firmware.webSupported) {
    error = "Manifest GitHub privo degli asset web verificabili";
    return false;
  }

  if(!firmware.webAvailable) {
    return true;
  }

  if(!networkReady(error)) {
    return false;
  }

  FSInfo filesystem;
  if(!LittleFS.info(filesystem)) {
    error = "LittleFS non disponibile";
    return false;
  }

  // Elimina soltanto residui temporanei di precedenti aggiornamenti.
  // Nessuna configurazione privata viene toccata.
  githubWebCleanup();

  size_t stagingBytes = 0;
  for(size_t index = 0; index < GITHUB_WEB_ASSET_COUNT; index++) {
    if(firmware.web[index].needsUpdate) {
      stagingBytes += firmware.web[index].size;
    }
  }

  if(filesystem.totalBytes < filesystem.usedBytes ||
     stagingBytes + WEB_FS_MARGIN_BYTES >
       filesystem.totalBytes - filesystem.usedBytes) {
    error = "Spazio LittleFS insufficiente per i file web temporanei";
    return false;
  }

  // Fase 1: tutti i file necessari sono salvati e verificati SHA256.
  // Un errore di rete lascia intatta la dashboard precedentemente attiva.
  for(size_t index = 0; index < GITHUB_WEB_ASSET_COUNT; index++) {
    if(!firmware.web[index].needsUpdate) {
      continue;
    }

    if(!downloadVerifiedWebAsset(index, firmware.web[index], error)) {
      githubWebCleanup();
      return false;
    }
  }

  // Fase 2: promozione con rename atomico del singolo file.
  // I nomi dei file di destinazione sono fissati nel firmware.
  // Un'interruzione non cancella credenziali/configurazione/codici IR.
  for(size_t index = 0; index < GITHUB_WEB_ASSET_COUNT; index++) {
    if(!firmware.web[index].needsUpdate) {
      continue;
    }

    if(!LittleFS.rename(WEB_TEMP[index], WEB_LOCAL[index])) {
      error = "Impossibile applicare asset web: " + String(WEB_NAMES[index]) +
              ". Ripetere verifica aggiornamenti.";
      githubWebCleanup();
      return false;
    }
  }

  githubWebCleanup();
  return true;
}
