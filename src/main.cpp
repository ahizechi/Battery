// Wireless Battery - tray app showing the battery level of wireless mice, headsets and Bluetooth devices.
#include <windows.h>
#include <shellapi.h>
#include <dbt.h>
#include <initguid.h>
#include <hidclass.h>
#include <commctrl.h>
#include <dwmapi.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cwctype>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "devices.h"
#include "widget.h"
#include "../res/options.h"

namespace {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_RESULTS = WM_APP + 2;
constexpr UINT WM_WIDGET_CLICK = WM_APP + 3;
constexpr UINT_PTR TIMER_DEBOUNCE = 1;
constexpr UINT_PTR TIMER_WIDGET = 2;
constexpr DWORD DEFAULT_POLL_SECONDS = 60;
constexpr DWORD DEFAULT_LOW_BATTERY = 15;
constexpr DWORD POLL_SECONDS[] = {30, 60, 120, 300};
constexpr DWORD LOW_BATTERY_LEVELS[] = {5, 10, 15, 20, 25, 30};
constexpr UINT PLACEHOLDER_ID = 1;

enum MenuId : UINT { ID_REFRESH = 100, ID_OPTIONS, ID_EXIT, ID_DEVICE_FIRST = 1000 };

const wchar_t* kAppName = L"Wireless Battery";
const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunValue = L"WirelessBattery";

const wchar_t* kSettingsKey = L"Software\\WirelessBattery";

HINSTANCE g_inst;
HWND g_hwnd;
HANDLE g_refreshEvt, g_stopEvt, g_worker;
UINT g_taskbarCreatedMsg;
bool g_lightTaskbar;
enum DisplayMode : DWORD { TrayIcons, TaskbarWidget, DesktopWidget };
DisplayMode g_displayMode = TaskbarWidget;
bool g_useWidget = false;      // what is actually shown right now
bool g_hideLastKnown = false;  // hide (last known) / asleep devices from taskbar and tray
std::atomic<DWORD> g_pollSeconds{DEFAULT_POLL_SECONDS};
int g_lowBattery = DEFAULT_LOW_BATTERY;
bool g_trayAlerts = true;

struct TrayEntry {
    UINT uid = 0;
    DeviceStatus st;
    int lastPct = -1;        // last known percentage, kept while the device sleeps
    ULONGLONG lastSeen = 0;
    bool added = false;
    bool lowWarned = false;
    std::wstring iconSig;    // what the current icon shows, to avoid needless redraws
    HICON icon = nullptr;
};
std::map<std::wstring, TrayEntry> g_entries;
bool g_placeholder = false;
HICON g_placeholderIcon = nullptr;

// ---- helpers ----------------------------------------------------------------------------------

UINT StableId(const std::wstring& key) {
    UINT h = 2166136261u;
    for (wchar_t c : key) { h ^= c; h *= 16777619u; }
    return (h & 0x7FFFFFFF) | 0x100;
}

bool ReadLightTaskbar() {
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS)
        return v != 0;
    return false;
}

std::wstring ExePath() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return p;
}

// Devices the user chose to hide (by key), stored as a REG_MULTI_SZ.
std::set<std::wstring> g_hidden;

void LoadHidden() {
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"HiddenDevices", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || !size)
        return;
    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1, 0);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"HiddenDevices", RRF_RT_REG_MULTI_SZ, nullptr, buf.data(), &size) != ERROR_SUCCESS)
        return;
    for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1) g_hidden.insert(p);
}

void SaveHidden() {
    std::wstring multi;
    for (const auto& k : g_hidden) { multi += k; multi.push_back(L'\0'); }
    multi.push_back(L'\0');
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"HiddenDevices", REG_MULTI_SZ, multi.data(), (DWORD)(multi.size() * sizeof(wchar_t)));
}

bool IsHidden(const std::wstring& key) { return g_hidden.count(key) != 0; }

bool IsEntryHidden(const TrayEntry& e) {
    return IsHidden(e.st.key) || (g_hideLastKnown && (e.st.asleep || (e.st.percent < 0 && e.lastPct >= 0)));
}

DWORD LoadSetting(const wchar_t* name, DWORD fallback) {
    DWORD value, size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD,
                        nullptr, &value, &size) == ERROR_SUCCESS ? value : fallback;
}

bool SaveSetting(const wchar_t* name, DWORD value) {
    return RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, name, REG_DWORD,
                           &value, sizeof(value)) == ERROR_SUCCESS;
}

template <size_t N>
DWORD LoadPreset(const wchar_t* name, DWORD fallback, const DWORD (&values)[N]) {
    DWORD value = LoadSetting(name, fallback);
    return std::find(values, values + N, value) != values + N ? value : fallback;
}

bool AutostartEnabled() {
    wchar_t buf[MAX_PATH * 2];
    DWORD sz = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, buf, &sz) != ERROR_SUCCESS) return false;
    return _wcsicmp(buf, (L"\"" + ExePath() + L"\"").c_str()) == 0;
}

