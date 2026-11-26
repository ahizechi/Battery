#include "widget.h"

#include "i18n.h"
#include "widget_rules.h"

#include <commctrl.h>
#include <uiautomation.h>
#include <algorithm>
#include <climits>
#include <cmath>

namespace {

HWND g_wnd, g_taskbar, g_tip, g_notify, g_rebar;
UINT g_clickMsg;
bool g_light, g_hover;
bool g_background;  // opaque rounded plate behind the text, for transparent / restyled taskbars
bool g_stacked;     // icon above the percentage (menu option, PR #525)
bool g_showCharging = true;  // green level bar while charging / full on power (menu option, Zek's fork)
bool g_vertical;    // the primary taskbar is docked left or right: one block per device, stacked (PR #321)
std::vector<WidgetItem> g_items;
std::wstring g_tipText;
RECT g_lastTray{}, g_lastBar{}, g_lastWidgets{};
int g_lastW = -1, g_lastDpi = 0, g_lastLeft = -1;
SIZE g_size{};
HINSTANCE g_inst;

// Copies of the strip on secondary-monitor taskbars (Shell_SecondaryTrayWnd), so the levels stay visible
// while a game runs fullscreen on the main screen. Same drawing, same click and tooltip; each one sits
// left of its taskbar's clock. The primary strip above is unchanged, and with one monitor none exist.
struct Mirror {
    HWND bar = nullptr, wnd = nullptr;
    bool hover = false;
    SIZE size{};
    int dpi = 0;
    int lastLeft = INT_MIN;  // client x it was last placed at; INT_MIN = not placed (hidden)
};
std::vector<Mirror> g_mirrors;
// Which taskbars show the strip: "all", "main", or one monitor's id (MonitorId), e.g. only monitor 3.
// With one monitor chosen, the primary strip is hidden; if that monitor is not connected, the primary
// strip is shown instead so the levels never disappear.
std::wstring g_choice = L"all";
bool g_primaryHidden = false;
HWND g_dpiBar;  // while a mirror is painted or placed, Px() uses its taskbar's DPI

const wchar_t* kClass = L"WirelessBatteryWidget";

int Dpi() {
    HWND bar = g_dpiBar ? g_dpiBar : g_taskbar;
    UINT d = bar ? GetDpiForWindow(bar) : 0;
    return d ? (int)d : 96;
}
int Px(int v) { return MulDiv(v, Dpi(), 96); }

// Windows 11 puts the Widgets (weather) button right next to the tray when the taskbar is
// left-aligned (start-aligned on right-to-left Windows). It is XAML, so only UI Automation knows
// where it is. The element is looked up once per taskbar; its rectangle is re-read every few
// seconds because the weather text changes its width.
//
// UI Automation calls go to Explorer and wait for its UI thread, while our widget is a child window
// of the taskbar, so Explorer in turn waits for our UI thread whenever it lays out or sends input to
// the widget. Making those calls from our UI thread deadlocks both: Explorer is reported hung
// ("Cross-process", waiting on BatteryHub.exe) and restarted, again and again. So the lookup
// runs on its own thread, which owns no windows, and the UI thread only reads the last rectangle.
SRWLOCK g_wbLock = SRWLOCK_INIT;
RECT g_wbRect{};             // last Widgets button rectangle (screen), empty when unknown
HWND g_wbRectBar;            // taskbar g_wbRect was found in (guarded by g_wbLock)
HWND g_wbTaskbar;            // taskbar to search, null while there is no widget (guarded by g_wbLock)
HANDLE g_wbWake;             // auto-reset: wakes the lookup thread early

// Where a Windows 11 secondary taskbar's clock, widgets button and task buttons are. Its XAML has no child
// windows for them, so the same lookup thread reads them with UI Automation (the clock has AutomationId
// "SystemTrayIcon", widgets button "WidgetsButton", task buttons class "Taskbar.TaskListButtonAutomationPeer";
// found by PR #107).
struct Span {
    HWND bar = nullptr;
    bool measured = false;   // UI Automation answered for this taskbar
    bool hasClock = false;   // the clock can be turned off on secondary taskbars
    int clockLeft = 0;       // screen x
    bool hasWidgets = false; // Widgets (weather) button on secondary taskbar
    RECT widgets{};          // bounding rectangle of WidgetsButton (screen coords)
    bool hasTasks = false;
    int tasksRight = 0;      // right edge of the last task button left of the clock (screen x)
};
std::vector<Span> g_spans;   // guarded by g_wbLock
bool g_wbMirrors;            // guarded by g_wbLock: measure secondary taskbars too

IUIAutomationCondition* StringCondition(IUIAutomation* uia, PROPERTYID prop, const wchar_t* value) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(value);
    IUIAutomationCondition* c = nullptr;
    if (v.bstrVal) uia->CreatePropertyCondition(prop, v, &c);
    VariantClear(&v);
    return c;
}

Span MeasureSpan(IUIAutomation* uia, HWND bar, bool widgetsNearTray) {
    Span s;
    s.bar = bar;
    RECT br{};
    IUIAutomationElement* root = nullptr;
    if (!GetWindowRect(bar, &br) || FAILED(uia->ElementFromHandle(bar, &root)) || !root) return s;
    s.measured = true;
    auto inBar = [&](const RECT& r) { return r.right > r.left && r.left >= br.left && r.right <= br.right; };
    if (IUIAutomationCondition* clock = StringCondition(uia, UIA_AutomationIdPropertyId, L"SystemTrayIcon")) {
        IUIAutomationElementArray* all = nullptr;
        int n = 0;
        if (SUCCEEDED(root->FindAll(TreeScope_Descendants, clock, &all)) && all && SUCCEEDED(all->get_Length(&n)))
            for (int i = 0; i < n; i++) {  // leftmost of the clock area's buttons
                IUIAutomationElement* el = nullptr;
                RECT r{};
                if (SUCCEEDED(all->GetElement(i, &el)) && el && SUCCEEDED(el->get_CurrentBoundingRectangle(&r)) && inBar(r) &&
                    (!s.hasClock || r.left < s.clockLeft)) {
                    s.clockLeft = r.left;
                    s.hasClock = true;
                }
                if (el) el->Release();
            }
        if (all) all->Release();
        clock->Release();
    }
    IUIAutomationCondition* wbCond = widgetsNearTray ? StringCondition(uia, UIA_AutomationIdPropertyId, L"WidgetsButton") : nullptr;
    if (wbCond) {
        IUIAutomationElement* wbEl = nullptr;
        RECT r{};
        if (SUCCEEDED(root->FindFirst(TreeScope_Descendants, wbCond, &wbEl)) && wbEl &&
            SUCCEEDED(wbEl->get_CurrentBoundingRectangle(&r)) && inBar(r)) {
            s.widgets = r;
            s.hasWidgets = true;
        }
        if (wbEl) wbEl->Release();
        wbCond->Release();
    }
    if (IUIAutomationCondition* task = StringCondition(uia, UIA_ClassNamePropertyId, L"Taskbar.TaskListButtonAutomationPeer")) {
        IUIAutomationElementArray* all = nullptr;
        int n = 0;
        if (SUCCEEDED(root->FindAll(TreeScope_Descendants, task, &all)) && all && SUCCEEDED(all->get_Length(&n)))
            for (int i = 0; i < n; i++) {
                IUIAutomationElement* el = nullptr;
                RECT r{};
                if (SUCCEEDED(all->GetElement(i, &el)) && el && SUCCEEDED(el->get_CurrentBoundingRectangle(&r)) && inBar(r) &&
                    (!s.hasClock || r.right <= s.clockLeft) && (!s.hasWidgets || r.right <= s.widgets.left) &&
                    (!s.hasTasks || r.right > s.tasksRight)) {
                    s.tasksRight = r.right;
                    s.hasTasks = true;
                }
                if (el) el->Release();
            }
        if (all) all->Release();
        task->Release();
    }
    root->Release();
    return s;
}

