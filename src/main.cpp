#include <Arduino.h>
#include "duplex_audio.h"
#include "character_ui.h"
#include "proximity_sensor.h"
#include "assistant_config.h"
#include "delegation_tracker.h"
#include "api_event_filter.h"
#include <ArduinoJson.h>
#include <M5Unified.h>
#include "burner_config.h"
#include "device_log.h"
#include "launcher_support.h"
#include <esp_log.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>
#include <math.h>
#include <time.h>

// TLS and diagnostics need more headroom than Arduino's default 8 KB loop stack.
SET_LOOP_TASK_STACK_SIZE(16384);

DeviceLog deviceLog;

namespace {
constexpr char BUILD[] = "koebiyori-0.8.0";
constexpr uint32_t RATE = 16000;
constexpr size_t FRAME = DuplexAudio::Frame;
constexpr size_t MAX_MESSAGE = 65536;
constexpr uint32_t SESSION_LIMIT_MS = 300000;
constexpr uint32_t IDLE_LIMIT_MS = 30000;
constexpr uint32_t FAREWELL_TIMEOUT_MS = 20000;
extern const char rootCA[] asm("_binary_certs_gts_root_r4_pem_start");
extern const int16_t startupPcm[] asm("_binary_assets_generated_startup_pcm_start");
extern const int16_t startupPcmEnd[] asm("_binary_assets_generated_startup_pcm_end");

struct PsramAllocator : ArduinoJson::Allocator {
  void* allocate(size_t size) override { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
  void deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t size) override { return heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
} jsonAllocator;
AssistantConfig assistantConfig(&jsonAllocator);
JsonDocument apiEventFilter(&jsonAllocator);
uint32_t socketEvents = 0, maxSocketPollUs = 0, maxJsonParseUs = 0, maxMessageBytes = 0;
uint32_t lastAudioArrivalUs = 0, maxAudioArrivalGapUs = 0;
DelegationTracker delegations;
uint32_t webSearches = 0, webSearchCompleted = 0, lastClockUpdate = 0;
String sourceUrls[6];
String sourcesDelegationId;
size_t sourceCount = 0;
bool traceEnabled = false, traceTruncated = false;
String conversationTrace;

void traceText(const char* role, const char* text) {
  if (!traceEnabled || !*text) return;
  const String entry = String(millis()) + " " + role + ": " + text + "\n";
  if (conversationTrace.length() + entry.length() > 12000) { traceTruncated = true; return; }
  conversationTrace += entry;
}

using Phase = CharacterUI::State;
Phase phase = Phase::Setup;
M5Canvas canvas(&M5.Display);
CharacterUI character(canvas);
BurnerConfig burnerConfig;
WebSocketsClient socket;
DuplexAudio audio;
ProximitySensor proximity;
bool automaticStart = false, greetingPending = false;
bool apiSessionStarted = false, localCueReported = false, localCueFinishedReported = false;
// The worker exclusively owns the WebSocket until the handshake completes.
// Only then does the main loop send session.start and resume normal socket I/O.
bool connectionPending = false;
std::atomic<bool> connectionActive{false}, connectionCancel{false};
enum class ConnectResult { Pending, Connected, Failed };
ConnectResult connectionResult = ConnectResult::Pending;
std::atomic<uint32_t> connectionStackFree{0};
uint32_t localCueLatencyMs = 0, localCueFinishedMs = 0;
uint32_t startupUiFrames = 0, startupMouthFrames = 0, startupMouthAt = 0, startupFrameAt = 0;
bool farewellPending = false, farewellHeard = false;
uint32_t presenceStarts = 0, greetingsSent = 0, farewellsSent = 0;
uint32_t farewellSince = 0, farewellLastSpeech = 0;
enum class Utterance { None, Greeting, Farewell };
Utterance awaitedUtterance = Utterance::None;
String utteranceEventId;
bool utteranceAccepted = false;
uint32_t utteranceSequence = 0;
uint32_t connectRequestedAt = 0, connectedMs = 0, liveStartMs = 0, firstSpeechMs = 0;
bool inputMuted = false;
String ssid, password, apiKey, authHeader, serialLine;
String detail = "Configure with M5Burner";
bool serialOverflow = false;
uint32_t lastSerialByte = 0;
bool transportActive = false;
bool socketConnected = false;
bool uiDirty = true;
bool fragmentActive = false;
bool closeFinalized = false;
bool pendingFailure = false;
size_t fragmentLength = 0;
char* fragments = nullptr;
uint8_t* decoded = nullptr;
char outgoing[1800] = {};
uint32_t phaseSince = 0;
uint32_t sessionSince = 0;
uint32_t lastInteraction = 0;
uint32_t lastStatus = 0;
uint32_t lastDraw = 0;
uint64_t sentSamples = 0, receivedSamples = 0;
uint32_t inputTextBytes = 0, outputTextBytes = 0;
double usageSeconds = 0;

void userActivity() {
  lastInteraction = millis();
  if (farewellPending) {
    farewellPending = false;
    awaitedUtterance = Utterance::None;
    utteranceAccepted = false;
    deviceLog.println("{\"event\":\"farewell_cancelled\"}");
  }
}

const char* phaseName() {
  switch (phase) {
    case Phase::Setup: return "SETUP";
    case Phase::Wifi: return "WIFI CONNECTING";
    case Phase::Clock: return "SYNCING CLOCK";
    case Phase::Ready: return "READY";
    case Phase::Noticed: return "NOTICED";
    case Phase::Connecting: return "CONNECTING";
    case Phase::Live: return "LIVE";
    case Phase::Closing: return "ENDING";
    case Phase::Error: return "ERROR";
  }
  return "UNKNOWN";
}

void emitStatus() {
  if (!deviceLog.enabled) return;
  const auto metrics = audio.stats();
  JsonDocument doc;
  doc["event"] = "status";
  doc["build"] = BUILD;
  doc["uptime_ms"] = millis();
  doc["state"] = phaseName();
  doc["detail"] = detail;
  doc["wifi"] = WiFi.status() == WL_CONNECTED;
  doc["configured"] = !ssid.isEmpty() && !apiKey.isEmpty();
  doc["launcher_available"] = character.launcherAvailable;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["free_psram"] = ESP.getFreePsram();
  doc["loop_stack_min_free_bytes"] = uxTaskGetStackHighWaterMark(nullptr);
  doc["sent_samples"] = sentSamples;
  doc["received_samples"] = receivedSamples;
  doc["played_samples"] = metrics.played;
  doc["queued_samples"] = metrics.queued;
  doc["captured_samples"] = metrics.captured;
  doc["mic_raw_peak"] = metrics.rawPeak;
  doc["aec_process_us"] = metrics.processUs;
  doc["audio_io_errors"] = metrics.ioErrors;
  doc["muted"] = inputMuted;
  doc["mic_peak"] = metrics.cleanPeak;
  doc["speaker_peak"] = metrics.playbackPeak;
  doc["mic_underruns"] = metrics.inputDrops;
  doc["queue_overflows"] = metrics.outputOverflows;
  doc["playback_starvations"] = metrics.outputStarvations;
  doc["playback_wait_samples"] = metrics.outputWaitSamples;
  doc["playback_sound_starvations"] = metrics.soundStarvations;
  doc["audio_max_cycle_us"] = metrics.maxCycleUs;
  doc["audio_late_cycles"] = metrics.lateCycles;
  doc["max_audio_chunk_samples"] = metrics.maxChunkSamples;
  doc["max_queued_samples"] = metrics.maxQueuedSamples;
  doc["playback_reserve_ms"] = metrics.reserveSamples / 16;
  doc["mic_sent_peak"] = metrics.sentPeak;
  doc["mic_gain"] = metrics.micGain;
  doc["mic_clipped_samples"] = metrics.micClippedSamples;
  doc["trace_enabled"] = traceEnabled;
  doc["max_socket_poll_us"] = maxSocketPollUs;
  doc["max_json_parse_us"] = maxJsonParseUs;
  doc["max_api_message_bytes"] = maxMessageBytes;
  doc["max_audio_arrival_gap_us"] = maxAudioArrivalGapUs;
  doc["input_text_bytes"] = inputTextBytes;
  doc["output_text_bytes"] = outputTextBytes;
  doc["usage_seconds"] = usageSeconds;
  doc["finalized"] = closeFinalized;
  doc["ui_frames"] = character.frameCount;
  doc["mouth_frames"] = character.mouthFrames;
  doc["blink_count"] = character.blinkCount;
  doc["ui_max_draw_us"] = character.maxDrawUs;
  doc["controls_visible"] = character.controlsVisible(millis());
  doc["ui_pixels_sent"] = character.pixelsSent;
  auto sensor = doc["proximity"].to<JsonObject>();
  sensor["available"] = proximity.available;
  sensor["enabled"] = proximity.enabled;
  sensor["calibrated"] = proximity.gate.calibrated();
  sensor["armed"] = proximity.gate.isArmed();
  sensor["value"] = proximity.value;
  sensor["baseline"] = proximity.gate.background();
  sensor["near_threshold"] = proximity.gate.nearThreshold();
  sensor["far_threshold"] = proximity.gate.farThreshold();
  sensor["saturated"] = proximity.saturated;
  sensor["read_errors"] = proximity.readErrors;
  sensor["samples"] = proximity.samples;
  sensor["starts"] = presenceStarts;
  doc["automatic_start"] = automaticStart;
  doc["greetings_sent"] = greetingsSent;
  doc["farewell_pending"] = farewellPending;
  doc["farewells_sent"] = farewellsSent;
  doc["farewell_heard"] = farewellHeard;
  doc["ws_connect_ms"] = connectedMs;
  doc["live_start_ms"] = liveStartMs;
  doc["first_speech_ms"] = firstSpeechMs;
  doc["local_cue_first_sound_ms"] = localCueLatencyMs;
  doc["local_cue_finished_ms"] = localCueFinishedMs;
  doc["local_cue_active"] = metrics.localActive;
  doc["local_cue_played_samples"] = metrics.localPlayed;
  doc["startup_ui_frames"] = startupUiFrames;
  doc["startup_mouth_frames"] = startupMouthFrames;
  doc["connect_stack_min_free_bytes"] = connectionStackFree.load();
  doc["web_search_enabled"] = assistantConfig.searchEnabled();
  doc["backend_active"] = delegations.active();
  doc["backend_waiting"] = delegations.waiting();
  doc["backend_started"] = delegations.started;
  doc["backend_finished"] = delegations.finished;
  doc["backend_timeouts"] = delegations.timeouts;
  doc["backend_last_duration_ms"] = delegations.lastDuration;
  doc["web_searches"] = webSearches;
  doc["web_search_completed"] = webSearchCompleted;
  doc["source_count"] = sourceCount;
  serializeJson(doc, deviceLog);
  deviceLog.println();
}

void setPhase(Phase next, const String& text) {
  if (next != Phase::Ready || phase != Phase::Ready) proximity.block(millis());
  phase = next;
  detail = text;
  phaseSince = millis();
  uiDirty = true;
  emitStatus();
}

void stopAudio() {
  audio.end();
  inputMuted = false;
}

void stopTransport() {
  if (connectionPending) { connectionCancel = true; return; }
  transportActive = false;
  socketConnected = false;
  apiSessionStarted = false;
  socket.disconnect();
  fragmentActive = false;
  fragmentLength = 0;
}

void fail(const char* message) {
  // Defer disconnect until outside the WebSocket event callback.
  pendingFailure = true;
  setPhase(Phase::Error, message);
}

bool sendJson(JsonDocument& doc) {
  String message;
  serializeJson(doc, message);
  return socket.sendTXT(message);
}

void closeSession() {
  greetingPending = farewellPending = false;
  awaitedUtterance = Utterance::None;
  utteranceAccepted = false;
  if (phase == Phase::Closing) return;
  if (connectionPending) {
    stopAudio();
    connectionCancel = true;
    setPhase(Phase::Closing, "Cancelling connection");
  } else if (apiSessionStarted && socketConnected) {
    stopAudio();
    JsonDocument doc;
    doc["type"] = "session.close";
    if (!sendJson(doc)) { fail("Close send failed"); return; }
    setPhase(Phase::Closing, "Finishing session");
  } else {
    stopAudio();
    stopTransport();
    setPhase(WiFi.status() == WL_CONNECTED ? Phase::Ready : Phase::Error, "Tap START to connect");
  }
}

bool sendUtteranceInstruction(Utterance kind, const char* instruction) {
  awaitedUtterance = kind;
  utteranceAccepted = false;
  utteranceEventId = "device_utterance_" + String(++utteranceSequence);
  JsonDocument event;
  event["type"] = "session.instructions.append";
  event["event_id"] = utteranceEventId;
  event["delegation_id"] = nullptr;
  event["content"] = instruction;
  return sendJson(event);
}

bool greet() {
  if (!sendUtteranceInstruction(Utterance::Greeting, assistantConfig.greeting())) return false;
  ++greetingsSent;
  return true;
}

uint16_t peakOf(const int16_t* data, size_t count) {
  int peak = 0;
  for (size_t i = 0; i < count; ++i) peak = max(peak, abs(static_cast<int>(data[i])));
  return static_cast<uint16_t>(peak);
}

void collectCitation(JsonVariantConst annotation) {
  if (strcmp(annotation["type"] | "", "url_citation")) return;
  const char* url = annotation["url"] | "";
  if ((strncmp(url, "https://", 8) && strncmp(url, "http://", 7)) || strlen(url) > 1536) return;
  String escaped;
  for (const uint8_t* p = reinterpret_cast<const uint8_t*>(url); *p; ++p) {
    if (*p < 33 || *p >= 127) { char hex[4]; snprintf(hex, sizeof(hex), "%%%02X", *p); escaped += hex; }
    else escaped += static_cast<char>(*p);
  }
  for (size_t i = 0; i < sourceCount; ++i) if (sourceUrls[i] == escaped) return;
  if (sourceCount < 6) sourceUrls[sourceCount++] = escaped;
}

void backendEvent(JsonObjectConst event, const char* delegationId) {
  const char* type = event["type"] | "";
  const uint32_t now = millis();
  if (!strcmp(type, "response.created")) {
    if (!delegations.start(event["response"]["id"] | "", now)) { fail("Too many backend requests"); return; }
    lastInteraction = now;
    farewellPending = false;
    deviceLog.println("{\"event\":\"backend_started\"}");
  } else if (!strcmp(type, "response.completed") || !strcmp(type, "response.failed") ||
             !strcmp(type, "response.incomplete") || !strcmp(type, "response.cancelled")) {
    if (delegations.finish(event["response"]["id"] | "", now)) {
      lastInteraction = now;
      deviceLog.printf("{\"event\":\"backend_finished\",\"status\":\"%s\",\"elapsed_ms\":%u}\n", type, delegations.lastDuration);
    }
  } else if (!strcmp(type, "response.output_item.added") && !strcmp(event["item"]["type"] | "", "web_search_call")) {
    ++webSearches;
    if (sourcesDelegationId != delegationId) {
      sourcesDelegationId = delegationId;
      for (auto& url : sourceUrls) url = "";
      sourceCount = 0;
    }
    deviceLog.println("{\"event\":\"web_search_started\"}");
  } else if (!strcmp(type, "response.output_item.done")) {
    auto item = event["item"];
    if (!strcmp(item["type"] | "", "message"))
      for (JsonObjectConst content : item["content"].as<JsonArrayConst>()) traceText("backend", content["text"] | "");
    if (!strcmp(item["type"] | "", "web_search_call") && !strcmp(item["status"] | "", "completed")) {
      ++webSearchCompleted;
      deviceLog.println("{\"event\":\"web_search_completed\"}");
    }
    if (sourcesDelegationId == delegationId)
      for (JsonObjectConst content : item["content"].as<JsonArrayConst>())
        for (JsonVariantConst annotation : content["annotations"].as<JsonArrayConst>()) collectCitation(annotation);
  } else if (!strcmp(type, "response.output_text.annotation.added") && sourcesDelegationId == delegationId) collectCitation(event["annotation"]);
}

void receiveMessage(uint8_t* payload, size_t length) {
  if (length > MAX_MESSAGE) { fail("API message too large"); return; }
  maxMessageBytes = max(maxMessageBytes, static_cast<uint32_t>(length));
  JsonDocument doc(&jsonAllocator);
  const uint32_t parseStart = micros();
  const auto error = deserializeJson(doc, payload, length, DeserializationOption::Filter(apiEventFilter));
  maxJsonParseUs = max<uint32_t>(maxJsonParseUs, micros() - parseStart);
  if (error) { fail("Invalid API message"); return; }
  const char* type = doc["type"] | "";
  if (!strcmp(type, "response.event")) {
    if (phase == Phase::Live) backendEvent(doc["event"].as<JsonObjectConst>(), doc["delegation_id"] | "");
  } else if (!strcmp(type, "session.started")) {
    if (phase != Phase::Connecting) return;
    apiSessionStarted = true;
    sessionSince = lastInteraction = millis();
    sentSamples = receivedSamples = 0;
    inputTextBytes = outputTextBytes = 0;
    usageSeconds = 0;
    closeFinalized = false;
    farewellPending = farewellHeard = false;
    audio.enableInput();
    liveStartMs = millis() - connectRequestedAt;
  } else if (!strcmp(type, "session.instructions.appended")) {
    if (awaitedUtterance != Utterance::None && utteranceEventId == (doc["client_event_id"] | "")) {
      utteranceAccepted = true;
      deviceLog.println("{\"event\":\"utterance_instruction_accepted\"}");
    }
  } else if (!strcmp(type, "session.output_audio.delta")) {
    if (!apiSessionStarted || (phase != Phase::Live && phase != Phase::Connecting)) return;
    const char* encoded = doc["delta"] | "";
    size_t decodedLength = 0;
    if (mbedtls_base64_decode(decoded, MAX_MESSAGE, &decodedLength,
        reinterpret_cast<const unsigned char*>(encoded), strlen(encoded)) != 0 || (decodedLength & 1)) {
      fail("Invalid audio data"); return;
    }
    const size_t count = decodedLength / 2;
    const uint32_t arrival = micros();
    if (lastAudioArrivalUs) maxAudioArrivalGapUs = max(maxAudioArrivalGapUs, arrival - lastAudioArrivalUs);
    lastAudioArrivalUs = arrival;
    const auto* samples = reinterpret_cast<const int16_t*>(decoded);
    receivedSamples += count;
    if (peakOf(samples, count) > 650) lastInteraction = millis();
    if (!audio.play(samples, count)) { fail("Audio queue overflow"); return; }
  } else if (!strcmp(type, "session.input_transcript.delta")) {
    inputTextBytes += strlen(doc["delta"] | "");
    traceText("user", doc["delta"] | "");
    if (strlen(doc["delta"] | "")) userActivity();
  } else if (!strcmp(type, "session.output_transcript.delta")) {
    outputTextBytes += strlen(doc["delta"] | "");
    traceText("assistant", doc["delta"] | "");
  } else if (!strcmp(type, "session.usage.updated")) {
    usageSeconds = doc["usage"]["seconds"] | usageSeconds;
  } else if (!strcmp(type, "session.closed")) {
    closeFinalized = true;
    usageSeconds = doc["usage"]["seconds"] | usageSeconds;
    stopAudio();
    // Disconnect is performed by loop after final usage was received.
    setPhase(Phase::Ready, "Session ended. Tap START");
  } else if (!strcmp(type, "error")) {
    // Log only stable error identifiers, never arbitrary server text or credentials.
    JsonDocument error;
    error["event"] = "api_error";
    error["code"] = doc["error"]["code"] | "unknown";
    error["type"] = doc["error"]["type"] | "unknown";
    serializeJson(error, deviceLog); deviceLog.println();
    fail("API error; check USB log");
  }
}

void socketEvent(WStype_t type, uint8_t* payload, size_t length) {
  if (connectionActive) {
    // No session has been started yet. Handshake callbacks never touch the UI,
    // audio, JSON allocator or application state from the connection task.
    if (type == WStype_CONNECTED) connectionResult = ConnectResult::Connected;
    else if (type == WStype_ERROR || type == WStype_DISCONNECTED) connectionResult = ConnectResult::Failed;
    return;
  }
  ++socketEvents;
  switch (type) {
    case WStype_CONNECTED: {
      if (phase != Phase::Connecting) return;
      socketConnected = true;
      connectedMs = millis() - connectRequestedAt;
      JsonDocument doc;
      doc["type"] = "session.start";
      assistantConfig.session(doc["session"].to<JsonObject>(), RATE, time(nullptr));
      if (!sendJson(doc)) fail("Session start send failed");
      break;
    }
    case WStype_DISCONNECTED:
      socketConnected = false;
      if (transportActive && !closeFinalized && phase != Phase::Error) fail("Connection lost; tap RETRY");
      break;
    case WStype_TEXT: receiveMessage(payload, length); break;
    case WStype_FRAGMENT_TEXT_START:
      fragmentActive = true;
      fragmentLength = 0;
      [[fallthrough]];
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
      if (!fragmentActive || length > MAX_MESSAGE - fragmentLength) { fail("Invalid message fragments"); break; }
      memcpy(fragments + fragmentLength, payload, length);
      fragmentLength += length;
      if (type == WStype_FRAGMENT_FIN) {
        fragmentActive = false;
        receiveMessage(reinterpret_cast<uint8_t*>(fragments), fragmentLength);
        fragmentLength = 0;
      }
      break;
    case WStype_ERROR: fail("WebSocket error"); break;
    default: break;
  }
}

void connectTask(void*) {
  while (!connectionCancel && connectionResult == ConnectResult::Pending && millis() - connectRequestedAt < 25000) {
    socket.loop();
    delay(1);
  }
  connectionStackFree = uxTaskGetStackHighWaterMark(nullptr);
  connectionActive = false;  // Publish the result only after the final socket access.
  vTaskDelete(nullptr);
}

void pollConnection() {
  if (!connectionPending || connectionActive) return;
  connectionPending = false;
  if (connectionCancel || phase == Phase::Error) {
    stopTransport();
    if (phase == Phase::Closing) setPhase(WiFi.status() == WL_CONNECTED ? Phase::Ready : Phase::Error, "Connection cancelled");
  } else if (connectionResult == ConnectResult::Connected) socketEvent(WStype_CONNECTED, nullptr, 0);
  else fail("Connection failed; tap RETRY");
}

void startSession(bool fromPresence = false) {
  if (connectionPending || (phase != Phase::Ready && phase != Phase::Noticed)) return;
  if (WiFi.status() != WL_CONNECTED) { fail("Wi-Fi unavailable"); return; }
  if (time(nullptr) < 1704067200) { fail("Clock not synchronized"); return; }
  stopAudio();
  automaticStart = fromPresence;
  greetingPending = farewellPending = false;
  awaitedUtterance = Utterance::None;
  utteranceAccepted = false;
  connectRequestedAt = millis();
  connectedMs = liveStartMs = firstSpeechMs = 0;
  socketEvents = maxSocketPollUs = maxJsonParseUs = maxMessageBytes = 0;
  lastAudioArrivalUs = maxAudioArrivalGapUs = 0;
  delegations.reset();
  webSearches = webSearchCompleted = 0;
  lastClockUpdate = millis();
  sentSamples = receivedSamples = inputTextBytes = outputTextBytes = 0;
  conversationTrace = "";
  traceTruncated = false;
  usageSeconds = 0;
  farewellHeard = false;
  localCueLatencyMs = localCueFinishedMs = 0;
  localCueReported = localCueFinishedReported = apiSessionStarted = false;
  startupUiFrames = startupMouthFrames = 0;
  startupFrameAt = character.frameCount;
  startupMouthAt = character.mouthFrames;
  closeFinalized = false;
  pendingFailure = false;
  if (!audio.begin(false) || !audio.playLocal(startupPcm, startupPcmEnd - startupPcm)) {
    fail("Local startup audio failed"); return;
  }
  authHeader = "Authorization: Bearer " + apiKey;
  socket.beginSslWithCA("api.openai.com", 443, "/v1/live/sessions", rootCA, "");
  socket.setExtraHeaders(authHeader.c_str());
  socket.setReconnectInterval(1000);
  socket.enableHeartbeat(15000, 5000, 2);
  transportActive = true;
  setPhase(Phase::Connecting, "Opening secure session");
  connectionCancel = false;
  connectionResult = ConnectResult::Pending;
  connectionStackFree = 0;
  connectionPending = true;
  connectionActive = true;
  if (xTaskCreatePinnedToCore(connectTask, "live-connect", 16384, nullptr, 1, nullptr, 0) != pdPASS) {
    connectionPending = false;
    connectionActive = false;
    fail("Connection task failed");
  }
}

void pumpAudio() {
  if (!apiSessionStarted || (phase != Phase::Live && phase != Phase::Connecting)) return;
  DuplexAudio::Input input;
  for (int frames = 0; frames < 8 && audio.take(input); ++frames) {
    const uint16_t inputPeak = input.activityPeak;
    if (inputPeak > 1200) userActivity();
    const char prefix[] = "{\"type\":\"session.input_audio.append\",\"audio\":\"";
    memcpy(outgoing, prefix, sizeof(prefix) - 1);
    size_t encodedLength = 0;
    int result = mbedtls_base64_encode(reinterpret_cast<unsigned char*>(outgoing + sizeof(prefix) - 1),
      sizeof(outgoing) - sizeof(prefix) - 3, &encodedLength, reinterpret_cast<const unsigned char*>(input.samples), FRAME * 2);
    if (result) { fail("Audio encoding failed"); return; }
    const size_t end = sizeof(prefix) - 1 + encodedLength;
    outgoing[end] = '\"'; outgoing[end+1] = '}'; outgoing[end+2] = 0;
    if (!socket.sendTXT(outgoing, end + 2)) { fail("Audio send failed"); return; }
    sentSamples += FRAME;
  }
  const auto metrics = audio.stats();
  if (!firstSpeechMs && metrics.remoteFirstSoundAt) {
    firstSpeechMs = metrics.remoteFirstSoundAt - connectRequestedAt;
    deviceLog.printf("{\"event\":\"first_speech\",\"elapsed_ms\":%u}\n", firstSpeechMs);
  }
  if (metrics.ioErrors || metrics.inputDrops) { fail("Audio stream stalled"); return; }
  if (phase != Phase::Live) return;
  if (millis() - sessionSince > SESSION_LIMIT_MS) { closeSession(); return; }
  // Inspect audio actually sent to I2S, so the final word is not cut off by queued audio.
  if (farewellPending) {
    if (metrics.playbackPeak > 190) { farewellHeard = true; farewellLastSpeech = millis(); }
    if ((farewellHeard && millis() - farewellLastSpeech >= 1500) || millis() - farewellSince >= FAREWELL_TIMEOUT_MS)
      closeSession();
  } else if (!greetingPending && !delegations.waiting() && millis() - lastInteraction > IDLE_LIMIT_MS) {
    farewellPending = true;
    farewellHeard = false;
    farewellSince = millis();
    if (!sendUtteranceInstruction(Utterance::Farewell, assistantConfig.farewell())) {
      fail("Farewell send failed"); return;
    }
    ++farewellsSent;
    deviceLog.println("{\"event\":\"farewell_requested\"}");
  }
}

void updateStartup() {
  if (phase != Phase::Connecting) return;
  const auto metrics = audio.stats();
  startupUiFrames = character.frameCount - startupFrameAt;
  startupMouthFrames = character.mouthFrames - startupMouthAt;
  if (!localCueReported && metrics.localFirstSoundAt) {
    localCueReported = true;
    localCueLatencyMs = metrics.localFirstSoundAt - connectRequestedAt;
    deviceLog.printf("{\"event\":\"local_cue_first_sound\",\"elapsed_ms\":%u}\n", localCueLatencyMs);
  }
  if (!localCueFinishedReported && metrics.localFinishedAt) {
    localCueFinishedReported = true;
    localCueFinishedMs = metrics.localFinishedAt - connectRequestedAt;
    deviceLog.printf("{\"event\":\"local_cue_finished\",\"elapsed_ms\":%u,\"samples\":%u}\n", localCueFinishedMs, metrics.localPlayed);
  }
  if (metrics.ioErrors || metrics.inputDrops) { fail("Startup audio stalled"); return; }
  if (apiSessionStarted && !metrics.localActive) {
    lastInteraction = millis();
    setPhase(Phase::Live, "Speak naturally anytime");
    greetingPending = true;
  }
}

void connectWifi() {
  stopAudio(); stopTransport();
  if (!burnerConfig.configured()) { setPhase(Phase::Setup, "Configure with M5Burner"); return; }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid.c_str(), password.c_str());
  setPhase(Phase::Wifi, "Joining configured network");
}

void drawUi() {
  const uint32_t now = millis();
  if (!uiDirty && now - lastDraw < 50) return;
  lastDraw = now; uiDirty = false;
  character.searching = phase == Phase::Live && delegations.waiting();
  character.draw(phase, WiFi.status() == WL_CONNECTED, WiFi.RSSI(), inputMuted,
                 audio.takePlaybackPeak(), now);
}

void returnToLauncher() {
  if (!character.launcherAvailable || transportActive || connectionPending || connectionActive) {
    deviceLog.println("{\"event\":\"launcher_unavailable_or_busy\"}");
    return;
  }
  proximity.enabled = false;
  stopAudio();
  WiFi.disconnect(true, false);
  deviceLog.println("{\"event\":\"launcher_restart\"}");
  Serial.flush();
  M5.Display.setBrightness(0);
  LauncherSupport::restart();
}

void handleTap(int x, int y) {
  if (burnerConfig.active) return;
  const auto action = character.tap(x, y, phase, millis());
  uiDirty = true;
  switch (action) {
    case CharacterUI::Action::Start: startSession(); break;
    case CharacterUI::Action::End: closeSession(); break;
    case CharacterUI::Action::Retry: connectWifi(); break;
    case CharacterUI::Action::Launcher: returnToLauncher(); break;
    case CharacterUI::Action::ToggleMute:
      inputMuted = !inputMuted; audio.mute(inputMuted); break;
    default: break;
  }
}

// Keep diagnostic buffers out of the command dispatcher's stack frame.
__attribute__((noinline)) void audioTest() {
  if (transportActive) { deviceLog.println("{\"event\":\"test_busy\"}"); return; }
  detail = "Testing duplex audio"; uiDirty = true; drawUi();
  if (!audio.begin()) { deviceLog.println("{\"event\":\"audio_test\",\"initialized\":false}"); return; }
  constexpr size_t Count = RATE * 4;
  auto* capture = static_cast<int16_t*>(ps_malloc(Count * 3 * sizeof(int16_t)));
  if (!capture) { audio.end(); return; }
  size_t offset = 0;
  DuplexAudio::Input input;
  int16_t tone[FRAME];
  const uint32_t deadline = millis() + 6000;
  while (offset + FRAME <= Count && static_cast<int32_t>(deadline - millis()) > 0) {
    if (!audio.take(input)) { delay(1); continue; }
    for (size_t i = 0; i < FRAME; ++i) {
      const size_t position = offset + i;
      const float t = position / static_cast<float>(RATE);
      tone[i] = (t > 0.5f && t < 2.5f) ? 6500 * sinf(2 * PI * (330 * t + 100 * t * t)) : 0;
      capture[position * 3] = input.raw[i];
      capture[position * 3 + 1] = input.reference[i];
      capture[position * 3 + 2] = input.samples[i];
    }
    audio.play(tone, FRAME);
    offset += FRAME;
  }
  const auto metrics = audio.stats();
  audio.end();
  deviceLog.println("{\"event\":\"audio_capture_begin\",\"rate\":16000,\"channels\":3}");
  unsigned char encoded[1372];
  const auto* bytes = reinterpret_cast<const unsigned char*>(capture);
  size_t total = offset * 3 * 2;
  for (size_t pos = 0; pos < total; pos += 1024) {
    size_t length = 0;
    mbedtls_base64_encode(encoded, sizeof(encoded), &length, bytes + pos, min(size_t(1024), total - pos));
    deviceLog.write(encoded, length); deviceLog.println();
  }
  free(capture);
  JsonDocument result;
  result["event"] = "audio_test";
  result["initialized"] = true;
  result["samples"] = offset;
  result["io_errors"] = metrics.ioErrors;
  result["input_drops"] = metrics.inputDrops;
  result["aec_process_us"] = metrics.processUs;
  serializeJson(result, deviceLog); deviceLog.println();
  detail = "Tap START to connect"; uiDirty = true;
}

__attribute__((noinline)) void screenshot(int mouth = -1, int blink = -1) {
  if (phase == Phase::Live || phase == Phase::Connecting || phase == Phase::Closing) {
    deviceLog.println("{\"event\":\"screen_busy\"}"); return;
  }
  character.draw(phase, WiFi.status() == WL_CONNECTED, WiFi.RSSI(), inputMuted,
                 0, millis(), mouth, blink);
  deviceLog.println("{\"event\":\"screen_begin\",\"width\":320,\"height\":240,\"format\":\"rgb888\"}");
  uint8_t pixels[320 * 3];
  unsigned char encoded[1284];
  for (int y = 0; y < 240; ++y) {
    canvas.readRectRGB(0, y, 320, 1, pixels);
    size_t length = 0;
    mbedtls_base64_encode(encoded, sizeof(encoded), &length, pixels, sizeof(pixels));
    deviceLog.write(encoded, length); deviceLog.println();
  }
  deviceLog.println("{\"event\":\"screen_end\"}");
}

void command(const String& line) {
  if (line.startsWith("CMD::")) {
    deviceLog.enabled = false;
    burnerConfig.handle(line, transportActive || connectionPending);
    if (burnerConfig.active) proximity.block(millis());
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, line)) { deviceLog.println("{\"event\":\"invalid_command\"}"); return; }
  burnerConfig.disconnect();
  deviceLog.enabled = true;
  const char* cmd = doc["cmd"] | "";
  if (!strcmp(cmd, "status")) emitStatus();
  else if (!strcmp(cmd, "mic_gain")) {
    if (doc["value"].is<uint8_t>()) audio.setMicGain(doc["value"].as<uint8_t>());
    emitStatus();
  } else if (!strcmp(cmd, "trace")) {
    if (transportActive) { deviceLog.println("{\"event\":\"trace_busy\"}"); return; }
    if (doc["enabled"].is<bool>()) {
      traceEnabled = doc["enabled"].as<bool>();
      conversationTrace = "";
      traceTruncated = false;
      if (traceEnabled) conversationTrace.reserve(12000);
      apiEventFilter["event"]["item"]["content"][0]["text"] = traceEnabled;
      emitStatus();
    } else {
      JsonDocument result(&jsonAllocator);
      result["text"] = conversationTrace;
      result["truncated"] = traceTruncated;
      String serialized;
      serializeJson(result, serialized);
      const size_t capacity = ((serialized.length() + 2) / 3) * 4 + 1;
      auto* encoded = static_cast<unsigned char*>(ps_malloc(capacity));
      if (!encoded) { deviceLog.println("{\"event\":\"trace_failed\"}"); return; }
      size_t length = 0;
      mbedtls_base64_encode(encoded, capacity, &length, reinterpret_cast<const unsigned char*>(serialized.c_str()), serialized.length());
      deviceLog.print("{\"event\":\"conversation_trace\",\"base64\":\"");
      deviceLog.write(encoded, length); deviceLog.println("\"}");
      free(encoded);
    }
  }
  else if (!strcmp(cmd, "export_config")) {
    if (transportActive) { deviceLog.println("{\"event\":\"export_busy\"}"); return; }
    JsonDocument config(&jsonAllocator);
    assistantConfig.session(config.to<JsonObject>(), RATE, time(nullptr));
    String serialized;
    serializeJson(config, serialized);
    const size_t capacity = ((serialized.length() + 2) / 3) * 4 + 1;
    auto* encoded = static_cast<unsigned char*>(ps_malloc(capacity));
    if (!encoded) { deviceLog.println("{\"event\":\"export_failed\"}"); return; }
    size_t length = 0;
    mbedtls_base64_encode(encoded, capacity, &length, reinterpret_cast<const unsigned char*>(serialized.c_str()), serialized.length());
    deviceLog.print("{\"event\":\"session_config\",\"base64\":\"");
    deviceLog.write(encoded, length); deviceLog.println("\"}");
    free(encoded);
  } else if (!strcmp(cmd, "sources")) {
    if (transportActive) { deviceLog.println("{\"event\":\"sources_busy\"}"); return; }
    JsonDocument result;
    result["event"] = "sources";
    auto urls = result["urls"].to<JsonArray>();
    for (size_t i = 0; i < sourceCount; ++i) urls.add(sourceUrls[i]);
    serializeJson(result, deviceLog); deviceLog.println();
  } else if (!strcmp(cmd, "erase_config")) {
    if (transportActive) { deviceLog.println("{\"event\":\"erase_busy\"}"); return; }
    if (!burnerConfig.clear()) { deviceLog.println("{\"event\":\"erase_failed\"}"); return; }
    ssid = password = apiKey = authHeader = "";
    WiFi.disconnect(true, true);
    setPhase(Phase::Setup, "Configuration erased");
    deviceLog.println("{\"event\":\"configuration_erased\"}");
  }
  else if (!strcmp(cmd, "start")) startSession();
  else if (!strcmp(cmd, "end")) closeSession();
  else if (!strcmp(cmd, "launcher")) returnToLauncher();
  else if (!strcmp(cmd, "mute") && phase == Phase::Live) { inputMuted = doc["enabled"] | false; audio.mute(inputMuted); uiDirty = true; }
  else if (!strcmp(cmd, "test")) audioTest();
  else if (!strcmp(cmd, "screen")) screenshot(doc["mouth"] | -1, doc["blink"] | -1);
  else if (!strcmp(cmd, "tap")) handleTap(doc["x"] | 160, doc["y"] | 100);
  else if (!strcmp(cmd, "retry") && !transportActive) connectWifi();
  else if (!strcmp(cmd, "proximity")) {
    if (doc["enabled"].is<bool>()) {
      proximity.enabled = doc["enabled"].as<bool>();
      proximity.block(millis());
      if (!proximity.enabled && phase == Phase::Noticed) closeSession();
    }
    emitStatus();
  }
  else if (!strcmp(cmd, "greet") && phase == Phase::Live) {
    if (!greet()) fail("Greeting send failed");
  } else deviceLog.println("{\"event\":\"unknown_command\"}");
}

