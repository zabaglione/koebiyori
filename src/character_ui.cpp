#include "character_ui.h"
#include <esp_timer.h>
#include <math.h>

namespace {
extern const uint8_t artworkStart[] asm("_binary_assets_generated_sara_rgb_start");
extern const uint8_t artworkEnd[] asm("_binary_assets_generated_sara_rgb_end");
constexpr size_t AssetBytes = 186930;
constexpr uint16_t Plum = 0x2949, White = 0xFFBF, Mint = 0x87B6, Pink = 0xFCB5, Amber = 0xFE2E;

bool canChooseVoice(CharacterUI::State state) {
  return state == CharacterUI::State::Ready || state == CharacterUI::State::Error ||
         state == CharacterUI::State::Setup || state == CharacterUI::State::Wifi || state == CharacterUI::State::Clock;
}

uint16_t blend(uint16_t a, uint16_t b, unsigned opacity) {
  unsigned inv = 255 - opacity;
  return ((((a >> 11) * inv + (b >> 11) * opacity) / 255) << 11) |
         (((((a >> 5) & 63) * inv + ((b >> 5) & 63) * opacity) / 255) << 5) |
         (((a & 31) * inv + (b & 31) * opacity) / 255);
}
}

bool CharacterUI::begin() {
  if (artworkEnd - artworkStart != AssetBytes) return false;
  pixels = static_cast<uint16_t*>(ps_malloc(AssetBytes));
  if (!pixels) return false;
  memcpy(pixels, artworkStart, AssetBytes);
  // M5Canvas stores RGB565 in display byte order. Convert the immutable atlas once.
  for (size_t i = 0; i < AssetBytes / 2; ++i) pixels[i] = __builtin_bswap16(pixels[i]);
  framebuffer = static_cast<uint16_t*>(canvas.getBuffer());
  return true;
}

bool CharacterUI::intersects(int x, int y, int w, int h) const {
  return x < clip.x + clip.w && x + w > clip.x && y < clip.y + clip.h && y + h > clip.y;
}

void CharacterUI::patch(size_t offset, int x, int y, int w, int h, int dx, int dy) {
  const uint16_t* source = pixels + offset / 2;
  // Feather only the outer edge of the authored facial patch.
  if (!intersects(x + dx, y + dy, w, h)) return;
  const int y0 = max(0, clip.y - y - dy), y1 = min(h, clip.y + clip.h - y - dy);
  const int x0 = max(0, clip.x - x - dx), x1 = min(w, clip.x + clip.w - x - dx);
  for (int py = y0; py < y1; ++py) {
    for (int px = x0; px < x1; ++px) {
      const int edge = min(min(px, w - 1 - px), min(py, h - 1 - py));
      const int tx = x + px + dx, ty = y + py + dy;
      const uint16_t color = source[py * w + px];
      auto& destination = framebuffer[ty * 320 + tx];
      destination = edge >= 3 ? color : __builtin_bswap16(blend(__builtin_bswap16(destination), __builtin_bswap16(color), edge * 85));
    }
  }
}

void CharacterUI::glass(int x, int y, int w, int h, int radius, uint8_t alpha) {
  if (!intersects(x, y, w, h)) return;
  const int y0 = max(0, clip.y - y), y1 = min(h, clip.y + clip.h - y);
  for (int py = y0; py < y1; ++py) {
    const int cy = py < radius ? radius - py : (py >= h - radius ? py - (h - radius - 1) : 0);
    const int inset = cy ? radius - static_cast<int>(sqrtf(radius * radius - cy * cy)) : 0;
    for (int px = max(inset, clip.x - x); px < min(w - inset, clip.x + clip.w - x); ++px) {
      const int tx = x + px, ty = y + py;
      auto& destination = framebuffer[ty * 320 + tx];
      destination = __builtin_bswap16(blend(__builtin_bswap16(destination), Plum, alpha));
    }
  }
}

void CharacterUI::micIcon(int x, int y, uint16_t color, bool muted) {
  canvas.fillRoundRect(x - 3, y - 8, 7, 12, 3, color);
  canvas.drawArc(x, y + 1, 7, 6, 0, 180, color);
  canvas.drawWideLine(x, y + 7, x, y + 10, 0.8f, color);
  canvas.drawWideLine(x - 4, y + 10, x + 4, y + 10, 0.8f, color);
  if (muted) canvas.drawWideLine(x - 9, y - 9, x + 9, y + 9, 1.2f, Pink);
}

