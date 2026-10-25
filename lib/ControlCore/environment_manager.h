#pragma once
#include "energy_manager.h"
namespace solar {
class EnvironmentManager {
 public:
  Sample temperature, humidity;
  void update(float t, float h, uint32_t now, float alpha) {
    if (!std::isfinite(t) || !std::isfinite(h) || t < -40 || t > 125 || h < 0 || h > 100) {
      temperature.valid = humidity.valid = false; return;
    }
    temperature = {temperature.valid ? temperature.value + alpha*(t-temperature.value) : t,now,true};
    humidity = {humidity.valid ? humidity.value + alpha*(h-humidity.value) : h,now,true};
  }
};
}