void readSerial() {
  if (millis() - lastSerialByte > 1500) { serialLine = ""; serialOverflow = false; }
  size_t budget = 2048;
  while (Serial.available() && budget--) {
    const char c = static_cast<char>(Serial.read());
    lastSerialByte = millis();
    if (c == '\n') {
      if (!serialOverflow && !serialLine.isEmpty()) command(serialLine);
      serialLine = ""; serialOverflow = false;
    } else if (c != '\r' && !serialOverflow) {
      if (c == '\0') { serialOverflow = true; serialLine = ""; }
      else if (serialLine.length() < 1500) serialLine += c;
      else { serialOverflow = true; serialLine = ""; }
    }
  }
}
}  // namespace

void setup() {
  esp_log_level_set("*", ESP_LOG_NONE);
  // The default 256-byte USB queue can truncate pasted API keys during a redraw.
  const bool serialOk = Serial.setRxBufferSize(4096) == 4096;
  auto config = M5.config();
  config.serial_baudrate = 115200;
  config.clear_display = true;
  M5.begin(config);
  Serial.begin(115200);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(110);
  M5.Speaker.end(); M5.Mic.end();
  canvas.setColorDepth(16);
  canvas.setPsram(true);
  bool screenOk = canvas.createSprite(320, 240) != nullptr;
  decoded = static_cast<uint8_t*>(ps_malloc(MAX_MESSAGE));
  fragments = static_cast<char*>(ps_malloc(MAX_MESSAGE));
  if (!serialOk || !screenOk || !decoded || !fragments || !character.begin() || !assistantConfig.begin()) {
    M5.Display.fillScreen(0x2949);
    M5.Display.drawTriangle(160, 95, 137, 135, 183, 135, TFT_RED);
    deviceLog.println("{\"event\":\"fatal_initialization\"}");
    while (true) delay(1000);
  }
  serialLine.reserve(1500);
  character.launcherAvailable = LauncherSupport::available();
  configureApiEventFilter(apiEventFilter);
  if (apiEventFilter.overflowed()) {
    deviceLog.println("{\"event\":\"fatal_event_filter\"}");
    while (true) delay(1000);
  }
  if (!burnerConfig.begin()) {
    setPhase(Phase::Error, "Settings storage unavailable");
    drawUi();
    return;
  }
  ssid = burnerConfig.ssid();
  password = burnerConfig.password();
  apiKey = burnerConfig.apiKey();
  socket.onEvent(socketEvent);
  deviceLog.printf("{\"event\":\"boot\",\"build\":\"%s\"}\n", BUILD);
  deviceLog.printf("{\"event\":\"proximity_init\",\"available\":%s}\n", proximity.begin() ? "true" : "false");
  connectWifi();
  drawUi();
}

