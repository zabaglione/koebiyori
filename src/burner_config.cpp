#include "burner_config.h"

namespace {
constexpr char Mask[] = "********";
bool known(const String& key) {
  return key == "wifi_ssid" || key == "wifi_password" || key == "openai_api_key";
}
size_t fieldIndex(const String& key) { return key == "wifi_ssid" ? 0 : key == "wifi_password" ? 1 : 2; }
bool valid(const String& key, const String& value) {
  const size_t limit = key == "wifi_ssid" ? 32 : key == "wifi_password" ? 64 : 512;
  if (value.length() > limit) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const auto c = static_cast<uint8_t>(value[i]);
    if (c < 32 || c == 127 || (key == "openai_api_key" && (c <= 32 || c >= 127))) return false;
  }
  // Empty values explicitly clear a field, including an open network password.
  if (value.isEmpty()) return true;
  if (key == "openai_api_key") return value.startsWith("sk-") && value.length() >= 20;
  if (key == "wifi_password") {
    if (value.length() < 8) return false;
    if (value.length() == 64) {
      for (size_t i = 0; i < value.length(); ++i) if (!isxdigit(static_cast<unsigned char>(value[i]))) return false;
    }
  }
  return true;
}
}

bool BurnerConfig::begin() {
  available = preferences.begin("koebiyori", false);
  return available;
}

bool BurnerConfig::configured() {
  const String network = ssid(), key = apiKey();
  return !network.isEmpty() && !key.isEmpty() && valid("wifi_ssid", network) &&
         valid("wifi_password", password()) && valid("openai_api_key", key);
}

bool BurnerConfig::clear() {
  status = "";
  for (auto& error : fieldErrors) error = "";
  return available && preferences.clear();
}

String BurnerConfig::get(const String& key, bool busy) {
  if (key == "status") {
    if (busy) return "BUSY - finish the conversation first";
    for (const auto& error : fieldErrors) if (!error.isEmpty()) return error;
    if (!status.isEmpty()) return status;
    return configured() ? "READY - disconnect Burner NVS and restart CoreS3" : "SETUP - save Wi-Fi and your OpenAI API key";
  }
  if (!known(key)) return "ERROR - unknown key";
  const String value = preferences.getString(key.c_str());
  if (key != "wifi_ssid" && !value.isEmpty()) return Mask;
  return value;
}

bool BurnerConfig::set(const String& key, const String& value, bool busy) {
  if (busy) { status = "BUSY - finish the conversation first"; return false; }
  if (!known(key)) { status = "ERROR - unknown or read-only key"; return false; }
  auto& error = fieldErrors[fieldIndex(key)];
  if (key != "wifi_ssid" && value == Mask && !preferences.getString(key.c_str()).isEmpty()) { status = error = ""; return true; }
  if (!valid(key, value)) { error = "ERROR - invalid " + key; return false; }
  // Verify persistence without sending the stored secret back to the computer.
  preferences.putString(key.c_str(), value);
  if (preferences.getString(key.c_str(), "\x01") != value) {
    error = "ERROR - could not save " + key;
    return false;
  }
  status = error = "";
  return true;
}

bool BurnerConfig::handle(const String& line, bool busy) {
  if (!line.startsWith("CMD::")) return false;
  if (line == "CMD::INIT:") {
    active = available;
    status = "";
    Serial.println(available ? "__NVS_EXIST__" : "__NVS_NOT_FOUND__");
    return true;
  }
  if (!active) { Serial.println("ERROR - connect Burner NVS first"); return true; }
  if (line == "CMD::LIST:") Serial.println("/wifi_ssid/wifi_password/openai_api_key/status");
  else if (line.startsWith("CMD::GET:")) Serial.println(get(line.substring(9), busy));
  else if (line.startsWith("CMD::SET:")) {
    const int separator = line.indexOf('=', 9);
    if (separator < 0) status = "ERROR - invalid command";
    const bool saved = separator >= 0 && set(line.substring(9, separator), line.substring(separator + 1), busy);
    Serial.println(saved ? "OK" : "ERROR - read status");
  } else if (line.startsWith("CMD::SUB:") || line.startsWith("CMD::UNSUB:")) {
    // These commands have no acknowledgement. Secrets are never broadcast.
  } else Serial.println("ERROR - unsupported command");
  return true;
}
