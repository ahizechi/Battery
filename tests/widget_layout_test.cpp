// Geometry of the taskbar strip (src/widget.cpp Layout): where every icon, number and level bar lands.
// Renders with the real fonts on a hidden window at 96 DPI; nothing is shown.
#include "../src/widget.cpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>

namespace {

WidgetItem Battery(DeviceKind kind, int percent) {
    WidgetItem w;
    w.kind = kind;
    w.percent = percent;
    return w;
}

WidgetItem Reading(const wchar_t* tag, const wchar_t* text) {
    WidgetItem w;
    w.tag = tag;
    w.text = text;
    return w;
}

bool Same(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
int Bottom(const Piece& p) { return p.y + p.m.h; }
int Baseline(const Piece& p) { return p.y + p.m.base; }
int Right(const Piece& p) { return p.x + std::max(p.m.w, p.barW); }
// Twice the vertical centre, so odd heights compare exactly.
int Centre2(int top, int bottom) { return top + bottom; }

std::vector<const Piece*> Find(const std::vector<Piece>& pieces, Role role) {
    std::vector<const Piece*> out;
    for (const auto& p : pieces)
        if (p.role == role) out.push_back(&p);
    return out;
}

void CheckInside(const std::vector<Piece>& pieces, int W, int H) {
    for (const auto& p : pieces) {
        assert(p.x >= 0 && p.y >= 0 && p.x + p.m.w <= W && Bottom(p) <= H);
        if (p.barW) assert(p.barX >= 0 && p.barX + p.barW <= W && p.barY + p.barH <= H);
    }
}

}  // namespace

int main() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = inst;
    wc.lpszClassName = L"WidgetLayoutTestBar";
    RegisterClassW(&wc);
    g_taskbar = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP, 0, 0, 1200, 48, nullptr, nullptr, inst, nullptr);
    assert(g_taskbar && Dpi() == 96);
    g_light = false;
    const Rgb fg{ 255, 255, 255 };

    // Zek's screenshot: a charging headset at 84% and a mouse at 39%, plus a full keyboard and a low pad.
    WidgetItem headset = Battery(DeviceKind::Headset, 84);
    headset.charging = true;
    WidgetItem mouse = Battery(DeviceKind::Mouse, 39);
    WidgetItem keyboard = Battery(DeviceKind::Keyboard, 100);
    keyboard.full = true;
    WidgetItem pad = Battery(DeviceKind::Gamepad, 5);
    pad.low = true;
    g_items = { headset, mouse, keyboard, pad };

    int W = 0, H = 0;
    auto pieces = Layout(1200, 48, W, H);
    assert(H == 48 && W > 0);
    auto icons = Find(pieces, Role::Icon);
    auto numbers = Find(pieces, Role::Number);
    assert(icons.size() == 4 && numbers.size() == 4);
    CheckInside(pieces, W, H);

    // Every bar has one width, fits the widest number ("100%"), starts under its number and keeps clear of it.
    const int bw = numbers[0]->barW;
    for (auto* n : numbers) {
        assert(n->barW == bw && n->barW >= n->m.w && n->barX == n->x);
        assert(n->barH >= 2 && n->barY >= Bottom(*n) + 2);
    }
    // The bar width does not depend on the levels shown, so the strip keeps its width when a device reaches 100%.
    g_items = { mouse };
    assert(Find(Layout(1200, 48, W, H), Role::Number)[0]->barW == bw);
    g_items = { headset, mouse, keyboard, pad };
    pieces = Layout(1200, 48, W, H);
    icons = Find(pieces, Role::Icon);
    numbers = Find(pieces, Role::Number);
    // The icon and the number are both centred on the taskbar's midline (to the pixel); the bar hangs below.
    for (size_t i = 0; i < icons.size(); i++) {
        assert(std::abs(Centre2(icons[i]->y, Bottom(*icons[i])) - H) <= 1);
        assert(std::abs(Centre2(numbers[i]->y, Baseline(*numbers[i])) - H) <= 1);
        assert(Baseline(*numbers[i]) == Baseline(*numbers[0]));  // one baseline
    }
    // Left to right: icon, then its number with room between, then a wider gap before the next device.
    for (size_t i = 0; i < icons.size(); i++) {
        assert(numbers[i]->x >= Right(*icons[i]) + 4);
        if (i + 1 < icons.size()) assert(icons[i + 1]->x >= Right(*numbers[i]) + 12);
    }
    assert(icons[0]->x >= 6 && W - Right(*numbers.back()) >= 6);

    // The bar: filled to the level, green while charging or full, red when low, otherwise the text colour.
    assert(numbers[0]->fill == (84 * bw + 50) / 100 && Same(numbers[0]->barColor, Rgb{ 108, 203, 95 }));
    assert(numbers[1]->fill == (39 * bw + 50) / 100 && Same(numbers[1]->barColor, fg));
    assert(numbers[2]->fill == bw && Same(numbers[2]->barColor, Rgb{ 108, 203, 95 }));
    assert(numbers[3]->fill >= numbers[3]->barH && Same(numbers[3]->barColor, Rgb{ 255, 90, 90 }));  // 5%: still a dot
    // ... and the number itself stays in the text colour, also when low.
    for (auto* n : numbers) assert(Same(n->color, fg));
    g_showCharging = false;
    assert(Same(Find(Layout(1200, 48, W, H), Role::Number)[0]->barColor, fg));
    g_showCharging = true;

    // 0% shows an empty track; an unknown level shows "?" without a bar.
    g_items = { Battery(DeviceKind::Mouse, 0), Battery(DeviceKind::Mouse, -1) };
    numbers = Find(pieces = Layout(1200, 48, W, H), Role::Number);
    assert(numbers.size() == 2 && numbers[0]->barW > 0 && numbers[0]->fill == 0 && numbers[1]->barW == 0);

    // Hardware readings: tag and number sit on the batteries' baseline and have no bar.
    g_items = { headset, Reading(L"CPU", L"42%"), Reading(L"NET", L"1.2M") };
    pieces = Layout(1200, 48, W, H);
    auto tags = Find(pieces, Role::Tag);
    numbers = Find(pieces, Role::Number);
    assert(tags.size() == 2 && numbers.size() == 3);
    for (auto* t : tags) assert(Baseline(*t) == Baseline(*numbers[0]));
    for (size_t i = 1; i < numbers.size(); i++) assert(Baseline(*numbers[i]) == Baseline(*numbers[0]) && numbers[i]->barW == 0);
    CheckInside(pieces, W, H);
    // Readings alone are centred on the midline.
    g_items = { Reading(L"CPU", L"42%") };
    numbers = Find(pieces = Layout(1200, 48, W, H), Role::Number);
    assert(std::abs(Centre2(numbers[0]->y, Baseline(*numbers[0])) - H) <= 1);

    // Stacked (icon above the number) on a tall taskbar: all inside, the bar under the number.
    g_stacked = true;
    g_items = { headset, mouse };
    pieces = Layout(1200, 72, W, H);
    icons = Find(pieces, Role::Icon);
    numbers = Find(pieces, Role::Number);
    assert(icons.size() == 2 && numbers.size() == 2);
    CheckInside(pieces, W, H);
    for (size_t i = 0; i < 2; i++) {
        assert(Bottom(*icons[i]) <= numbers[i]->y && numbers[i]->barY >= Bottom(*numbers[i]));
        assert(numbers[i]->barW == bw);
    }
    g_stacked = false;

    // Vertical taskbar: one block per device, top to bottom, inside the bar's width.
    g_vertical = true;
    pieces = Layout(64, 800, W, H);
    icons = Find(pieces, Role::Icon);
    numbers = Find(pieces, Role::Number);
    assert(W == 64 && icons.size() == 2 && numbers.size() == 2);
    CheckInside(pieces, W, H);
    assert(icons[1]->y > numbers[0]->barY + numbers[0]->barH);
    g_vertical = false;

    std::puts("widget_layout_test: ok");
    return 0;
}
