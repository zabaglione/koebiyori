#pragma once
#include "proximity_gate.h"

class ProximitySensor {
 public:
  bool begin();
  bool poll(uint32_t now, bool canStart);
  void block(uint32_t now) { gate.block(now); eligible = false; }
  bool available = false, enabled = true, saturated = false;
  uint16_t value = 0;
  uint32_t readErrors = 0, samples = 0;
  ProximityGate gate;
 private:
  bool eligible = false;
  uint32_t lastPoll = 0;
  uint8_t consecutiveErrors = 0;
};