bool IsXamlTaskbar(HWND bar) {
    return FindWindowExW(bar, nullptr, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr) != nullptr;
}

// Reads one of the taskbar's DWORD settings; widget_rules::kUnset when it was never written.
int TaskbarSetting(HKEY key, const wchar_t* name) {
    DWORD v = 0, size = sizeof(v);
    return key && RegQueryValueExW(key, name, nullptr, nullptr, (BYTE*)&v, &size) == ERROR_SUCCESS ? (int)v
                                                                                                  : widget_rules::kUnset;
}

DWORD WINAPI WidgetsButtonThread(LPVOID) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IUIAutomation* uia = nullptr;  // created only once a search is needed
    IUIAutomationElement* btn = nullptr;
    HWND btnBar = nullptr;
    int backoffMs = 0;           // wait after the last search that found no button
    ULONGLONG nextSearch = 0;    // GetTickCount64() at which to search again
    // The taskbar alignment and Widgets settings: a change wakes the thread and restarts the search at once.
    HKEY advanced = nullptr;
    RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", 0,
                  KEY_QUERY_VALUE | KEY_NOTIFY, &advanced);
    HANDLE changed = advanced ? CreateEventW(nullptr, FALSE, FALSE, nullptr) : nullptr;
    if (changed && RegNotifyChangeKeyValue(advanced, FALSE, REG_NOTIFY_CHANGE_LAST_SET, changed, TRUE) != ERROR_SUCCESS) {
        CloseHandle(changed);
        changed = nullptr;
    }
    DWORD wait = INFINITE;  // no widget yet (tray-icon mode): sleep until one
    for (;;) {
        HANDLE events[2] = { g_wbWake, changed };
        DWORD woke = WaitForMultipleObjects(changed ? 2 : 1, events, FALSE, wait);
        if (woke == WAIT_OBJECT_0 + 1) {
            RegNotifyChangeKeyValue(advanced, FALSE, REG_NOTIFY_CHANGE_LAST_SET, changed, TRUE);
            backoffMs = 0;
            nextSearch = 0;
        }
        AcquireSRWLockShared(&g_wbLock);
        HWND bar = g_wbTaskbar;
        bool mirrors = g_wbMirrors;
        ReleaseSRWLockShared(&g_wbLock);
        if (bar != btnBar) {  // another taskbar (Explorer restarted, monitor change): look again straight away
            if (btn) { btn->Release(); btn = nullptr; }
            backoffMs = 0;
            nextSearch = 0;
        }
        btnBar = bar;
        // Only a left-aligned taskbar with Widgets on puts the button beside the tray; a centred one (Windows 11's
        // default) puts it at the far left, out of the strip's way, so it is never searched for there.
        const bool nearTray = widget_rules::WidgetsButtonNearTray(TaskbarSetting(advanced, L"TaskbarAl"),
                                                              TaskbarSetting(advanced, L"TaskbarDa"));
        if (!nearTray && btn) { btn->Release(); btn = nullptr; }
        RECT r{};
        const ULONGLONG now = GetTickCount64();
        if (bar && IsWindow(bar) && (nearTray || mirrors) && !uia)
            CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), (void**)&uia);
        if (bar && IsWindow(bar) && nearTray && uia) {
            if (!btn && now >= nextSearch) {
                IUIAutomationElement* root = nullptr;
                if (IUIAutomationCondition* cond = StringCondition(uia, UIA_AutomationIdPropertyId, L"WidgetsButton")) {
                    if (SUCCEEDED(uia->ElementFromHandle(bar, &root)) && root) root->FindFirst(TreeScope_Descendants, cond, &btn);
                    cond->Release();
                }
                if (root) root->Release();
                if (!btn) {
                    backoffMs = widget_rules::NextRetryMs(backoffMs);
                    nextSearch = now + backoffMs;
                }
            }
            if (btn && FAILED(btn->get_CurrentBoundingRectangle(&r))) {
                btn->Release();  // stale (Widgets turned off, Explorer restarted): look it up again next time
                btn = nullptr;
                r = {};
            }
        }
        std::vector<Span> spans;
        if (bar && mirrors && uia)
            for (HWND t = FindWindowExW(nullptr, nullptr, L"Shell_SecondaryTrayWnd", nullptr); t;
                 t = FindWindowExW(nullptr, t, L"Shell_SecondaryTrayWnd", nullptr))
                if (IsXamlTaskbar(t)) spans.push_back(MeasureSpan(uia, t, nearTray));  // Windows 10 bars have real child windows
        AcquireSRWLockExclusive(&g_wbLock);
        if (bar && g_wbTaskbar == bar) {  // drop a result for a taskbar we no longer sit on
            g_wbRect = r;
            g_wbRectBar = bar;
        }
        g_spans = std::move(spans);
        ReleaseSRWLockExclusive(&g_wbLock);
        // Next wake: re-read a found button's rectangle (the weather text changes its width) and the secondary
        // taskbars every 5 s; with no button found, at the next search (5 s, 10 s ... 10 min after a miss).
        wait = INFINITE;
        if (bar && (btn || mirrors)) wait = 5000;
        if (bar && nearTray && !btn) {
            const ULONGLONG t = GetTickCount64();
            wait = std::min<DWORD>(wait, nextSearch > t ? (DWORD)(nextSearch - t) : 0);
        }
    }
}

// Points the lookup thread at `taskbar` (null to idle it), starting the thread on first use.
// The last rectangle is kept for its taskbar, so switching from tray-icon back to taskbar mode on the
// same taskbar steps past the Widgets button straight away instead of after the next lookup.
void WatchWidgetsButton(HWND taskbar) {
    AcquireSRWLockExclusive(&g_wbLock);
    g_wbTaskbar = taskbar;
    ReleaseSRWLockExclusive(&g_wbLock);
    if (!g_wbWake) {
        g_wbWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        HANDLE t = g_wbWake ? CreateThread(nullptr, 0, WidgetsButtonThread, nullptr, 0, nullptr) : nullptr;
        if (t) CloseHandle(t);
    }
    if (g_wbWake) SetEvent(g_wbWake);
}

// Never blocks: returns what the lookup thread last found on the current taskbar.
RECT WidgetsButtonRect() {
    AcquireSRWLockShared(&g_wbLock);
    RECT r = g_wbRectBar && g_wbRectBar == g_wbTaskbar ? g_wbRect : RECT{};
    ReleaseSRWLockShared(&g_wbLock);
    return r;
}

void ApplyTipTheme() {
    if (!g_tip) return;
    using SetWindowThemeFn = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
    static HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    static auto setTheme = ux ? (SetWindowThemeFn)GetProcAddress(ux, "SetWindowTheme") : nullptr;
    if (setTheme) {
        setTheme(g_tip, g_light ? L"Explorer" : L"DarkMode_Explorer", nullptr);
        SendMessageW(g_tip, WM_THEMECHANGED, 0, 0);
    }
}

