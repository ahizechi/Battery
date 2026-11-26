#pragma once
// Pure helpers behind the hardware readings (usage, temperatures, power, network speed), kept free of
// Windows calls so tests/hardware_model_test.cpp can check them. hardware.cpp does the reading.
#include <algorithm>
#include <climits>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

namespace hardware_model {

// One instance of a wildcard performance counter, e.g. "\GPU Engine(*)\Utilization Percentage". The name points
// into the buffer it was read from (a few hundred instances every refresh, so they are not copied).
struct CounterSample {
    const wchar_t* instance;  // "pid_1234_luid_0x00000000_0x0000D1E4_phys_0_eng_3_engtype_3D"
    double value = 0;
};

inline std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)std::towlower(c);
    return s;
}

// How the GPU performance counters name an adapter: "luid_0x<high>_0x<low>", compared case-insensitively.
inline std::wstring LuidTag(long high, unsigned long low) {
    wchar_t buf[40];
    std::swprintf(buf, 40, L"luid_0x%08lx_0x%08lx", (unsigned long)high, low);
    return buf;
}

inline wchar_t AsciiLower(wchar_t c) { return c >= L'A' && c <= L'Z' ? (wchar_t)(c + 32) : c; }

// Where lower-case ASCII `needle` starts in `hay`, ignoring case; null when it is not there.
inline const wchar_t* FindNoCase(const wchar_t* hay, const wchar_t* needle) {
    if (!hay || !*needle) return nullptr;
    for (; *hay; hay++) {
        size_t i = 0;
        while (needle[i] && AsciiLower(hay[i]) == needle[i]) i++;
        if (!needle[i]) return hay;
    }
    return nullptr;
}

inline bool MatchesAdapter(const wchar_t* instance, const std::wstring& luidTag) {
    return !luidTag.empty() && FindNoCase(instance, luidTag.c_str());
}

// Reads the decimal number at `p` (at most 4 digits); -1 when there is none.
inline int ReadIndex(const wchar_t*& p) {
    int n = -1;
    for (int digits = 0; digits < 4 && *p >= L'0' && *p <= L'9'; digits++, p++) n = (n < 0 ? 0 : n * 10) + (*p - L'0');
    return n;
}

// GPU usage like Task Manager: per engine ("phys_<part>_eng_<n>"), add up every process's share; the busiest
// engine is the GPU's usage. -1 when no instance belongs to the adapter. Graphics drivers number parts from 0
// and engines below 64; an instance outside that is skipped.
inline int GpuUsage(const std::vector<CounterSample>& engines, const std::wstring& luidTag) {
    constexpr int kParts = 4, kEngines = 64;
    double perEngine[kParts * kEngines] = {};
    bool any = false;
    for (const auto& s : engines) {
        if (!MatchesAdapter(s.instance, luidTag)) continue;
        const wchar_t* p = FindNoCase(s.instance, L"phys_");
        if (!p) continue;
        p += 5;
        int part = ReadIndex(p);
        if (part < 0 || part >= kParts || FindNoCase(p, L"_eng_") != p) continue;
        p += 5;
        int eng = ReadIndex(p);
        if (eng < 0 || eng >= kEngines) continue;
        perEngine[part * kEngines + eng] += s.value;
        any = true;
    }
    if (!any) return -1;
    double busiest = 0;
    for (double e : perEngine) busiest = std::max(busiest, e);
    return (int)std::clamp(busiest + 0.5, 0.0, 100.0);
}

// Bytes reported by "\GPU Adapter Memory(*)\Dedicated Usage" for the adapter; -1 when none match.
inline double AdapterBytes(const std::vector<CounterSample>& memory, const std::wstring& luidTag) {
    double sum = 0;
    bool found = false;
    for (const auto& s : memory)
        if (MatchesAdapter(s.instance, luidTag)) { sum += s.value; found = true; }
    return found ? sum : -1;
}

// Rounded share of `part` in `whole`, 0..100; -1 when either is unknown.
inline int Percent(double part, double whole) {
    if (part < 0 || whole <= 0) return -1;
    return (int)std::clamp(part * 100.0 / whole + 0.5, 0.0, 100.0);
}

// CPU usage from two GetSystemTimes calls: the kernel time includes the idle time.
inline int CpuUsage(unsigned long long idleDelta, unsigned long long kernelDelta, unsigned long long userDelta) {
    unsigned long long total = kernelDelta + userDelta;
    if (total == 0 || idleDelta > total) return -1;
    return Percent((double)(total - idleDelta), (double)total);
}

// A physical disk's idle and clock time from the disk driver (DISK_PERFORMANCE), in 100 ns units.
struct DiskTimes {
    unsigned long id = 0;  // the disk number
    long long idle = 0, clock = 0;
};

// Disk activity like Task Manager's "Active time": the busiest disk since `before`, counting only disks
// in both lists. -1 when there is none.
inline int DiskActivity(const std::vector<DiskTimes>& before, const std::vector<DiskTimes>& now) {
    int busiest = -1;
    for (const auto& n : now)
        for (const auto& b : before)
            if (b.id == n.id) {
                long long clock = n.clock - b.clock, idle = n.idle - b.idle;
                if (clock > 0 && idle >= 0)
                    busiest = std::max(busiest, (int)std::clamp(100.0 - idle * 100.0 / clock + 0.5, 0.0, 100.0));
                break;
            }
    return busiest;
}

