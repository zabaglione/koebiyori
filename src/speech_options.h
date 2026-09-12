#pragma once
#include <stddef.h>
#include <string.h>

namespace SpeechOptions {
struct Voice {
  const char* id;
  const char* name;
  const char* region;
  const char* presentation;
};

// OpenAI Live built-in voices, verified 2026-09-12:
// https://developers.openai.com/api/docs/guides/live-conversations#voice-options
// https://developers.openai.com/api/reference/typescript/resources/live/subresources/sessions/methods/accept
constexpr Voice Voices[] = {
    {"marin", "Marin", "Built-in voice / default", ""},
    {"cedar", "Cedar", "Built-in voice", ""},
    {"alloy", "Alloy", "Built-in voice", ""},
    {"ash", "Ash", "Built-in voice", ""},
    {"ballad", "Ballad", "Built-in voice", ""},
    {"coral", "Coral", "Built-in voice", ""},
    {"echo", "Echo", "Built-in voice", ""},
    {"sage", "Sage", "Built-in voice", ""},
    {"shimmer", "Shimmer", "Built-in voice", ""},
    {"verse", "Verse", "Built-in voice", ""},
    {"quartz", "Quartz", "English / Australia", "Feminine"},
    {"ripple", "Ripple", "English / Australia", "Masculine"},
    {"vesper", "Vesper", "English / Britain", "Masculine"},
    {"willow", "Willow", "English / Ireland", "Feminine"},
    {"stone", "Stone", "English / Ireland", "Masculine"},
    {"gleam", "Gleam", "English / North America", "Feminine"},
    {"meridian", "Meridian", "English / North America", "Masculine"},
    {"bossa", "Bossa", "Portuguese / Brazil", "Feminine"},
    {"tempo", "Tempo", "Portuguese / Brazil", "Masculine"},
    {"beacon", "Beacon", "English / Philippines", "Masculine"},
    {"delta", "Delta", "English / Southern US", "Feminine"},
    {"cinder", "Cinder", "English / Southern US", "Masculine"},
};
struct Style { const char* id; const char* name; };
constexpr Style Styles[] = {
    {"anime", "Anime"},
    {"natural", "Natural"},
};
constexpr size_t VoiceCount = sizeof(Voices) / sizeof(Voices[0]);
constexpr size_t StyleCount = sizeof(Styles) / sizeof(Styles[0]);

inline int voiceIndex(const char* id) {
  if (id) for (size_t i = 0; i < VoiceCount; ++i) if (!strcmp(Voices[i].id, id)) return static_cast<int>(i);
  return -1;
}
inline int styleIndex(const char* id) {
  if (id) for (size_t i = 0; i < StyleCount; ++i) if (!strcmp(Styles[i].id, id)) return static_cast<int>(i);
  return -1;
}
struct Selection {
  size_t voice = 0, style = 0;
  bool valid() const { return voice < VoiceCount && style < StyleCount; }
};

// A draft stays separate from saved settings until the caller persists Save.
class Picker {
 public:
  enum class Action { None, Save, Cancel };
  bool visible = false, saveFailed = false;
  Selection selection;
  void open(Selection saved) { selection = saved; visible = true; saveFailed = false; }
  void close() { visible = false; saveFailed = false; }
  Action tap(int x, int y) {
    if (!visible || x < 0 || x >= 320 || y < 0 || y >= 240) return Action::None;
    if (y >= 54 && y < 104) {
      if (x >= 24 && x < 74) selection.voice = (selection.voice + VoiceCount - 1) % VoiceCount;
      else if (x >= 246 && x < 296) selection.voice = (selection.voice + 1) % VoiceCount;
    } else if (x >= 64 && x < 256 && y >= 134 && y < 170) {
      selection.style = (selection.style + 1) % StyleCount;
    } else if (y >= 186 && y < 226) {
      if (x >= 24 && x < 150) { close(); return Action::Cancel; }
      if (x >= 170 && x < 296) return Action::Save;
    }
    return Action::None;
  }
};
}  // namespace SpeechOptions
