#pragma once
#include <Arduino.h>

// Metadati di un firmware pubblicato nella release "firmware-stable".
struct GithubFirmwareInfo {
  String version;
  String sha256;
  size_t size=0;
  bool available=false;
};

// Consultazione manuale della release stabile via HTTPS verificato.
bool githubFirmwareCheck(const char* installedVersion,
                         GithubFirmwareInfo& result,
                         String& error);

// Scarica, verifica SHA256, installa e richiede il riavvio al chiamante.
// NON riavvia il dispositivo e NON agisce sul climatizzatore.
bool githubFirmwareInstall(const GithubFirmwareInfo& firmware,
                           String& error);

const char* githubFirmwareBoard();
