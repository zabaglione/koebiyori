#pragma once
#include <ArduinoJson.h>
#include <time.h>
#include "speech_options.h"

// Character customization lives in config/character.json, independently of I2S/UI.
class AssistantConfig {
 public:
  explicit AssistantConfig(ArduinoJson::Allocator* allocator) : profile(allocator) {}
  bool begin();
  void session(JsonObject target, uint32_t sampleRate, time_t now, SpeechOptions::Selection speech) const;
  String clockContext(time_t now) const;
  const char* defaultVoice() const { return profile["voice"]; }
  const char* defaultVoiceStyle() const { return profile["voice_style"]; }
  const char* greeting() const { return profile["greeting"]; }
  const char* farewell() const { return profile["farewell"]; }
  bool searchEnabled() const { return profile["web_search_enabled"]; }
  uint32_t searchWaitMs() const { return profile["search_wait_ms"]; }
 private:
  JsonDocument profile;
};
