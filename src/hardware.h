#pragma once
// Hardware readings shown after the batteries on the taskbar strip: CPU, GPU, RAM, video memory and disk
// usage, GPU and SSD temperature, CPU power (watts) and GPU power (percent of its limit), and network
// speed. Everything comes from documented Windows interfaces that need no driver and no admin rights
// (performance counters, DXGI, D3DKMT, the storage temperature query, the network interface table).
// GPU temperature and power are only what the graphics driver reports.
// There is no CPU temperature: Windows only exposes it through a kernel driver.
// Call everything from the UI thread.

namespace hardware {

// The values are saved as a bitmask, so new metrics go at the end.
enum Metric : unsigned { Cpu, Gpu, GpuTemp, Vram, Ram, SsdTemp, Disk, NetDown, NetUp, CpuPower, GpuPower, MetricCount };
constexpr unsigned Bit(Metric m) { return 1u << m; }
constexpr unsigned kAllMetrics = (1u << MetricCount) - 1;

struct Readings {
    // Percent, degrees Celsius, watts (CPU power), or bytes per second (network, rounded as shown);
    // -1 = not available.
    int value[MetricCount];
    Readings() { for (int& v : value) v = -1; }
};

// Opens what the metrics in `mask` need and closes the rest; cheap when the mask is unchanged.
void Configure(unsigned mask);
// Reads every configured metric. Returns true when a rounded value changed since the previous call.
bool Sample(Readings& out);
// Closes everything (Configure(0)).
void Shutdown();

}  // namespace hardware
