#pragma once
#include <ArduinoJson.h>

// Keep the fields used by the device. Backend text, reasoning and echoed
// session metadata are skipped; retaining them in PSRAM adds no UI value.
inline void configureApiEventFilter(JsonDocument& filter) {
  filter["type"] = true;
  filter["delta"] = true;
  filter["client_event_id"] = true;
  filter["delegation_id"] = true;
  filter["usage"]["seconds"] = true;
  filter["error"]["type"] = true;
  filter["error"]["code"] = true;
  auto event = filter["event"].to<JsonObject>();
  event["type"] = true;
  event["response"]["id"] = true;
  event["item"]["type"] = true;
  event["item"]["status"] = true;
  event["item"]["content"][0]["annotations"][0]["type"] = true;
  event["item"]["content"][0]["annotations"][0]["url"] = true;
  event["annotation"]["type"] = true;
  event["annotation"]["url"] = true;
}
