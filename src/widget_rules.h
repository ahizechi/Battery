// Pure rules for the taskbar strip, kept apart from widget.cpp so they can be tested without a taskbar.
#pragma once

#include <algorithm>

namespace widget_rules {

// A registry value that was never written.
constexpr int kUnset = -1;

// Whether the Widgets (weather) button can sit next to the notification area, where it would overlap the strip.
// al = TaskbarAl (0 = left-aligned; 1 or unset = centred, which puts the button at the far left),
// da = TaskbarDa (0 = Widgets off; 1 or unset = on).
inline bool WidgetsButtonNearTray(int al, int da) { return al == 0 && da != 0; }

// How long to wait before searching again after a search that found no button: 5 s, then twice as long each time,
// at most 10 minutes. A taskbar setting change or Explorer restart resets the wait.
inline int NextRetryMs(int prev) { return prev <= 0 ? 5000 : std::min(prev * 2, 600000); }

}  // namespace widget_rules
