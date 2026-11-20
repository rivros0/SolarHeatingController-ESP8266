#pragma once

#include <Arduino.h>
#include <stddef.h>

// ============================================================================
// MANIFEST DEL CANALE STABILE GITHUB
//
// Firmware e dashboard hanno aggiornamenti indipendenti: la dashboard puo'
// cambiare senza un cambio di versione del firmware. I tre file web sono gli
// unici autorizzati a essere sostituiti in LittleFS. Configurazioni private,
// password, modalita' e codici IR RAW non vengono mai toccati.
// ============================================================================
static const size_t GITHUB_WEB_ASSET_COUNT = 3;

struct GithubWebAssetInfo {
  String sha256;
  size_t size = 0;
  bool needsUpdate = false;
};

struct GithubFirmwareInfo {
  String version;
  String sha256;
  size_t size = 0;

  // Flag precedente conservato: indica esclusivamente nuovo firmware.
  bool available = false;

  bool webSupported = false;
  bool webAvailable = false;
  GithubWebAssetInfo web[GITHUB_WEB_ASSET_COUNT];

  bool anythingAvailable() const {
    return available || webAvailable;
  }
};

// Controlla HTTPS, versione, layout, scheda, SHA256 e stato dei file web.
// I file locali vengono confrontati in streaming, senza alterare LittleFS.
bool githubFirmwareCheck(const char* installedVersion,
                         GithubFirmwareInfo& result,
                         String& error);

// Scarica e scrive esclusivamente una nuova immagine firmware verificata.
// Mantiene LittleFS e la procedura OTA precedente.
bool githubFirmwareInstall(const GithubFirmwareInfo& firmware,
                           String& error);

// Aggiorna in modo incrementale soltanto /index.html, /app.js e /style.css.
// Tutti i download sono verificati con SHA256 prima della sostituzione.
// Configurazioni, credenziali e catture IR non vengono alterate.
bool githubWebInstall(const GithubFirmwareInfo& firmware,
                      String& error);

// Elimina esclusivamente eventuali file temporanei OTA lasciati da un
// download interrotto. Non tocca altri file LittleFS.
void githubWebCleanup();

const char* githubFirmwareBoard();
