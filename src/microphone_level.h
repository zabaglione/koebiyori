#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

// The AEC output is much quieter than the M5Unified microphone path, which
// applies software magnification. Boost after AEC, keeping its reference intact.
inline uint32_t amplifyMicrophone(int16_t* samples, size_t count, uint8_t gain) {
  uint32_t clipped = 0;
  for (size_t i = 0; i < count; ++i) {
    const int32_t value = static_cast<int32_t>(samples[i]) * gain;
    if (value > 32767 || value < -32768) ++clipped;
    samples[i] = static_cast<int16_t>(std::max<int32_t>(-32768, std::min<int32_t>(32767, value)));
  }
  return clipped;
}