void CharacterUI::phoneIcon(int x, int y, uint16_t color, bool hangup) {
  if (hangup) {
    canvas.drawArc(x, y + 7, 13, 9, 218, 322, color);
    canvas.fillRoundRect(x - 13, y - 2, 6, 8, 2, color);
    canvas.fillRoundRect(x + 7, y - 2, 6, 8, 2, color);
  } else {
    canvas.drawArc(x + 7, y - 7, 16, 12, 75, 165, color);
    canvas.fillRoundRect(x - 9, y - 11, 6, 9, 2, color);
    canvas.fillRoundRect(x + 3, y + 3, 9, 6, 2, color);
  }
}

void CharacterUI::closeIcon(int x, int y, uint16_t color) {
  canvas.drawWideLine(x - 5, y - 5, x + 5, y + 5, 1.0f, color);
  canvas.drawWideLine(x - 5, y + 5, x + 5, y - 5, 1.0f, color);
}

void CharacterUI::retryIcon(int x, int y, uint16_t color) {
  canvas.drawArc(x, y, 9, 7, 40, 320, color);
  canvas.fillTriangle(x + 8, y - 10, x + 10, y, x, y - 3, color);
}

void CharacterUI::launcherIcon(int x, int y) {
  canvas.fillCircle(x, y, 20, 0x4A4D);
  for (int row = 0; row < 2; ++row)
    for (int col = 0; col < 2; ++col)
      canvas.fillRoundRect(x - 9 + col * 11, y - 9 + row * 11, 7, 7, 2, White);
}

void CharacterUI::statusIcon(State state, bool muted, uint32_t now) {
  const int x = 23, y = 23;
  glass(7, 7, 32, 32, 16, 211);
  switch (state) {
    case State::Noticed:
      canvas.drawWideLine(x - 8, y, x + 8, y, 2, Mint);
      canvas.drawWideLine(x, y - 8, x, y + 8, 2, Mint);
      canvas.fillCircle(x + 7, y - 7, 2, White);
      break;
    case State::Ready:
      canvas.drawArc(x, y, 8, 6, 35, 325, White);
      canvas.fillCircle(x + 4, y - 5, 2, Mint);
      break;
    case State::Live:
      if (muted) micIcon(x, y - 1, White, true);
      else if (mouth) {
        for (int i = -1; i <= 1; ++i) {
          int height = 4 + static_cast<int>((0.5f + 0.5f * sinf(now * .016f + i * 2)) * 11);
          canvas.fillRoundRect(x + i * 6 - 1, y - height / 2, 3, height, 1, Mint);
        }
      } else if (searching) {
        canvas.drawCircle(x - 2, y - 2, 6, Amber);
        canvas.drawWideLine(x + 3, y + 3, x + 8, y + 8, 2, Amber);
      } else micIcon(x, y - 1, Mint, false);
      break;
    case State::Clock:
      canvas.drawCircle(x, y, 9, White);
      canvas.drawWideLine(x, y, x, y - 5, .8f, White);
      canvas.drawWideLine(x, y, x + 4, y + 2, .8f, White);
      break;
    case State::Wifi:
    case State::Connecting:
      canvas.drawArc(x, y, 10, 8, (now / 5) % 360, (now / 5) % 360 + 240, Amber);
      canvas.fillCircle(x, y, 2, White);
      break;
    case State::Closing:
      phoneIcon(x, y, White, true);
      break;
    case State::Error:
      canvas.drawTriangle(x, y - 10, x - 10, y + 8, x + 10, y + 8, Pink);
      canvas.fillRoundRect(x - 1, y - 4, 3, 7, 1, Pink);
      canvas.fillCircle(x, y + 5, 1, Pink);
      break;
    case State::Setup:
      canvas.drawRoundRect(x - 8, y - 5, 16, 11, 3, White);
      canvas.drawWideLine(x - 4, y - 5, x - 4, y - 9, .8f, White);
      canvas.drawWideLine(x + 4, y - 5, x + 4, y - 9, .8f, White);
      canvas.drawWideLine(x, y + 6, x, y + 11, .8f, White);
      break;
  }
}

