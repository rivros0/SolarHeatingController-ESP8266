#include "history_store.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <cstring>

// ============================================================================
// FORMATO BINARY - 22 BYTE/CAMPIONE, 1.440 CAMPIONI/GIORNO ~= 31 KiB
//
// I 26 file orari ruotano per 26 ore: nel periodo di 24 ore nessuno
// slot ancora necessario viene sovrascritto.
// Ogni file inizia con magic+ora UTC; il controllo evita di riutilizzare
// record obsoleti dopo riavvii o salti dell'orologio.
// ============================================================================

static const uint32_t HISTORY_MAGIC = 0x31545348UL;  // "HST1"
static const uint32_t HISTORY_MIN_EPOCH = 1704067200UL;  // 2024-01-01 UTC
static const uint32_t HISTORY_WINDOW_MINUTES = 1440UL;
static const uint32_t HISTORY_SLOT_COUNT = 26UL;
static const uint32_t HISTORY_MINUTES_PER_HOUR = 60UL;

struct __attribute__((packed)) HistoryFileHeader {
  uint32_t magic;
  uint32_t hourUtc;
};

struct __attribute__((packed)) HistoryRecord {
  uint32_t minuteUtc;
  int32_t pvW;
  int32_t loadW;
  int32_t batteryW;
  int16_t tempTenths;
  int16_t humidityTenths;
  uint8_t flags;
  uint8_t checksum;
};

static_assert(sizeof(HistoryRecord)==22,
              "Formato dello storico modificato: aggiornare la migrazione.");
static_assert(sizeof(HistoryFileHeader)==8,
              "Intestazione storico incompatibile.");

static uint32_t writtenCount=0;
static uint32_t errorCount=0;

// L'ultimo minuto salvato viene verificato anche su LittleFS: il dato
// in RAM da solo non sopravvive ai riavvii.
static uint32_t lastWrittenMinute=0;

bool historyClockSynchronized(uint32_t nowUtc) {
  return nowUtc>=HISTORY_MIN_EPOCH;
}

uint32_t historyStoreWrittenCount() {
  return writtenCount;
}

uint32_t historyStoreErrorCount() {
  return errorCount;
}

// ---------------------------------------------------------------------------
// Primitive di serializzazione. Nessun dato viene interpretato senza
// controllare dimensioni del file, ora UTC e checksum.
// ---------------------------------------------------------------------------
static uint8_t historyChecksum(const HistoryRecord& record) {
  const uint8_t* bytes=reinterpret_cast<const uint8_t*>(&record);
  uint8_t checksum=0xA5;
  for(size_t index=0;index<sizeof(HistoryRecord)-1;index++) {
    checksum^=bytes[index];
  }
  return checksum;
}

static void historyFilename(uint32_t hourUtc,char* destination,size_t size) {
  snprintf(destination,size,"/hist_%02lu.bin",
           static_cast<unsigned long>(hourUtc%HISTORY_SLOT_COUNT));
}

static bool readHeader(File& file,HistoryFileHeader& header) {
  if(file.size()<sizeof(HistoryFileHeader)) return false;
  if(!file.seek(0,SeekSet)) return false;
  return file.read(reinterpret_cast<uint8_t*>(&header),sizeof(header))==
         sizeof(header);
}

static bool validHourFile(File& file,uint32_t hourUtc) {
  HistoryFileHeader header{};
  if(!readHeader(file,header)) return false;
  if(header.magic!=HISTORY_MAGIC || header.hourUtc!=hourUtc) return false;

  const size_t bodyLength=file.size()-sizeof(HistoryFileHeader);
  if(bodyLength%sizeof(HistoryRecord)!=0) return false;
  if(bodyLength>HISTORY_MINUTES_PER_HOUR*sizeof(HistoryRecord)) return false;
  return true;
}

static bool createHourFile(const char* path,uint32_t hourUtc) {
  File file=LittleFS.open(path,"w");
  if(!file) return false;

  HistoryFileHeader header={HISTORY_MAGIC,hourUtc};
  const bool ok=file.write(reinterpret_cast<const uint8_t*>(&header),
                           sizeof(header))==sizeof(header);
  file.close();
  return ok;
}

static bool lastMinuteInFile(File& file,uint32_t& minute) {
  const size_t fileSize=file.size();
  if(fileSize<sizeof(HistoryFileHeader)+sizeof(HistoryRecord)) return false;

  const size_t offset=fileSize-sizeof(HistoryRecord);
  if(!file.seek(offset,SeekSet)) return false;
  HistoryRecord last{};
  if(file.read(reinterpret_cast<uint8_t*>(&last),sizeof(last))!=sizeof(last)) {
    return false;
  }
  if(last.checksum!=historyChecksum(last)) return false;
  minute=last.minuteUtc;
  return true;
}

