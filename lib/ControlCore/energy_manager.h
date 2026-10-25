#pragma once
#include <cmath>
#include <cstdint>
namespace solar {
inline uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }
struct Sample {
  float value = 0; uint32_t at = 0; bool valid = false;
  bool fresh(uint32_t now, uint32_t timeout) const {
    return valid && std::isfinite(value) && elapsed(now, at) <= timeout;
  }
};
struct EnergyConfig {
  float tempOn = 19, tempOff = 21, reserveW = 500, climateW = 1200;
  float dischargeW = 100, minVoltage = 0, maxDischargeA = 0;
  uint32_t stableMs = 600000, dropMs = 300000, restartMs = 600000;
  uint32_t minRunMs = 300000, staleMs = 30000;
  bool negativeCharge = false, negativeCurrentCharge = false;
};
struct EnergyData {
  Sample pv, load, battery, voltage, current, soc, importW, exportW;
};
enum class Action { None, On, Off };
enum class Mode { Auto, ForceOn, ForceOff, Diagnostic };
struct Decision { Action action; const char* reason; float available; bool critical; };
class EnergyManager {
  bool stable_ = false, low_ = false;
  uint32_t stableAt_ = 0, lowAt_ = 0, offAt_ = 0, onAt_ = 0;
 public:
  void resetTimers() { stable_ = low_ = false; }
  void changed(bool on, uint32_t now) {
    if (on) onAt_ = now; else offAt_ = now;
    resetTimers();
  }
  Decision evaluate(uint32_t now, const EnergyConfig& c, const EnergyData& d,
                    const Sample& temperature, Mode mode, bool commandedOn,
                    bool mqttConnected, bool irReady, bool forceExpired) {
    const bool required = mqttConnected && d.pv.fresh(now,c.staleMs) &&
      d.load.fresh(now,c.staleMs) && d.battery.fresh(now,c.staleMs);
    const bool env = temperature.fresh(now,c.staleMs);
    const float battery = (c.negativeCharge ? -1 : 1) * d.battery.value;
    // load includes the running climate. Battery/grid flows are NOT subtracted
    // a second time. Grid measurements and actual charge act as independent vetoes.
    const float available = d.pv.value - d.load.value - c.reserveW;
    const bool voltageNeeded = c.minVoltage > 0;
    const bool currentNeeded = c.maxDischargeA > 0;
    const bool protectionMissing = (voltageNeeded && !d.voltage.fresh(now,c.staleMs)) ||
                                  (currentNeeded && !d.current.fresh(now,c.staleMs));
    const bool batteryCritical = required && (battery < -c.dischargeW ||
      (voltageNeeded && d.voltage.fresh(now,c.staleMs) && d.voltage.value < c.minVoltage) ||
      (currentNeeded && d.current.fresh(now,c.staleMs) &&
        (c.negativeCurrentCharge ? -d.current.value : d.current.value) < -c.maxDischargeA));
    const bool critical = !required || !env || protectionMissing || batteryCritical;
    const char* failure = !required ? "Dati energia assenti/scaduti" : !env ? "Sensore ambiente non valido" :
      protectionMissing ? "Telemetria protezione AGM assente" : "Protezione batteria";
    if (critical) {
      resetTimers();
      return {commandedOn ? Action::Off : Action::None, failure, available, true};
    }
    if (mode == Mode::ForceOff) { resetTimers(); return {commandedOn ? Action::Off : Action::None,"Forza OFF",available,false}; }
    if (mode == Mode::Diagnostic) { resetTimers(); return {Action::None,"Diagnostica: automazione sospesa",available,false}; }
    if (temperature.value >= c.tempOff) {
      resetTimers(); return {commandedOn ? Action::Off : Action::None,"Temperatura massima raggiunta",available,false};
    }
    if (mode == Mode::ForceOn) {
      resetTimers();
      if (forceExpired) return {commandedOn ? Action::Off : Action::None,"Limite Forza ON raggiunto",available,false};
      if (!commandedOn && elapsed(now,offAt_) < c.restartMs) return {Action::None,"Attesa anti-riavvio",available,false};
      return {!commandedOn && irReady ? Action::On : Action::None,"Forza ON",available,false};
    }
    if (!irReady) { resetTimers(); return {Action::None,"IR discreti non verificati",available,false}; }
    const bool importing = d.importW.valid && (!d.importW.fresh(now,c.staleMs) || d.importW.value > 50);
    const bool exportingStale = d.exportW.valid && !d.exportW.fresh(now,c.staleMs);
    const bool enough = !importing && !exportingStale && battery >= c.reserveW &&
                       available >= (commandedOn ? 0 : c.climateW);
    if (!commandedOn) {
      low_ = false;
      if (!enough || temperature.value >= c.tempOn) { stable_ = false; return {Action::None,"Attesa surplus/temperatura",available,false}; }
      if (!stable_) { stable_ = true; stableAt_ = now; }
      if (elapsed(now,stableAt_) >= c.stableMs && elapsed(now,offAt_) >= c.restartMs)
        return {Action::On,"Surplus stabile e richiesta calore",available,false};
      return {Action::None,"Verifica stabilita e anti-riavvio",available,false};
    }
    stable_ = false;
    if (enough) { low_ = false; return {Action::None,"Riscaldamento con riserva batterie",available,false}; }
    if (!low_) { low_ = true; lowAt_ = now; }
    if (elapsed(now,lowAt_) >= c.dropMs && elapsed(now,onAt_) >= c.minRunMs)
      return {Action::Off,"Riserva carica/surplus insufficiente",available,false};
    return {Action::None,"Verifica calo produzione",available,false};
  }
};
}