bool SetAutostart(bool on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return false;
    LSTATUS result;
    if (on) {
        std::wstring v = L"\"" + ExePath() + L"\"";
        result = RegSetValueExW(k, kRunValue, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(k, kRunValue);
    }
    RegCloseKey(k);
    return result == ERROR_SUCCESS || (!on && result == ERROR_FILE_NOT_FOUND);
}

// ---- icon rendering ---------------------------------------------------------------------------
// Draws the percentage as large digits with a small level bar underneath. GDI text is rendered
// white-on-black into a scratch bitmap and its brightness is used as the alpha channel, which
// gives smooth edges on the transparent icon.

struct Rgb { BYTE r, g, b; };

HICON RenderIcon(const std::wstring& text, int barPct, Rgb textColor, Rgb barColor, bool dim) {
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    UINT dpi = taskbar ? GetDpiForWindow(taskbar) : 0;
    const int S = GetSystemMetricsForDpi(SM_CXSMICON, dpi ? dpi : GetDpiForSystem());
    const int barH = std::max(2, S / 8);
    const int textH = S - barH - 1;

    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    // scratch bitmap for the glyphs
    const int W = S * 3, H = S * 2;
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    void* sbits = nullptr;
    HBITMAP scratch = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &sbits, nullptr, 0);
    HGDIOBJ oldBmp = SelectObject(dc, scratch);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    auto* sp = static_cast<DWORD*>(sbits);

    int bx0 = 0, by0 = 0, bx1 = -1, by1 = -1;
    for (int fh = textH * 2; fh >= 6; fh--) {
        HFONT f = CreateFontW(-fh, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HGDIOBJ oldF = SelectObject(dc, f);
        SetTextCharacterExtra(dc, text.size() >= 3 ? -1 : 0);
        memset(sbits, 0, (size_t)W * H * 4);
        TextOutW(dc, 2, 0, text.c_str(), (int)text.size());
        GdiFlush();
        SelectObject(dc, oldF);
        DeleteObject(f);
        bx0 = W; by0 = H; bx1 = -1; by1 = -1;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (sp[y * W + x] & 0xFF) { bx0 = std::min(bx0, x); bx1 = std::max(bx1, x); by0 = std::min(by0, y); by1 = std::max(by1, y); }
        if (bx1 < 0) break;
        if (bx1 - bx0 + 1 <= S && by1 - by0 + 1 <= textH) break;
    }

    // final icon bitmap
    bi.bmiHeader.biWidth = S;
    bi.bmiHeader.biHeight = -S;
    void* ibits = nullptr;
    HBITMAP color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &ibits, nullptr, 0);
    auto* ip = static_cast<DWORD*>(ibits);
    memset(ip, 0, (size_t)S * S * 4);
    const int alphaScale = dim ? 140 : 255;
    auto put = [&](int x, int y, Rgb c, int a) {
        if (x < 0 || y < 0 || x >= S || y >= S || a <= 0) return;
        a = a * alphaScale / 255;
        ip[y * S + x] = ((DWORD)a << 24) | ((DWORD)c.r << 16) | ((DWORD)c.g << 8) | c.b;  // straight alpha
    };
    if (bx1 >= 0) {
        int gw = bx1 - bx0 + 1, gh = by1 - by0 + 1;
        int ox = (S - gw) / 2, oy = (textH - gh) / 2;
        for (int y = 0; y < gh; y++)
            for (int x = 0; x < gw; x++)
                put(ox + x, oy + y, textColor, sp[(by0 + y) * W + (bx0 + x)] & 0xFF);
    }
    // level bar: faint track + filled part
    int fill = barPct < 0 ? 0 : std::max(barPct > 0 ? 1 : 0, (barPct * S + 50) / 100);
    for (int y = S - barH; y < S; y++)
        for (int x = 0; x < S; x++) {
            if (x < fill) put(x, y, barColor, 255);
            else put(x, y, g_lightTaskbar ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 }, 70);
        }

    SelectObject(dc, oldBmp);
    DeleteObject(scratch);
    DeleteDC(dc);

    HBITMAP mask = CreateBitmap(S, S, 1, 1, nullptr);
    {
        HDC mdc = CreateCompatibleDC(nullptr);
        HGDIOBJ o = SelectObject(mdc, mask);
        PatBlt(mdc, 0, 0, S, S, BLACKNESS);
        SelectObject(mdc, o);
        DeleteDC(mdc);
    }
    ICONINFO ii{ TRUE, 0, 0, mask, color };
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

Rgb Foreground() { return g_lightTaskbar ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 }; }

HICON IconFor(int pct, bool charging, bool dim) {
    Rgb fg = Foreground();
    Rgb bar = charging        ? Rgb{ 0, 170, 255 }
            : pct <= g_lowBattery ? Rgb{ 232, 32, 48 }
            : pct <= 30       ? Rgb{ 255, 150, 0 }
                              : Rgb{ 30, 190, 90 };
    Rgb text = (!charging && pct >= 0 && pct <= g_lowBattery) ? Rgb{ 240, 50, 60 } : fg;
    std::wstring t = pct < 0 ? L"?" : std::to_wstring(pct);
    return RenderIcon(t, pct, text, bar, dim);
}

// ---- tray management --------------------------------------------------------------------------

void CopyTip(wchar_t* dst, size_t cap, const std::wstring& s) {
    wcsncpy(dst, s.c_str(), cap - 1);
    dst[cap - 1] = 0;
}

std::wstring StatusText(const TrayEntry& e) {
    const auto& s = e.st;
    if (!s.connected) return L"off / not connected";
    std::wstring t;
    if (!s.levelText.empty() && s.percent >= 0) t = s.levelText;
    else if (s.percent >= 0) t = std::to_wstring(s.percent) + L"%";
    else if (e.lastPct >= 0) t = std::to_wstring(e.lastPct) + L"% (last known)";
    else t = L"unknown";
    if (s.charging) t += L", charging";
    if (s.asleep) t += L", asleep";
    return t;
}

std::wstring DeviceDetails(const TrayEntry& e) {
    return e.st.via.empty() ? StatusText(e) : StatusText(e) + L"  [" + e.st.via + L"]";
}

void TrayCall(DWORD msg, UINT uid, HICON icon, const std::wstring& tip, const wchar_t* infoTitle = nullptr,
              const std::wstring& info = L"") {
    NOTIFYICONDATAW nid{ sizeof(nid) };
    nid.hWnd = g_hwnd;
    nid.uID = uid;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = icon;
    CopyTip(nid.szTip, ARRAYSIZE(nid.szTip), tip);
    if (infoTitle) {
        nid.uFlags |= NIF_INFO;
        CopyTip(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), infoTitle);
        CopyTip(nid.szInfo, ARRAYSIZE(nid.szInfo), info);
        nid.dwInfoFlags = NIIF_WARNING;
    }
    Shell_NotifyIconW(msg, &nid);
    if (msg == NIM_ADD) {
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
    }
}