// Watts that make sense for a CPU or GPU; anything else is a meter that is absent or not ready.
inline int PlausibleWatts(int w) { return w >= 0 && w < 2000 ? w : -1; }

// Windows' energy meters (the Energy Metering Interface) name the processor's own RAPL meters like
// "RAPL_Package0_PKG" (the whole package) and "RAPL_Package0_Core3_CORE" (already part of it).
inline bool IsPackageMeter(const std::wstring& channel) {
    std::wstring n = Lower(channel);
    return n.size() >= 4 && n.compare(n.size() - 4, 4, L"_pkg") == 0;
}

// One package meter's running totals: energy in picowatt-hours, its clock in 100 ns units.
struct EnergyReading {
    unsigned id = 0;  // which meter and channel
    unsigned long long energy = 0, clock = 0;
};

// CPU power in whole watts since `before`: the package meters' energy over their time, added up for PCs
// with more than one processor. -1 when no meter is in both lists.
inline int CpuPackageWatts(const std::vector<EnergyReading>& before, const std::vector<EnergyReading>& now) {
    double watts = 0;
    bool found = false;
    for (const auto& n : now)
        for (const auto& b : before)
            if (b.id == n.id) {
                if (n.clock > b.clock && n.energy >= b.energy) {
                    // 1 pWh = 3.6e-9 J and the clock counts 1e-7 s: W = pWh * 0.036 / ticks.
                    watts += (double)(n.energy - b.energy) * 0.036 / (double)(n.clock - b.clock);
                    found = true;
                }
                break;
            }
    return found ? PlausibleWatts((int)(watts + 0.5)) : -1;
}

// The GPU's power draw as a whole percent of its power limit, from the driver's tenths of a percent
// (D3DKMT_ADAPTER_PERFDATA::Power). 0 means the driver does not report it; above 200% is not real.
inline int PowerSharePercent(unsigned long tenths) {
    return tenths == 0 || tenths > 2000 ? -1 : (int)((tenths + 5) / 10);
}

// Byte counters of one network interface, from the interface table.
struct InterfaceOctets {
    unsigned long long id = 0;  // the interface's lasting identity (its LUID)
    unsigned long long in = 0, out = 0;
};

// Bytes received and sent since `before`, counting only interfaces in both lists: one that just appeared
// (a reconnecting Wi-Fi adapter) brings its whole count since boot, and one whose counter went back
// (reset) has no usable difference.
inline void OctetsMoved(const std::vector<InterfaceOctets>& before, const std::vector<InterfaceOctets>& now,
                        unsigned long long& in, unsigned long long& out) {
    in = out = 0;
    for (const auto& n : now)
        for (const auto& b : before)
            if (b.id == n.id) {
                if (n.in >= b.in) in += n.in - b.in;
                if (n.out >= b.out) out += n.out - b.out;
                break;
            }
}

// A network speed in bytes per second, rounded to what RateText shows, so the strip only redraws when
// the text changes. -1 when unknown.
inline int RoundRate(double bytesPerSecond) {
    if (bytesPerSecond < 0) return -1;
    double step = bytesPerSecond < 1e6 ? 1e3 : bytesPerSecond < 1e7 ? 1e5 : bytesPerSecond < 1e9 ? 1e6 : 1e8;
    double r = (double)(long long)(bytesPerSecond / step + 0.5) * step;
    return (int)std::min(r, (double)INT_MAX);
}

// "0K", "640K", "1.2M", "45M", "1.2G": decimal kilo/mega/gigabytes per second, like browsers and Steam.
inline std::wstring RateText(int bytesPerSecond) {
    wchar_t buf[16];
    double b = bytesPerSecond < 0 ? 0 : (double)bytesPerSecond;
    if (b < 999.5e3) std::swprintf(buf, 16, L"%.0fK", b / 1e3);
    else if (b < 9.95e6) std::swprintf(buf, 16, L"%.1fM", b / 1e6);
    else if (b < 999.5e6) std::swprintf(buf, 16, L"%.0fM", b / 1e6);
    else std::swprintf(buf, 16, L"%.1fG", b / 1e9);
    return buf;
}

// Temperatures in whole degrees Celsius; anything outside 1..149 is a sensor that is absent or not ready.
inline bool PlausibleTemperature(int c) { return c > 0 && c < 150; }

enum class Heat { Normal, Warm, Hot };
// Orange from 80 C and red from 90 C for a GPU; 65 / 75 C for an SSD (NVMe drives throttle near 80).
inline Heat GpuHeat(int c) { return c >= 90 ? Heat::Hot : c >= 80 ? Heat::Warm : Heat::Normal; }
inline Heat SsdHeat(int c) { return c >= 75 ? Heat::Hot : c >= 65 ? Heat::Warm : Heat::Normal; }

}  // namespace hardware_model