bool historyStoreAppend(const HistoryMinuteSample& sample) {
  if(!historyClockSynchronized(sample.minuteUtc*60UL)) return false;
  if(sample.minuteUtc==lastWrittenMinute) return true;

  const uint32_t hourUtc=sample.minuteUtc/HISTORY_MINUTES_PER_HOUR;
  char path[20];
  historyFilename(hourUtc,path,sizeof(path));

  // Un file precedente appartenente allo stesso slot ma con ora diversa
  // va sostituito; un file non allineato o corrotto viene reinizializzato.
  bool validFile=false;
  File previous=LittleFS.open(path,"r");
  if(previous) {
    validFile=validHourFile(previous,hourUtc);
    if(validFile) {
      uint32_t existingMinute=0;
      if(lastMinuteInFile(previous,existingMinute)) {
        if(existingMinute>=sample.minuteUtc) {
          previous.close();
          lastWrittenMinute=sample.minuteUtc;
          return true;
        }
      }
    }
    previous.close();
  }

  if(!validFile && !createHourFile(path,hourUtc)) {
    errorCount++;
    return false;
  }

  HistoryRecord record{};
  record.minuteUtc=sample.minuteUtc;
  record.pvW=sample.pvW;
  record.loadW=sample.loadW;
  record.batteryW=sample.batteryW;
  record.tempTenths=sample.tempTenths;
  record.humidityTenths=sample.humidityTenths;
  record.flags=sample.flags;
  record.checksum=historyChecksum(record);

  File out=LittleFS.open(path,"a");
  if(!out) {
    errorCount++;
    return false;
  }
  const bool written=out.write(reinterpret_cast<const uint8_t*>(&record),
                               sizeof(record))==sizeof(record);
  out.close();
  if(!written) {
    errorCount++;
    return false;
  }

  writtenCount++;
  lastWrittenMinute=sample.minuteUtc;
  return true;
}

// ---------------------------------------------------------------------------
// API STREAMING: 1.440 righe circa, senza allocare array o JSON completo.
//
// Il server emette blocchi da ~1 KiB anziche' 1.440 piccole scritture.
// I valori NULL mantengono i gap grafici, senza inventare misurazioni.
// ---------------------------------------------------------------------------
void historyStoreSendJson(ESP8266WebServer& server,uint32_t nowUtc) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Cache-Control","no-store");
  server.send(200,"application/json","");
  server.sendContent("[");

  if(!historyClockSynchronized(nowUtc)) {
    server.sendContent("]");
    server.sendContent("");
    return;
  }

  const uint32_t currentMinute=nowUtc/60UL;
  const uint32_t startMinute=currentMinute-HISTORY_WINDOW_MINUTES+1UL;
  const uint32_t firstHour=startMinute/HISTORY_MINUTES_PER_HOUR;
  const uint32_t lastHour=currentMinute/HISTORY_MINUTES_PER_HOUR;

  String buffer;
  buffer.reserve(1200);
  bool first=true;

  for(uint32_t hour=firstHour;hour<=lastHour;hour++) {
    char path[20];
    historyFilename(hour,path,sizeof(path));
    File file=LittleFS.open(path,"r");

    if(!file) continue;
    if(!validHourFile(file,hour)) {
      file.close();
      continue;
    }

    if(!file.seek(sizeof(HistoryFileHeader),SeekSet)) {
      file.close();
      continue;
    }

    const size_t recordCount=
      (file.size()-sizeof(HistoryFileHeader))/sizeof(HistoryRecord);

    for(size_t index=0;index<recordCount;index++) {
      HistoryRecord sample{};
      const size_t read=file.read(reinterpret_cast<uint8_t*>(&sample),
                                  sizeof(sample));
      if(read!=sizeof(sample)) break;
      if(sample.checksum!=historyChecksum(sample)) continue;
      if(sample.minuteUtc<startMinute || sample.minuteUtc>currentMinute) continue;
      if(sample.minuteUtc/HISTORY_MINUTES_PER_HOUR!=hour) continue;

      StaticJsonDocument<224> doc;
      doc["s"]=sample.minuteUtc*60UL;

      if(sample.flags&HISTORY_ENERGY_VALID) {
        doc["pv"]=sample.pvW;
        doc["load"]=sample.loadW;
        doc["battery"]=sample.batteryW;
      } else {
        doc["pv"]=nullptr;
        doc["load"]=nullptr;
        doc["battery"]=nullptr;
      }

      if(sample.flags&HISTORY_ENV_VALID) {
        doc["t"]=sample.tempTenths/10.0f;
        doc["h"]=sample.humidityTenths/10.0f;
      } else {
        doc["t"]=nullptr;
        doc["h"]=nullptr;
      }

      doc["on"]=(sample.flags&HISTORY_IR_ON)!=0;
      doc["known"]=(sample.flags&HISTORY_IR_KNOWN)!=0;

      String row;
      serializeJson(doc,row);
      if(!first) buffer+=",";
      buffer+=row;
      first=false;

      if(buffer.length()>1050) {
        server.sendContent(buffer);
        buffer="";
        yield();
      }
    }
    file.close();
  }

  if(buffer.length()>0) server.sendContent(buffer);
  server.sendContent("]");
  server.sendContent("");
}
