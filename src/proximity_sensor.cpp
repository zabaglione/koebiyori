#include "proximity_sensor.h"
#include <M5Unified.h>

namespace {
constexpr uint8_t Address = 0x23;
constexpr uint32_t BusHz = 400000;
bool readRegister(uint8_t reg, uint8_t* bytes, size_t count) {
  return M5.In_I2C.readRegister(Address, reg, bytes, count, BusHz);
}
bool writeRegister(uint8_t reg, uint8_t value) {
  return M5.In_I2C.writeRegister8(Address, reg, value, BusHz);
}
}

bool ProximitySensor::begin() {
  uint8_t id[2] = {};
  if (!readRegister(0x86, id, 2) || (id[0] & 0xF0) != 0x90 || id[1] != 0x05) return false;
  // LTR-553ALS-WA datasheet: standby, 40 kHz/100 mA, 8 pulses, 50 ms,
  // interrupts off (poll fresh data), then gain x16 + saturation flag + active PS.
  available = writeRegister(0x81, 0x00) && writeRegister(0x82, 0x3F) && writeRegister(0x83, 0x08) &&
              writeRegister(0x84, 0x00) && writeRegister(0x8F, 0x00) && writeRegister(0x81, 0x23);
  return available;
}

bool ProximitySensor::poll(uint32_t now, bool canStart) {
  const bool allow = available && enabled && canStart;
  if (!allow) {
    if (eligible) block(now);
    return false;
  }
  if (!eligible) { gate.block(now); eligible = true; lastPoll = now; }
  if (now - lastPoll < 50) return false;
  lastPoll = now;
  uint8_t status = 0, data[2] = {};
  if (!readRegister(0x8C, &status, 1) || ((status & 1) && !readRegister(0x8D, data, 2))) {
    ++readErrors;
    gate.reject();
    if (++consecutiveErrors >= 5) { available = false; block(now); }
    return false;
  }
  consecutiveErrors = 0;
  if (!(status & 1)) return false;  // Do not debounce repeated/stale conversions.
  saturated = data[1] & 0x80;
  value = data[0] | ((data[1] & 7) << 8);
  ++samples;
  if (saturated) { gate.reject(); return false; }
  return gate.update(value, now);
}
