#pragma once
#include <algorithm>
#include <stdint.h>

// Independent of I2C so timing and re-arming can be tested with recorded samples.
class ProximityGate {
 public:
  static constexpr uint16_t NearMargin = 80, FarMargin = 60;
  static constexpr uint32_t HoldMs = 150, ReleaseMs = 250, CooldownMs = 500;
  void block(uint32_t now) {
    armed = false;
    nearTiming = farTiming = haveSample = false;
    backgroundCount = 0;
    blockedAt = now;
  }
  void reject() { nearTiming = farTiming = haveSample = false; backgroundCount = 0; }
  bool update(uint16_t value, uint32_t now) {
    if (haveSample && now - lastSample > 200) reject();
    lastSample = now;
    haveSample = true;
    if (!calibrated()) {
      learnBackground(value);
      return false;
    }
    // A median prevents one startup zero from permanently lowering the release
    // threshold below the real background. Only learn released readings.
    if (value <= farThreshold()) learnBackground(value);
    else backgroundCount = 0;
    if (!armed) {
      nearTiming = false;
      if (value <= farThreshold()) {
        if (!farTiming) { farTiming = true; farSince = now; }
        if (now - farSince >= ReleaseMs && now - blockedAt >= CooldownMs) armed = true;
      } else farTiming = false;
      return false;
    }
    if (value < nearThreshold()) { nearTiming = false; return false; }
    if (!nearTiming) { nearTiming = true; nearSince = now; }
    if (now - nearSince < HoldMs) return false;
    block(now);
    return true;
  }
  bool calibrated() const { return haveBaseline; }
  bool isArmed() const { return armed; }
  uint16_t background() const { return calibrated() ? baseline : 0; }
  uint16_t nearThreshold() const { return std::min<uint16_t>(2047, baseline + NearMargin); }
  uint16_t farThreshold() const { return std::min<uint16_t>(2047, baseline + FarMargin); }

 private:
  void learnBackground(uint16_t value) {
    backgroundSamples[backgroundCount++] = value;
    if (backgroundCount < 20) return;
    std::sort(backgroundSamples, backgroundSamples + 20);
    baseline = (backgroundSamples[9] + backgroundSamples[10]) / 2;
    backgroundCount = 0;
    haveBaseline = true;
  }
  uint16_t baseline = 0, backgroundSamples[20] = {};
  uint8_t backgroundCount = 0;
  bool haveBaseline = false;
  bool armed = false, nearTiming = false, farTiming = false, haveSample = false;
  uint32_t blockedAt = 0, nearSince = 0, farSince = 0, lastSample = 0;
};
