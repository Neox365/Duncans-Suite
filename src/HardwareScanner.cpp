#include "HardwareScanner.h"

#include <initguid.h>   // muss vor devguid.h/devpkey.h stehen, damit GUIDs/DEVPKEYs definiert werden
#include <setupapi.h>
#include <devguid.h>
#include <cfgmgr32.h>
#include <devpkey.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>

#ifdef _MSC_VER
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")
#endif

namespace
{
    // ---- Hilfsfunktionen ------------------------------------------------------------------

    // Liest eine String-Eigenschaft aus der Registry-Property-API (bei REG_MULTI_SZ: erster Eintrag).
    std::wstring GetRegistryString(HDEVINFO set, SP_DEVINFO_DATA& dev, DWORD property)
    {
        DWORD type = 0, needed = 0;
        SetupDiGetDeviceRegistryPropertyW(set, &dev, property, &type, nullptr, 0, &needed);
        if (needed == 0)
            return {};

        std::vector<BYTE> buf(needed + 2 * sizeof(wchar_t), 0);
        if (!SetupDiGetDeviceRegistryPropertyW(set, &dev, property, &type, buf.data(), needed, nullptr))
            return {};
        return std::wstring(reinterpret_cast<const wchar_t*>(buf.data()));
    }

    // Liest alle Eintraege einer REG_MULTI_SZ-Eigenschaft (in GROSSBUCHSTABEN) und haengt sie an out an.
    void AppendMultiString(HDEVINFO set, SP_DEVINFO_DATA& dev, DWORD property, std::vector<std::wstring>& out)
    {
        DWORD type = 0, needed = 0;
        SetupDiGetDeviceRegistryPropertyW(set, &dev, property, &type, nullptr, 0, &needed);
        if (needed == 0)
            return;
        std::vector<BYTE> buf(needed + 2 * sizeof(wchar_t), 0);
        if (!SetupDiGetDeviceRegistryPropertyW(set, &dev, property, &type, buf.data(), needed, nullptr))
            return;
        for (const wchar_t* p = reinterpret_cast<const wchar_t*>(buf.data()); *p; p += wcslen(p) + 1)
        {
            std::wstring id(p);
            CharUpperBuffW(&id[0], static_cast<DWORD>(id.size()));
            out.push_back(std::move(id));
        }
    }

    // Liest eine String-Eigenschaft ueber die neuere DEVPKEY-API.
    std::wstring GetPropertyString(HDEVINFO set, SP_DEVINFO_DATA& dev, const DEVPROPKEY& key)
    {
        DEVPROPTYPE type = 0;
        DWORD needed = 0;
        SetupDiGetDevicePropertyW(set, &dev, &key, &type, nullptr, 0, &needed, 0);
        if (needed == 0)
            return {};

        std::vector<BYTE> buf(needed + 2 * sizeof(wchar_t), 0);
        if (!SetupDiGetDevicePropertyW(set, &dev, &key, &type, buf.data(), needed, nullptr, 0)
            || type != DEVPROP_TYPE_STRING)
            return {};
        return std::wstring(reinterpret_cast<const wchar_t*>(buf.data()));
    }

    std::wstring GetPropertyDate(HDEVINFO set, SP_DEVINFO_DATA& dev, const DEVPROPKEY& key, int* dateKey)
    {
        DEVPROPTYPE type = 0;
        FILETIME ft{};
        DWORD size = sizeof(ft);
        if (!SetupDiGetDevicePropertyW(set, &dev, &key, &type, reinterpret_cast<PBYTE>(&ft), sizeof(ft), &size, 0)
            || type != DEVPROP_TYPE_FILETIME)
            return {};

        SYSTEMTIME st{};
        if (!FileTimeToSystemTime(&ft, &st))
            return {};

        if (dateKey)
            *dateKey = st.wYear * 10000 + st.wMonth * 100 + st.wDay;

        wchar_t text[16];
        wsprintfW(text, L"%02u.%02u.%04u", st.wDay, st.wMonth, st.wYear);
        text[15] = 0;
        return text;
    }

    struct ClassName
    {
        const GUID* guid;
        const wchar_t* name;
        CategoryIcon icon;
    };

    // Einheitliche, deutsche Kategorienamen fuer die haeufigsten Geraeteklassen.
    const ClassName kClassNames[] = {
        { &GUID_DEVCLASS_DISPLAY,         L"Grafikkarte", CategoryIcon::Gpu },
        { &GUID_DEVCLASS_NET,             L"Netzwerk", CategoryIcon::Network },
        { &GUID_DEVCLASS_MEDIA,           L"Audio", CategoryIcon::Audio },
        { &GUID_DEVCLASS_BLUETOOTH,       L"Bluetooth", CategoryIcon::Bluetooth },
        { &GUID_DEVCLASS_USB,             L"USB", CategoryIcon::Usb },
        { &GUID_DEVCLASS_HDC,             L"Speicher-Controller", CategoryIcon::Storage },
        { &GUID_DEVCLASS_SCSIADAPTER,     L"Speicher-Controller", CategoryIcon::Storage },
        { &GUID_DEVCLASS_DISKDRIVE,       L"Laufwerke", CategoryIcon::Storage },
        { &GUID_DEVCLASS_CDROM,           L"Optische Laufwerke", CategoryIcon::Storage },
        { &GUID_DEVCLASS_PROCESSOR,       L"Prozessor", CategoryIcon::Cpu },
        { &GUID_DEVCLASS_SYSTEM,          L"Systemgeräte", CategoryIcon::System },
        { &GUID_DEVCLASS_HIDCLASS,        L"Eingabegeräte", CategoryIcon::Input },
        { &GUID_DEVCLASS_KEYBOARD,        L"Tastatur", CategoryIcon::Input },
        { &GUID_DEVCLASS_MOUSE,           L"Maus", CategoryIcon::Input },
        { &GUID_DEVCLASS_MONITOR,         L"Monitor", CategoryIcon::Monitor },
        { &GUID_DEVCLASS_IMAGE,           L"Bildverarbeitung", CategoryIcon::Camera },
        { &GUID_DEVCLASS_BATTERY,         L"Akku", CategoryIcon::Battery },
        { &GUID_DEVCLASS_PRINTER,         L"Drucker", CategoryIcon::Printer },
        { &GUID_DEVCLASS_PORTS,           L"Anschlüsse (COM/LPT)", CategoryIcon::Port },
        { &GUID_DEVCLASS_SMARTCARDREADER, L"Smartcard-Leser", CategoryIcon::Other },
    };