wchar_t Glyph(DeviceKind k, bool bluetooth) {
    switch (k) {
        case DeviceKind::Headset:  return 0xE7F6;  // Headphone
        case DeviceKind::Mouse:    return 0xE962;  // Mouse
        case DeviceKind::Keyboard: return 0xE765;  // KeyboardClassic
        case DeviceKind::Gamepad:  return 0xE7FC;  // Game
        case DeviceKind::Phone:    return 0xE8EA;  // CellPhone
        case DeviceKind::Microphone: return 0xE720;  // Microphone
        default:                   return bluetooth ? 0xE702 : 0xE772;  // Bluetooth / Devices
    }
}
constexpr wchar_t kBatteryUnknown = 0xE996;  // shown when there is nothing to display

// Creates a font, falling back when the preferred face is not installed (GDI would silently substitute).
HFONT MakeFont(int px, int weight, const wchar_t* face, const wchar_t* fallback, int fallbackPx = 0);

HFONT MakeFont(int px, int weight, const wchar_t* face, const wchar_t* fallback, int fallbackPx) {
    for (const wchar_t* f : { face, fallback }) {
        int h_px = (f == fallback && fallbackPx > 0) ? fallbackPx : px;
        HFONT h = CreateFontW(-h_px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, DEFAULT_PITCH, f);
        HDC dc = CreateCompatibleDC(nullptr);
        HGDIOBJ o = SelectObject(dc, h);
        wchar_t got[LF_FACESIZE] = {};
        GetTextFaceW(dc, LF_FACESIZE, got);
        SelectObject(dc, o);
        DeleteDC(dc);
        if (!_wcsicmp(got, f) || f == fallback) return h;
        DeleteObject(h);
    }
    return nullptr;
}

// Geist (Vercel, SIL Open Font License; installer\THIRD_PARTY_NOTICES.txt), the typeface of Zek's Glide: the strip's
// numbers and tags use its SemiBold. res\Geist-Variable.ttf is unpacked from Glide's Geist-Variable.woff2 and built
// into the exe; it is loaded, privately to this process, the first time the strip is drawn. GDI lists each named
// instance of the variable font as a face of its own, so SemiBold is asked for by name at normal weight (asking for
// "Geist" at semibold weight would embolden the regular instance instead).
constexpr wchar_t kGeist[] = L"Geist SemiBold";
bool g_geistTried = false;
bool g_geistLoaded = false;

bool LoadGeist(const void* data, DWORD size) {
    g_geistTried = true;
    DWORD faces = 0;
    // GDI keeps its own copy; the font stays registered for the life of the process.
    g_geistLoaded = AddFontMemResourceEx(const_cast<void*>(data), size, nullptr, &faces) != nullptr && faces > 0;
    return g_geistLoaded;
}

void EnsureGeist() {
    if (g_geistTried) return;
    g_geistTried = true;
    HRSRC r = FindResourceW(nullptr, L"GEIST", RT_RCDATA);
    HGLOBAL res = r ? LoadResource(nullptr, r) : nullptr;
    const void* p = res ? LockResource(res) : nullptr;
    if (p) LoadGeist(p, SizeofResource(nullptr, r));
}

// Geist SemiBold at px pixels, or null when it is not loaded.
HFONT GeistFont(int px) {
    if (!g_geistLoaded) return nullptr;
    HFONT h = CreateFontW(-px, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                          ANTIALIASED_QUALITY, DEFAULT_PITCH, kGeist);
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ o = SelectObject(dc, h);
    wchar_t got[LF_FACESIZE] = {};
    GetTextFaceW(dc, LF_FACESIZE, got);
    SelectObject(dc, o);
    DeleteDC(dc);
    if (_wcsicmp(got, kGeist)) {
        DeleteObject(h);
        return nullptr;
    }
    return h;
}

// Whether the font has a glyph for every character of s (GDI does not fall back from a private font).
bool Covers(HFONT font, const std::wstring& s) {
    if (!font || s.empty()) return false;
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ o = SelectObject(dc, font);
    std::vector<WORD> glyphs(s.size());
    bool all = GetGlyphIndicesW(dc, s.c_str(), (int)s.size(), glyphs.data(), GGI_MARK_NONEXISTING_GLYPHS) != GDI_ERROR;
    for (WORD g : glyphs) all = all && g != 0xFFFF;
    SelectObject(dc, o);
    DeleteDC(dc);
    return all;
}

// Geist when it can show s, otherwise the Segoe fallback (translated level words in other scripts).
HFONT Pick(HFONT geist, HFONT segoe, const std::wstring& s) { return Covers(geist, s) ? geist : segoe; }

// Grayscale coverage mask of a string, cropped to its ink bounds (the strip is alpha-blended, so no ClearType).
struct Mask {
    int w = 0, h = 0;
    int base = 0;  // text: rows from the top to the baseline
    std::vector<BYTE> a;
};

Mask RenderMask(const std::wstring& s, HFONT font, bool darkOnLight) {
    Mask m;
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ of = SelectObject(dc, font);
    SIZE sz;
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    int W = sz.cx + 8, H = sz.cy + 8;
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ ob = SelectObject(dc, bmp);
    memset(bits, darkOnLight ? 0xFF : 0x00, (size_t)W * H * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, darkOnLight ? RGB(0, 0, 0) : RGB(255, 255, 255));
    TextOutW(dc, 4, 4, s.c_str(), (int)s.size());
    GdiFlush();
    auto* p = static_cast<DWORD*>(bits);
    auto cover = [&](DWORD v) -> BYTE {
        int c = (int)((v & 0xFF) + ((v >> 8) & 0xFF) + ((v >> 16) & 0xFF)) / 3;
        return (BYTE)(darkOnLight ? 255 - c : c);
    };
    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (cover(p[y * W + x])) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
    if (x1 >= 0) {
        m.w = x1 - x0 + 1;
        m.h = y1 - y0 + 1;
        m.base = 4 + tm.tmAscent - y0;
        m.a.resize((size_t)m.w * m.h);
        for (int y = 0; y < m.h; y++)
            for (int x = 0; x < m.w; x++) m.a[y * m.w + x] = cover(p[(y0 + y) * W + x0 + x]);
    }
    SelectObject(dc, ob);
    SelectObject(dc, of);
    DeleteObject(bmp);
    DeleteDC(dc);
    return m;
}

struct Rgb { int r, g, b; };

struct Canvas {
    int w, h;
    std::vector<DWORD> px;  // premultiplied BGRA
    Canvas(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_, 0) {}
    void Blend(int x, int y, Rgb c, int a) {  // source-over, a = 0..255
        if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
        DWORD d = px[y * w + x];
        int inv = 255 - a;
        int da = d >> 24, dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
        int oa = a + da * inv / 255;
        int orr = c.r * a / 255 + dr * inv / 255;
        int og = c.g * a / 255 + dg * inv / 255;
        int ob = c.b * a / 255 + db * inv / 255;
        px[y * w + x] = ((DWORD)oa << 24) | ((DWORD)orr << 16) | ((DWORD)og << 8) | (DWORD)ob;
    }
    void Draw(const Mask& m, int x, int y, Rgb c, int alpha) {
        for (int j = 0; j < m.h; j++)
            for (int i = 0; i < m.w; i++) Blend(x + i, y + j, c, m.a[j * m.w + i] * alpha / 255);
    }
};

