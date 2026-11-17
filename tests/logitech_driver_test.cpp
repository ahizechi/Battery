// Exercise Logitech collection selection and battery requests without attached hardware.
#include "../src/drivers/drivers.h"

#include <cassert>
#include <cstdio>

namespace {
std::vector<BYTE> reply;
bool adcOnly = false;
bool charging = false;
constexpr int kHeadsetVoltage = 3748;

HidColl Collection(USHORT pid, USHORT usage, USHORT length) {
    HidColl c;
    c.vid = 0x046D;
    c.pid = pid;
    c.caps.UsagePage = 0xFF43;
    c.caps.Usage = usage;
    c.caps.InputReportByteLength = length;
    c.caps.OutputReportByteLength = length;
    return c;
}
}  // namespace

bool HidDev::Open(const HidColl& c) {
    c_ = c;
    h_ = reinterpret_cast<HANDLE>(1);
    return true;
}
void HidDev::Close() { h_ = INVALID_HANDLE_VALUE; }
bool HidDev::Write(const std::vector<BYTE>& request, int) {
    BYTE index = request[1], feature = request[2], function = request[3];
    // Both the C54F receiver and its paired mouse answer the version ping.
    reply = {request[0], index, feature, function, 0, 0, 0};
    if (feature == 0 && (function >> 4) == 1 && (index == 0xFF || index == 1)) {
        reply[4] = 4;
    } else if (adcOnly && feature == 0 && request[4] == 0x1F && request[5] == 0x20) {
        reply[4] = 2;
    } else if (adcOnly && feature == 2 && (function >> 4) == 0) {
        reply[4] = kHeadsetVoltage >> 8;
        reply[5] = kHeadsetVoltage & 0xFF;
        reply[6] = charging ? 3 : 1;
    } else if (!adcOnly && feature == 0 && request[4] == 0x10 && request[5] == 0x04) {
        reply[4] = 2;
    } else if (feature == 2 && (function >> 4) == 1) {
        reply[4] = 42;
    } else if (index > 1 && index != 0xFF) {
        reply = {request[0], index, 0xFF, feature, function, 0x06, 0};
    }
    return true;
}
int HidDev::Read(std::vector<BYTE>& out, int) {
    out.swap(reply);
    reply.clear();
    return static_cast<int>(out.size());
}
std::wstring HidProduct(const HidColl&) { return L"Logitech USB Device"; }
std::wstring Hex4(USHORT v) {
    wchar_t text[8];
    swprintf(text, 8, L"%04X", v);
    return text;
}
int Interpolate(int mv, const int* voltages, const int* percentages, int count) {
    assert(mv == kHeadsetVoltage);
    for (int i = 1; i < count; ++i) {
        assert(voltages[i - 1] > voltages[i]);
        if (mv >= voltages[i]) {
            return percentages[i] + (mv - voltages[i]) * (percentages[i - 1] - percentages[i]) /
                                        (voltages[i - 1] - voltages[i]);
        }
    }
    return percentages[count - 1];
}
DeviceKind GuessKind(const std::wstring&) { return DeviceKind::Other; }

int main() {
    PollContext receiver;
    receiver.colls = {Collection(0xC54F, 0x0301, 7), Collection(0xC54F, 0x0302, 20)};
    PollLogitech(receiver);
    assert(receiver.out.size() == 1 && receiver.out[0].key == L"logi:C54F:1");
    assert(receiver.out[0].percent == 42 && receiver.out[0].kind == DeviceKind::Mouse);

    PollContext shortOnly;
    shortOnly.colls = {Collection(0xC54F, 0x0301, 7)};
    PollLogitech(shortOnly);
    assert(shortOnly.out.size() == 1 && shortOnly.out[0].percent == 42);

    PollContext wired;
    wired.colls = {Collection(0xC0A9, 0x0302, 20)};
    PollLogitech(wired);
    assert(wired.out.size() == 1 && wired.out[0].key == L"logi:C0A9:255");
    assert(wired.out[0].percent == 42 && wired.out[0].kind == DeviceKind::Mouse);

    PollContext headset;
    headset.colls = {Collection(0x0AA7, 0x0202, 20)};
    PollLogitech(headset);
    assert(headset.out.size() == 1 && headset.out[0].kind == DeviceKind::Headset);

    adcOnly = true;
    PollContext proX;
    proX.colls = {Collection(0x0ABA, 0x0202, 20)};
    PollLogitech(proX);
    assert(proX.out.size() == 1 && proX.out[0].millivolts == kHeadsetVoltage);
    assert(proX.out[0].percent == 45 && !proX.out[0].charging);

    charging = true;
    proX.out.clear();
    proX.claimed.clear();
    PollLogitech(proX);
    assert(proX.out.size() == 1 && proX.out[0].percent == 22 && proX.out[0].charging);

    charging = false;
    headset.out.clear();
    headset.claimed.clear();
    PollLogitech(headset);
    assert(headset.out.size() == 1 && headset.out[0].percent == 22);
    puts("Logitech driver tests passed (Superstrike, headset detection, PRO X discharge calibration).");
}
