// Hardware readings; see hardware.h. Each source opens only while a metric that needs it is switched on,
// and every library is loaded from System32 by name at that point and unloaded again when the last
// metric that needs it is switched off, so a reading that is off costs no memory and no work.
// Rough cost while on: CPU and RAM nothing (two kernel calls); GPU temperature and power one driver
// call each; SSD temperature one open handle; disk activity opens each disk briefly for one driver call;
// CPU power keeps the processor's energy meters open; network speed loads iphlpapi.dll and reads the
// interface table; GPU and VRAM usage load pdh.dll and walk every process's GPU performance counters
// (the most expensive readings). Switching off the last reading that needs a library unloads it.
#include <winsock2.h>  // before windows.h, for the network interface table
#include <windows.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <winioctl.h>
#include <cfgmgr32.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <dxgi.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "hardware.h"
#include "hardware_model.h"

namespace hardware {
namespace {

using hardware_model::CounterSample;

HMODULE SystemDll(const wchar_t* name) { return LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32); }

template <typename Fn>
bool Resolve(HMODULE m, const char* name, Fn& fn) {
    fn = m ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(m, name))) : nullptr;
    return fn != nullptr;
}

unsigned g_mask = 0;
Readings g_last;

// ---- CPU and RAM ---------------------------------------------------------------------------------

ULONGLONG g_idle = 0, g_kernel = 0, g_user = 0;  // previous GetSystemTimes; g_kernel == 0: none yet

ULONGLONG Ticks(const FILETIME& f) { return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; }

int ReadCpu() {
    FILETIME i, k, u;
    if (!GetSystemTimes(&i, &k, &u)) return -1;
    ULONGLONG idle = Ticks(i), kernel = Ticks(k), user = Ticks(u);
    int pct = g_kernel && kernel >= g_kernel && user >= g_user && idle >= g_idle
                  ? hardware_model::CpuUsage(idle - g_idle, kernel - g_kernel, user - g_user)
                  : -1;
    g_idle = idle;
    g_kernel = kernel;
    g_user = user;
    return pct;
}

int ReadRam() {
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof(m);
    if (!GlobalMemoryStatusEx(&m) || !m.ullTotalPhys) return -1;
    return hardware_model::Percent((double)(m.ullTotalPhys - m.ullAvailPhys), (double)m.ullTotalPhys);
}

// ---- The graphics adapter ------------------------------------------------------------------------

struct Adapter {
    bool found = false;
    LUID luid{};
    double dedicatedBytes = 0;
    std::wstring tag;  // how the performance counters name it
};
Adapter g_adapter;
bool g_adapterTried = false;
int g_gpuMisses = 0;       // samples in a row with no GPU value; the adapter is looked up again after a while
int g_gpuRetryAfter = 30;  // misses before the next lookup; doubles while nothing turns up (at most an hour)

// The hardware adapter with the most dedicated memory: the discrete GPU when there is one.
void FindAdapter() {
    if (g_adapterTried) return;
    g_adapterTried = true;
    HMODULE dxgi = SystemDll(L"dxgi.dll");
    using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
    CreateFactoryFn create = nullptr;
    IDXGIFactory1* factory = nullptr;
    if (Resolve(dxgi, "CreateDXGIFactory1", create) &&
        SUCCEEDED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) && factory) {
        IDXGIAdapter1* a = nullptr;
        for (UINT i = 0; SUCCEEDED(factory->EnumAdapters1(i, &a)) && a; i++) {
            DXGI_ADAPTER_DESC1 d{};
            bool physical = SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                            d.VendorId != 0x1414;  // 0x1414: Microsoft Basic Render Driver
            if (physical && (!g_adapter.found || (double)d.DedicatedVideoMemory > g_adapter.dedicatedBytes)) {
                g_adapter.found = true;
                g_adapter.luid = d.AdapterLuid;
                g_adapter.dedicatedBytes = (double)d.DedicatedVideoMemory;
            }
            a->Release();
            a = nullptr;
        }
        factory->Release();
    }
    if (dxgi) FreeLibrary(dxgi);
    if (g_adapter.found) g_adapter.tag = hardware_model::LuidTag(g_adapter.luid.HighPart, g_adapter.luid.LowPart);
}