void CharacterUI::compose(Rect area, State state, bool connected, int bars, bool muted,
                          bool closedEyes, bool showControls, int dx, int dy, uint32_t now) {
  clip = area;
  canvas.setClipRect(area.x, area.y, area.w, area.h);
  // Restore only this dirty rectangle from the fixed portrait.
  for (int y = area.y; y < area.y + area.h; ++y) {
    memcpy(framebuffer + y * 320 + area.x, pixels + (y - dy) * 328 + area.x - dx, area.w * 2);
  }
  if (mouth == 1) patch(162688, 140, 162, 48, 36, dx, dy);
  else if (mouth == 2) patch(166144, 140, 162, 48, 36, dx, dy);
  if (closedEyes) {
    patch(169600, 88, 110, 67, 59, dx, dy);
    patch(177506, 175, 107, 76, 62, dx, dy);
  }
  if (intersects(7, 7, 32, 32)) statusIcon(state, muted, now);
  if (intersects(255, 8, 57, 27)) {
    glass(255, 8, 57, 27, 13, 210);
    const uint16_t signal = connected ? Mint : Pink;
    for (int i = 0; i < 3; ++i) canvas.fillRoundRect(263 + i * 4, 24 - i * 3, 2, 3 + i * 3, 1, i < bars ? signal : 0x7B4E);
    if (!connected) canvas.drawLine(262, 25, 274, 14, Pink);
    canvas.setTextDatum(TL_DATUM);
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(White);
    canvas.drawString("WiFi", 280, 18);
  }
  if (showControls && canChooseVoice(state) && intersects(100, 8, 120, 32)) {
    glass(100, 8, 120, 32, 16, 234);
    canvas.setFont(&fonts::Font2);
    canvas.setTextDatum(MC_DATUM);
    canvas.setTextColor(White);
    canvas.drawString("VOICE", 160, 24);
  }
  if (showControls && intersects(68, 184, 184, 52)) {
    const bool active = state == State::Live;
    const bool canCall = state == State::Ready || state == State::Error;
    glass(68, 184, 184, 52, 25, 234);
    if (active) {
      canvas.fillCircle(104, 210, 20, muted ? 0x626E : 0x4A4D);
      micIcon(104, 209, White, muted);
      canvas.fillCircle(160, 210, 21, 0xE28F);
      phoneIcon(160, 209, White, true);
      closeIcon(216, 210, White);
    } else if (canCall) {
      const int callX = launcherAvailable ? 160 : 132;
      if (launcherAvailable) launcherIcon(104, 210);
      canvas.fillCircle(callX, 210, 21, state == State::Ready ? 0x2D12 : 0x7B0F);
      if (state == State::Ready) phoneIcon(callX, 209, White, false);
      else retryIcon(callX, 210, White);
      closeIcon(launcherAvailable ? 216 : 196, 210, White);
    } else if (launcherAvailable && (state == State::Setup || state == State::Wifi || state == State::Clock)) {
      launcherIcon(132, 210);
      closeIcon(196, 210, White);
    } else closeIcon(160, 210, White);
  }
  if (voicePicker.visible) drawVoicePicker();
  canvas.clearClipRect();
  M5.Display.setClipRect(area.x, area.y, area.w, area.h);
  canvas.pushSprite(0, 0);
  M5.Display.clearClipRect();
  pixelsSent += area.w * area.h;
}

void CharacterUI::drawVoicePicker() {
  const auto& voice = SpeechOptions::Voices[voicePicker.selection.voice];
  canvas.fillRoundRect(12, 12, 296, 220, 16, Plum);
  canvas.drawRoundRect(12, 12, 296, 220, 16, 0x6B50);
  canvas.setTextDatum(MC_DATUM);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(White);
  char heading[32];
  snprintf(heading, sizeof(heading), "VOICE  %u / %u", static_cast<unsigned>(voicePicker.selection.voice + 1),
           static_cast<unsigned>(SpeechOptions::VoiceCount));
  canvas.drawString(heading, 160, 33);
  canvas.fillRoundRect(24, 54, 50, 50, 12, 0x4A4D);
  canvas.fillRoundRect(246, 54, 50, 50, 12, 0x4A4D);
  canvas.fillTriangle(42, 79, 54, 70, 54, 88, White);
  canvas.fillTriangle(278, 79, 266, 70, 266, 88, White);
  canvas.setFont(&fonts::Font4);
  canvas.drawString(voice.name, 160, 78);
  canvas.setFont(&fonts::Font0);
  canvas.drawString(voice.region, 160, 113);
  canvas.drawString(voice.presentation, 160, 125);
  canvas.fillRoundRect(64, 134, 192, 36, 10, 0x4A4D);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(Mint);
  canvas.drawString(String("Style: ") + SpeechOptions::Styles[voicePicker.selection.style].name, 160, 152);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(voicePicker.saveFailed ? Pink : White);
  canvas.drawString(voicePicker.saveFailed ? "Save failed. Try again." : "Applies to the next conversation", 160, 177);
  canvas.fillRoundRect(24, 186, 126, 40, 12, 0x4A4D);
  canvas.fillRoundRect(170, 186, 126, 40, 12, 0x2D12);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(White);
  canvas.drawString("Cancel", 87, 206);
  canvas.drawString("Save", 233, 206);
}

