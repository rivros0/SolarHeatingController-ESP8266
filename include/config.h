#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "energy_manager.h"
#include "climate_power_detector.h"
struct Config {
  String ssid, wifiPassword, mqttHost, mqttUser, mqttPassword;
  String adminUser="admin", adminPassword;
  String discovery="solar_assistant/#";
  String topics[9];
  String protocol="RAW", powerKind="unverified", fan="auto";
  int model=1; float setpoint=22;
  uint32_t irFingerprint=0;
  uint16_t mqttPort=1883, mqttKeepalive=20;
  uint32_t sampleMs=5000, historyMs=30000, forceOnMs=3600000;
  float filterAlpha=0.25;
  bool receiver=false, irVerified=false, automationEnabled=false;
  solar::EnergyConfig energy;
  solar::DetectorConfig detector;
};
extern Config cfg;
extern bool storageReady, configReady;
const char* const metricNames[]={"pv","load","battery","voltage","current","soc","import","export","inverter"};
void configJson(JsonObject o,bool secrets);
bool parseConfig(JsonObjectConst o,Config& c,String& error);
bool saveConfig(const Config& c);
void loadConfig();