    std::wstring CategoryFor(const GUID& classGuid, CategoryIcon& icon)
    {
        for (const auto& entry : kClassNames)
            if (IsEqualGUID(*entry.guid, classGuid))
            {
                icon = entry.icon;
                return entry.name;
            }

        // Fallback: lokalisierte Klassenbeschreibung von Windows
        wchar_t desc[256] = {};
        if (SetupDiGetClassDescriptionW(&classGuid, desc, 256, nullptr) && desc[0] != 0)
            return desc;
        return L"Sonstige";
    }

    int CompareNoCase(const std::wstring& a, const std::wstring& b)
    {
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) - CSTR_EQUAL;
    }
}

// ---- HardwareScanner ----------------------------------------------------------------------

HardwareScanner::~HardwareScanner()
{
    Cancel();
}

void HardwareScanner::Join()
{
    if (m_thread.joinable())
        m_thread.join();
}

void HardwareScanner::Cancel()
{
    m_cancel = true;
    Join();
}

bool HardwareScanner::StartAsync(HWND notifyWindow)
{
    if (m_running.exchange(true))
        return false;

    Join();           // vorherigen (beendeten) Thread aufraeumen
    m_cancel = false;

    m_thread = std::thread([this, notifyWindow]()
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        std::vector<DriverInfo> result = ScanBlocking(notifyWindow, &m_cancel);
        const bool ok = !m_cancel.load();
        const size_t count = result.size();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (ok)
                m_results = std::move(result);
        }
        m_running = false;
        if (notifyWindow && IsWindow(notifyWindow))
            PostMessageW(notifyWindow, WM_SCAN_DONE, static_cast<WPARAM>(count), ok ? 1 : 0);
    });
    return true;
}

std::vector<DriverInfo> HardwareScanner::GetResults() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_results;
}

std::vector<DriverInfo> HardwareScanner::ScanBlocking(HWND notifyWindow, const std::atomic<bool>* cancel)
{
    std::vector<DriverInfo> drivers;

    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE)
        return drivers;

    // Gesamtzahl ermitteln (fuer den Fortschrittsbalken)
    SP_DEVINFO_DATA dev{};
    dev.cbSize = sizeof(dev);
    DWORD total = 0;
    while (SetupDiEnumDeviceInfo(set, total, &dev))
        ++total;

    for (DWORD index = 0; index < total; ++index)
    {
        if (cancel && cancel->load())
            break;

        dev = SP_DEVINFO_DATA{};
        dev.cbSize = sizeof(dev);
        if (!SetupDiEnumDeviceInfo(set, index, &dev))
            break;

        DriverInfo info;
        info.driverVersion = GetPropertyString(set, dev, DEVPKEY_Device_DriverVersion);
        info.provider      = GetPropertyString(set, dev, DEVPKEY_Device_DriverProvider);
        info.infName       = GetPropertyString(set, dev, DEVPKEY_Device_DriverInfPath);

        // Geraete ganz ohne Treiberinformation (z. B. reine Bus-Platzhalter) ueberspringen.
        if (!info.driverVersion.empty() || !info.provider.empty())
        {
            info.deviceName = GetRegistryString(set, dev, SPDRP_FRIENDLYNAME);
            if (info.deviceName.empty())
                info.deviceName = GetRegistryString(set, dev, SPDRP_DEVICEDESC);
            if (info.deviceName.empty())
                info.deviceName = L"Unbekanntes Gerät";

            info.category     = CategoryFor(dev.ClassGuid, info.icon);
            info.manufacturer = GetRegistryString(set, dev, SPDRP_MFG);
            info.hardwareId   = GetRegistryString(set, dev, SPDRP_HARDWAREID);
            AppendMultiString(set, dev, SPDRP_HARDWAREID, info.hardwareIds);
            info.driverDate   = GetPropertyDate(set, dev, DEVPKEY_Device_DriverDate, &info.driverDateKey);

            ULONG cmStatus = 0, cmProblem = 0;
            if (CM_Get_DevNode_Status(&cmStatus, &cmProblem, dev.DevInst, 0) == CR_SUCCESS && cmProblem != 0)
            {
                info.problemCode = cmProblem;
                info.status = DriverStatus::Problem;
            }
            else
            {
                info.status = DriverStatus::Ok;
            }

            drivers.push_back(std::move(info));
        }

        if (notifyWindow && ((index & 7) == 0 || index + 1 == total))
            PostMessageW(notifyWindow, WM_SCAN_PROGRESS, index + 1, total);
    }

    SetupDiDestroyDeviceInfoList(set);

    std::sort(drivers.begin(), drivers.end(), [](const DriverInfo& a, const DriverInfo& b)
    {
        int c = CompareNoCase(a.category, b.category);
        return c != 0 ? c < 0 : CompareNoCase(a.deviceName, b.deviceName) < 0;
    });
    return drivers;
}
