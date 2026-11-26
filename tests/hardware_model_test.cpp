#include "../src/hardware_model.h"
#include <cassert>
#include <cstdio>
using namespace hardware_model;

int main() {
    // Adapter tag: how "\GPU Engine(*)" instance names spell the LUID.
    const std::wstring tag = LuidTag(0, 0xD1E4);
    assert(tag == L"luid_0x00000000_0x0000d1e4");
    assert(MatchesAdapter(L"pid_42_luid_0x00000000_0x0000D1E4_phys_0_eng_0_engtype_3D", tag));  // case-insensitive
    assert(!MatchesAdapter(L"pid_42_luid_0x00000000_0x0000AAAA_phys_0_eng_0_engtype_3D", tag));
    assert(!MatchesAdapter(L"pid_42_luid_0x00000000_0x0000D1E4_phys_0_eng_0_engtype_3D", L""));

    // GPU usage: processes add up per engine, the busiest engine wins, other adapters are ignored.
    std::vector<CounterSample> engines = {
        { L"pid_1_luid_0x00000000_0x0000D1E4_phys_0_eng_0_engtype_3D", 30 },
        { L"pid_2_luid_0x00000000_0x0000D1E4_phys_0_eng_0_engtype_3D", 25 },
        { L"pid_1_luid_0x00000000_0x0000D1E4_phys_0_eng_3_engtype_VideoDecode", 40 },
        { L"pid_3_luid_0x00000000_0x0000AAAA_phys_0_eng_0_engtype_3D", 99 },  // the integrated GPU
    };
    assert(GpuUsage(engines, tag) == 55);
    const std::vector<CounterSample> busy = {
        { L"pid_1_luid_0x00000000_0x0000D1E4_phys_0_eng_0_engtype_3D", 70 },
        { L"pid_2_luid_0x00000000_0x0000D1E4_phys_0_eng_0_engtype_3D", 60 },
    };
    assert(GpuUsage(busy, tag) == 100);  // capped
    assert(GpuUsage(engines, LuidTag(0, 0x1234)) == -1);  // adapter not in the list
    assert(GpuUsage(std::vector<CounterSample>(), tag) == -1);
    // Two physical parts of one adapter stay separate engines.
    const std::vector<CounterSample> linked = {
        { L"pid_1_luid_0x00000000_0x0000D1E4_phys_0_eng_0_engtype_3D", 20 },
        { L"pid_1_luid_0x00000000_0x0000D1E4_phys_1_eng_0_engtype_3D", 35 },
    };
    assert(GpuUsage(linked, tag) == 35);
    // Engine numbers past 9 are their own engines (eng_1 is not eng_12); a name without an engine is skipped.
    const std::vector<CounterSample> many = {
        { L"pid_1_luid_0x00000000_0x0000D1E4_phys_0_eng_1_engtype_Copy", 30 },
        { L"pid_2_luid_0x00000000_0x0000D1E4_phys_0_eng_12_engtype_Compute", 45 },
        { L"pid_3_luid_0x00000000_0x0000D1E4_phys_0_eng_12_engtype_Compute", 5 },
        { L"pid_4_luid_0x00000000_0x0000D1E4_phys_0", 90 },
    };
    assert(GpuUsage(many, tag) == 50);

    // Video memory: bytes for the adapter, as a share of its dedicated memory.
    std::vector<CounterSample> memory = {
        { L"luid_0x00000000_0x0000D1E4_phys_0", 2.0e9 },
        { L"luid_0x00000000_0x0000AAAA_phys_0", 5.0e8 },
    };
    assert(AdapterBytes(memory, tag) == 2.0e9);
    assert(AdapterBytes(memory, LuidTag(0, 0x1234)) == -1);
    assert(Percent(AdapterBytes(memory, tag), 8.0e9) == 25);
    assert(Percent(-1, 8.0e9) == -1);
    assert(Percent(1, 0) == -1);

    // CPU: kernel time includes idle time.
    assert(CpuUsage(750, 900, 100) == 25);
    assert(CpuUsage(0, 0, 0) == -1);
    assert(CpuUsage(2000, 900, 100) == -1);  // inconsistent counters
    assert(CpuUsage(0, 600, 400) == 100);

    // Temperatures and their colours.
    assert(!PlausibleTemperature(0) && PlausibleTemperature(35) && !PlausibleTemperature(150) && !PlausibleTemperature(-5));
    assert(GpuHeat(79) == Heat::Normal && GpuHeat(80) == Heat::Warm && GpuHeat(90) == Heat::Hot);
    assert(SsdHeat(64) == Heat::Normal && SsdHeat(65) == Heat::Warm && SsdHeat(75) == Heat::Hot);

    // Disk activity: the busiest disk that is in both readings (times in 100 ns units).
    const std::vector<DiskTimes> disksBefore = { { 0, 1000, 10000 }, { 1, 5000, 10000 }, { 2, 0, 0 } };
    const std::vector<DiskTimes> disksNow = {
        { 0, 10000, 20000 },  // idle 9000 of 10000: 10% active
        { 1, 8500, 20000 },   // idle 3500 of 10000: 65% active
        { 3, 0, 99999 },      // just plugged in: no baseline
    };
    assert(DiskActivity(disksBefore, disksNow) == 65);
    const std::vector<DiskTimes> overshootNow = { { 0, 11000, 20000 } };  // idle a little past the clock
    assert(DiskActivity(disksBefore, overshootNow) == 0);
    const std::vector<DiskTimes> stalledNow = { { 0, 1000, 10000 } };  // no time passed
    assert(DiskActivity(disksBefore, stalledNow) == -1);
    assert(DiskActivity(std::vector<DiskTimes>(), disksNow) == -1);

    // CPU power: only the package meters count; the per-core ones are already part of them.
    assert(IsPackageMeter(L"RAPL_Package0_PKG") && IsPackageMeter(L"rapl_package1_pkg"));
    assert(!IsPackageMeter(L"RAPL_Package0_Core3_CORE") && !IsPackageMeter(L"PKG") && !IsPackageMeter(L""));
    // 50 W for one second: 50 / 3600 Wh = 13,888,888,889 pWh over 10,000,000 ticks of 100 ns.
    const std::vector<EnergyReading> energyBefore = { { 1, 1000000000000ull, 5000000 }, { 2, 0, 5000000 } };
    const std::vector<EnergyReading> energyNow = { { 1, 1013888888889ull, 15000000 }, { 2, 6944444444ull, 15000000 } };
    assert(CpuPackageWatts(energyBefore, energyNow) == 75);  // two processors: 50 W + 25 W
    const std::vector<EnergyReading> oneNow = { { 1, 1013888888889ull, 15000000 } };
    assert(CpuPackageWatts(energyBefore, oneNow) == 50);
    const std::vector<EnergyReading> resetNow = { { 1, 5, 15000000 } };  // the meter's count went back
    assert(CpuPackageWatts(energyBefore, resetNow) == -1);
    const std::vector<EnergyReading> absurdNow = { { 1, 1000000000000ull + 1000000000000000ull, 15000000 } };
    assert(CpuPackageWatts(energyBefore, absurdNow) == -1);  // megawatts: not a real reading
    assert(CpuPackageWatts(std::vector<EnergyReading>(), energyNow) == -1);
    assert(PlausibleWatts(0) == 0 && PlausibleWatts(450) == 450 && PlausibleWatts(-1) == -1);

    // GPU power: the driver's tenths of a percent of the power limit; 0 means it reports none.
    assert(PowerSharePercent(128) == 13 && PowerSharePercent(1000) == 100 && PowerSharePercent(1104) == 110);
    assert(PowerSharePercent(0) == -1 && PowerSharePercent(2001) == -1);

    // Network: only interfaces present both times count; a counter that went back adds nothing.
    const std::vector<InterfaceOctets> before = { { 1, 1000, 500 }, { 2, 9000, 9000 } };
    const std::vector<InterfaceOctets> now = { { 1, 6000, 700 }, { 2, 100, 9100 }, { 3, 5000000000ull, 5000000000ull } };
    unsigned long long in = 0, out = 0;
    OctetsMoved(before, now, in, out);
    assert(in == 5000 && out == 300);  // interface 2's received counter went back; 3 is new
    OctetsMoved(std::vector<InterfaceOctets>(), now, in, out);
    assert(in == 0 && out == 0);

    // Network speed text, and the rounding that keeps it from redrawing when the text would not change.
    assert(RoundRate(-1) == -1);
    assert(RoundRate(0) == 0 && RoundRate(499) == 0 && RoundRate(640400) == 640000);
    assert(RoundRate(1234567) == 1200000 && RoundRate(45.4e6) == 45000000 && RoundRate(1.26e9) == 1300000000);
    assert(RoundRate(9.0e12) == INT_MAX);
    assert(RateText(0) == L"0K" && RateText(640000) == L"640K" && RateText(999000) == L"999K");
    assert(RateText(1000000) == L"1.0M" && RateText(1200000) == L"1.2M" && RateText(45000000) == L"45M");
    assert(RateText(1300000000) == L"1.3G");
    const double rates[] = { 0, 812, 640400, 999700, 1234567, 9960000, 45.4e6, 999.6e6, 1.26e9 };
    for (double b : rates) assert(RateText(RoundRate(b)) == RateText(RoundRate(RoundRate(b))));  // stable

    puts("Hardware reading tests passed");
}
