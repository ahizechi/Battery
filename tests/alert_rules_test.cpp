// Standalone portable policy tests; no real wireless device required.
#include "../src/alert_rules.h"

#include <cassert>
#include <cstdio>
#include <cwchar>
#include <map>

int main() {
    using namespace alerts;
    std::map<std::wstring, int> prefs;
    const std::wstring mouse = L"mouse:key";
    const std::wstring headset = L"headset:key";
    assert(EffectiveThreshold(prefs, mouse) == 15);
    assert(OverrideFor(prefs, mouse) == kUseDefault);
    prefs[mouse] = 20;
    prefs[headset] = kDisabled;
    assert(EffectiveThreshold(prefs, mouse) == 20);
    assert(EffectiveThreshold(prefs, headset) == kDisabled);
    assert(EffectiveThreshold(prefs, L"other:key") == 15);
    assert(ValidOverride(kDisabled));
    for (int n : kThresholds) assert(ValidOverride(n));
    assert(!ValidOverride(0) && !ValidOverride(51) && !ValidOverride(kUseDefault));

    DeviceStatus s;
    s.key = mouse;
    s.percent = 21;
    bool warned = false;
    assert(!ShouldNotify(s, 20, warned));
    s.percent = 20;
    assert(ShouldNotify(s, 20, warned));
    assert(warned);
    assert(!ShouldNotify(s, 20, warned));  // same reading must not notify again
    s.percent = 25;
    assert(!ShouldNotify(s, 20, warned));  // hysteresis prevents flapping
    s.percent = -1;
    assert(!ShouldNotify(s, 20, warned));
    assert(warned);                        // unknown reading preserves latch
    s.percent = 10;
    assert(!ShouldNotify(s, 20, warned));  // returning from unknown is not a new alert
    s.asleep = true;
    assert(!ShouldNotify(s, 20, warned));
    s.asleep = false;
    s.connected = false;
    assert(!ShouldNotify(s, 20, warned));
    s.connected = true;
    assert(!ShouldNotify(s, 20, warned));
    s.percent = 31;
    assert(!ShouldNotify(s, 20, warned));  // armed again only > threshold + 10
    assert(!warned);
    s.percent = 19;
    assert(ShouldNotify(s, 20, warned));

    s.charging = true;
    assert(!ShouldNotify(s, 20, warned));
    assert(!warned);
    s.charging = false;
    assert(ShouldNotify(s, 20, warned));

    assert(!ShouldNotify(s, kDisabled, warned));
    assert(!warned);
    assert(ShouldNotify(s, 20, warned));   // re-enabled: new low battery alert
    s.percent = 105;
    warned = false;
    assert(!ShouldNotify(s, 20, warned));  // invalid data never alerts
    s.percent = 0;
    assert(ShouldNotify(s, 20, warned));   // 0% is a valid critical reading
    DeviceStatus headphones;
    headphones.key = headset;
    headphones.percent = 9;
    bool secondWarned = false;
    assert(ShouldNotify(headphones, 15, secondWarned));
    assert(!ShouldNotify(headphones, 15, secondWarned));
    assert(warned);                        // the mouse and headphones have independent latches

    // Issue #5: the red "low" colour follows the device's alert threshold, not a fixed 15%.
    std::map<std::wstring, int> lows;
    lows[L"at30"] = 30;
    lows[L"at5"] = 5;
    lows[L"off"] = kDisabled;
    assert(LowThreshold(lows, L"at30") == 30);
    assert(LowThreshold(lows, L"at5") == 5);
    assert(LowThreshold(lows, L"unset") == kDefaultThreshold);
    assert(LowThreshold(lows, L"off") == kDefaultThreshold);  // alerts off still shows low at the default

    // Issue #6: past the cap, the latch seen longest ago goes, not the alphabetically first one.
    std::map<std::wstring, Latch> latches;
    latches[L"a:first-by-name"] = { true, 1000 };  // warned, recently seen: must survive
    for (size_t i = 0; i < kMaxLatches - 1; i++) {
        wchar_t key[16];
        std::swprintf(key, 16, L"k%04zu", i);
        latches[key] = { false, 2000 + i };
    }
    latches[L"z:oldest"] = { false, 10 };
    assert(latches.size() == kMaxLatches + 1);
    TrimLatches(latches);
    assert(latches.size() == kMaxLatches);
    assert(latches.count(L"a:first-by-name") && latches[L"a:first-by-name"].warned);
    assert(!latches.count(L"z:oldest"));

    std::puts("Smart Battery Alerts policy tests passed");
}
