#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "speech_options.h"

class BurnerConfig {
 public:
  bool begin(const char* defaultVoice, const char* defaultStyle);
  bool handle(const String& line, bool busy);
  bool clear();
  bool configured();
  void disconnect() { active = false; }
  bool active = false;
  String ssid() { return preferences.getString("wifi_ssid"); }
  String password() { return preferences.getString("wifi_password"); }
  String apiKey() { return preferences.getString("openai_api_key"); }
  SpeechOptions::Selection speech();
  bool saveSpeech(SpeechOptions::Selection selection);

 private:
  Preferences preferences;
  bool available = false;
  String status;
  String fieldErrors[5];
  SpeechOptions::Selection speechDefaults;
  String get(const String& key, bool busy);
  bool set(const String& key, const String& value, bool busy);
};
