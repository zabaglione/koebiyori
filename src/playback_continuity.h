#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

// A small jitter reserve for the continuous network PCM stream. The local cue
// bypasses this path. Never drop queued speech when recovering from starvation.
class PlaybackContinuity {
 public:
  static constexpr size_t ReserveSamples = 2048; // Initial 128 ms at 16 kHz.
  static constexpr size_t MaximumReserveSamples = 4096; // Cap adaptation at 256 ms.
  static constexpr size_t RampSamples = 32;      // 2 ms at 16 kHz.

  size_t take(size_t available, size_t frame, size_t maxChunk) {
    if (waiting) {
      // A whole 100 ms packet can arrive at once. Its size alone does not imply
      // any jitter reserve: hold for real playback ticks after the first data.
      // A backlog with a complete packet PLUS the reserve can already be played;
      // delaying that backlog further would needlessly increase queue pressure.
      if (!available) reserveElapsed = 0;
      const bool haveReserve = available >= maxChunk + targetReserve;
      if (!available || (reserveElapsed < targetReserve && !haveReserve) || available < frame) {
        if (available) reserveElapsed += frame;
        if (started) waitSamples += frame;
        return 0;
      }
      waiting = false;
      started = true;
      fadeIn = true;
    }
    const size_t used = std::min(available, frame);
    if (used < frame) {
      waiting = true;
      reserveElapsed = 0;
      ++starvations;
      targetReserve = std::min(targetReserve + 1024, MaximumReserveSamples);
      waitSamples += frame - used;
    }
    return used;
  }

  // Fill missing PCM with silence, avoiding an abrupt nonzero-to-zero edge.
  // Fully buffered audio is bit-exact apart from the first 2 ms after a restart.
  void smooth(int16_t* samples, size_t used, size_t frame) {
    if (fadeIn && used) {
      const size_t ramp = std::min(used, RampSamples);
      for (size_t i = 0; i < ramp; ++i)
        samples[i] = static_cast<int32_t>(samples[i]) * static_cast<int32_t>(i + 1) / static_cast<int32_t>(ramp);
      fadeIn = false;
    }
    std::fill(samples + used, samples + frame, 0);
    if (used < frame) {
      if (used) {
        const size_t ramp = std::min(used, RampSamples);
        for (size_t i = 0; i < ramp; ++i)
          samples[used - ramp + i] = static_cast<int32_t>(samples[used - ramp + i]) * static_cast<int32_t>(ramp - i - 1) / static_cast<int32_t>(ramp);
      } else {
        const size_t ramp = std::min(frame, RampSamples);
        for (size_t i = 0; i < ramp; ++i)
          samples[i] = static_cast<int32_t>(lastSample) * static_cast<int32_t>(ramp - i - 1) / static_cast<int32_t>(ramp);
      }
    }
    lastSample = samples[frame - 1];
  }

  uint32_t starvations = 0, waitSamples = 0;
  size_t reserveSamples() const { return targetReserve; }

 private:
  bool waiting = true, started = false, fadeIn = false;
  size_t reserveElapsed = 0;
  size_t targetReserve = ReserveSamples;
  int16_t lastSample = 0;
};