void loop() {
  M5.update();
  readSerial();
  const uint32_t now = millis();
  if (phase == Phase::Wifi) {
    if (WiFi.status() == WL_CONNECTED) {
      configTime(0, 0, "time.cloudflare.com", "pool.ntp.org", "time.google.com");
      setPhase(Phase::Clock, "Verifying time for TLS");
    } else if (now - phaseSince > 30000) fail("Wi-Fi failed; check settings");
  } else if (phase == Phase::Clock) {
    if (time(nullptr) > 1704067200) setPhase(Phase::Ready, "Tap START to connect");
    else if (now - phaseSince > 30000) fail("Clock sync failed; tap RETRY");
  }
  if ((phase == Phase::Ready || phase == Phase::Noticed || phase == Phase::Live || phase == Phase::Connecting) && WiFi.status() != WL_CONNECTED) fail("Wi-Fi disconnected");
  if (M5.Touch.getCount()) {
    const auto touch = M5.Touch.getDetail(0);
    if (touch.wasPressed()) handleTap(touch.x, touch.y);
  }
  pollConnection();
  if (transportActive && !connectionPending && !pendingFailure) {
    // WebSockets handles one frame per poll. Drain bursts of search notifications
    // before drawing again, with a time/count budget so microphone input still runs.
    const uint32_t pollStart = micros();
    for (unsigned i = 0; i < 12; ++i) {
      const uint32_t before = socketEvents;
      socket.loop();
      if (socketEvents == before || pendingFailure || !transportActive || micros() - pollStart >= 8000) break;
    }
    maxSocketPollUs = max<uint32_t>(maxSocketPollUs, micros() - pollStart);
  }
  if (pendingFailure) {
    stopAudio(); stopTransport(); pendingFailure = false;
  } else if (phase == Phase::Ready && transportActive && closeFinalized) stopTransport();
  if (phase == Phase::Connecting && millis() - phaseSince > 25000) fail("Session start timed out");
  if (phase == Phase::Closing && millis() - phaseSince > 15000) fail("Final usage unconfirmed");
  pumpAudio();
  updateStartup();
  if (phase == Phase::Live && delegations.expire(millis(), assistantConfig.searchWaitMs())) {
    lastInteraction = millis();
    JsonDocument event;
    event["type"] = "session.commentary.append";
    event["delegation_id"] = nullptr;
    event["content"] = "The lookup is taking longer than expected. The current information is not verified yet.";
    if (!sendJson(event)) fail("Lookup update failed");
    deviceLog.println("{\"event\":\"backend_wait_timeout\"}");
  }
  if (phase == Phase::Live && millis() - lastClockUpdate >= 60000) {
    lastClockUpdate = millis();
    JsonDocument event;
    event["type"] = "session.thinking.append";
    event["delegation_id"] = nullptr;
    event["content"] = assistantConfig.clockContext(time(nullptr));
    if (!sendJson(event)) fail("Clock context update failed");
  }
  if (greetingPending && phase == Phase::Live) {
    greetingPending = false;
    if (!greet()) fail("Greeting send failed");
  }
  if (utteranceAccepted && phase == Phase::Live) {
    const auto kind = awaitedUtterance;
    awaitedUtterance = Utterance::None;
    utteranceAccepted = false;
    JsonDocument event;
    event["type"] = "session.commentary.append";
    event["event_id"] = "device_cue_" + String(utteranceSequence);
    event["delegation_id"] = nullptr;
    event["content"] = kind == Utterance::Farewell ? u8"\u307e\u305f\u306d\u3002" : "Begin the conversation now, following the greeting instructions provided.";
    if (!sendJson(event)) fail("Speech cue send failed");
    else deviceLog.println("{\"event\":\"utterance_cue_sent\"}");
  }
  if (proximity.poll(millis(), phase == Phase::Ready && !transportActive && !burnerConfig.active)) {
    ++presenceStarts;
    setPhase(Phase::Noticed, "Hand detected");
    startSession(true);
  }
  if (deviceLog.enabled && now - lastStatus > 5000) { emitStatus(); lastStatus = now; }
  drawUi();
  delay(1);
}
