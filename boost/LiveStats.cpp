#include "LiveStats.h"

#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>

namespace boostmode {

int CpuPercent(unsigned long long idle0, unsigned long long kernel0, unsigned long long user0,
               unsigned long long idle1, unsigned long long kernel1, unsigned long long user1)
{
    if (idle1 < idle0 || kernel1 < kernel0 || user1 < user0) return -1;
    const unsigned long long idle = idle1 - idle0, total = (kernel1 - kernel0) + (user1 - user0);   // Kernel-Zeit enthaelt die Leerlaufzeit
    if (total == 0 || idle > total) return -1;
    return static_cast<int>((total - idle) * 100 / total);
}

int GpuPercent(const std::vector<std::wstring>& instances, const std::vector<double>& values)
{
    double sum = 0;
    bool any = false;
    for (size_t i = 0; i < instances.size() && i < values.size(); ++i) {
        if (instances[i].find(L"engtype_3D") == std::wstring::npos) continue;
        any = true;
        if (values[i] > 0) sum += values[i];
    }
    if (!any) return -1;
    return static_cast<int>(std::clamp(sum, 0.0, 100.0) + 0.5);
}

int LoadLevel(int p) { return p >= 90 ? 2 : (p >= 70 ? 1 : 0); }
int TempLevel(int c) { return c >= 85 ? 2 : (c >= 75 ? 1 : 0); }

namespace {
struct NvmlUtil { unsigned gpu, memory; };
using NvmlInit = int (*)();
using NvmlShutdown = int (*)();
using NvmlHandleByIndex = int (*)(unsigned, void**);
using NvmlTemp = int (*)(void*, int, unsigned*);
using NvmlName = int (*)(void*, char*, unsigned);
}

struct LiveStats::Impl {
    // CPU
    bool haveCpu = false;
    unsigned long long idle0 = 0, kernel0 = 0, user0 = 0;
    // GPU-Leistungsindikator
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER counter = nullptr;
    bool pdhReady = false;
    // NVML
    HMODULE nvml = nullptr;
    NvmlShutdown shutdown = nullptr;
    NvmlTemp getTemp = nullptr;
    void* device = nullptr;
    std::wstring gpuName;

    static unsigned long long Ft(const FILETIME& f) { return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime; }

    Impl() {
        FILETIME i, k, u;
        if (GetSystemTimes(&i, &k, &u)) { idle0 = Ft(i); kernel0 = Ft(k); user0 = Ft(u); haveCpu = true; }
        if (PdhOpenQueryW(nullptr, 0, &query) == ERROR_SUCCESS) {
            if (PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) == ERROR_SUCCESS) {
                PdhCollectQueryData(query);                                           // Basiswert; die Auslastung kommt ab der zweiten Messung
                pdhReady = true;
            }
        }
        wchar_t sys[MAX_PATH] = {};
        GetSystemDirectoryW(sys, MAX_PATH);
        const std::wstring path = std::wstring(sys) + L"\\nvml.dll";
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {          // nur die Datei aus System32 (kein Suchpfad)
            nvml = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (nvml) {
                auto init = reinterpret_cast<NvmlInit>(reinterpret_cast<void*>(GetProcAddress(nvml, "nvmlInit_v2")));
                auto byIndex = reinterpret_cast<NvmlHandleByIndex>(reinterpret_cast<void*>(GetProcAddress(nvml, "nvmlDeviceGetHandleByIndex_v2")));
                getTemp = reinterpret_cast<NvmlTemp>(reinterpret_cast<void*>(GetProcAddress(nvml, "nvmlDeviceGetTemperature")));
                shutdown = reinterpret_cast<NvmlShutdown>(reinterpret_cast<void*>(GetProcAddress(nvml, "nvmlShutdown")));
                auto nameFn = reinterpret_cast<NvmlName>(reinterpret_cast<void*>(GetProcAddress(nvml, "nvmlDeviceGetName")));
                if (init && byIndex && getTemp && init() == 0 && byIndex(0, &device) == 0 && device) {
                    char name[96] = {};
                    if (nameFn && nameFn(device, name, sizeof(name)) == 0)
                        for (const char* p = name; *p; ++p) gpuName.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
                } else {
                    getTemp = nullptr;
                    device = nullptr;
                }
            }
        }
    }
    ~Impl() {
        if (query) PdhCloseQuery(query);
        if (nvml) {
            if (shutdown && device) shutdown();
            FreeLibrary(nvml);
        }
    }
};

LiveStats::LiveStats() : m(new Impl) {}
LiveStats::~LiveStats() { delete m; }

Live LiveStats::Sample()
{
    Live r;
    // CPU
    FILETIME i, k, u;
    if (GetSystemTimes(&i, &k, &u)) {
        const unsigned long long i1 = Impl::Ft(i), k1 = Impl::Ft(k), u1 = Impl::Ft(u);
        if (m->haveCpu) r.cpu = CpuPercent(m->idle0, m->kernel0, m->user0, i1, k1, u1);
        m->idle0 = i1; m->kernel0 = k1; m->user0 = u1; m->haveCpu = true;
    }
    // RAM
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms) && ms.ullTotalPhys > 0) {
        r.ram = static_cast<int>((ms.ullTotalPhys - ms.ullAvailPhys) * 100 / ms.ullTotalPhys);
        r.ramTotalMb = ms.ullTotalPhys / (1024 * 1024);
        r.ramUsedMb = (ms.ullTotalPhys - ms.ullAvailPhys) / (1024 * 1024);
    }
    // GPU-Auslastung
    if (m->pdhReady && PdhCollectQueryData(m->query) == ERROR_SUCCESS) {
        DWORD bytes = 0, count = 0;
        PDH_STATUS st = PdhGetFormattedCounterArrayW(m->counter, PDH_FMT_DOUBLE, &bytes, &count, nullptr);
        if (static_cast<DWORD>(st) == static_cast<DWORD>(PDH_MORE_DATA) && bytes > 0 && bytes < 8u * 1024 * 1024) {
            std::vector<unsigned char> buf(bytes);
            auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
            if (PdhGetFormattedCounterArrayW(m->counter, PDH_FMT_DOUBLE, &bytes, &count, items) == ERROR_SUCCESS) {
                std::vector<std::wstring> names;
                std::vector<double> vals;
                for (DWORD n = 0; n < count; ++n) {
                    names.push_back(items[n].szName ? items[n].szName : L"");
                    vals.push_back(items[n].FmtValue.CStatus == ERROR_SUCCESS || items[n].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA ? items[n].FmtValue.doubleValue : 0.0);
                }
                r.gpu = GpuPercent(names, vals);
            }
        }
    }
    // GPU-Temperatur (NVIDIA)
    if (m->getTemp && m->device) {
        unsigned t = 0;
        if (m->getTemp(m->device, 0, &t) == 0 && t > 0 && t < 150) r.gpuTemp = static_cast<int>(t);
    }
    r.gpuName = m->gpuName;
    return r;
}

}  // namespace boostmode