// ---- GPU and video memory usage: performance counters -----------------------------------------------

struct PdhApi {
    PDH_STATUS(WINAPI* open)(LPCWSTR, DWORD_PTR, PDH_HQUERY*) = nullptr;
    PDH_STATUS(WINAPI* add)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*) = nullptr;
    PDH_STATUS(WINAPI* collect)(PDH_HQUERY) = nullptr;
    PDH_STATUS(WINAPI* array)(PDH_HCOUNTER, DWORD, LPDWORD, LPDWORD, PPDH_FMT_COUNTERVALUE_ITEM_W) = nullptr;
    PDH_STATUS(WINAPI* close)(PDH_HQUERY) = nullptr;
};
PdhApi g_pdh;
HMODULE g_pdhDll = nullptr;
PDH_HQUERY g_query = nullptr;
PDH_HCOUNTER g_engines = nullptr, g_memory = nullptr;
unsigned g_queryMask = 0;  // the metrics whose counters the open query holds
std::vector<BYTE> g_buffer;

bool LoadPdh() {
    if (g_pdhDll) return true;
    g_pdhDll = SystemDll(L"pdh.dll");
    bool ok = Resolve(g_pdhDll, "PdhOpenQueryW", g_pdh.open) && Resolve(g_pdhDll, "PdhAddEnglishCounterW", g_pdh.add) &&
              Resolve(g_pdhDll, "PdhCollectQueryData", g_pdh.collect) &&
              Resolve(g_pdhDll, "PdhGetFormattedCounterArrayW", g_pdh.array) &&
              Resolve(g_pdhDll, "PdhCloseQuery", g_pdh.close);
    if (!ok) {
        if (g_pdhDll) FreeLibrary(g_pdhDll);
        g_pdhDll = nullptr;
        g_pdh = PdhApi{};
    }
    return ok;
}

// Closes the query and unloads pdh.dll, so with every counter reading off they cost no memory at all.
void CloseQuery() {
    if (g_query && g_pdh.close) g_pdh.close(g_query);
    g_query = nullptr;
    g_engines = g_memory = nullptr;
    g_queryMask = 0;
    std::vector<BYTE>().swap(g_buffer);
    if (g_pdhDll) FreeLibrary(g_pdhDll);
    g_pdhDll = nullptr;
    g_pdh = PdhApi{};
}

PDH_HCOUNTER AddCounter(const wchar_t* path) {
    PDH_HCOUNTER c = nullptr;
    return g_pdh.add(g_query, path, 0, &c) == ERROR_SUCCESS ? c : nullptr;
}

// Holds exactly the counters of the metrics in `want` (a toggle is rare, so a change simply reopens).
void ApplyQuery(unsigned want) {
    if (want == g_queryMask && (g_query || !want)) return;
    CloseQuery();
    if (!want || !LoadPdh()) return;
    if (g_pdh.open(nullptr, 0, &g_query) != ERROR_SUCCESS) {
        g_query = nullptr;
        CloseQuery();
        return;
    }
    // English names work in every Windows language; the wildcards pick up processes that start later.
    if (want & Bit(Gpu)) g_engines = AddCounter(L"\\GPU Engine(*)\\Utilization Percentage");
    if (want & Bit(Vram)) g_memory = AddCounter(L"\\GPU Adapter Memory(*)\\Dedicated Usage");
    g_queryMask = want;
    g_pdh.collect(g_query);  // the utilization is a rate: it needs a first sample to compare with
}

