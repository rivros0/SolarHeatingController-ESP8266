#include <iostream>
#include <limits>
#include <stdexcept>
#include "energy_manager.h"
#include "environment_manager.h"
#include "climate_power_detector.h"
using namespace solar;
int checks=0;
#define CHECK(x) do{checks++;if(!(x))throw std::runtime_error(#x);}while(0)
struct Fixture {
  EnergyConfig c; EnergyData d; Sample t; Sample humidity; EnergyManager m; uint32_t now=1000000;
  Fixture(){c.stableMs=10000;c.restartMs=60000;c.dropMs=10000;c.minRunMs=30000;live();}
  void live(float pv=4000,float load=1000,float battery=3000,float temp=18,float rh=50) {
    d.pv={pv,now,true};d.load={load,now,true};d.battery={battery,now,true};
    t={temp,now,true};
    humidity={rh,now,true};
  }
  Decision step(bool on=false,Mode mode=Mode::Auto,bool connected=true,bool ready=true,bool expired=false) {
    return m.evaluate(now,c,d,t,mode,on,connected,ready,expired,humidity);
  }
};
void energyTests(){
  {Fixture f;CHECK(f.step().action==Action::None);f.now+=10000;f.live();CHECK(f.step().action==Action::On);}
  {Fixture f;f.live(2500,1000,1500);CHECK(f.step().available==1000);f.now+=20000;f.live(2500,1000,1500);CHECK(f.step().action==Action::None);}
  // Once running, its 1200 W is already inside total load: no second subtraction.
  {Fixture f;f.m.changed(true,f.now);f.live(4000,2200,1800);CHECK(f.step(true).available==1300);f.now+=40000;f.live(4000,2200,1800);CHECK(f.step(true).action==Action::None);}
  {Fixture f;f.step();f.now+=5000;f.live(1000,1000,0);f.step();f.now+=5000;f.live();CHECK(f.step().action==Action::None);f.now+=10000;f.live();CHECK(f.step().action==Action::On);}
  {Fixture f;f.m.changed(false,f.now);f.step();f.now+=10000;f.live();CHECK(f.step().action==Action::None);f.now+=50000;f.live();CHECK(f.step().action==Action::On);}
  {Fixture f;f.m.changed(true,f.now);f.live(1500,1800,-150);CHECK(f.step(true).action==Action::Off);CHECK(f.step(true).critical);}
  {Fixture f;f.m.changed(true,f.now);f.live(1500,1400,100);CHECK(f.step(true).action==Action::None);f.now+=10000;f.live(1500,1400,100);CHECK(f.step(true).action==Action::None);f.now+=20000;f.live(1500,1400,100);CHECK(f.step(true).action==Action::Off);}
  {Fixture f;f.now+=31000;CHECK(f.step().action==Action::None);CHECK(f.step(true).action==Action::Off);CHECK(f.step(true).critical);}
  {Fixture f;CHECK(f.step(true,Mode::ForceOn,false).action==Action::Off);f.t.valid=false;CHECK(f.step(true,Mode::ForceOn).action==Action::Off);}
  {Fixture f;f.c.negativeCharge=true;f.live(4000,1000,-3000);f.step();f.now+=10000;f.live(4000,1000,-3000);CHECK(f.step().action==Action::On);f.live(4000,1000,101);CHECK(f.step(true).critical);}
  {Fixture f;f.live(4000,1000,3000,21);f.m.changed(true,f.now);CHECK(f.step(true).action==Action::Off);CHECK(f.step(false,Mode::ForceOn).action==Action::None);}
  {Fixture f;CHECK(f.step(true,Mode::ForceOff).action==Action::Off);CHECK(f.step(false,Mode::ForceOff).action==Action::None);CHECK(f.step(true,Mode::Diagnostic).action==Action::None);}
  {Fixture f;CHECK(f.step(false,Mode::ForceOn).action==Action::On);CHECK(f.step(true,Mode::ForceOn,true,true,true).action==Action::Off);}
  {Fixture f;f.c.minVoltage=47;CHECK(f.step().critical);f.d.voltage={46,f.now,true};CHECK(f.step(true).action==Action::Off);f.d.voltage.value=49;CHECK(!f.step().critical);}
  {Fixture f;f.c.maxDischargeA=10;CHECK(f.step().critical);f.d.current={-11,f.now,true};CHECK(f.step(true).critical);f.d.current.value=1;CHECK(!f.step().critical);}
  {Fixture f;f.d.importW={600,f.now,true};f.step();f.now+=10000;f.live();CHECK(f.step().action==Action::None);}
  {Fixture f;CHECK(f.step(false,Mode::Auto,true,false).action==Action::None);}
  {Fixture f;f.d.pv.value=std::numeric_limits<float>::quiet_NaN();CHECK(f.step(true).critical);}
  {Fixture f;f.now=UINT32_MAX-5000;f.live();f.m.changed(false,f.now-100000);CHECK(f.step().action==Action::None);f.now+=10000;f.live();CHECK(f.step().action==Action::On);}
}
// ============================================================================
// REGRESSIONI ASSISTENZA UMIDITA' - NON CAMBIANO LE PROTEZIONI PREESISTENTI
// ============================================================================
void humidityTests() {
  // La funzione e' facoltativa: senza opt-in l'UR non puo' accendere HEAT.
  {
    Fixture f;
    f.live(4000,1000,3000,20,75);
    f.step();
    f.now+=10000;
    f.live(4000,1000,3000,20,75);
    CHECK(f.step().action==Action::None);
  }

  // Attivazione con UR elevata e temperatura nel margine ammesso.
  {
    Fixture f;
    f.c.humidityAssist=true;
    f.live(4000,1000,3000,20,65);
    CHECK(f.step().action==Action::None);
    f.now+=10000;
    f.live(4000,1000,3000,20,65);
    CHECK(f.step().action==Action::On);
    f.m.changed(true,f.now);
    CHECK(f.m.humidityOnlyRun());

    // L'isteresi impedisce un arresto se 55 < UR < 60.
    f.now+=10000;
    f.live(4000,2200,1800,20,58);
    CHECK(f.step(true).action==Action::None);

    // Dopo rientro sotto 55%, resta valido il tempo minimo di marcia.
    f.now+=10000;
    f.live(4000,2200,1800,20,54);
    CHECK(f.step(true).action==Action::None);
    f.now+=10000;
    f.live(4000,2200,1800,20,54);
    CHECK(f.step(true).action==Action::Off);
  }

  // Se l'ambiente diventa freddo, la richiesta termica prende priorita':
  // il funzionamento deve continuare fino al normale limite di temperatura.
  {
    Fixture f;
    f.c.humidityAssist=true;
    f.live(4000,1000,3000,20,67);
    f.step();
    f.now+=10000;
    f.live(4000,1000,3000,20,67);
    CHECK(f.step().action==Action::On);
    f.m.changed(true,f.now);
    f.now+=10000;
    f.live(4000,2200,1800,18,50);
    CHECK(f.step(true).action==Action::None);
    CHECK(!f.m.humidityOnlyRun());
    f.now+=20000;
    f.live(4000,2200,1800,20,50);
    CHECK(f.step(true).action==Action::None);
    f.live(4000,2200,1800,21,50);
    CHECK(f.step(true).action==Action::Off);
  }

  // Non si puo' avviare solo per UR quando il limite termico e' raggiunto.
  {
    Fixture f;
    f.c.humidityAssist=true;
    f.live(4000,1000,3000,21,85);
    CHECK(f.step().action==Action::None);
    f.now+=20000;
    f.live(4000,1000,3000,21,85);
    CHECK(f.step().action==Action::None);
  }

  // I campioni di UR scaduti/non validi non generano nuovi comandi.
  {
    Fixture f;
    f.c.humidityAssist=true;
    f.live(4000,1000,3000,20,70);
    f.humidity.valid=false;
    f.step();
    f.now+=20000;
    f.live(4000,1000,3000,20,70);
    f.humidity.valid=false;
    CHECK(f.step().action==Action::None);
  }

  // La richiesta da umidita' rispetta anche il surplus reale.
  {
    Fixture f;
    f.c.humidityAssist=true;
    f.live(1500,1000,3000,20,70);
    f.step();
    f.now+=20000;
    f.live(1500,1000,3000,20,70);
    CHECK(f.step().action==Action::None);
  }

  // Il consenso IR non puo' essere aggirato dal controllo dell'UR.
  {
    Fixture f;
    f.c.humidityAssist=true;
    f.live(4000,1000,3000,20,70);
    f.step(false,Mode::Auto,true,false);
    f.now+=20000;
    f.live(4000,1000,3000,20,70);
    CHECK(f.step(false,Mode::Auto,true,false).action==Action::None);
  }
}

