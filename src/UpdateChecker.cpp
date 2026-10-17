#include "UpdateChecker.h"

#include "Dispatch.h"

#include <algorithm>
#include <functional>


using namespace dispatch;

namespace
{
    // Eigentliche Suche; fuellt updates oder error.
    bool QueryWindowsUpdate(std::vector<AvailableUpdate>& updates, std::wstring& error)
    {
        CLSID clsid;
        if (FAILED(CLSIDFromProgID(L"Microsoft.Update.Session", &clsid)))
        {
            error = L"Windows-Update-Dienst nicht verfügbar";
            return false;
        }
        IDispatch* raw = nullptr;
        HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, kIidDispatch,
                                      reinterpret_cast<void**>(&raw));
        if (FAILED(hr) || !raw)
        {
            error = L"Windows-Update-Agent konnte nicht gestartet werden";
            return false;
        }
        Disp session(raw);

        Disp searcher = GetObject(session.p, L"CreateUpdateSearcher", DISPATCH_METHOD);
        if (!searcher.p)
        {
            error = L"Update-Suche konnte nicht erstellt werden";
            return false;
        }

        VARIANT criteria = BstrArg(L"IsInstalled=0 and Type='Driver' and IsHidden=0");
        Disp result = GetObject(searcher.p, L"Search", DISPATCH_METHOD, { criteria });
        VariantClear(&criteria);
        if (!result.p)
        {
            error = L"Update-Suche fehlgeschlagen (offline oder Windows Update deaktiviert?)";
            return false;
        }

        Disp collection = GetObject(result.p, L"Updates");
        if (!collection.p)
        {
            error = L"Update-Liste nicht lesbar";
            return false;
        }

        const long count = GetLong(collection.p, L"Count");
        for (long i = 0; i < count; ++i)
        {
            Disp update = GetObject(collection.p, L"Item", DISPATCH_PROPERTYGET, { LongArg(i) });
            if (!update.p)
                continue;

            AvailableUpdate u;
            u.title = GetString(update.p, L"Title");
            if (Disp identity = GetObject(update.p, L"Identity"))
                u.id = GetString(identity.p, L"UpdateID");
            u.hardwareId = GetString(update.p, L"DriverHardwareID");
            u.provider = GetString(update.p, L"DriverProvider");
            GetDate(update.p, L"DriverVerDate", u.dateText, u.dateKey);
            if (u.hardwareId.empty())
                continue;
            CharUpperBuffW(&u.hardwareId[0], static_cast<DWORD>(u.hardwareId.size()));
            updates.push_back(std::move(u));
        }
        return true;
    }
}

UpdateChecker::~UpdateChecker()
{
    Cancel();
}

void UpdateChecker::Join()
{
    if (m_thread.joinable())
        m_thread.join();
}

void UpdateChecker::Cancel()
{
    Join();   // WUA-Suche laesst sich nicht sauber abbrechen; beim Beenden kurz warten
}

bool UpdateChecker::StartAsync(HWND notifyWindow)
{
    if (m_running.exchange(true))
        return false;
    Join();

    m_thread = std::thread([this, notifyWindow]()
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        std::vector<AvailableUpdate> updates;
        std::wstring error;
        bool ok = false;

        if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
        {
            ok = QueryWindowsUpdate(updates, error);
            CoUninitialize();
        }
        else
        {
            error = L"COM-Initialisierung fehlgeschlagen";
        }

        const size_t count = updates.size();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_updates = std::move(updates);
            m_error = error;
        }
        m_running = false;
        if (notifyWindow && IsWindow(notifyWindow))
            PostMessageW(notifyWindow, WM_UPDATE_DONE, static_cast<WPARAM>(count), ok ? 1 : 0);
    });
    return true;
}

std::vector<AvailableUpdate> UpdateChecker::GetUpdates() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_updates;
}

std::wstring UpdateChecker::GetError() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_error;
}

int UpdateChecker::Apply(std::vector<DriverInfo>& drivers, const std::vector<AvailableUpdate>& updates)
{
    int marked = 0;
    for (auto& d : drivers)
    {
        // Zuruecksetzen, falls ein vorheriger Abgleich Updates markiert hatte
        if (d.status == DriverStatus::UpdateAvailable)
            d.status = DriverStatus::Ok;
        d.updateTitle.clear();
        d.updateId.clear();
        d.updateUrl.clear();
        d.updateDate.clear();
        if (d.status != DriverStatus::Ok)
            continue;

        const AvailableUpdate* best = nullptr;
        for (const auto& u : updates)
        {
            if (std::find(d.hardwareIds.begin(), d.hardwareIds.end(), u.hardwareId) == d.hardwareIds.end())
                continue;
            if (d.driverDateKey != 0 && u.dateKey <= d.driverDateKey)
                continue;   // installierter Treiber ist gleich neu oder neuer
            if (!best || u.dateKey > best->dateKey)
                best = &u;
        }
        if (best)
        {
            d.status = DriverStatus::UpdateAvailable;
            d.updateTitle = best->title;
            d.updateId = best->id;
            d.updateDate = best->dateText;
            ++marked;
        }
    }
    return marked;
}