// The counter's instances with valid values; their names point into g_buffer, so use them before the next call.
std::vector<CounterSample> Instances(PDH_HCOUNTER counter) {
    std::vector<CounterSample> out;
    if (!counter) return out;
    for (int attempt = 0; attempt < 3; attempt++) {  // the instance list can grow between the size and the read
        DWORD bytes = (DWORD)g_buffer.size(), count = 0;
        auto* items = g_buffer.empty() ? nullptr : reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(g_buffer.data());
        PDH_STATUS s = g_pdh.array(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count, items);
        if ((DWORD)s == (DWORD)PDH_MORE_DATA) {
            g_buffer.resize((size_t)bytes + 4096);
            continue;
        }
        if (s != ERROR_SUCCESS || !items) return out;
        for (DWORD i = 0; i < count; i++) {
            const auto& it = items[i];
            if (it.szName && (it.FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || it.FmtValue.CStatus == PDH_CSTATUS_NEW_DATA))
                out.push_back({ it.szName, it.FmtValue.doubleValue });
        }
        return out;
    }
    return out;
}

// ---- GPU temperature and power: the graphics driver (D3DKMT) ----
// Only what the driver reports: NVIDIA's nvml.dll would add about 20 MB that never unloads (Zek, 26 Nov 2026).

// D3DKMT structures as declared in the Windows SDK's d3dkmthk.h (not every MinGW header set has them).
struct KmtOpenAdapterFromLuid { LUID AdapterLuid; UINT hAdapter; };
struct KmtQueryAdapterInfo { UINT hAdapter; UINT Type; void* pPrivateDriverData; UINT PrivateDriverDataSize; };
struct KmtCloseAdapter { UINT hAdapter; };
struct KmtAdapterPerfData {
    UINT32 PhysicalAdapterIndex;
    ULONGLONG MemoryFrequency, MaxMemoryFrequency, MaxMemoryFrequencyOC, MemoryBandwidth, PCIEBandwidth;
    ULONG FanRPM, Power, Temperature;  // Power in tenths of a percent of the power limit, Temperature in tenths of a degree C
    UCHAR PowerStateOverride;
};
constexpr UINT kAdapterPerfData = 62;  // KMTQAITYPE_ADAPTERPERFDATA, WDDM 2.4 (Windows 10 1803) and later
using KmtCall = LONG(APIENTRY*)(void*);
KmtCall g_kmtQuery = nullptr, g_kmtClose = nullptr;
UINT g_kmtAdapter = 0;

bool KmtPerf(KmtAdapterPerfData& p) {
    p = KmtAdapterPerfData{};
    if (!g_kmtAdapter || !g_kmtQuery) return false;
    KmtQueryAdapterInfo q{ g_kmtAdapter, kAdapterPerfData, &p, sizeof(p) };
    return g_kmtQuery(&q) == 0;
}

int KmtTemperature() {
    KmtAdapterPerfData p;
    if (!KmtPerf(p)) return -1;
    int c = (int)((p.Temperature + 5) / 10);
    return hardware_model::PlausibleTemperature(c) ? c : -1;
}

// The GPU's power draw as a share of its power limit, as the driver reports it (not every driver does).
int ReadGpuPower() {
    KmtAdapterPerfData p;
    return KmtPerf(p) ? hardware_model::PowerSharePercent(p.Power) : -1;
}

void CloseKmt() {
    if (g_kmtAdapter && g_kmtClose) {
        KmtCloseAdapter c{ g_kmtAdapter };
        g_kmtClose(&c);
    }
    g_kmtAdapter = 0;
}

void OpenKmt() {
    if (!g_adapter.found || g_kmtAdapter) return;
    HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
    KmtCall open = nullptr;
    if (Resolve(gdi, "D3DKMTOpenAdapterFromLuid", open) && Resolve(gdi, "D3DKMTQueryAdapterInfo", g_kmtQuery) &&
        Resolve(gdi, "D3DKMTCloseAdapter", g_kmtClose)) {
        KmtOpenAdapterFromLuid o{ g_adapter.luid, 0 };
        if (open(&o) == 0) g_kmtAdapter = o.hAdapter;
    }
}

int ReadGpuTemperature() { return KmtTemperature(); }

// ---- SSD temperature: the drive that holds Windows ------------------------------------------------

HANDLE g_disk = INVALID_HANDLE_VALUE;
constexpr ULONGLONG kSsdQueryMs = 10000;
ULONGLONG g_ssdNextQuery = 0;  // GetTickCount64() of the next real query; 0 = query on the next Sample
int g_ssdLast = -1;