// Soft glow around a glyph, like a lit RGB device: the coverage mask padded by r pixels, box-blurred
// twice and brightened so a thin icon outline still gives a visible glow.
Mask Halo(const Mask& m, int r) {
    Mask g;
    g.w = m.w + 2 * r;
    g.h = m.h + 2 * r;
    std::vector<int> a((size_t)g.w * g.h, 0), tmp(a.size(), 0);
    for (int y = 0; y < m.h; y++)
        for (int x = 0; x < m.w; x++) a[(y + r) * g.w + x + r] = m.a[y * m.w + x];
    int n = 2 * r + 1;
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < g.h; y++)
            for (int x = 0; x < g.w; x++) {
                int s = 0;
                for (int k = -r; k <= r; k++) if (x + k >= 0 && x + k < g.w) s += a[y * g.w + x + k];
                tmp[y * g.w + x] = s / n;
            }
        for (int y = 0; y < g.h; y++)
            for (int x = 0; x < g.w; x++) {
                int s = 0;
                for (int k = -r; k <= r; k++) if (y + k >= 0 && y + k < g.h) s += tmp[(y + k) * g.w + x];
                a[y * g.w + x] = s / n;
            }
    }
    g.a.resize(a.size());
    for (size_t i = 0; i < a.size(); i++) g.a[i] = (BYTE)std::min(255, a[i] * 3);
    return g;
}

// A rounded rectangle, anti-aliased: corner radius r (h / 2 gives a capsule, the level bar's shape).
void RoundRect(Canvas& cv, int x, int y, int w, int h, double r, Rgb c, int alpha) {
    if (w <= 0 || h <= 0 || alpha <= 0) return;
    r = std::min({ r, w / 2.0, h / 2.0 });
    const double l = x + r, t = y + r, rr = x + w - r, b = y + h - r;  // the corners' centres
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) {
            double px = xx + 0.5, py = yy + 0.5;
            double dx = px < l ? l - px : px > rr ? px - rr : 0.0;
            double dy = py < t ? t - py : py > b ? py - b : 0.0;
            double cover = dx == 0 || dy == 0 ? std::clamp(r - std::max(dx, dy) + 0.5, 0.0, 1.0)
                                               : std::clamp(r - std::sqrt(dx * dx + dy * dy) + 0.5, 0.0, 1.0);
            if (cover > 0) cv.Blend(xx, yy, c, (int)(alpha * cover + 0.5));
        }
}

// One drawn piece of the strip, at its final place. A battery's number carries its level bar: a track of
// barW x barH at (barX, barY) and the first `fill` pixels of it in barColor. Branded devices add a glow behind
// the glyph; a caption is a tiny label on the corner of its icon, outlined in the taskbar's colour.
enum class Role { Icon, Number, Tag, Caption };
struct Piece {
    Mask m;
    Rgb color{};
    int alpha = 255;
    Role role = Role::Icon;
    int x = 0, y = 0;
    int barX = 0, barY = 0, barW = 0, barH = 0, fill = 0;
    Rgb barColor{};
    Mask halo;
    int haloR = 0, haloAlpha = 0;
    int anchor = -1;  // a caption's icon (index into the pieces)
};

// The strip's measurements at 100% scaling, scaled with Px().
constexpr int kPad = 8;          // left and right of the strip (the hover highlight's edge)
constexpr int kIconGap = 6;      // icon or tag to its number
constexpr int kDeviceGap = 16;   // one device to the next
constexpr int kBarGap = 3;       // number to its level bar
constexpr int kBarHeight = 3;

