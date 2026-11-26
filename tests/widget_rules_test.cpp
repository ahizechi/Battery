// The Widgets (weather) button search: only when the button can sit next to the notification area.
#include "../src/widget_rules.h"

#include <cassert>
#include <cstdio>

int main() {
    using widget_rules::kUnset;
    using widget_rules::WidgetsButtonNearTray;

    // Left-aligned taskbar (TaskbarAl = 0) with Widgets on (TaskbarDa = 1, or never changed): next to the tray.
    assert(WidgetsButtonNearTray(0, 1));
    assert(WidgetsButtonNearTray(0, kUnset));
    // Widgets switched off: there is no button.
    assert(!WidgetsButtonNearTray(0, 0));
    // Centred taskbar (Windows 11's default, also when the value was never changed): the button is at the far left.
    assert(!WidgetsButtonNearTray(1, 1));
    assert(!WidgetsButtonNearTray(kUnset, 1));
    assert(!WidgetsButtonNearTray(kUnset, kUnset));

    // After a search that found nothing, wait longer each time (5 s, 10 s, 20 s ...), at most 10 minutes.
    assert(widget_rules::NextRetryMs(0) == 5000);
    assert(widget_rules::NextRetryMs(5000) == 10000);
    assert(widget_rules::NextRetryMs(320000) == 600000);
    assert(widget_rules::NextRetryMs(600000) == 600000);

    std::puts("widget_rules_test: ok");
    return 0;
}