void OpenDisk() {
    if (g_disk != INVALID_HANDLE_VALUE) return;
    wchar_t windows[MAX_PATH];
    UINT n = GetWindowsDirectoryW(windows, MAX_PATH);
    if (n < 2 || n >= MAX_PATH || windows[1] != L':') return;
    wchar_t volume[] = L"\\\\.\\C:";
    volume[4] = windows[0];
    // Access 0: enough for the queries below, and no admin rights needed.
    HANDLE v = CreateFileW(volume, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (v == INVALID_HANDLE_VALUE) return;
    alignas(8) BYTE buf[sizeof(VOLUME_DISK_EXTENTS) + 8 * sizeof(DISK_EXTENT)];
    DWORD got = 0;
    BOOL ok = DeviceIoControl(v, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, buf, sizeof(buf), &got, nullptr);
    CloseHandle(v);
    const auto* extents = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(buf);
    if (!ok || got < sizeof(VOLUME_DISK_EXTENTS) || extents->NumberOfDiskExtents < 1) return;
    wchar_t disk[40];
    std::swprintf(disk, 40, L"\\\\.\\PhysicalDrive%lu", (unsigned long)extents->Extents[0].DiskNumber);
    g_disk = CreateFileW(disk, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
}

void CloseDisk() {
    if (g_disk != INVALID_HANDLE_VALUE) CloseHandle(g_disk);
    g_disk = INVALID_HANDLE_VALUE;
    g_ssdNextQuery = 0;  // switched on again later: read at once, never a stale value
    g_ssdLast = -1;
}

// STORAGE_TEMPERATURE_INFO / STORAGE_TEMPERATURE_DATA_DESCRIPTOR as declared in the Windows SDK's
// winioctl.h (Windows 10); declared here because older MinGW headers lack them or hide them behind NTDDI.
struct SsdTemperatureInfo {
    WORD Index;
    SHORT Temperature;  // degrees Celsius
    SHORT OverThreshold, UnderThreshold;
    BOOLEAN OverThresholdChangable, UnderThresholdChangable, EventGenerated;
    BYTE Reserved0;
    DWORD Reserved1;
};
struct SsdTemperatureDescriptor {
    DWORD Version, Size;
    SHORT CriticalTemperature, WarningTemperature;
    WORD InfoCount;
    BYTE Reserved0[2];
    DWORD Reserved1[2];
    SsdTemperatureInfo TemperatureInfo[1];
};
static_assert(sizeof(SsdTemperatureInfo) == 16, "STORAGE_TEMPERATURE_INFO layout");
static_assert(offsetof(SsdTemperatureDescriptor, TemperatureInfo) == 24, "STORAGE_TEMPERATURE_DATA_DESCRIPTOR layout");
constexpr int kStorageDeviceTemperatureProperty = 52;  // STORAGE_PROPERTY_ID StorageDeviceTemperatureProperty

// The drive's own temperature (the NVMe "composite" sensor comes first); -1 when it reports none.
int QuerySsdTemperature() {
    STORAGE_PROPERTY_QUERY q{};
    q.PropertyId = static_cast<STORAGE_PROPERTY_ID>(kStorageDeviceTemperatureProperty);
    q.QueryType = PropertyStandardQuery;
    alignas(8) BYTE buf[512];
    DWORD got = 0;
    if (!DeviceIoControl(g_disk, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), buf, sizeof(buf), &got, nullptr)) return -1;
    const size_t head = offsetof(SsdTemperatureDescriptor, TemperatureInfo);
    if (got < head) return -1;
    const auto* d = reinterpret_cast<const SsdTemperatureDescriptor*>(buf);
    const SsdTemperatureInfo* info = d->TemperatureInfo;
    for (WORD i = 0; i < d->InfoCount && head + (size_t)(i + 1) * sizeof(SsdTemperatureInfo) <= got; i++)
        if (hardware_model::PlausibleTemperature(info[i].Temperature)) return info[i].Temperature;
    return -1;
}

// The query keeps the calling (UI) thread busy for about 2-3.5 ms, inside the driver even with an overlapped
// handle (measured 27 Nov 2026), so it runs at most every kSsdQueryMs and the last value is reused in between:
// a drive's temperature changes slowly and is shown in whole degrees (issue #4).
int ReadSsdTemperature() {
    if (g_disk == INVALID_HANDLE_VALUE) return -1;
    const ULONGLONG now = GetTickCount64();
    if (g_ssdNextQuery && now < g_ssdNextQuery) return g_ssdLast;
    g_ssdNextQuery = now + kSsdQueryMs;
    g_ssdLast = QuerySsdTemperature();
    return g_ssdLast;
}

// ---- Disk activity: each disk's idle time from its driver ------------------------------------------

std::vector<hardware_model::DiskTimes> g_diskTimes;  // the previous reading
bool g_diskBaseline = false;

// Opens each disk only for the one query: a handle kept open would stop a USB drive from being ejected.
std::vector<hardware_model::DiskTimes> ReadDiskTimes() {
    std::vector<hardware_model::DiskTimes> out;
    for (unsigned long i = 0, misses = 0; i < 32 && misses < 4; i++) {  // disk numbers can have small gaps
        wchar_t path[40];
        std::swprintf(path, 40, L"\\\\.\\PhysicalDrive%lu", i);
        HANDLE h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            misses++;
            continue;
        }
        misses = 0;
        DISK_PERFORMANCE p{};
        DWORD got = 0;
        if (DeviceIoControl(h, IOCTL_DISK_PERFORMANCE, nullptr, 0, &p, sizeof(p), &got, nullptr) && got >= sizeof(p))
            out.push_back({ i, p.IdleTime.QuadPart, p.QueryTime.QuadPart });
        CloseHandle(h);
    }
    return out;
}