// Three arrangements of the same pieces:
//  * row (default): "[icon] 84%  [icon] 39%", every level bar under its number
//  * stacked (menu option, from PR #525): each device's icon above its number, to save width; used only
//    when the taskbar is tall enough, otherwise the row
//  * vertical (left / right taskbar, from PR #321): one block per device, top to bottom
// All numbers share one baseline and all level bars one width (that of "100%"), each number centred on its bar,
// so devices line up whatever their levels. `width` and `height` return the strip's size; vertically the width is the bar's.
std::vector<Piece> Layout(int barW, int barH, int& width, int& height) {
    const Rgb fg = g_light ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 };
    const Rgb red = g_light ? Rgb{ 196, 30, 40 } : Rgb{ 255, 90, 90 };
    const Rgb green = g_light ? Rgb{ 16, 124, 16 } : Rgb{ 108, 203, 95 };  // Windows "success" green: charging / full
    const Rgb orange = g_light ? Rgb{ 188, 75, 0 } : Rgb{ 255, 160, 70 };  // hardware temperature warning
    EnsureGeist();
    HFONT tagFont = MakeFont(Px(10), FW_SEMIBOLD, L"Segoe UI Variable Text", L"Segoe UI");  // "CPU", "GPU", ...
    HFONT icons = MakeFont(Px(16), FW_NORMAL, L"Segoe Fluent Icons", L"Segoe MDL2 Assets");
    HFONT text = MakeFont(Px(12), FW_SEMIBOLD, L"Segoe UI Variable Text", L"Segoe UI");
    HFONT geistTag = GeistFont(Px(10)), geistText = GeistFont(Px(12));  // null when Geist is not loaded
    HFONT caption = MakeFont(std::max(8, Px(7)), FW_SEMIBOLD, L"Segoe UI Variable Text", L"Segoe UI");  // never under 8 px
    const int bh = std::max(2, Px(kBarHeight)), barGap = Px(kBarGap);

    std::vector<Piece> out;
    struct Slot { int icon, number; };  // indices of one device's pieces
    std::vector<Slot> slots;
    if (g_items.empty()) out.push_back({ RenderMask(std::wstring(1, kBatteryUnknown), icons, g_light), fg, 110 });
    for (const auto& it : g_items) {
        const int a = it.dim ? 120 : 255;
        const bool battery = it.tag.empty();
        const bool branded = it.accent != Accent::None;
        const BrandColor bc = AccentColor(it.accent, g_light);
        const Rgb accent{ bc.r, bc.g, bc.b };
        slots.push_back({ (int)out.size(), (int)out.size() + 1 });
        // Hardware readings show a short tag ("CPU") instead of a glyph, a little fainter than the number.
        Piece icon = battery ? Piece{ RenderMask(std::wstring(1, Glyph(it.kind, it.bluetooth)), icons, g_light),
                                      branded ? accent : fg, a, Role::Icon }
                             : Piece{ RenderMask(it.tag, Pick(geistTag, tagFont, it.tag), g_light), fg, a * 170 / 255,
                                      Role::Tag };
        if (branded && !it.dim) {
            icon.haloR = Px(3);
            icon.halo = Halo(icon.m, icon.haloR);
            icon.haloAlpha = g_light ? 60 : 140;
        }
        out.push_back(std::move(icon));
        const std::wstring t = !it.text.empty() ? it.text : it.percent >= 0 ? std::to_wstring(it.percent) + L"%" : L"?";
        // Zek's fork: a battery's number stays in the text colour and an Apple-style level bar under it carries
        // the state: green while charging or full on power ("Show charging"), red when low, otherwise the text
        // colour (or the brand colour with Brand colors on). Hardware readings keep orange / red warning text.
        Piece number{ RenderMask(t, Pick(geistText, text, t), g_light), battery ? fg : it.low ? red : it.warm ? orange : fg,
                      a, Role::Number };
        if (battery && it.percent >= 0) {
            number.fill = it.full ? 100 : it.percent;  // the level for now; pixels once the bar's width is known
            number.barH = bh;
            number.barColor = it.low ? red : (g_showCharging && (it.charging || it.full)) ? green : branded ? accent : fg;
        }
        const size_t glyph = out.size() - 1;
        out.push_back(std::move(number));
        if (!it.label.empty()) {
            out.push_back({ RenderMask(it.label, caption, g_light), fg, a, Role::Caption });
            out.back().anchor = (int)glyph;
        }
    }
    // Every level bar is as wide as "100%", whatever the levels, so bars compare at a glance and the strip keeps
    // its width when a device reaches 100% (a coarse level like "Medium" may widen it).
    int barWidth = RenderMask(L"100%", geistText ? geistText : text, g_light).w;
    DeleteObject(icons);
    DeleteObject(tagFont);
    DeleteObject(text);
    DeleteObject(caption);
    if (geistTag) DeleteObject(geistTag);
    if (geistText) DeleteObject(geistText);

    // Shared measures: the tallest icon, the deepest ascent and descent of the numbers.
    int iconH = 0, ascent = 0, descent = 0;
    bool bars = false;
    for (const Slot& s : slots) {
        const Piece& n = out[s.number];
        if (out[s.icon].role == Role::Icon) iconH = std::max(iconH, out[s.icon].m.h);
        ascent = std::max(ascent, n.m.base);
        descent = std::max(descent, n.m.h - n.m.base);
        if (n.barH) { bars = true; barWidth = std::max(barWidth, n.m.w); }
    }
    for (const Slot& s : slots) {
        Piece& n = out[s.number];
        if (!n.barH) continue;
        n.barW = barWidth;
        n.fill = n.fill <= 0 ? 0 : std::min(barWidth, std::max(bh, (n.fill * barWidth + 50) / 100));  // never thinner than round
    }
    // The number block: ascent, then (when any battery shows a bar) the gap and the bar; descenders hang into the gap.
    const int blockH = ascent + (bars ? barGap + bh : descent);
    auto placeNumber = [&](Piece& n, int x, int top) {
        n.x = x + std::max(0, n.barW - n.m.w) / 2;  // centred on its bar (Zek, 26 Nov 2026)
        n.y = top + ascent - n.m.base;
        n.barX = x;
        n.barY = top + ascent + barGap;
    };
    auto placeCaptions = [&]() {
        for (Piece& c : out)
            if (c.role == Role::Caption) {
                const Piece& icon = out[c.anchor];
                c.x = icon.x + icon.m.w - c.m.w + Px(3);
                c.y = icon.y + icon.m.h - c.m.h + Px(3);
            }
    };

    height = barH;
    const int rowGap = Px(4);
    if (g_vertical && !slots.empty()) {
        // One block per device, centred in the bar's width: icon, then the number and its bar.
        width = barW;
        int y = Px(6);
        for (const Slot& s : slots) {
            Piece& ic = out[s.icon];
            Piece& n = out[s.number];
            ic.x = (barW - ic.m.w) / 2;
            ic.y = y;
            y += ic.m.h + rowGap;
            const int w = std::max(n.m.w, n.barW);
            placeNumber(n, (barW - w) / 2, y);
            n.x = (barW - n.m.w) / 2;
            y += blockH + Px(10);
        }
        height = y;
        placeCaptions();
        return out;
    }
    if (!g_vertical && g_stacked && !slots.empty() && barH >= iconH + rowGap + blockH + Px(6)) {
        // Icon on top, the number and its bar centred under it; the pair centred in the bar's height.
        const int top = (barH - (iconH + rowGap + blockH)) / 2;
        int x = Px(kPad);
        for (const Slot& s : slots) {
            Piece& ic = out[s.icon];
            Piece& n = out[s.number];
            const int slotW = std::max({ ic.m.w, n.m.w, n.barW });
            ic.x = x + (slotW - ic.m.w) / 2;
            ic.y = top + (iconH - ic.m.h) / 2;
            placeNumber(n, x + (slotW - std::max(n.m.w, n.barW)) / 2, top + iconH + rowGap);
            n.x = x + (slotW - n.m.w) / 2;
            x += slotW + Px(12);
        }
        width = x - Px(12) + Px(kPad);
        placeCaptions();
        return out;
    }
    // Row: every icon and every number centred on the bar's midline, the level bars hanging below the numbers.
    const int top = (barH - ascent) / 2;
    int x = Px(kPad);
    for (size_t i = 0; i < slots.size(); i++) {
        Piece& ic = out[slots[i].icon];
        Piece& n = out[slots[i].number];
        ic.x = x;
        // A tag is text: it sits on the numbers' baseline. A glyph is centred.
        ic.y = ic.role == Role::Tag ? top + ascent - ic.m.base : (barH - ic.m.h) / 2;
        x += ic.m.w + Px(ic.role == Role::Tag ? 4 : kIconGap);
        placeNumber(n, x, top);
        x += std::max(n.m.w, n.barW) + (i + 1 < slots.size() ? Px(kDeviceGap) : 0);
    }
    if (slots.empty()) {  // the "no devices" battery glyph
        out[0].x = x;
        out[0].y = (barH - out[0].m.h) / 2;
        x += out[0].m.w;
    }
    width = x + Px(kPad);
    placeCaptions();
    return out;
}

// Paints the pieces into a premultiplied BGRA canvas of the strip's size.
Canvas Compose(const std::vector<Piece>& pieces, int W, int H, bool hover) {
    Canvas cv(W, H);
    // Nearly transparent background so the whole strip receives mouse input.
    for (auto& p : cv.px) p = 0x01000000;
    // Optional plate in the taskbar's own colours: keeps the text readable when a taskbar theme
    // (e.g. Windhawk's Taskbar Styler) makes the bar transparent over the wallpaper.
    const Rgb barColor = g_light ? Rgb{ 243, 243, 243 } : Rgb{ 32, 32, 32 };
    const Rgb fg = g_light ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 };
    const int inset = std::min(Px(4), H / 4);
    if (g_background) RoundRect(cv, 0, inset, W, H - 2 * inset, Px(6), barColor, 240);
    // Hover highlight like the notification area's buttons.
    if (hover) RoundRect(cv, 0, inset, W, H - 2 * inset, Px(4), fg, g_light ? 20 : 26);
    for (const Piece& p : pieces) {
        if (p.haloR) cv.Draw(p.halo, p.x - p.haloR, p.y - p.haloR, p.color, p.haloAlpha * p.alpha / 255);
        if (p.barW) {  // a faint track, the level on top
            RoundRect(cv, p.barX, p.barY, p.barW, p.barH, p.barH / 2.0, fg, (g_light ? 46 : 64) * p.alpha / 255);
            RoundRect(cv, p.barX, p.barY, p.fill, p.barH, p.barH / 2.0, p.barColor, p.alpha);
        }
        if (p.role == Role::Caption)  // one-pixel outline so it reads over the icon's lines
            for (int oy = -1; oy <= 1; oy++)
                for (int ox = -1; ox <= 1; ox++)
                    if (ox || oy) cv.Draw(p.m, p.x + ox, p.y + oy, barColor, 255);
        cv.Draw(p.m, p.x, p.y, p.color, p.alpha);
    }
    return cv;
}

