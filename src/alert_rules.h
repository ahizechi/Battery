#pragma once
// Pure, portable policy for per-device low-battery notifications.
// No HID access, Windows dependencies, timers or disk/network writes.
#include "devices.h"

#include <cstddef>
#include <map>
#include <string>

namespace alerts {
constexpr int kDefaultThreshold = 15;
constexpr int kUseDefault = -2;   // no per-device override persisted
constexpr int kDisabled = -1;     // notifications disabled for this device
constexpr int kThresholds[] = {5, 10, 15, 20, 25, 30, 35, 40};
constexpr int kThresholdCount = sizeof(kThresholds) / sizeof(kThresholds[0]);

inline bool ValidOverride(int value) {
    if (value == kDisabled) return true;
    for (int threshold : kThresholds)
        if (value == threshold) return true;
    return false;
}

inline int OverrideFor(const std::map<std::wstring, int>& overrides, const std::wstring& key) {
    const auto it = overrides.find(key);
    return it == overrides.end() ? kUseDefault : it->second;
}

inline int EffectiveThreshold(const std::map<std::wstring, int>& overrides, const std::wstring& key) {
    int value = OverrideFor(overrides, key);
    return value == kUseDefault ? kDefaultThreshold : value;
}

// The level at which a device shows as low (red bar on the strip, red tray icon): its alert threshold, so
// the colour and the notification agree. With alerts off for the device, the default still marks it low.
inline int LowThreshold(const std::map<std::wstring, int>& overrides, const std::wstring& key) {
    int value = EffectiveThreshold(overrides, key);
    return value == kDisabled ? kDefaultThreshold : value;
}

// Whether a device already showed its low-battery notification, kept by device key so a device that briefly
// drops out does not alert again. `seen` is when it last reported, for evicting the oldest.
struct Latch {
    bool warned = false;
    unsigned long long seen = 0;
};
constexpr size_t kMaxLatches = 256;

// Keeps at most kMaxLatches entries by dropping the ones seen longest ago.
inline void TrimLatches(std::map<std::wstring, Latch>& latches) {
    while (latches.size() > kMaxLatches) {
        auto oldest = latches.begin();
        for (auto it = latches.begin(); it != latches.end(); ++it)
            if (it->second.seen < oldest->second.seen) oldest = it;
        latches.erase(oldest);
    }
}

// This is invoked only for a fresh poll result. Unknown, sleeping and disconnected
// readings don't reset the latch: a transient missing report must not spam alerts.
inline bool ShouldNotify(const DeviceStatus& s, int threshold, bool& warned) {
    if (threshold == kDisabled) { warned = false; return false; }
    if (!s.connected || s.asleep || s.percent < 0 || s.percent > 100) return false;
    if (s.charging) { warned = false; return false; }
    if (s.percent > threshold + 10) { warned = false; return false; }
    if (s.percent <= threshold && !warned) {
        warned = true;
        return true;
    }
    return false;
}
}  // namespace alerts