int ReadDisk() {
    std::vector<hardware_model::DiskTimes> now = ReadDiskTimes();
    int busy = g_diskBaseline ? hardware_model::DiskActivity(g_diskTimes, now) : -1;
    g_diskTimes.swap(now);
    g_diskBaseline = true;
    return busy;
}

void CloseDiskActivity() {
    std::vector<hardware_model::DiskTimes>().swap(g_diskTimes);
    g_diskBaseline = false;
}

// ---- CPU power: the processor's own energy meters (Windows' Energy Metering Interface) ---------------

// The Energy Metering Interface as declared in the Windows SDK's emi.h (Windows 10 1809 and later);
// declared here because MinGW headers lack it.
const GUID kEnergyMeterInterface = { 0x45bd8344, 0x7ed6, 0x49cf, { 0xa4, 0x40, 0xc2, 0x76, 0xc9, 0x33, 0xb0, 0x53 } };
constexpr DWORD kEmiGetVersion = CTL_CODE(FILE_DEVICE_UNKNOWN, 0, METHOD_BUFFERED, FILE_READ_ACCESS);
constexpr DWORD kEmiGetMetadataSize = CTL_CODE(FILE_DEVICE_UNKNOWN, 1, METHOD_BUFFERED, FILE_READ_ACCESS);
constexpr DWORD kEmiGetMetadata = CTL_CODE(FILE_DEVICE_UNKNOWN, 2, METHOD_BUFFERED, FILE_READ_ACCESS);
constexpr DWORD kEmiGetMeasurement = CTL_CODE(FILE_DEVICE_UNKNOWN, 3, METHOD_BUFFERED, FILE_READ_ACCESS);
constexpr USHORT kEmiVersion2 = 2;
struct EmiMetadataV2Head {  // EMI_METADATA_V2 up to its channels
    WCHAR HardwareOEM[16], HardwareModel[16];
    USHORT HardwareRevision, ChannelCount;
};
struct EmiChannelV2Head {  // EMI_CHANNEL_V2 up to its name
    int MeasurementUnit;
    USHORT ChannelNameSize;  // bytes
};
struct EmiMeasurement {  // EMI_CHANNEL_MEASUREMENT_DATA
    ULONGLONG AbsoluteEnergy;  // picowatt-hours
    ULONGLONG AbsoluteTime;    // 100 ns units
};
static_assert(sizeof(EmiMetadataV2Head) == 68, "EMI_METADATA_V2 layout");
static_assert(sizeof(EmiChannelV2Head) == 8, "EMI_CHANNEL_V2 layout");
constexpr size_t kEmiChannelName = 6;  // FIELD_OFFSET(EMI_CHANNEL_V2, ChannelName)