// Draws the strip for one window: the primary one or a mirror on a secondary taskbar.
void PaintStrip(HWND wnd, HWND taskbar, bool hover, SIZE& size) {
    RECT bar;
    GetClientRect(taskbar, &bar);
    int W = 0, H = 0;
    auto pieces = Layout(bar.right - bar.left, bar.bottom - bar.top, W, H);
    if (W <= 0 || H <= 0) return;
    Canvas cv = Compose(pieces, W, H, hover);

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    memcpy(bits, cv.px.data(), (size_t)W * H * 4);
    HGDIOBJ ob = SelectObject(mem, bmp);
    size = { W, H };
    SIZE sz{ W, H };
    POINT src{ 0, 0 };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(wnd, screen, nullptr, &sz, mem, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

void Paint() {
    if (!g_wnd) return;
    PaintStrip(g_wnd, g_taskbar, g_hover, g_size);
    g_lastW = -1;  // force a re-position with the new size
}

void PaintMirror(Mirror& m) {
    g_dpiBar = m.bar;
    m.dpi = Dpi();
    PaintStrip(m.wnd, m.bar, m.hover, m.size);
    g_dpiBar = nullptr;
    m.lastLeft = INT_MIN;  // force a re-position with the new size
}

void HideMirror(Mirror& m) {
    if (m.lastLeft != INT_MIN || IsWindowVisible(m.wnd)) ShowWindow(m.wnd, SW_HIDE);
    m.lastLeft = INT_MIN;
}

// Places a mirror left of its taskbar's clock (and left of the Widgets button if present), or hides it
// when that spot is unknown or taken by task buttons: never guess and cover something.
void PositionMirror(Mirror& m) {
    RECT bar{};
    if (!GetWindowRect(m.bar, &bar) || (GetWindowLongW(m.bar, GWL_EXSTYLE) & WS_EX_LAYOUTRTL)) { HideMirror(m); return; }
    g_dpiBar = m.bar;
    if (Dpi() != m.dpi || m.size.cy != bar.bottom - bar.top) PaintMirror(m), g_dpiBar = m.bar;
    int right = 0, minLeft = bar.left;
    bool known = false;
    // StartAllBack can put a full notification area on each secondary taskbar.
    // Use its left edge, as on the primary taskbar, to clear all of the tray icons.
    HWND tray = FindWindowExW(m.bar, nullptr, L"TrayNotifyWnd", nullptr);
    RECT tr{};
    if (tray && IsWindowVisible(tray) && GetWindowRect(tray, &tr) &&
        tr.left > bar.left && tr.right <= bar.right && tr.right > tr.left &&
        tr.bottom > bar.top && tr.top < bar.bottom) {
        right = tr.left;
        known = true;
    } else if (!IsXamlTaskbar(m.bar)) {  // Windows 10: the clock is a real child window (absent when it is turned off)
        HWND clock = FindWindowExW(m.bar, nullptr, L"ClockButton", nullptr);
        RECT cr{};
        right = clock && IsWindowVisible(clock) && GetWindowRect(clock, &cr) ? cr.left : bar.right - Px(8);
        for (const wchar_t* cls : { L"ShellFeedsTaskbarViewWindow", L"DynamicContent2" }) {
            HWND feeds = FindWindowExW(m.bar, nullptr, cls, nullptr);
            RECT fr{};
            if (feeds && IsWindowVisible(feeds) && GetWindowRect(feeds, &fr) && fr.left < right && fr.right > fr.left)
                right = fr.left;
        }
        known = true;
    } else {
        AcquireSRWLockShared(&g_wbLock);
        for (const auto& s : g_spans)
            if (s.bar == m.bar && s.measured) {
                right = s.hasClock ? s.clockLeft : bar.right - Px(8);
                if (s.hasWidgets && s.widgets.left < right && s.widgets.right > right - m.size.cx - Px(2))
                    right = s.widgets.left;
                if (s.hasTasks) minLeft = s.tasksRight + Px(4);
                known = true;
            }
        ReleaseSRWLockShared(&g_wbLock);
    }
    DWORD offset = 0, sz = sizeof(offset);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\WirelessBattery", L"TaskbarOffset", RRF_RT_REG_DWORD, nullptr, &offset, &sz) ==
        ERROR_SUCCESS)
        right -= Px((int)offset);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\WirelessBattery", L"SecondaryTaskbarOffset", RRF_RT_REG_DWORD, nullptr, &offset, &sz) ==
        ERROR_SUCCESS)
        right -= Px((int)offset);
    int left = right - m.size.cx - Px(2);
    g_dpiBar = nullptr;
    if (!known || m.size.cx <= 0 || left < minLeft) { HideMirror(m); return; }
    POINT p{ left, bar.top };
    ScreenToClient(m.bar, &p);
    bool covered = GetWindow(m.wnd, GW_HWNDPREV) != nullptr;
    if (!covered && p.x == m.lastLeft) return;
    m.lastLeft = p.x;
    SetWindowPos(m.wnd, HWND_TOP, p.x, 0, m.size.cx, m.size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void AddTool(HWND w) {
    if (!g_tip) return;
    TTTOOLINFOW ti{ sizeof(ti) };
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = g_notify;
    ti.uId = (UINT_PTR)w;
    ti.lpszText = const_cast<wchar_t*>(g_tipText.empty() ? L"BatteryHub" : g_tipText.c_str());
    SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

void DestroyMirror(Mirror& m) {
    if (g_tip) {
        TTTOOLINFOW ti{ sizeof(ti) };
        ti.hwnd = g_notify;
        ti.uId = (UINT_PTR)m.wnd;
        SendMessageW(g_tip, TTM_DELTOOLW, 0, (LPARAM)&ti);
    }
    if (m.wnd && IsWindow(m.wnd)) DestroyWindow(m.wnd);
    m.wnd = nullptr;
}

void SetMeasureMirrors(bool on) {
    AcquireSRWLockExclusive(&g_wbLock);
    bool changed = g_wbMirrors != on;
    g_wbMirrors = on;
    ReleaseSRWLockExclusive(&g_wbLock);
    if (changed && on && g_wbWake) SetEvent(g_wbWake);  // measure the new taskbar now, not in 5 s
}

std::vector<HWND> SecondaryTaskbars() {
    std::vector<HWND> out;
    for (HWND t = FindWindowExW(nullptr, nullptr, L"Shell_SecondaryTrayWnd", nullptr); t;
         t = FindWindowExW(nullptr, t, L"Shell_SecondaryTrayWnd", nullptr)) {
        RECT r{};
        if (IsWindowVisible(t) && GetWindowRect(t, &r) && r.right - r.left > r.bottom - r.top) out.push_back(t);  // horizontal only
    }
    return out;
}

// Keeps one mirror per secondary taskbar: adds them for new monitors, removes them for gone ones.
// Called from Reposition() about once a second; with one monitor it is a single FindWindowEx call.
// A monitor's lasting identity: its device interface name, which survives reboots, Explorer restarts and
// Windows renumbering the displays (falls back to "\\.\DISPLAYn").
std::wstring MonitorId(HWND bar) {
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromWindow(bar, MONITOR_DEFAULTTONULL);
    if (!mon || !GetMonitorInfoW(mon, &mi)) return L"";
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);
    if (EnumDisplayDevicesW(mi.szDevice, 0, &dd, EDD_GET_DEVICE_INTERFACE_NAME) && dd.DeviceID[0]) return dd.DeviceID;
    return mi.szDevice;
}

// "Display 3 (right)": the number Windows gives the display and where it sits next to the main one.
std::wstring MonitorLabel(HWND bar) {
    MONITORINFOEXW mi{}, pm{};
    mi.cbSize = pm.cbSize = sizeof(MONITORINFOEXW);
    HMONITOR mon = MonitorFromWindow(bar, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(mon, &mi)) return T(L"Other monitor");
    std::wstring dev = mi.szDevice;
    size_t d = dev.find(L"DISPLAY");
    std::wstring label = d != std::wstring::npos ? TF(L"Display {number}", { { L"number", dev.substr(d + 7) } }) : T(L"Other monitor");
    POINT origin{ 0, 0 };
    if (GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &pm)) {
        int dx = (mi.rcMonitor.left + mi.rcMonitor.right - pm.rcMonitor.left - pm.rcMonitor.right) / 2;
        int dy = (mi.rcMonitor.top + mi.rcMonitor.bottom - pm.rcMonitor.top - pm.rcMonitor.bottom) / 2;
        label += std::wstring(L" (") + T(abs(dx) >= abs(dy) ? (dx < 0 ? L"left" : L"right") : (dy < 0 ? L"above" : L"below")) + L")";
    }
    return label;
}

// Hides or shows the strip on the main taskbar (it keeps running, just out of sight).
void SetPrimaryHidden(bool hide) {
    if (hide == g_primaryHidden || !g_wnd) return;
    g_primaryHidden = hide;
    if (hide) {
        ShowWindow(g_wnd, SW_HIDE);
        if (g_rebar && IsWindow(g_rebar)) {  // Windows 10: give the task buttons their full width back
            RECT r;
            GetClientRect(g_taskbar, &r);
            PostMessageW(g_taskbar, WM_SIZE, SIZE_RESTORED, MAKELPARAM(r.right, r.bottom));
        }
    }
    g_lastW = -1;  // re-place (and show) on the next pass
}

void SyncMirrors() {
    std::vector<HWND> all = g_wnd ? SecondaryTaskbars() : std::vector<HWND>{}, bars;
    bool chosenFound = false;
    if (g_choice == L"all") {
        bars = all;
    } else if (g_choice != L"main") {
        for (HWND b : all)
            if (MonitorId(b) == g_choice) {
                bars.push_back(b);
                chosenFound = true;
                break;
            }
    }
    SetPrimaryHidden(chosenFound);
    for (auto it = g_mirrors.begin(); it != g_mirrors.end();) {
        if (std::find(bars.begin(), bars.end(), it->bar) == bars.end() || !IsWindow(it->wnd)) {
            DestroyMirror(*it);
            it = g_mirrors.erase(it);
        } else {
            ++it;
        }
    }
    for (HWND b : bars) {
        if (std::any_of(g_mirrors.begin(), g_mirrors.end(), [b](const Mirror& m) { return m.bar == b; })) continue;
        Mirror m;
        m.bar = b;
        m.wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE, kClass, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 1, 1, b, nullptr,
                                g_inst, nullptr);
        if (!m.wnd) continue;
        AddTool(m.wnd);
        g_mirrors.push_back(m);
        PaintMirror(g_mirrors.back());
    }
    SetMeasureMirrors(!g_mirrors.empty());
    for (auto& m : g_mirrors) PositionMirror(m);
}

