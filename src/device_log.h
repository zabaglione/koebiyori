#pragma once
#include <Arduino.h>
#include <atomic>

// Burner NVS expects one plain response per command, without background logs.
class DeviceLog : public Print {
 public:
  std::atomic<bool> enabled{false};
  size_t write(uint8_t value) override { return enabled ? Serial.write(value) : 1; }
  size_t write(const uint8_t* data, size_t size) override {
    return enabled ? Serial.write(data, size) : size;
  }
};

extern DeviceLog deviceLog;