struct EnergyMeter {
    HANDLE handle = INVALID_HANDLE_VALUE;
    USHORT channels = 0;
    std::vector<USHORT> packages;  // the channels that meter a whole processor package
};
std::vector<EnergyMeter> g_meters;
bool g_metersTried = false;
std::vector<hardware_model::EnergyReading> g_energy;  // the previous reading
bool g_energyBaseline = false;

// The package channels of a meter, from its V2 metadata; empty for anything else.
std::vector<USHORT> PackageChannels(HANDLE h, USHORT& channels) {
    std::vector<USHORT> out;
    channels = 0;
    USHORT version = 0;
    ULONG size = 0;
    DWORD got = 0;
    if (!DeviceIoControl(h, kEmiGetVersion, nullptr, 0, &version, sizeof(version), &got, nullptr) || version != kEmiVersion2 ||
        !DeviceIoControl(h, kEmiGetMetadataSize, nullptr, 0, &size, sizeof(size), &got, nullptr) ||
        size < sizeof(EmiMetadataV2Head) || size > 65536)
        return out;
    std::vector<BYTE> meta(size);
    if (!DeviceIoControl(h, kEmiGetMetadata, nullptr, 0, meta.data(), size, &got, nullptr) || got < sizeof(EmiMetadataV2Head))
        return out;
    EmiMetadataV2Head head;
    std::memcpy(&head, meta.data(), sizeof(head));
    size_t at = sizeof(EmiMetadataV2Head);
    for (USHORT c = 0; c < head.ChannelCount; c++) {
        EmiChannelV2Head ch;
        if (at + sizeof(ch) > got) return {};
        std::memcpy(&ch, meta.data() + at, sizeof(ch));
        size_t name = at + kEmiChannelName, next = name + ch.ChannelNameSize;
        if (next > got) return {};
        std::wstring channel(reinterpret_cast<const wchar_t*>(meta.data() + name), ch.ChannelNameSize / sizeof(wchar_t));
        channel.resize(std::wcslen(channel.c_str()));
        if (hardware_model::IsPackageMeter(channel)) out.push_back(c);
        at = next;  // EMI_CHANNEL_V2_NEXT_CHANNEL: the next channel follows the name directly
    }
    channels = head.ChannelCount;
    return out;
}

void CloseEnergy() {
    for (auto& m : g_meters) CloseHandle(m.handle);
    std::vector<EnergyMeter>().swap(g_meters);
    std::vector<hardware_model::EnergyReading>().swap(g_energy);
    g_metersTried = false;
    g_energyBaseline = false;
}