void PaintAll() {
    Paint();
    for (auto& m : g_mirrors) PaintMirror(m);
}

// Hover highlight for the strip under the mouse: the primary one or a mirror.
void SetHover(HWND h, bool on) {
    if (h == g_wnd) {
        if (g_hover == on) return;
        g_hover = on;
        Paint();
        widget::Reposition();
        return;
    }
    for (auto& m : g_mirrors)
        if (m.wnd == h && m.hover != on) {
            m.hover = on;
            PaintMirror(m);
            PositionMirror(m);
        }
}

LRESULT CALLBACK WidgetProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
            SetHover(h, true);
            return 0;
        }
        case WM_MOUSELEAVE:
            SetHover(h, false);
            return 0;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
            PostMessageW(g_notify, g_clickMsg, 0, 0);
            return 0;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

namespace widget {

bool Create(HINSTANCE inst, HWND notifyWnd, UINT clickMsg, bool lightTheme) {
    Destroy();
    g_taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!g_taskbar || !FindWindowExW(g_taskbar, nullptr, L"TrayNotifyWnd", nullptr)) { g_taskbar = nullptr; return false; }
    RECT tb;
    GetWindowRect(g_taskbar, &tb);
    g_vertical = tb.bottom - tb.top > tb.right - tb.left;  // left / right taskbar: devices stacked in a column
    // Windows 11's taskbar is a XAML island; on Windows 10 the task buttons live in ReBarWindow32
    // and have to be narrowed so they never slide under the widget.
    g_rebar = FindWindowExW(g_taskbar, nullptr, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr)
                  ? nullptr
                  : FindWindowExW(g_taskbar, nullptr, L"ReBarWindow32", nullptr);
    g_notify = notifyWnd;
    g_clickMsg = clickMsg;
    g_light = lightTheme;
    g_inst = inst;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = WidgetProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wc.lpszClassName = kClass;
        registered = RegisterClassExW(&wc) != 0;
    }
    g_wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE, kClass, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 1, 1,
                            g_taskbar, nullptr, inst, nullptr);
    if (!g_wnd) { g_taskbar = nullptr; return false; }

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);
    g_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0,
                            notifyWnd, nullptr, inst, nullptr);
    if (g_tip) {
        ApplyTipTheme();
        TTTOOLINFOW ti{ sizeof(ti) };
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = notifyWnd;
        ti.uId = (UINT_PTR)g_wnd;
        ti.lpszText = const_cast<wchar_t*>(L"BatteryHub");
        SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        SendMessageW(g_tip, TTM_SETMAXTIPWIDTH, 0, 600);
    }
    WatchWidgetsButton(g_taskbar);
    g_lastDpi = Dpi();
    Paint();
    g_lastW = -1;
    Reposition();
    if (!g_primaryHidden) ShowWindow(g_wnd, SW_SHOWNOACTIVATE);  // hidden when only another monitor is chosen
    return true;
}

void Destroy() {
    if (g_rebar && IsWindow(g_rebar) && g_taskbar && IsWindow(g_taskbar)) {
        // Let Explorer lay the task buttons out again over the full width.
        RECT r;
        GetClientRect(g_taskbar, &r);
        PostMessageW(g_taskbar, WM_SIZE, SIZE_RESTORED, MAKELPARAM(r.right, r.bottom));
    }
    g_rebar = nullptr;
    for (auto& m : g_mirrors) DestroyMirror(m);
    g_mirrors.clear();
    SetMeasureMirrors(false);
    g_primaryHidden = false;  // the next Create() decides again
    if (g_wbWake) WatchWidgetsButton(nullptr);
    if (g_tip) { DestroyWindow(g_tip); g_tip = nullptr; }
    if (g_wnd) { DestroyWindow(g_wnd); g_wnd = nullptr; }
    g_taskbar = nullptr;
    g_hover = false;
}

bool Alive() { return g_wnd && IsWindow(g_wnd) && g_taskbar && IsWindow(g_taskbar); }

