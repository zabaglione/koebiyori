#pragma once
#include <Arduino.h>
#include <atomic>
#include <cstddef>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

class DuplexAudio {
 public:
  static constexpr size_t Frame = 256;
  static constexpr uint32_t Rate = 16000;
  struct alignas(16) Input {
    int16_t samples[Frame];
    int16_t raw[Frame];
    int16_t reference[Frame];
    uint16_t activityPeak;
  };
  static_assert(alignof(Input) >= 16 && sizeof(Input) % 16 == 0, "AEC frame alignment");
  static_assert(offsetof(Input, samples) % 16 == 0 && offsetof(Input, raw) % 16 == 0 &&
                offsetof(Input, reference) % 16 == 0, "AEC channel alignment");
  struct Stats {
    uint32_t captured, played, inputDrops, outputOverflows, ioErrors;
    uint32_t outputStarvations, outputWaitSamples;
    uint32_t soundStarvations, maxCycleUs, lateCycles, maxChunkSamples, maxQueuedSamples;
    uint32_t reserveSamples, micClippedSamples;
    uint16_t sentPeak;
    uint8_t micGain;
    uint16_t rawPeak, cleanPeak, playbackPeak;
    uint32_t processUs;
    size_t queued;
    uint32_t localPlayed, localFirstSoundAt, localFinishedAt, remoteFirstSoundAt;
    bool localActive;
  };
  bool begin(bool inputEnabled = true);
  void end();
  bool take(Input& input);
  bool play(const int16_t* samples, size_t count);
  void mute(bool enabled) { muted = enabled; }
  Stats stats();
  uint16_t takePlaybackPeak() { return visualPeak.exchange(0); }
  void clearPlayback();
  bool playLocal(const int16_t* samples, size_t count);
  void enableInput() { inputEnabled = true; }
  void setVolume(uint8_t percent) { volume = min<uint8_t>(percent, 80); }
  void setMicGain(uint8_t gain) { micGain = constrain(gain, 1, 4); }

 private:
  static void taskEntry(void* arg);
  void run();
  bool configureCodecs();
  bool driverInstalled = false;
  std::atomic<bool> running{false}, muted{false};
  std::atomic<bool> inputEnabled{false}, localActive{false};
  std::atomic<uint8_t> volume{35};
  std::atomic<uint8_t> micGain{4};
  std::atomic<bool> taskActive{false};
  QueueHandle_t inputQueue = nullptr;
  SemaphoreHandle_t outputLock = nullptr;
  void* echo = nullptr;
  int16_t* playback = nullptr;
  static constexpr size_t Capacity = 8192;
  size_t readIndex = 0, writeIndex = 0, queued = 0;
  const int16_t* localSamples = nullptr;
  size_t localRemaining = 0;
  uint8_t localDrainFrames = 0;
  std::atomic<uint32_t> localPlayed{0}, localFirstSoundAt{0}, localFinishedAt{0}, remoteFirstSoundAt{0};
  std::atomic<uint32_t> captured{0}, played{0}, inputDrops{0}, outputOverflows{0}, ioErrors{0};
  std::atomic<uint32_t> outputStarvations{0}, outputWaitSamples{0};
  std::atomic<uint32_t> soundStarvations{0}, maxCycleUs{0}, lateCycles{0}, maxChunkSamples{0}, maxQueuedSamples{0};
  std::atomic<uint32_t> reserveSamples{0}, micClippedSamples{0};
  std::atomic<uint16_t> sentPeak{0};
  std::atomic<uint16_t> rawPeak{0}, cleanPeak{0}, playbackPeak{0};
  std::atomic<uint16_t> visualPeak{0};
  std::atomic<uint32_t> processUs{0};
};