// Keeps a handle on every meter that has a package channel (processor meters cannot be removed).
void OpenEnergy() {
    if (g_metersTried) return;
    g_metersTried = true;
    ULONG len = 0;
    GUID iface = kEnergyMeterInterface;
    if (CM_Get_Device_Interface_List_SizeW(&len, &iface, nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS || len < 2)
        return;
    std::vector<wchar_t> list(len);
    if (CM_Get_Device_Interface_ListW(&iface, nullptr, list.data(), len, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS)
        return;
    list.back() = 0;
    for (const wchar_t* p = list.data(); *p; p += std::wcslen(p) + 1) {
        HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        EnergyMeter m;
        m.packages = PackageChannels(h, m.channels);
        if (m.packages.empty()) {
            CloseHandle(h);
            continue;
        }
        m.handle = h;
        g_meters.push_back(std::move(m));
    }
}

int ReadCpuPower() {
    std::vector<hardware_model::EnergyReading> now;
    std::vector<EmiMeasurement> data;
    for (size_t i = 0; i < g_meters.size(); i++) {
        const EnergyMeter& m = g_meters[i];
        data.assign(m.channels, EmiMeasurement{});
        DWORD bytes = (DWORD)(data.size() * sizeof(EmiMeasurement)), got = 0;
        if (!DeviceIoControl(m.handle, kEmiGetMeasurement, nullptr, 0, data.data(), bytes, &got, nullptr) || got < bytes) continue;
        for (USHORT c : m.packages)
            now.push_back({ (unsigned)((i << 16) | c), data[c].AbsoluteEnergy, data[c].AbsoluteTime });
    }
    int watts = g_energyBaseline ? hardware_model::CpuPackageWatts(g_energy, now) : -1;
    g_energy.swap(now);
    g_energyBaseline = true;
    return watts;
}

// ---- Network speed: the byte counters of the network interfaces ------------------------------------

using GetIfTable2Fn = DWORD(WINAPI*)(PMIB_IF_TABLE2*);
using FreeMibTableFn = VOID(WINAPI*)(PVOID);
HMODULE g_iphlp = nullptr;
GetIfTable2Fn g_getIfTable = nullptr;
FreeMibTableFn g_freeMibTable = nullptr;
std::vector<hardware_model::InterfaceOctets> g_octets;  // the previous reading
ULONGLONG g_octetsAt = 0;                               // when it was taken (GetTickCount64); 0: none yet

void CloseNetwork() {
    if (g_iphlp) FreeLibrary(g_iphlp);
    g_iphlp = nullptr;
    g_getIfTable = nullptr;
    g_freeMibTable = nullptr;
    std::vector<hardware_model::InterfaceOctets>().swap(g_octets);
    g_octetsAt = 0;
}

void OpenNetwork() {
    if (g_iphlp) return;
    g_iphlp = SystemDll(L"iphlpapi.dll");
    if (!Resolve(g_iphlp, "GetIfTable2", g_getIfTable) || !Resolve(g_iphlp, "FreeMibTable", g_freeMibTable))
        CloseNetwork();
}

// Real network adapters that are up (Ethernet, Wi-Fi). Virtual switches, VPN tunnels, loopback and the
// filter layers stacked on an adapter are left out: they carry the same bytes again.
bool ReadOctets(std::vector<hardware_model::InterfaceOctets>& out) {
    out.clear();
    PMIB_IF_TABLE2 table = nullptr;
    if (!g_getIfTable || g_getIfTable(&table) != NO_ERROR || !table) return false;
    for (ULONG i = 0; i < table->NumEntries; i++) {
        const MIB_IF_ROW2& r = table->Table[i];
        if (!r.InterfaceAndOperStatusFlags.HardwareInterface || r.InterfaceAndOperStatusFlags.FilterInterface ||
            r.OperStatus != IfOperStatusUp)
            continue;
        out.push_back({ r.InterfaceLuid.Value, r.InOctets, r.OutOctets });
    }
    g_freeMibTable(table);
    return true;
}

// Download and upload speed since the previous call, in bytes per second (-1 on the first call).
void ReadNetwork(int& down, int& up) {
    down = up = -1;
    std::vector<hardware_model::InterfaceOctets> now;
    ULONGLONG at = GetTickCount64();
    if (!ReadOctets(now)) return;
    if (g_octetsAt && at > g_octetsAt) {
        unsigned long long in = 0, out = 0;
        hardware_model::OctetsMoved(g_octets, now, in, out);
        double seconds = (double)(at - g_octetsAt) / 1000.0;
        down = hardware_model::RoundRate((double)in / seconds);
        up = hardware_model::RoundRate((double)out / seconds);
    }
    g_octets.swap(now);
    g_octetsAt = at;
}

constexpr unsigned kNetworkMetrics = Bit(NetDown) | Bit(NetUp);
constexpr unsigned kGpuCounterMetrics = Bit(Gpu) | Bit(Vram);
constexpr unsigned kCounterMetrics = kGpuCounterMetrics;
constexpr unsigned kAdapterMetrics = kGpuCounterMetrics | Bit(GpuTemp) | Bit(GpuPower);

// Opens the GPU's own sources the mask needs (looking the adapter up first) and closes the others.
void ApplyGpu(unsigned mask) {
    if (mask & kAdapterMetrics) FindAdapter();
    if (g_adapter.found && (mask & (Bit(GpuTemp) | Bit(GpuPower)))) OpenKmt();
    else CloseKmt();
    if (!(mask & kAdapterMetrics)) {
        g_adapter = Adapter{};
        g_adapterTried = false;
    }
}

// Opens every source the mask needs and closes the rest.
void ApplySources(unsigned mask) {
    ApplyGpu(mask);
    ApplyQuery(g_adapter.found ? mask & kCounterMetrics : 0);
    if (mask & kNetworkMetrics) OpenNetwork();
    else CloseNetwork();
    if (mask & Bit(CpuPower)) OpenEnergy();
    else CloseEnergy();
    if (!(mask & Bit(Disk))) CloseDiskActivity();
}

}  // namespace

void Configure(unsigned mask) {
    mask &= kAllMetrics;
    if (mask == g_mask) return;
    ApplySources(mask);
    // Baselines, so the first Sample already has a value.
    if ((mask & kNetworkMetrics) && !g_octetsAt) {
        int down, up;
        ReadNetwork(down, up);
    }
    if ((mask & Bit(Disk)) && !g_diskBaseline) ReadDisk();
    if ((mask & Bit(CpuPower)) && !g_energyBaseline) ReadCpuPower();
    if (mask & Bit(SsdTemp)) OpenDisk();
    else CloseDisk();
    if (!(mask & Bit(Cpu))) g_kernel = 0;
    else if (!g_kernel) ReadCpu();  // the baseline, so the first Sample already has a CPU value
    g_gpuMisses = 0;
    g_gpuRetryAfter = 30;
    g_mask = mask;
    g_last = Readings{};
}

bool Sample(Readings& out) {
    Readings r;
    if (g_query) g_pdh.collect(g_query);
    if (g_mask & Bit(Cpu)) r.value[Cpu] = ReadCpu();
    if (g_mask & Bit(Ram)) r.value[Ram] = ReadRam();
    if (g_mask & Bit(Gpu)) r.value[Gpu] = hardware_model::GpuUsage(Instances(g_engines), g_adapter.tag);
    if (g_mask & Bit(Vram))
        r.value[Vram] = hardware_model::Percent(hardware_model::AdapterBytes(Instances(g_memory), g_adapter.tag),
                                                g_adapter.dedicatedBytes);
    if (g_mask & Bit(GpuTemp)) r.value[GpuTemp] = ReadGpuTemperature();
    if (g_mask & Bit(SsdTemp)) r.value[SsdTemp] = ReadSsdTemperature();
    if (g_mask & Bit(Disk)) r.value[Disk] = ReadDisk();
    if (g_mask & Bit(CpuPower)) r.value[CpuPower] = ReadCpuPower();
    if (g_mask & Bit(GpuPower)) r.value[GpuPower] = ReadGpuPower();
    if (g_mask & kNetworkMetrics) {
        int down, up;
        ReadNetwork(down, up);
        if (g_mask & Bit(NetDown)) r.value[NetDown] = down;
        if (g_mask & Bit(NetUp)) r.value[NetUp] = up;
    }

    // A driver update or a GPU reset gives the adapter a new LUID: after a while with nothing, look again,
    // less and less often while the PC keeps reporting nothing (a GPU without these counters or sensors).
    if (g_mask & kAdapterMetrics) {
        bool any = r.value[Gpu] >= 0 || r.value[Vram] >= 0 || r.value[GpuTemp] >= 0 || r.value[GpuPower] >= 0;
        if (any) {
            g_gpuMisses = 0;
            g_gpuRetryAfter = 30;
        } else if (++g_gpuMisses >= g_gpuRetryAfter) {
            g_gpuMisses = 0;
            g_gpuRetryAfter = std::min(g_gpuRetryAfter * 2, 1800);
            ApplyGpu(0);
            ApplySources(g_mask);
        }
    }

    bool changed = std::memcmp(r.value, g_last.value, sizeof(r.value)) != 0;
    g_last = r;
    out = r;
    return changed;
}

void Shutdown() { Configure(0); }

}  // namespace hardware