void TrayDelete(UINT uid) {
    NOTIFYICONDATAW nid{ sizeof(nid) };
    nid.hWnd = g_hwnd;
    nid.uID = uid;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void UpdatePlaceholder() {
    if (g_useWidget) {
        if (g_placeholder) { TrayDelete(PLACEHOLDER_ID); g_placeholder = false; }
        return;
    }
    bool any = false;
    for (auto& [k, e] : g_entries) any |= e.added;
    if (!any && !g_placeholder) {
        if (!g_placeholderIcon) g_placeholderIcon = RenderIcon(L"-", -1, Foreground(), Foreground(), true);
        TrayCall(NIM_ADD, PLACEHOLDER_ID, g_placeholderIcon, L"Wireless Battery\nNo devices with battery info found");
        g_placeholder = true;
    } else if (any && g_placeholder) {
        TrayDelete(PLACEHOLDER_ID);
        g_placeholder = false;
    }
}

void ApplyEntry(TrayEntry& e) {
    const auto& s = e.st;
    bool visible = s.connected && !g_useWidget && !IsEntryHidden(e);
    if (!visible) {
        if (e.added) { TrayDelete(e.uid); e.added = false; }
        return;
    }
    int shown = s.percent >= 0 ? s.percent : e.lastPct;
    bool dim = s.percent < 0;
    std::wstring sig = std::to_wstring(shown) + (s.charging ? L"c" : L"") + (dim ? L"d" : L"") + (g_lightTaskbar ? L"l" : L"k");
    if (sig != e.iconSig || !e.icon) {
        if (e.icon) DestroyIcon(e.icon);
        e.icon = IconFor(shown, s.charging, dim);
        e.iconSig = sig;
    }
    std::wstring tip = s.name + L"\n" + DeviceDetails(e);

    const wchar_t* infoTitle = nullptr;
    std::wstring info;
    if (s.charging || shown > g_lowBattery + 10) e.lowWarned = false;
    if (g_trayAlerts && !s.charging && s.percent >= 0 && s.percent <= g_lowBattery && !e.lowWarned) {
        e.lowWarned = true;
        infoTitle = L"Low battery";
        info = s.name + L" is at " + std::to_wstring(s.percent) + L"%";
    }
    TrayCall(e.added ? NIM_MODIFY : NIM_ADD, e.uid, e.icon, tip, infoTitle, info);
    e.added = true;
}

int KindOrder(DeviceKind k) {  // headset first, then mouse, ... (matches the left-to-right layout)
    switch (k) {
        case DeviceKind::Headset:  return 0;
        case DeviceKind::Mouse:    return 1;
        case DeviceKind::Keyboard: return 2;
        case DeviceKind::Gamepad:  return 3;
        default:                   return 4;
    }
}

void UpdateWidget() {
    if (!g_useWidget) return;
    std::vector<const TrayEntry*> list;
    for (auto& [k, e] : g_entries)
        if (e.st.connected && !IsEntryHidden(e)) list.push_back(&e);
    std::sort(list.begin(), list.end(), [](const TrayEntry* a, const TrayEntry* b) {
        int ka = KindOrder(a->st.kind), kb = KindOrder(b->st.kind);
        return ka != kb ? ka < kb : a->st.name < b->st.name;
    });
    std::vector<WidgetItem> items;
    std::wstring tip;
    auto appendTip = [&](const std::wstring& line) {
        if (!tip.empty()) tip += L"\n";
        tip += line;
    };
    if (g_displayMode == DesktopWidget) {
        list.erase(std::remove_if(list.begin(), list.end(), [](const TrayEntry* e) {
            return e->st.kind != DeviceKind::Headset && e->st.kind != DeviceKind::Mouse;
        }), list.end());
    }
    for (auto* e : list) {
        WidgetItem it;
        it.kind = e->st.kind;
        it.percent = e->st.percent >= 0 ? e->st.percent : e->lastPct;
        if (e->st.percent >= 0) it.text = e->st.levelText;
        it.charging = e->st.charging;
        it.dim = e->st.percent < 0;
        it.low = !it.charging && it.percent >= 0 && it.percent <= g_lowBattery;
        it.bluetooth = e->st.via == L"Bluetooth";
        items.push_back(it);
        appendTip(e->st.name + L": " + DeviceDetails(*e));
    }
    if (g_displayMode == DesktopWidget) {
        for (auto kind : {DeviceKind::Headset, DeviceKind::Mouse}) {
            if (std::any_of(items.begin(), items.end(), [=](const WidgetItem& it) { return it.kind == kind; }))
                continue;
            WidgetItem missing;
            missing.kind = kind;
            missing.text = L"\u2014";
            missing.dim = true;
            items.push_back(missing);
            appendTip(kind == DeviceKind::Headset ? L"Headset: no visible battery reading" :
                                                   L"Mouse: no visible battery reading");
        }
        std::stable_sort(items.begin(), items.end(), [](const WidgetItem& a, const WidgetItem& b) {
            return KindOrder(a.kind) < KindOrder(b.kind);
        });
    }
    if (tip.empty()) tip = L"Wireless Battery: no devices with battery info found";
    widget::SetItems(items, tip);
}

void OnResults(std::vector<DeviceStatus>& results) {
    std::map<std::wstring, bool> seen;
    for (auto& s : results) {
        auto& e = g_entries[s.key];
        if (!e.uid) e.uid = StableId(s.key);
        e.st = s;
        if (s.percent >= 0) e.lastPct = s.percent;
        e.lastSeen = GetTickCount64();
        seen[s.key] = true;
        ApplyEntry(e);
    }
    for (auto it = g_entries.begin(); it != g_entries.end();) {
        auto& e = it->second;
        // Some receivers drop sleeping devices from the link; keep showing the last value for a while.
        bool inGrace = !seen.count(it->first) && e.st.graceSec > 0 && e.st.connected &&
                       GetTickCount64() - e.lastSeen < (ULONGLONG)e.st.graceSec * 1000;
        if (inGrace) {
            e.st.percent = -1;
            e.st.asleep = true;
            ApplyEntry(e);
        }
        if (!seen.count(it->first) && !inGrace) {
            if (it->second.added) TrayDelete(it->second.uid);
            if (it->second.icon) DestroyIcon(it->second.icon);
            it = g_entries.erase(it);
        } else {
            ++it;
        }
    }
    UpdatePlaceholder();
    UpdateWidget();
}

// Makes the actual display match the chosen mode; falls back to tray icons if the taskbar
// widget cannot be created (e.g. Explorer not ready yet). Called once a second.
void Reconcile() {
    bool desktop = g_displayMode == DesktopWidget;
    bool alive = widget::Alive();
    bool created = false;
    if (g_displayMode == TrayIcons) {
        widget::Destroy();
        alive = false;
    } else if (!alive || widget::IsDesktop() != desktop) {
        alive = widget::Create(g_inst, g_hwnd, WM_WIDGET_CLICK, g_lightTaskbar, desktop);
        created = alive;
    }
    bool changed = alive != g_useWidget;
    g_useWidget = alive;
    if (changed) {
        for (auto& [k, e] : g_entries) ApplyEntry(e);
        UpdatePlaceholder();
    }
    if (created) {
        UpdateWidget();
    } else if (g_useWidget) {
        widget::Reposition();
    }
}

void ReAddAll() {
    g_placeholder = false;
    for (auto& [k, e] : g_entries) { e.added = false; ApplyEntry(e); }
    UpdatePlaceholder();
}

void RedrawAll() {
    if (g_placeholderIcon) {
        DestroyIcon(g_placeholderIcon);
        g_placeholderIcon = nullptr;
        if (g_placeholder) { TrayDelete(PLACEHOLDER_ID); g_placeholder = false; }
    }
    for (auto& [k, e] : g_entries) { e.iconSig.clear(); ApplyEntry(e); }
    UpdatePlaceholder();
}

void AddChoice(HWND dialog, int control, const wchar_t* label, DWORD value, DWORD selected) {
    LRESULT index = SendDlgItemMessageW(dialog, control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    if (index < 0) return;
    SendDlgItemMessageW(dialog, control, CB_SETITEMDATA, index, value);
    if (value == selected) SendDlgItemMessageW(dialog, control, CB_SETCURSEL, index, 0);
}

DWORD ReadChoice(HWND dialog, int control, DWORD fallback) {
    LRESULT index = SendDlgItemMessageW(dialog, control, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR) return fallback;
    LRESULT value = SendDlgItemMessageW(dialog, control, CB_GETITEMDATA, index, 0);
    return value == CB_ERR ? fallback : static_cast<DWORD>(value);
}

constexpr COLORREF kSurface = RGB(25, 29, 36);
constexpr COLORREF kInk = RGB(225, 230, 237);
constexpr COLORREF kAccent = RGB(158, 186, 220);
constexpr COLORREF kField = RGB(36, 42, 52);
constexpr COLORREF kLine = RGB(66, 76, 89);
constexpr COLORREF kSelection = RGB(46, 60, 78);

void FillArea(HDC dc, const RECT& area, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, &area, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

bool HighContrast() {
    HIGHCONTRASTW settings{sizeof(settings)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(settings), &settings, 0) &&
           (settings.dwFlags & HCF_HIGHCONTRASTON);
}

std::wstring ChoiceLabel(HWND combo, LRESULT index) {
    LRESULT length = index == CB_ERR ? CB_ERR : SendMessageW(combo, CB_GETLBTEXTLEN, index, 0);
    if (length == CB_ERR) return {};
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(text.data()));
    text.resize(static_cast<size_t>(length));
    return text;
}

void PaintOptionControl(HWND control, HDC dc, bool combo) {
    RECT bounds{};
    GetClientRect(control, &bounds);
    auto px = [&](int value) { return MulDiv(value, GetDpiForWindow(control), 96); };
    int id = GetDlgCtrlID(control);
    bool action = id == IDOK || id == IDCANCEL;
    bool focus = GetFocus() == control;
    bool pressed = !combo && (SendMessageW(control, BM_GETSTATE, 0, 0) & BST_PUSHED);
    int state = SaveDC(dc);
    FillArea(dc, bounds, combo || action ? (pressed || id == IDOK ? kSelection : kField) : kSurface);
    SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0)));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kInk);
    RECT label = bounds;
    if (combo || action) {
        SetDCBrushColor(dc, focus || id == IDOK ? kAccent : kLine);
        FrameRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }
    if (combo) {
        label.left += px(8);
        label.right -= px(20);
        SetTextColor(dc, kAccent);
        RECT arrow{label.right, bounds.top, bounds.right - px(4), bounds.bottom};
        DrawTextW(dc, L"\u2304", -1, &arrow, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        SetTextColor(dc, kInk);
        auto text = ChoiceLabel(control, SendMessageW(control, CB_GETCURSEL, 0, 0));
        DrawTextW(dc, text.c_str(), -1, &label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    } else {
        wchar_t text[128]{};
        GetWindowTextW(control, text, ARRAYSIZE(text));
        if (!action) {
            int top = (bounds.bottom - px(13)) / 2;
            RECT mark{bounds.left, top, bounds.left + px(13), top + px(13)};
            bool checked = SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
            bool radio = (GetWindowLongPtrW(control, GWL_STYLE) & BS_TYPEMASK) == BS_AUTORADIOBUTTON;
            SelectObject(dc, GetStockObject(DC_PEN));
            SelectObject(dc, GetStockObject(DC_BRUSH));
            SetDCPenColor(dc, checked ? kAccent : kLine);
            SetDCBrushColor(dc, checked && !radio ? kAccent : kField);
            if (radio) {
                Ellipse(dc, mark.left, mark.top, mark.right, mark.bottom);
                if (checked) {
                    SetDCBrushColor(dc, kAccent);
                    Ellipse(dc, mark.left + px(4), mark.top + px(4), mark.right - px(4), mark.bottom - px(4));
                }
            } else {
                Rectangle(dc, mark.left, mark.top, mark.right, mark.bottom);
                if (checked) {
                    SetTextColor(dc, kSurface);
                    DrawTextW(dc, L"\u2713", -1, &mark, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                    SetTextColor(dc, kInk);
                }
            }
            label.left += px(19);
        }
        UINT flags = DT_SINGLELINE | DT_VCENTER | (action ? DT_CENTER : DT_LEFT);
        if (SendMessageW(control, WM_QUERYUISTATE, 0, 0) & UISF_HIDEACCEL) flags |= DT_HIDEPREFIX;
        DrawTextW(dc, text, -1, &label, flags);
        if (focus && !(SendMessageW(control, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
            InflateRect(&label, -px(2), -px(2));
            DrawFocusRect(dc, &label);
        }
    }
    if (state) RestoreDC(dc, state);
}

LRESULT CALLBACK OptionControlProc(HWND control, UINT message, WPARAM wp, LPARAM lp,
                                  UINT_PTR subclass, DWORD_PTR combo) {
    if (message == WM_NCDESTROY) RemoveWindowSubclass(control, OptionControlProc, subclass);
    if (!HighContrast()) {
        if (message == WM_ERASEBKGND) return TRUE;
        if (message == WM_PAINT) {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(control, &paint);
            PaintOptionControl(control, dc, combo != 0);
            EndPaint(control, &paint);
            return 0;
        }
        if (message == WM_PRINTCLIENT) {
            PaintOptionControl(control, reinterpret_cast<HDC>(wp), combo != 0);
            return 0;
        }
    }
    LRESULT result = DefSubclassProc(control, message, wp, lp);
    switch (message) {
        case BM_SETCHECK: case BM_SETSTATE: case CB_SETCURSEL:
        case WM_SETFOCUS: case WM_KILLFOCUS: case WM_UPDATEUISTATE:
        case WM_KEYDOWN: case WM_KEYUP: case WM_LBUTTONDOWN: case WM_LBUTTONUP:
            InvalidateRect(control, nullptr, FALSE);
            break;
    }
    return result;
}

INT_PTR CALLBACK OptionsProc(HWND dialog, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_INITDIALOG) {
        BOOL dark = !HighContrast();
        DwmSetWindowAttribute(dialog, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        if (dark) {
            DwmSetWindowAttribute(dialog, DWMWA_CAPTION_COLOR, &kSurface, sizeof(kSurface));
            DwmSetWindowAttribute(dialog, DWMWA_TEXT_COLOR, &kInk, sizeof(kInk));
        }
        HFONT heading = CreateFontW(-MulDiv(9, GetDpiForWindow(dialog), 72), 0, 0, 0, FW_SEMIBOLD,
                                    0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        if (heading) {
            SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(heading));
            SendDlgItemMessageW(dialog, IDC_OPTIONS_TITLE, WM_SETFONT, reinterpret_cast<WPARAM>(heading), FALSE);
            SendDlgItemMessageW(dialog, IDC_OPTIONS_MONITORING, WM_SETFONT, reinterpret_cast<WPARAM>(heading), FALSE);
        }
        for (int id : {IDC_MODE_TASKBAR, IDC_MODE_DESKTOP, IDC_MODE_TRAY, IDC_HIDE_ASLEEP,
                       IDC_TRAY_ALERTS, IDC_START_WITH_WINDOWS, IDOK, IDCANCEL,
                       IDC_POLL_INTERVAL, IDC_LOW_BATTERY}) {
            SetWindowSubclass(GetDlgItem(dialog, id), OptionControlProc, 1,
                              id == IDC_POLL_INTERVAL || id == IDC_LOW_BATTERY);
        }
        int display = g_displayMode == DesktopWidget ? IDC_MODE_DESKTOP :
                      g_displayMode == TrayIcons ? IDC_MODE_TRAY : IDC_MODE_TASKBAR;
        CheckRadioButton(dialog, IDC_MODE_TASKBAR, IDC_MODE_TRAY, display);
        for (DWORD seconds : POLL_SECONDS) {
            std::wstring label = std::to_wstring(seconds < 60 ? seconds : seconds / 60)
                               + (seconds < 60 ? L" sec" : L" min");
            AddChoice(dialog, IDC_POLL_INTERVAL, label.c_str(), seconds, g_pollSeconds.load());
        }
        for (DWORD level : LOW_BATTERY_LEVELS) {
            std::wstring label = std::to_wstring(level) + L"%";
            AddChoice(dialog, IDC_LOW_BATTERY, label.c_str(), level, g_lowBattery);
        }
        CheckDlgButton(dialog, IDC_HIDE_ASLEEP, g_hideLastKnown ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_TRAY_ALERTS, g_trayAlerts ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_START_WITH_WINDOWS, AutostartEnabled() ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    }
    if (message == WM_NCDESTROY) {
        HFONT heading = reinterpret_cast<HFONT>(GetWindowLongPtrW(dialog, DWLP_USER));
        if (heading) DeleteObject(heading);
    }
    if (message == WM_DRAWITEM) {
        auto& draw = *reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
        if (draw.CtlType != ODT_COMBOBOX) return FALSE;
        bool selected = draw.itemState & ODS_SELECTED;
        bool contrast = HighContrast();
        FillArea(draw.hDC, draw.rcItem, contrast ? GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW) :
                                                 selected ? kSelection : kField);
        SetTextColor(draw.hDC, contrast ? GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT) : kInk);
        SetBkMode(draw.hDC, TRANSPARENT);
        RECT label = draw.rcItem;
        label.left += MulDiv(7, GetDpiForWindow(dialog), 96);
        auto text = ChoiceLabel(draw.hwndItem, draw.itemID == UINT_MAX ? CB_ERR : static_cast<LRESULT>(draw.itemID));
        DrawTextW(draw.hDC, text.c_str(), -1, &label, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        if (draw.itemState & ODS_FOCUS) DrawFocusRect(draw.hDC, &draw.rcItem);
        return TRUE;
    }
    if ((message == WM_CTLCOLORDLG || message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORLISTBOX) && !HighContrast()) {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetDCBrushColor(dc, message == WM_CTLCOLORLISTBOX ? kField : kSurface);
        SetBkMode(dc, TRANSPARENT);
        int id = GetDlgCtrlID(reinterpret_cast<HWND>(lp));
        SetTextColor(dc, id == IDC_OPTIONS_TITLE || id == IDC_OPTIONS_MONITORING ? kAccent : kInk);
        return reinterpret_cast<INT_PTR>(GetStockObject(DC_BRUSH));
    }
    if (message == WM_COMMAND && HIWORD(wp) == CBN_SELCHANGE && lp)
        InvalidateRect(reinterpret_cast<HWND>(lp), nullptr, FALSE);
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wp) == IDCANCEL)) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    if (message != WM_COMMAND || LOWORD(wp) != IDOK) return FALSE;

    bool saved = true;
    auto save = [&](const wchar_t* name, DWORD current, DWORD requested) {
        if (current == requested || SaveSetting(name, requested)) return requested;
        saved = false;
        return current;
    };
    auto checked = [&](int control) { return IsDlgButtonChecked(dialog, control) == BST_CHECKED; };

    DWORD mode = checked(IDC_MODE_DESKTOP) ? DesktopWidget :
                 checked(IDC_MODE_TRAY) ? TrayIcons : TaskbarWidget;
    g_displayMode = static_cast<DisplayMode>(save(L"DisplayMode", g_displayMode, mode));
    g_hideLastKnown = save(L"HideLastKnownDevices", g_hideLastKnown, checked(IDC_HIDE_ASLEEP)) != 0;
    int previousLevel = g_lowBattery;
    bool previousAlerts = g_trayAlerts;
    g_lowBattery = static_cast<int>(save(L"LowBatteryThreshold", g_lowBattery,
                                       ReadChoice(dialog, IDC_LOW_BATTERY, g_lowBattery)));
    g_trayAlerts = save(L"TrayBatteryAlerts", g_trayAlerts, checked(IDC_TRAY_ALERTS)) != 0;
    if (previousLevel != g_lowBattery || previousAlerts != g_trayAlerts) {
        for (auto& [key, entry] : g_entries) entry.lowWarned = false;
    }

    DWORD previousPoll = g_pollSeconds.load();
    DWORD poll = save(L"PollIntervalSeconds", previousPoll, ReadChoice(dialog, IDC_POLL_INTERVAL, previousPoll));
    if (poll != previousPoll) {
        g_pollSeconds.store(poll);
        SetEvent(g_refreshEvt);
    }

    bool autostart = checked(IDC_START_WITH_WINDOWS);
    if (autostart != AutostartEnabled() && !SetAutostart(autostart)) saved = false;

    Reconcile();
    RedrawAll();
    UpdateWidget();
    if (saved) {
        EndDialog(dialog, IDOK);
    } else {
        MessageBoxW(dialog, L"Some options could not be saved. Try Save again to retry those changes.",
                    kAppName, MB_OK | MB_ICONWARNING);
    }
    return TRUE;
}

enum class MenuRowKind { Action, Device, Heading, Divider };

struct MenuRow {
    MenuRowKind kind;
    UINT id = 0;
    std::wstring label;
    std::wstring detail;
    bool checked = false;
    bool low = false;
};

int MenuPx(int value) {
    UINT dpi = g_hwnd ? GetDpiForWindow(g_hwnd) : GetDpiForSystem();
    return MulDiv(value, dpi ? dpi : 96, 96);
}

void PaintMenuRow(const DRAWITEMSTRUCT& draw, const MenuRow& row) {
    HDC dc = draw.hDC;
    int saved = SaveDC(dc);
    RECT bounds = draw.rcItem;
    bool selected = (draw.itemState & ODS_SELECTED) &&
                    (row.kind == MenuRowKind::Action || row.kind == MenuRowKind::Device);
    bool contrast = HighContrast();
    COLORREF ink = contrast ? GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT) : kInk;
    COLORREF accent = contrast ? ink : kAccent;
    FillArea(dc, bounds, contrast ? GetSysColor(COLOR_MENU) : kSurface);
    if (row.kind == MenuRowKind::Divider) {
        RECT line{bounds.left + MenuPx(12), bounds.top + MenuPx(3),
                  bounds.right - MenuPx(12), bounds.top + MenuPx(4)};
        FillArea(dc, line, contrast ? GetSysColor(COLOR_MENUTEXT) : kLine);
    } else {
        if (selected) {
            RECT selection{bounds.left + MenuPx(4), bounds.top + MenuPx(1),
                           bounds.right - MenuPx(4), bounds.bottom - MenuPx(1)};
            FillArea(dc, selection, contrast ? GetSysColor(COLOR_HIGHLIGHT) : kSelection);
        }
        HFONT font = CreateFontW(-MenuPx(row.kind == MenuRowKind::Heading ? 11 : 13), 0, 0, 0,
                                  row.kind == MenuRowKind::Heading ? FW_SEMIBOLD : FW_NORMAL, 0, 0, 0,
                                  DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        HGDIOBJ original = font ? SelectObject(dc, font) : nullptr;
        SetBkMode(dc, TRANSPARENT);
        constexpr UINT flags = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS;
        RECT label = bounds;
        label.left += MenuPx(row.kind == MenuRowKind::Device ? 31 : 14);
        label.right -= MenuPx(14);
        if (!row.detail.empty()) {
            SIZE detailSize{};
            GetTextExtentPoint32W(dc, row.detail.c_str(), static_cast<int>(row.detail.size()), &detailSize);
            label.right -= std::clamp(static_cast<int>(detailSize.cx) + MenuPx(12), MenuPx(54), MenuPx(133));
        }
        SetTextColor(dc, row.kind == MenuRowKind::Heading ? accent : ink);
        DrawTextW(dc, row.label.c_str(), -1, &label, flags);
        if (!row.detail.empty()) {
            RECT detail{label.right + MenuPx(7), bounds.top, bounds.right - MenuPx(14), bounds.bottom};
            SetTextColor(dc, row.low && !contrast ? RGB(241, 151, 157) : accent);
            DrawTextW(dc, row.detail.c_str(), -1, &detail, flags | DT_RIGHT);
        }
        if (row.checked) {
            RECT check{bounds.left + MenuPx(12), bounds.top, label.left, bounds.bottom};
            SetTextColor(dc, accent);
            DrawTextW(dc, L"\u2713", -1, &check, flags);
        }
        if (original) SelectObject(dc, original);
        if (font) DeleteObject(font);
    }
    if (saved) RestoreDC(dc, saved);
}

void ShowMenu() {
    std::vector<std::wstring> keys;
    std::vector<MenuRow> rows{{MenuRowKind::Action, ID_REFRESH, L"Refresh now"},
                              {MenuRowKind::Action, ID_OPTIONS, L"Options\u2026"},
                              {MenuRowKind::Divider},
                              {MenuRowKind::Heading, 0, g_entries.empty() ? L"NO BATTERY READINGS" : L"SHOW DEVICES"}};
    for (auto& [k, e] : g_entries) {
        std::wstring label = e.st.name;
        for (auto& character : label) if (iswspace(character)) character = L' ';
        rows.push_back({MenuRowKind::Device, ID_DEVICE_FIRST + (UINT)keys.size(), std::move(label),
                        StatusText(e), !IsHidden(k),
                        e.st.percent >= 0 && e.st.percent <= g_lowBattery && !e.st.charging});
        keys.push_back(k);
    }
    rows.push_back({MenuRowKind::Divider});
    rows.push_back({MenuRowKind::Action, ID_EXIT, L"Exit"});
    HMENU m = CreatePopupMenu();
    if (!m) return;
    for (auto& row : rows) {
        MENUITEMINFOW item{sizeof(item)};
        item.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STATE | MIIM_DATA | MIIM_STRING;
        item.fType = MFT_OWNERDRAW;
        item.wID = row.id;
        item.fState = row.kind == MenuRowKind::Divider || row.kind == MenuRowKind::Heading ? MFS_DISABLED :
                      row.checked ? MFS_CHECKED : MFS_ENABLED;
        item.dwTypeData = row.label.data();
        item.dwItemData = reinterpret_cast<ULONG_PTR>(&row);
        InsertMenuItemW(m, GetMenuItemCount(m), TRUE, &item);
    }
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    UINT cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
    switch (cmd) {
        case ID_REFRESH: SetEvent(g_refreshEvt); break;
        case ID_OPTIONS: DialogBoxParamW(g_inst, MAKEINTRESOURCEW(IDD_OPTIONS), g_hwnd, OptionsProc, 0); break;
        case ID_EXIT: DestroyWindow(g_hwnd); break;
        default:
            if (cmd >= ID_DEVICE_FIRST && cmd < ID_DEVICE_FIRST + keys.size()) {
                const std::wstring& key = keys[cmd - ID_DEVICE_FIRST];
                if (!g_hidden.erase(key)) g_hidden.insert(key);
                SaveHidden();
                auto it = g_entries.find(key);
                if (it != g_entries.end()) ApplyEntry(it->second);
                UpdatePlaceholder();
                UpdateWidget();
            }
    }
}

// ---- worker thread ----------------------------------------------------------------------------

DWORD WINAPI Worker(LPVOID) {
    for (;;) {
        auto* v = new std::vector<DeviceStatus>(PollAllDevices());
        if (!PostMessageW(g_hwnd, WM_RESULTS, 0, (LPARAM)v)) delete v;
        HANDLE hs[2] = { g_stopEvt, g_refreshEvt };
        if (WaitForMultipleObjects(2, hs, FALSE, g_pollSeconds.load() * 1000) == WAIT_OBJECT_0) return 0;
    }
}

void RefreshSoon(UINT ms) { SetTimer(g_hwnd, TIMER_DEBOUNCE, ms, nullptr); }

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g_taskbarCreatedMsg && msg != 0) {  // Explorer restarted
        widget::Destroy();
        g_useWidget = false;
        ReAddAll();
        Reconcile();
        return 0;
    }
    switch (msg) {
        case WM_MEASUREITEM: {
            auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
            if (measure->CtlType != ODT_MENU || !measure->itemData) break;
            auto& row = *reinterpret_cast<const MenuRow*>(measure->itemData);
            measure->itemWidth = MenuPx(340);
            measure->itemHeight = MenuPx(row.kind == MenuRowKind::Divider ? 8 :
                                        row.kind == MenuRowKind::Heading ? 22 : 29);
            return TRUE;
        }
        case WM_DRAWITEM: {
            auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
            if (draw->CtlType != ODT_MENU || !draw->itemData) break;
            PaintMenuRow(*draw, *reinterpret_cast<const MenuRow*>(draw->itemData));
            return TRUE;
        }
        case WM_MENUCHAR: {
            int key = towlower(LOWORD(wp));
            UINT command = key == L'r' ? ID_REFRESH : key == L'o' ? ID_OPTIONS : key == L'x' ? ID_EXIT : 0;
            if (!command) break;
            HMENU menu = reinterpret_cast<HMENU>(lp);
            int count = GetMenuItemCount(menu);
            for (int i = 0; i < count; ++i)
                if (GetMenuItemID(menu, i) == command) return MAKELRESULT(i, MNC_EXECUTE);
            break;
        }
        case WM_RESULTS: {
            auto* v = reinterpret_cast<std::vector<DeviceStatus>*>(lp);
            OnResults(*v);
            delete v;
            return 0;
        }
        case WM_TRAY:
            switch (LOWORD(lp)) {
                case WM_CONTEXTMENU:
                case NIN_SELECT:
                case NIN_KEYSELECT:
                    if (IsWindowEnabled(h)) ShowMenu();
                    break;
            }
            return 0;
        case WM_WIDGET_CLICK:
            if (IsWindowEnabled(h)) ShowMenu();
            return 0;
        case WM_ENDSESSION:  // sign-out / shutdown / installer restart manager
            if (wp) DestroyWindow(h);
            return 0;
        case WM_DEVICECHANGE:
            if (wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE) RefreshSoon(2500);
            return TRUE;
        case WM_POWERBROADCAST:
            if (wp == PBT_APMRESUMEAUTOMATIC) RefreshSoon(5000);
            return TRUE;
        case WM_TIMER:
            if (wp == TIMER_DEBOUNCE) { KillTimer(h, TIMER_DEBOUNCE); SetEvent(g_refreshEvt); }
            if (wp == TIMER_WIDGET) Reconcile();
            return 0;
        case WM_SETTINGCHANGE:
            if (lp && !lstrcmpW((LPCWSTR)lp, L"ImmersiveColorSet")) {
                bool light = ReadLightTaskbar();
                if (light != g_lightTaskbar) { g_lightTaskbar = light; RedrawAll(); widget::SetLightTheme(light); }
            }
            return 0;
        case WM_DESTROY:
            KillTimer(h, TIMER_WIDGET);
            widget::Destroy();
            SetEvent(g_stopEvt);
            if (g_worker) WaitForSingleObject(g_worker, 5000);
            for (auto& [k, e] : g_entries) {
                if (e.added) TrayDelete(e.uid);
                if (e.icon) DestroyIcon(e.icon);
            }
            if (g_placeholder) TrayDelete(PLACEHOLDER_ID);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// `WirelessBattery.exe --list` prints what it finds, `--dump` every HID collection.
int ListToConsole(bool dump) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!out || out == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    auto write = [&](const std::wstring& s) {
        int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
        std::string u(n, 0);
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), u.data(), n, nullptr, nullptr);
        DWORD w;
        WriteFile(out, u.data(), (DWORD)u.size(), &w, nullptr);
    };
    if (dump) {
        write(DumpHidCollections());
        return 0;
    }
    auto list = PollAllDevices();
    if (list.empty()) write(L"No devices with battery info found.\n");
    for (auto& s : list) {
        std::wstring line = s.name + L"  [" + s.via + L"]  ";
        if (!s.connected) line += L"off / not connected";
        else if (s.asleep) line += L"asleep";
        else if (!s.levelText.empty()) line += s.levelText;
        else if (s.percent >= 0) line += std::to_wstring(s.percent) + L"%";
        else line += L"unknown";
        if (s.charging) line += L", charging";
        if (s.millivolts) line += L", " + std::to_wstring(s.millivolts) + L" mV";
        write(line + L"   (" + s.key + L")\n");
    }
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    if (cmdLine && wcsstr(cmdLine, L"--list")) return ListToConsole(false);
    if (cmdLine && wcsstr(cmdLine, L"--dump")) return ListToConsole(true);
    if (cmdLine && wcsstr(cmdLine, L"--quit")) {  // used by the installer before upgrading/uninstalling
        if (HWND w = FindWindowW(L"WirelessBatteryWnd", nullptr)) {
            PostMessageW(w, WM_CLOSE, 0, 0);
            for (int i = 0; i < 50 && FindWindowW(L"WirelessBatteryWnd", nullptr); i++) Sleep(100);
        }
        return 0;
    }
    g_inst = inst;

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\WirelessBatteryTray");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    g_lightTaskbar = ReadLightTaskbar();
    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"WirelessBatteryWnd";
    RegisterClassExW(&wc);
    // A hidden top-level window (not message-only) so it receives the TaskbarCreated broadcast.
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, kAppName, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    if (!g_hwnd) return 1;

    DEV_BROADCAST_DEVICEINTERFACE_W filter{ sizeof(filter) };
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid = GUID_DEVINTERFACE_HID;
    HDEVNOTIFY notify = RegisterDeviceNotificationW(g_hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);

    g_refreshEvt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_stopEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD legacyMode = LoadSetting(L"TaskbarWidget", true) ? TaskbarWidget : TrayIcons;
    DWORD mode = LoadSetting(L"DisplayMode", legacyMode);
    g_displayMode = static_cast<DisplayMode>(mode <= DesktopWidget ? mode : legacyMode);
    g_hideLastKnown = LoadSetting(L"HideLastKnownDevices", false) != 0;
    g_pollSeconds.store(LoadPreset(L"PollIntervalSeconds", DEFAULT_POLL_SECONDS, POLL_SECONDS));
    g_lowBattery = static_cast<int>(LoadPreset(L"LowBatteryThreshold", DEFAULT_LOW_BATTERY, LOW_BATTERY_LEVELS));
    g_trayAlerts = LoadSetting(L"TrayBatteryAlerts", true) != 0;
    LoadHidden();
    Reconcile();
    UpdatePlaceholder();  // something to click on until the first poll finishes
    SetTimer(g_hwnd, TIMER_WIDGET, 1000, nullptr);
    g_worker = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    // drain results posted after shutdown started
    while (PeekMessageW(&m, nullptr, WM_RESULTS, WM_RESULTS, PM_REMOVE)) delete reinterpret_cast<std::vector<DeviceStatus>*>(m.lParam);
    if (notify) UnregisterDeviceNotification(notify);
    if (g_worker) CloseHandle(g_worker);
    ReleaseMutex(mutex);
    return 0;
}