void SetItems(const std::vector<WidgetItem>& items, const std::wstring& tooltip) {
    g_items = items;
    if (tooltip != g_tipText && g_tip) {
        g_tipText = tooltip;
        TTTOOLINFOW ti{ sizeof(ti) };
        ti.hwnd = g_notify;
        ti.lpszText = const_cast<wchar_t*>(g_tipText.c_str());
        ti.uId = (UINT_PTR)g_wnd;
        SendMessageW(g_tip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
        for (const auto& m : g_mirrors) {
            ti.uId = (UINT_PTR)m.wnd;
            SendMessageW(g_tip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
        }
    }
    PaintAll();
    Reposition();
}

void SetLightTheme(bool light) {
    g_light = light;
    ApplyTipTheme();
    PaintAll();
}

void SetBackground(bool on) {
    g_background = on;
    PaintAll();
}

void SetStacked(bool on) {
    g_stacked = on;
    PaintAll();
    if (g_wnd) Reposition();
}

void SetShowCharging(bool on) {
    g_showCharging = on;
    PaintAll();
    if (g_wnd) Reposition();
}

void SetMonitorChoice(const std::wstring& choice) {
    g_choice = choice.empty() ? L"all" : choice;
    SyncMirrors();  // adds, removes or hides strips right away
    if (g_wnd) Reposition();
}

std::vector<MonitorOption> OtherMonitors() {
    std::vector<MonitorOption> out;
    for (HWND b : SecondaryTaskbars()) out.push_back({ MonitorId(b), MonitorLabel(b) });
    std::sort(out.begin(), out.end(), [](const MonitorOption& a, const MonitorOption& b) { return a.label < b.label; });
    return out;
}

bool Reposition() {
    if (!Alive()) return false;
    SyncMirrors();
    if (g_primaryHidden) return true;  // only another monitor's taskbar shows the strip
    if (Dpi() != g_lastDpi) { g_lastDpi = Dpi(); Paint(); }
    HWND tray = FindWindowExW(g_taskbar, nullptr, L"TrayNotifyWnd", nullptr);
    if (!tray) return true;
    RECT tr, bar;
    GetWindowRect(tray, &tr);
    GetWindowRect(g_taskbar, &bar);
    // The taskbar can be moved between a horizontal and a vertical edge while the app runs.
    bool vertical = bar.bottom - bar.top > bar.right - bar.left;
    if (vertical != g_vertical) {
        g_vertical = vertical;
        Paint();
    }
    if (g_vertical) {
        // Left or right taskbar (PR #321): the column sits just above the notification area, centred in
        // the bar's width.
        RECT client{};
        GetClientRect(g_taskbar, &client);
        POINT anchor{ bar.left, tr.top };
        ScreenToClient(g_taskbar, &anchor);
        int top = anchor.y - g_size.cy - Px(2);
        DWORD offset = 0, osz = sizeof(offset);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\WirelessBattery", L"TaskbarOffset", RRF_RT_REG_DWORD, nullptr, &offset,
                         &osz) == ERROR_SUCCESS)
            top -= Px((int)offset);
        top = std::clamp(top, 0, (int)std::max<LONG>(0, client.bottom - g_size.cy));
        int left = (int)std::max<LONG>(0, (client.right - g_size.cx) / 2);
        // Windows 10: shorten the task-button area when it would reach behind the column.
        if (g_rebar && IsWindow(g_rebar)) {
            RECT rr{};
            GetWindowRect(g_rebar, &rr);
            POINT tl{ rr.left, rr.top };
            ScreenToClient(g_taskbar, &tl);
            if (tl.y < top && tl.y + (rr.bottom - rr.top) > top)
                MoveWindow(g_rebar, tl.x, tl.y, rr.right - rr.left, top - tl.y, TRUE);
        }
        bool covered = GetWindow(g_wnd, GW_HWNDPREV) != nullptr;
        if (!covered && EqualRect(&tr, &g_lastTray) && EqualRect(&bar, &g_lastBar) && g_lastW == g_size.cy && g_lastLeft == top)
            return true;
        g_lastTray = tr;
        g_lastBar = bar;
        g_lastW = g_size.cy;
        g_lastLeft = top;
        SetWindowPos(g_wnd, HWND_TOP, left, top, g_size.cx, g_size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return true;
    }
    // Right-to-left Windows (Hebrew, Arabic) mirrors the taskbar: client x runs from the screen's right
    // edge, so a window's client-space left edge is its screen-space right edge.
    bool rtl = GetWindowLongW(g_taskbar, GWL_EXSTYLE) & WS_EX_LAYOUTRTL;
    int edge = rtl ? tr.right : tr.left;  // screen x we sit next to
    // Windows 10: News and Interests / Weather sits immediately left of the tray. The window that hosts it
    // depends on the build: ShellFeedsTaskbarViewWindow on some, DynamicContent2 on others (e.g. 19045,
    // where the search highlights button is DynamicContent1). Both are direct children of the taskbar and
    // only a visible one counts. Its rectangle is read on every pass because the weather text changes its
    // width, so the widget follows it.
    if (!rtl)
        for (const wchar_t* cls : { L"ShellFeedsTaskbarViewWindow", L"DynamicContent2" }) {
            HWND feeds = FindWindowExW(g_taskbar, nullptr, cls, nullptr);
            if (!feeds || !IsWindowVisible(feeds)) continue;
            RECT fr;
            GetWindowRect(feeds, &fr);
            if (fr.left < edge && fr.right > fr.left) edge = fr.left;
        }
    // Step past the Widgets button when it sits where we would go.
    RECT wb = WidgetsButtonRect();
    if (wb.right > wb.left && wb.top < bar.bottom && wb.bottom > bar.top &&
        (rtl ? wb.left < edge + g_size.cx + Px(2) && wb.right > edge : wb.right > edge - g_size.cx - Px(2) && wb.left < edge))
        edge = rtl ? wb.right : wb.left;
    POINT p{ edge, bar.top };
    ScreenToClient(g_taskbar, &p);
    int left = p.x - g_size.cx - Px(2);


    // Optional user override in DPI-independent pixels (e.g. for custom padding).
    DWORD customOffset = 0, sz = sizeof(customOffset);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\WirelessBattery", L"TaskbarOffset",
                     RRF_RT_REG_DWORD, nullptr, &customOffset, &sz) == ERROR_SUCCESS) {
        left -= Px((int)customOffset);
    }

    // Windows 10: Explorer re-expands the task buttons on its own layout passes; narrow them again.
    if (g_rebar && IsWindow(g_rebar)) {
        RECT rr;
        GetWindowRect(g_rebar, &rr);
        POINT tl{ rtl ? rr.right : rr.left, rr.top };
        ScreenToClient(g_taskbar, &tl);
        if (tl.x + (rr.right - rr.left) > left && left > tl.x)
            MoveWindow(g_rebar, tl.x, tl.y, left - tl.x, rr.bottom - rr.top, TRUE);
    }

    // Keep us above the taskbar's own content (our sibling windows).
    bool covered = GetWindow(g_wnd, GW_HWNDPREV) != nullptr;
    if (!covered && EqualRect(&tr, &g_lastTray) && EqualRect(&bar, &g_lastBar) && EqualRect(&wb, &g_lastWidgets) &&
        g_lastW == g_size.cx && g_lastLeft == left)
        return true;
    g_lastTray = tr;
    g_lastWidgets = wb;
    g_lastBar = bar;
    g_lastW = g_size.cx;
    g_lastLeft = left;
    SetWindowPos(g_wnd, HWND_TOP, left, 0, g_size.cx, g_size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return true;
}

}  // namespace widget