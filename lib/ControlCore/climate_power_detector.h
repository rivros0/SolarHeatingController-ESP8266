#pragma once
#include "energy_manager.h"
namespace solar {
struct DetectorConfig {
  uint32_t baselineMs=60000, startupMs=180000, observationMs=120000;
  float deltaW=400, maxNoiseW=200;
};
// Time-weighted sampling is handled by the caller at a fixed one-second cadence.
class PowerDetector {
  float sum_=0, squares_=0, baseline_=0; uint32_t count_=0, windowAt_=0, commandAt_=0;
  bool pending_=false, desired_=false, windowStarted_=false;
 public:
  const char* estimated="unknown"; float confidence=0, delta=0, noise=0;
  float baselineMean=0, baselineNoise=0; bool baselineReady=false;
  void invalidate() { sum_=squares_=0; count_=0; windowStarted_=false; pending_=false;
    baselineReady=false; estimated="unknown"; confidence=0; }
  bool command(bool on,uint32_t now) {
    estimated="unknown"; confidence=0;
    if (!baselineReady) return false;
    baseline_=baselineMean; noise=baselineNoise; pending_=true; desired_=on; commandAt_=now;
    sum_=squares_=0; count_=0; windowStarted_=false; baselineReady=false; return true;
  }
  void sample(float watts,uint32_t now,const DetectorConfig& c) {
    if (!std::isfinite(watts)) { invalidate(); return; }
    if (pending_ && elapsed(now,commandAt_) < c.startupMs) return;
    if (!windowStarted_) { windowStarted_=true; windowAt_=now; }
    sum_+=watts; squares_+=watts*watts; count_++;
    if (elapsed(now,windowAt_) < (pending_ ? c.observationMs : c.baselineMs)) return;
    float mean=sum_/count_;
    float sd=std::sqrt(std::fmax(0.0f,squares_/count_-mean*mean));
    if (pending_) {
      delta=mean-baseline_;
      // Aggregate meter evidence can never provide certainty.
      bool direction=desired_ ? delta>=c.deltaW : delta<=-c.deltaW;
      bool quiet=std::fmax(sd,noise)<=c.maxNoiseW;
      confidence=direction && quiet ? 0.65f : 0.15f;
      estimated=direction && quiet ? (desired_ ? "likely_on" : "likely_off") : "unknown";
      pending_=false;
    }
    baselineMean=mean; baselineNoise=sd; baselineReady=true;
    sum_=squares_=0; count_=0; windowStarted_=false;
  }
};
}