void CharacterUI::draw(State state, bool connected, int rssi, bool muted, uint16_t playbackPeak,
                       uint32_t now, int mouthOverride, int blinkOverride) {
  const int64_t started = esp_timer_get_time();
  envelope = max(static_cast<float>(playbackPeak), envelope * .48f);
  const bool audioState = state == State::Live || state == State::Connecting || state == State::Noticed;
  if (!audioState) envelope = 0;
  const uint8_t target = envelope < 190 ? 0 : (envelope < 1700 ? 1 : 2);
  if (target != mouth && (target == 0 || now - mouthSince >= 75)) { mouth = target; mouthSince = now; }
  if (mouthOverride >= 0) mouth = min(2, mouthOverride);
  if (!blinking && static_cast<int32_t>(now - nextBlink) >= 0) {
    blinking = true; blinkSince = now; ++blinkCount;
  }
  if (blinking && now - blinkSince > 135) {
    blinking = false; nextBlink = now + 2800 + esp_random() % 3300;
  }
  const bool closedEyes = blinkOverride >= 0 ? blinkOverride != 0 : (blinking || state == State::Noticed);
  // During a call, keep the portrait stationary so audio never competes with a full-screen sway update.
  const int dx = voicePicker.visible ? -4 : audioState && cached ? lastDx : -4 + lroundf(1.3f * sinf(now * .00065f));
  const int dy = voicePicker.visible ? -4 : audioState && cached ? lastDy : -4 + lroundf(1.4f * sinf(now * .0013f));
  const bool showControls = controlsVisible(now);
  if (!showControls) controls = false;
  const int bars = !connected ? 0 : rssi > -55 ? 3 : rssi > -72 ? 2 : 1;
  const uint32_t iconTick = now / 100;
  const bool iconAnimated = state == State::Connecting || state == State::Wifi || (state == State::Live && mouth && !muted);
  Rect dirty[8];
  int count = 0;
  if (!cached || dx != lastDx || dy != lastDy) dirty[count++] = {0, 0, 320, 240};
  else if (!voicePicker.visible) {
    if (mouth != lastMouth) dirty[count++] = {140 + dx, 162 + dy, 48, 36};
    if (closedEyes != lastClosed) {
      dirty[count++] = {88 + dx, 110 + dy, 67, 59};
      dirty[count++] = {175 + dx, 107 + dy, 76, 62};
    }
    if (state != lastState || muted != lastMuted || searching != lastSearching || bool(mouth) != bool(lastMouth) || (iconAnimated && iconTick != lastIconTick))
      dirty[count++] = {7, 7, 32, 32};
    if (connected != lastConnected || bars != lastBars) dirty[count++] = {255, 8, 57, 27};
    if (showControls != lastControls || (showControls && (state != lastState || muted != lastMuted)))
      dirty[count++] = {68, 184, 184, 52};
    if (showControls != lastControls || (showControls && canChooseVoice(state) != canChooseVoice(lastState)))
      dirty[count++] = {100, 8, 120, 32};
  }
  cached = true;
  lastDx = dx; lastDy = dy; lastMouth = mouth; lastClosed = closedEyes;
  lastState = state; lastMuted = muted; lastConnected = connected; lastBars = bars;
  lastControls = showControls; lastIconTick = iconTick;
  lastSearching = searching;
  // An unchanged expression does no framebuffer work and sends nothing to the LCD.
  if (!count) return;
  for (int i = 0; i < count; ++i) compose(dirty[i], state, connected, bars, muted, closedEyes, showControls, dx, dy, now);
  ++frameCount;
  if (mouth) ++mouthFrames;
  maxDrawUs = max(maxDrawUs, static_cast<uint32_t>(esp_timer_get_time() - started));
}

CharacterUI::Action CharacterUI::tap(int x, int y, State state, uint32_t now) {
  if (voicePicker.visible) {
    cached = false;
    return voicePicker.tap(x, y) == SpeechOptions::Picker::Action::Save ? Action::SaveVoice : Action::None;
  }
  if (!controlsVisible(now)) { controls = true; controlSince = now; return Action::None; }
  controlSince = now;
  if (canChooseVoice(state) && x >= 100 && x < 220 && y >= 8 && y < 40) return Action::ChooseVoice;
  if (y < 184 || y > 238 || x < 68 || x > 252) { controls = false; return Action::None; }
  if (state == State::Live) {
    if (x >= 80 && x <= 128) return Action::ToggleMute;
    if (x >= 136 && x <= 184) { controls = false; return Action::End; }
  } else if (state == State::Ready || state == State::Error) {
    if (launcherAvailable && x >= 80 && x <= 128) { controls = false; return Action::Launcher; }
    const int callX = launcherAvailable ? 160 : 132;
    if (x >= callX - 26 && x <= callX + 26) {
      controls = false;
      return state == State::Ready ? Action::Start : Action::Retry;
    }
  } else if (launcherAvailable && (state == State::Setup || state == State::Wifi || state == State::Clock) &&
             x >= 106 && x <= 158) {
    controls = false; return Action::Launcher;
  } else if ((state == State::Connecting || state == State::Noticed) && x >= 136 && x <= 184) {
    controls = false; return Action::End;
  }
  controls = false;
  return Action::None;
}
