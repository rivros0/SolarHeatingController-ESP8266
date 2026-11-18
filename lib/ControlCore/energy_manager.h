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

  // Supporto al comfort igrometrico (spento di default per retrocompatibilita').
  // Non e' una funzione DRY: accende solo il riscaldamento HEAT, se sicuro.
  bool humidityAssist = false;
  float humidityOn = 60.0f;
  float humidityOff = 55.0f;
};
struct EnergyData {
  Sample pv, load, battery, voltage, current, soc, importW, exportW;
};
enum class Action { None, On, Off };
enum class Mode { Auto, ForceOn, ForceOff, Diagnostic };
struct Decision { Action action; const char* reason; float available; bool critical; };
class EnergyManager {
  bool stable_ = false, low_ = false;
  // Isteresi UR indipendente da quella termica. Serve a evitare oscillazioni.
  bool humidityDemand_ = false;
  // Se l'ultimo ON era dovuto unicamente all'UR, consentiamo uno stop
  // quando l'umidita' e' rientrata, senza cambiare i cicli termici normali.
  bool humidityOnlyRun_ = false;
  bool pendingHumidityOnlyRun_ = false;
  uint32_t stableAt_ = 0, lowAt_ = 0, offAt_ = 0, onAt_ = 0;
 public:
  void resetTimers() {
    stable_ = false;
    low_ = false;
    pendingHumidityOnlyRun_ = false;
  }
  void changed(bool on, uint32_t now) {
    if (on) {
      onAt_ = now;
      humidityOnlyRun_ = pendingHumidityOnlyRun_;
    } else {
      offAt_ = now;
      humidityOnlyRun_ = false;
    }
    resetTimers();
  }

  bool humidityDemand() const { return humidityDemand_; }
  bool humidityOnlyRun() const { return humidityOnlyRun_; }
  Decision evaluate(uint32_t now, const EnergyConfig& c, const EnergyData& d,
                    const Sample& temperature, Mode mode, bool commandedOn,
                    bool mqttConnected, bool irReady, bool forceExpired,
                    const Sample& humidity = Sample{}) {
    // Se il sensore UR non e' aggiornato, non avviamo mai per umidita'.
    // Il controllo termico tradizionale rimane invece disponibile.
    bool humidityFresh = humidity.fresh(now,c.staleMs);
    if(!c.humidityAssist || !humidityFresh) {
      humidityDemand_ = false;
    } else {
      if(humidity.value >= c.humidityOn) {
        humidityDemand_ = true;
      }
      if(humidity.value <= c.humidityOff) {
        humidityDemand_ = false;
      }
    }

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

    // La temperatura massima e' stata gia' applicata sopra. Il supporto UR
    // puo' richiedere HEAT soltanto sotto quel limite, con margine di 0,5 C.
    // HEAT riduce l'UR per riscaldamento, ma non rimuove l'acqua dall'aria.
    const bool tempDemand = temperature.value < c.tempOn;
    const bool humidityHeatDemand = c.humidityAssist && humidityDemand_ &&
      temperature.value < c.tempOff - 0.5f;

    // Una richiesta termica successiva assume la priorita' su un avvio da UR:
    // resta quindi la normale isteresi termica fino a tempOff.
    if(commandedOn && tempDemand) {
      humidityOnlyRun_ = false;
    }

    // Se il ciclo era iniziato soltanto per UR e questa e' rientrata,
    // arrestiamo dopo il minimo funzionamento. Un'autentica richiesta termica
    // prevale sempre e mantiene la vecchia isteresi tempOn/tempOff.
    if(commandedOn && humidityOnlyRun_ && !humidityHeatDemand && !tempDemand) {
      stable_ = false;
      low_ = false;
      if(elapsed(now,onAt_) >= c.minRunMs) {
        return {Action::Off,"Umidita rientrata: fine supporto HEAT",available,false};
      }
      return {Action::None,"Umidita rientrata: attesa minimo funzionamento",available,false};
    }

    const bool importing = d.importW.valid && (!d.importW.fresh(now,c.staleMs) || d.importW.value > 50);
    const bool exportingStale = d.exportW.valid && !d.exportW.fresh(now,c.staleMs);
    const bool enough = !importing && !exportingStale && battery >= c.reserveW &&
                       available >= (commandedOn ? 0 : c.climateW);
    if (!commandedOn) {
      low_ = false;
      if (!enough || (!tempDemand && !humidityHeatDemand)) {
        stable_ = false;
        return {Action::None,"Attesa surplus/temperatura/umidita",available,false};
      }
      if (!stable_) {
        stable_ = true;
        stableAt_ = now;
      }
      if (elapsed(now,stableAt_) >= c.stableMs && elapsed(now,offAt_) >= c.restartMs) {
        pendingHumidityOnlyRun_ = humidityHeatDemand && !tempDemand;
        if(pendingHumidityOnlyRun_) {
          return {Action::On,"Surplus stabile e UR alta: supporto HEAT",available,false};
        }
        return {Action::On,"Surplus stabile e richiesta calore",available,false};
      }
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
