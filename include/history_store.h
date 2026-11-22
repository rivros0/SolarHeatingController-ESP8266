#pragma once

#include <Arduino.h>
#include <ESP8266WebServer.h>

// ============================================================================
// STORICO CIRCOLARE 24H SU LITTLEFS - UNA MISURA OGNI MINUTO
//
// L'ESP8266 non dispone della RAM necessaria per memorizzare 1.440 record
// completi. Lo storico usa piccoli file orari, con 26 slot rotativi.
//
// Gli istanti UTC derivano da NTP (gia' usato dall'OTA HTTPS).
// Un orologio non ancora sincronizzato NON genera campioni con date false.
// Tutti i file dello storico sono separati da configurazione e codici IR.
// ============================================================================

// Ogni campione contiene dati numerici e flag di validita'. I valori
// energetici sono in watt interi; temperatura/umidita' in decimi.
struct HistoryMinuteSample {
  uint32_t minuteUtc = 0;
  int32_t pvW = 0;
  int32_t loadW = 0;
  int32_t batteryW = 0;
  int16_t tempTenths = 0;
  int16_t humidityTenths = 0;
  uint8_t flags = 0;
};

enum HistorySampleFlags : uint8_t {
  HISTORY_ENERGY_VALID = 1,
  HISTORY_ENV_VALID = 2,
  HISTORY_IR_ON = 4,
  HISTORY_IR_KNOWN = 8
};

// Campioni campionati a intervallo >= 60s; la funzione non fa formattazioni.
bool historyStoreAppend(const HistoryMinuteSample& sample);

// Restituisce 24h UTC senza caricare l'intera serie in RAM:
// risposta JSON streaming compatibile con la precedente /api/history.
void historyStoreSendJson(ESP8266WebServer& server, uint32_t nowUtc);

// L'orologio deve essere plausibile: blocca dati senza timestamp autentico.
bool historyClockSynchronized(uint32_t nowUtc);

// Verifica di integrita' e statistiche non persistenti (monitoraggio).
uint32_t historyStoreWrittenCount();
uint32_t historyStoreErrorCount();
