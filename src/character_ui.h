#pragma once
#include <M5Unified.h>

class CharacterUI {
 public:
  enum class State { Setup, Wifi, Clock, Ready, Noticed, Connecting, Live, Closing, Error };
  enum class Action { None, Start, End, ToggleMute, Retry, Launcher };
  explicit CharacterUI(M5Canvas& target) : canvas(target) {}
  bool begin();
  void draw(State state, bool connected, int rssi, bool muted, uint16_t playbackPeak,
            uint32_t now, int mouthOverride = -1, int blinkOverride = -1);
  Action tap(int x, int y, State state, uint32_t now);
  bool controlsVisible(uint32_t now) const { return controls && now - controlSince < 5000; }
  uint32_t frameCount = 0, mouthFrames = 0, blinkCount = 0, maxDrawUs = 0;
  uint64_t pixelsSent = 0;
  uint8_t mouth = 0;
  bool blinking = false;
  bool searching = false;
  bool launcherAvailable = false;

 private:
  void patch(size_t offset, int x, int y, int w, int h, int dx, int dy);
  void glass(int x, int y, int w, int h, int radius, uint8_t alpha);
  void micIcon(int x, int y, uint16_t color, bool muted);
  void phoneIcon(int x, int y, uint16_t color, bool hangup);
  void closeIcon(int x, int y, uint16_t color);
  void retryIcon(int x, int y, uint16_t color);
  void launcherIcon(int x, int y);
  void statusIcon(State state, bool muted, uint32_t now);
  struct Rect { int x, y, w, h; };
  void compose(Rect area, State state, bool connected, int bars, bool muted,
               bool closedEyes, bool showControls, int dx, int dy, uint32_t now);
  bool intersects(int x, int y, int w, int h) const;
  M5Canvas& canvas;
  uint16_t* pixels = nullptr;
  uint16_t* framebuffer = nullptr;
  Rect clip{0, 0, 320, 240};
  bool cached = false;
  int lastDx = 0, lastDy = 0, lastMouth = 0, lastBars = 0;
  bool lastClosed = false, lastMuted = false, lastConnected = false, lastControls = false;
  bool lastSearching = false;
  State lastState = State::Setup;
  uint32_t lastIconTick = 0;
  float envelope = 0;
  bool controls = false;
  uint32_t controlSince = 0, nextBlink = 2300, blinkSince = 0, mouthSince = 0;
};
