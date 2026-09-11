#pragma once
#include <array>
#include <cstdint>
#include <cstring>

// Bounded, allocation-free lifecycle tracking. Expiry stops holding the idle timer;
// it does not claim to cancel a managed backend request.
class DelegationTracker {
 public:
  bool start(const char* id, uint32_t now) {
    if (!id || !*id || std::strlen(id) >= 96) return false;
    for (const auto& item : items) if (!std::strcmp(item.id, id)) return true;
    for (auto& item : items) if (!*item.id) {
      std::strcpy(item.id, id); item.since = now; item.expired = false; ++started; return true;
    }
    return false;
  }
  bool finish(const char* id, uint32_t now) {
    if (!id || !*id) return false;
    for (auto& item : items) if (!std::strcmp(item.id, id)) {
      lastDuration = now - item.since; item.id[0] = 0; ++finished; return true;
    }
    return false;
  }
  bool expire(uint32_t now, uint32_t timeout) {
    bool changed = false;
    for (auto& item : items) if (*item.id && !item.expired && now - item.since >= timeout) {
      item.expired = true; changed = true; ++timeouts;
    }
    return changed;
  }
  bool waiting() const {
    for (const auto& item : items) if (*item.id && !item.expired) return true;
    return false;
  }
  size_t active() const {
    size_t count = 0;
    for (const auto& item : items) if (*item.id) ++count;
    return count;
  }
  void reset() { items = {}; started = finished = timeouts = lastDuration = 0; }
  uint32_t started = 0, finished = 0, timeouts = 0, lastDuration = 0;
 private:
  struct Item { char id[96]{}; uint32_t since = 0; bool expired = false; };
  std::array<Item, 8> items{};
};
