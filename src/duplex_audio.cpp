#include "duplex_audio.h"
#include "device_log.h"
#include "playback_continuity.h"
#include "microphone_level.h"
#include <M5Unified.h>
#include <driver/i2s.h>
#include <esp_aec.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

namespace {
constexpr i2s_port_t Port = I2S_NUM_0;
bool adc(uint8_t reg, uint8_t value) {
  bool ok = M5.In_I2C.writeRegister8(0x40, reg, value, 400000);
  if (!ok) deviceLog.printf("{\"event\":\"codec_error\",\"device\":\"adc\",\"register\":%u}\n", reg);
  return ok;
}
bool amp(uint8_t reg, uint16_t value) {
  const uint8_t data[] = {static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
  bool ok = M5.In_I2C.writeRegister(0x36, reg, data, 2, 400000);
  if (!ok) deviceLog.printf("{\"event\":\"codec_error\",\"device\":\"amp\",\"register\":%u}\n", reg);
  return ok;
}
uint16_t peak(const int16_t* data, size_t count) {
  int result = 0;
  for (size_t i = 0; i < count; ++i) result = max(result, abs(static_cast<int>(data[i])));
  return result;
}
}

bool DuplexAudio::configureCodecs() {
  // CoreS3 ES7210 initialization follows M5Unified, with 256fs MCLK and 16-bit I2S.
  if (!adc(0x00, 0xFF)) return false;
  const uint8_t registers[][2] = {
    {0x00,0x41}, {0x01,0x1F}, {0x06,0x00}, {0x07,0x20},
    {0x08,0x10}, {0x09,0x30}, {0x0A,0x30},
    {0x20,0x0A}, {0x21,0x2A}, {0x22,0x0A}, {0x23,0x2A},
    {0x02,0xC1}, {0x04,0x01}, {0x05,0x00}, {0x11,0x60},
    {0x40,0x42}, {0x41,0x70}, {0x42,0x70},
    {0x43,0x13}, {0x44,0x13}, {0x45,0x00}, {0x46,0x00},
    {0x47,0x00}, {0x48,0x00}, {0x49,0x00}, {0x4A,0x00},
    {0x4B,0x00}, {0x4C,0xFF}, {0x01,0x14}
  };
  for (const auto& item : registers) if (!adc(item[0], item[1])) return false;
  M5.In_I2C.bitOn(0x58, 0x02, 0x04, 400000);
  return amp(0x61, 0x0673) && amp(0x04, 0x4040) && amp(0x05, 0x0008) &&
         amp(0x06, 0x14C3) && amp(0x0C, 0x0064);
}

bool DuplexAudio::begin(bool enableInput) {
  if (running) return true;
  end();
  inputQueue = xQueueCreate(20, sizeof(Input));
  outputLock = xSemaphoreCreateMutex();
  playback = static_cast<int16_t*>(ps_malloc(Capacity * sizeof(int16_t)));
  echo = aec_pro_create(16, 1, 5);  // ESP32-S3 accelerated AEC supported by this ESP-SR binary.
  if (!inputQueue || !outputLock || !playback || !echo) {
    deviceLog.printf("{\"event\":\"audio_init_error\",\"stage\":\"allocation\",\"queue\":%d,\"lock\":%d,\"playback\":%d,\"aec\":%d}\n", !!inputQueue, !!outputLock, !!playback, !!echo);
    end(); return false;
  }
  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
  config.sample_rate = Rate;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 4;
  config.dma_buf_len = 128;
  config.use_apll = false;
  config.tx_desc_auto_clear = true;
  config.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  config.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;
  esp_err_t error = i2s_driver_install(Port, &config, 0, nullptr);
  if (error != ESP_OK) {
    deviceLog.printf("{\"event\":\"audio_init_error\",\"stage\":\"driver\",\"code\":%d}\n", error);
    end(); return false;
  }
  driverInstalled = true;
  i2s_pin_config_t pins = {};
  pins.mck_io_num = 0;
  pins.bck_io_num = 34;
  pins.ws_io_num = 33;
  pins.data_out_num = 13;
  pins.data_in_num = 14;
  error = i2s_set_pin(Port, &pins);
  if (error != ESP_OK) {
    deviceLog.printf("{\"event\":\"audio_init_error\",\"stage\":\"pins\",\"code\":%d}\n", error);
    end(); return false;
  }
  if (!configureCodecs()) { end(); return false; }
  i2s_zero_dma_buffer(Port);
  captured = played = inputDrops = outputOverflows = ioErrors = 0;
  outputStarvations = outputWaitSamples = 0;
  soundStarvations = maxCycleUs = lateCycles = maxChunkSamples = maxQueuedSamples = 0;
  reserveSamples = PlaybackContinuity::ReserveSamples;
  micClippedSamples = sentPeak = 0;
  rawPeak = cleanPeak = playbackPeak = visualPeak = 0;
  processUs = 0;
  localPlayed = localFirstSoundAt = localFinishedAt = remoteFirstSoundAt = 0;
  inputEnabled = enableInput;
  muted = false;
  running = true;
  taskActive = true;
  if (xTaskCreatePinnedToCore(taskEntry, "duplex-aec", 16384, this, 5, nullptr, 1) != pdPASS) {
    deviceLog.println("{\"event\":\"audio_init_error\",\"stage\":\"task\"}");
    taskActive = false;
    running = false; end(); return false;
  }
  return true;
}

void DuplexAudio::end() {
  running = false;
  while (taskActive) delay(1);
  if (driverInstalled) {
    i2s_driver_uninstall(Port);
    driverInstalled = false;
    amp(0x04, 0x4000);
    M5.In_I2C.bitOff(0x58, 0x02, 0x04, 400000);
    adc(0x00, 0xFF);
  }
  if (echo) { aec_destroy(echo); echo = nullptr; }
  if (inputQueue) { vQueueDelete(inputQueue); inputQueue = nullptr; }
  if (outputLock) { vSemaphoreDelete(outputLock); outputLock = nullptr; }
  free(playback); playback = nullptr;
  queued = readIndex = writeIndex = 0;
  localSamples = nullptr;
  localRemaining = localDrainFrames = 0;
  localActive = inputEnabled = false;
}

void DuplexAudio::taskEntry(void* arg) {
  auto* self = static_cast<DuplexAudio*>(arg);
  self->run();
  self->taskActive = false;
  vTaskDelete(nullptr);
}

void DuplexAudio::run() {
  // ESP32-S3 accelerated AEC uses vector buffers. Do not rely on incidental
  // stack layout: adding even a small metadata field used to break alignment.
  alignas(16) int16_t raw[Frame * 2] = {};
  alignas(16) int16_t tx[Frame * 2] = {};
  alignas(16) int16_t near[Frame] = {};
  alignas(16) int16_t far[Frame] = {};
  // Measured CoreS3 TX-to-RX latency is about 35 ms; retain a 32 ms reference delay.
  alignas(16) int16_t reference[2][Frame] = {};
  size_t referenceIndex = 0;
  Input result = {};
  Input discarded = {};
  PlaybackContinuity continuity;
  uint32_t lastCycleAt = 0;
  const bool aligned = ((reinterpret_cast<uintptr_t>(near) | reinterpret_cast<uintptr_t>(reference) |
                         reinterpret_cast<uintptr_t>(result.samples)) & 15) == 0;
  deviceLog.printf("{\"event\":\"audio_buffers\",\"aligned\":%s}\n", aligned ? "true" : "false");
  if (!aligned) { ++ioErrors; running = false; return; }
  while (running) {
    size_t bytesRead = 0;
    if (i2s_read(Port, raw, sizeof(raw), &bytesRead, pdMS_TO_TICKS(100)) != ESP_OK || bytesRead != sizeof(raw)) {
      ++ioErrors; continue;
    }
    const uint32_t cycleAt = micros();
    if (lastCycleAt) {
      const uint32_t elapsed = cycleAt - lastCycleAt;
      maxCycleUs = max(maxCycleUs.load(), elapsed);
      if (elapsed > 24000) ++lateCycles; // A 16 ms frame plus 8 ms scheduling tolerance.
    }
    lastCycleAt = cycleAt;
    for (size_t i = 0; i < Frame; ++i) near[i] = raw[i * 2];
    rawPeak = peak(near, Frame);
    const int64_t start = esp_timer_get_time();
    aec_process(echo, near, reference[referenceIndex], result.samples);
    memcpy(result.raw, near, sizeof(near));
    memcpy(result.reference, reference[referenceIndex], sizeof(near));
    processUs = esp_timer_get_time() - start;
    cleanPeak = peak(result.samples, Frame);
    result.activityPeak = (muted || localActive) ? 0 : cleanPeak.load();
    if (muted || localActive) memset(result.samples, 0, sizeof(result.samples));
    else micClippedSamples += amplifyMicrophone(result.samples, Frame, micGain.load());
    sentPeak = peak(result.samples, Frame);
    // Before session.started there is no receiver. Do not accumulate stale microphone
    // input during TLS. Once started, keep sending silence until the local cue drains.
    if (inputEnabled) {
      if (xQueueSend(inputQueue, &result, 0) != pdTRUE) {
        xQueueReceive(inputQueue, &discarded, 0);
        xQueueSend(inputQueue, &result, 0);
        ++inputDrops;
      }
      captured += Frame;
    }
    xSemaphoreTake(outputLock, portMAX_DELAY);
    const bool localFrame = localActive;
    size_t used = 0;
    if (localRemaining) {
      used = min(Frame, localRemaining);
      for (size_t i = 0; i < used; ++i)
        far[i] = static_cast<int32_t>(*localSamples++) * volume.load() / 100;
      localRemaining -= used;
      if (!localRemaining) localDrainFrames = 5;  // DMA + acoustic/AEC tail, 80 ms.
    } else if (localDrainFrames) {
      if (!--localDrainFrames) { localActive = false; localFinishedAt = millis(); }
    } else {
      used = continuity.take(queued, Frame, maxChunkSamples.load());
      for (size_t i = 0; i < used; ++i) {
        far[i] = static_cast<int32_t>(playback[readIndex]) * volume.load() / 100;
        readIndex = (readIndex + 1) % Capacity;
      }
      queued -= used;
    }
    xSemaphoreGive(outputLock);
    if (localFrame) memset(far + used, 0, (Frame - used) * sizeof(int16_t));
    else {
      if (continuity.starvations != outputStarvations && (playbackPeak > 190 || peak(far, used) > 190))
        ++soundStarvations;
      continuity.smooth(far, used, Frame);
      outputStarvations = continuity.starvations;
      outputWaitSamples = continuity.waitSamples;
      reserveSamples = continuity.reserveSamples();
    }
    playbackPeak = peak(far, Frame);
    uint16_t previous = visualPeak.load();
    while (previous < playbackPeak && !visualPeak.compare_exchange_weak(previous, playbackPeak.load())) {}
    for (size_t i = 0; i < Frame; ++i) tx[i * 2] = tx[i * 2 + 1] = far[i];
    size_t written = 0;
    if (i2s_write(Port, tx, sizeof(tx), &written, pdMS_TO_TICKS(100)) != ESP_OK || written != sizeof(tx)) ++ioErrors;
    if (localFrame) {
      localPlayed += used;
      if (!localFirstSoundAt && playbackPeak > 190) localFirstSoundAt = millis();
    } else {
      played += used;
      if (!remoteFirstSoundAt && playbackPeak > 190) remoteFirstSoundAt = millis();
    }
    memcpy(reference[referenceIndex], far, sizeof(far));
    referenceIndex = (referenceIndex + 1) % 2;
  }
}

bool DuplexAudio::take(Input& input) { return inputQueue && xQueueReceive(inputQueue, &input, 0) == pdTRUE; }

bool DuplexAudio::play(const int16_t* samples, size_t count) {
  if (!running) return false;
  xSemaphoreTake(outputLock, portMAX_DELAY);
  if (count > Capacity - queued) { ++outputOverflows; xSemaphoreGive(outputLock); return false; }
  for (size_t i = 0; i < count; ++i) { playback[writeIndex] = samples[i]; writeIndex = (writeIndex + 1) % Capacity; }
  queued += count;
  maxChunkSamples = max<uint32_t>(maxChunkSamples.load(), count);
  maxQueuedSamples = max<uint32_t>(maxQueuedSamples.load(), queued);
  xSemaphoreGive(outputLock);
  return true;
}

bool DuplexAudio::playLocal(const int16_t* samples, size_t count) {
  if (!running || !samples || !count) return false;
  xSemaphoreTake(outputLock, portMAX_DELAY);
  if (localActive || queued) { xSemaphoreGive(outputLock); return false; }
  // The embedded PCM remains valid for the entire call; the audio task streams it
  // directly, independently of TLS and the short network playback queue.
  localSamples = samples;
  localRemaining = count;
  localDrainFrames = 0;
  localActive = true;
  xSemaphoreGive(outputLock);
  return true;
}

void DuplexAudio::clearPlayback() {
  if (!outputLock) return;
  xSemaphoreTake(outputLock, portMAX_DELAY);
  queued = readIndex = writeIndex = 0;
  xSemaphoreGive(outputLock);
}

DuplexAudio::Stats DuplexAudio::stats() {
  size_t size = 0;
  if (outputLock) { xSemaphoreTake(outputLock, portMAX_DELAY); size = queued; xSemaphoreGive(outputLock); }
  return {captured.load(), played.load(), inputDrops.load(), outputOverflows.load(), ioErrors.load(),
          outputStarvations.load(), outputWaitSamples.load(),
          soundStarvations.load(), maxCycleUs.load(), lateCycles.load(), maxChunkSamples.load(), maxQueuedSamples.load(),
          reserveSamples.load(), micClippedSamples.load(), sentPeak.load(), micGain.load(),
          rawPeak.load(), cleanPeak.load(), playbackPeak.load(), processUs.load(), size,
          localPlayed.load(), localFirstSoundAt.load(), localFinishedAt.load(), remoteFirstSoundAt.load(), localActive.load()};
}