void currentSignTests(){Fixture f;f.c.maxDischargeA=10;f.c.negativeCurrentCharge=true;f.d.current={11,f.now,true};CHECK(f.step(true).critical);f.d.current.value=-11;CHECK(!f.step(true).critical);f.c.negativeCharge=true;f.live(4000,1000,-3000);CHECK(!f.step(true).critical);}
void environmentTests(){EnvironmentManager e;e.update(20,50,1000,.25);CHECK(e.temperature.value==20);e.update(24,60,2000,.25);CHECK(e.temperature.value==21);CHECK(e.humidity.value==52.5);e.update(NAN,50,3000,.25);CHECK(!e.temperature.valid);e.update(18,45,4000,.25);CHECK(e.temperature.value==18);e.update(20,101,5000,.25);CHECK(!e.humidity.valid);}
void detectorTests(){
  DetectorConfig c;c.baselineMs=2000;c.startupMs=3000;c.observationMs=2000;c.deltaW=400;c.maxNoiseW=100;
  PowerDetector d;CHECK(!d.command(true,0));for(uint32_t t=0;t<=2000;t+=1000) { d.sample(500,t,c); }CHECK(d.baselineReady);CHECK(d.command(true,3000));d.sample(500,4000,c);CHECK(d.confidence==0);for(uint32_t t=6000;t<=8000;t+=1000) { d.sample(1700,t,c); }CHECK(std::string(d.estimated)=="likely_on");CHECK(d.confidence<1);CHECK(d.delta==1200);
  for(uint32_t t=9000;t<=11000;t+=1000) { d.sample(1700,t,c); }CHECK(d.command(false,12000));for(uint32_t t=15000;t<=17000;t+=1000) { d.sample(500,t,c); }CHECK(std::string(d.estimated)=="likely_off");
  d.invalidate();CHECK(!d.baselineReady);CHECK(std::string(d.estimated)=="unknown");
  for(uint32_t t=18000;t<=20000;t+=1000) { d.sample(500,t,c); }d.command(true,21000);d.sample(300,24000,c);d.sample(3000,25000,c);d.sample(1800,26000,c);CHECK(std::string(d.estimated)=="unknown");CHECK(d.confidence<.5);
  d.invalidate();d.sample(NAN,27000,c);CHECK(!d.baselineReady);
}
int main(){try{energyTests();humidityTests();currentSignTests();environmentTests();detectorTests();std::cout<<checks<<" checks passed\n";}catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}}
