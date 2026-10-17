#include "UpdateInstaller.h"

#include "Dispatch.h"
#include "Utils.h"

#include <algorithm>

using namespace dispatch;

namespace
{
    // ---- Systemwiederherstellung (srclient.dll, dynamisch geladen) --------------------------

#pragma pack(push, 1)
    struct RestorePointInfo
    {
        DWORD   dwEventType;
        DWORD   dwRestorePtType;
        INT64   llSequenceNumber;
        WCHAR   szDescription[256];
    };
    struct StateMgrStatus
    {
        DWORD nStatus;
        INT64 llSequenceNumber;
    };
#pragma pack(pop)

    constexpr DWORD kBeginSystemChange = 100;
    constexpr DWORD kEndSystemChange   = 101;
    constexpr DWORD kDeviceDriverInstall = 10;

    using SRSetRestorePointFn = BOOL(WINAPI*)(RestorePointInfo*, StateMgrStatus*);

    // Legt einen Wiederherstellungspunkt an. Gibt false + Hinweistext zurueck, wenn das nicht geht
    // (Systemschutz aus, oder Windows erlaubt standardmaessig nur einen Punkt pro 24 h).
    bool CreateRestorePoint(std::wstring& note)
    {
        HMODULE lib = LoadLibraryW(L"srclient.dll");
        if (!lib)
        {
            note = L"Systemwiederherstellung nicht verfügbar";
            return false;
        }
        auto fn = reinterpret_cast<SRSetRestorePointFn>(reinterpret_cast<void*>(GetProcAddress(lib, "SRSetRestorePointW")));
        bool ok = false;
        if (fn)
        {
            RestorePointInfo info{};
            info.dwEventType = kBeginSystemChange;
            info.dwRestorePtType = kDeviceDriverInstall;
            lstrcpynW(info.szDescription, L"Duncan's Driver Updater: Treiber-Update", 256);
            StateMgrStatus status{};
            if (fn(&info, &status) && status.nStatus == 0)
            {
                ok = true;
                RestorePointInfo end{};
                end.dwEventType = kEndSystemChange;
                end.llSequenceNumber = status.llSequenceNumber;
                StateMgrStatus endStatus{};
                fn(&end, &endStatus);
            }
            else
            {
                note = L"Wiederherstellungspunkt nicht erstellt (Systemschutz deaktiviert oder bereits einer in den letzten 24 h)";
            }
        }
        else
        {
            note = L"Systemwiederherstellung nicht verfügbar";
        }
        FreeLibrary(lib);
        return ok;
    }

    // ---- Eigentlicher Ablauf -----------------------------------------------------------------

    void Run(HWND notify, const std::vector<std::wstring>& ids, InstallResult& r)
    {
        auto stage = [&](InstallStage s)
        {
            if (notify && IsWindow(notify))
                PostMessageW(notify, WM_INSTALL_STAGE, static_cast<WPARAM>(s), 0);
        };

        stage(StageRestorePoint);
        r.restorePointCreated = CreateRestorePoint(r.restorePointNote);

        Disp session = CreateByProgId(L"Microsoft.Update.Session");
        if (!session)
        {
            r.error = L"Windows-Update-Agent konnte nicht gestartet werden";
            return;
        }

        // Updates erneut suchen und per UpdateID filtern
        stage(StageSearching);
        Disp searcher = GetObject(session.p, L"CreateUpdateSearcher", DISPATCH_METHOD);
        if (!searcher)
        {
            r.error = L"Update-Suche konnte nicht erstellt werden";
            return;
        }
        VARIANT criteria = BstrArg(L"IsInstalled=0 and Type='Driver' and IsHidden=0");
        Disp found = GetObject(searcher.p, L"Search", DISPATCH_METHOD, { criteria });
        VariantClear(&criteria);
        Disp all = found ? GetObject(found.p, L"Updates") : Disp();
        if (!all)
        {
            r.error = L"Update-Suche fehlgeschlagen (offline?)";
            return;
        }

        Disp selection = CreateByProgId(L"Microsoft.Update.UpdateColl");
        if (!selection)
        {
            r.error = L"Update-Auswahl konnte nicht erstellt werden";
            return;
        }

        const long count = GetLong(all.p, L"Count");
        for (long i = 0; i < count; ++i)
        {
            Disp update = GetObject(all.p, L"Item", DISPATCH_PROPERTYGET, { LongArg(i) });
            if (!update)
                continue;
            Disp identity = GetObject(update.p, L"Identity");
            const std::wstring id = identity ? GetString(identity.p, L"UpdateID") : std::wstring();
            if (std::find(ids.begin(), ids.end(), id) == ids.end())
                continue;

            // Updates mit nicht akzeptierter Lizenz werden nicht automatisch bestaetigt.
            if (!GetBool(update.p, L"EulaAccepted"))
                continue;
            VARIANT item;
            VariantInit(&item);
            item.vt = VT_DISPATCH;
            item.pdispVal = update.p;
            Invoke(selection.p, L"Add", DISPATCH_METHOD, nullptr, { item });
        }

        const long selected = GetLong(selection.p, L"Count");
        r.skipped = std::max(0, static_cast<int>(ids.size()) - static_cast<int>(selected));   // nicht mehr verfuegbar / EULA offen
        if (selected == 0)
        {
            r.error = L"Keines der Updates ist (noch) verfügbar oder installierbar";
            return;
        }

        // Herunterladen
        stage(StageDownloading);
        Disp downloader = GetObject(session.p, L"CreateUpdateDownloader", DISPATCH_METHOD);
        if (!downloader || FAILED(PutObject(downloader.p, L"Updates", selection.p)))
        {
            r.error = L"Download konnte nicht vorbereitet werden";
            return;
        }
        Disp dlResult = GetObject(downloader.p, L"Download", DISPATCH_METHOD);
        if (!dlResult)
        {
            r.error = L"Download der Treiber fehlgeschlagen";
            return;
        }
        const long dlCode = GetLong(dlResult.p, L"ResultCode");   // 2 = ok, 3 = ok mit Fehlern
        if (dlCode != 2 && dlCode != 3)
        {
            r.error = L"Download der Treiber fehlgeschlagen (Code " + std::to_wstring(dlCode) + L")";
            return;
        }

        // Installieren
        stage(StageInstalling);
        Disp installer = GetObject(session.p, L"CreateUpdateInstaller", DISPATCH_METHOD);
        if (!installer || FAILED(PutObject(installer.p, L"Updates", selection.p)))
        {
            r.error = L"Installation konnte nicht vorbereitet werden";
            return;
        }
        PutBool(installer.p, L"AllowSourcePrompts", false);
        Disp result = GetObject(installer.p, L"Install", DISPATCH_METHOD);
        if (!result)
        {
            r.error = L"Installation fehlgeschlagen (Administratorrechte?)";
            return;
        }
        r.rebootRequired = GetBool(result.p, L"RebootRequired");

        // Ergebnis pro Update auswerten
        for (long i = 0; i < selected; ++i)
        {
            VARIANT idx = LongArg(i);
            Disp item = GetObject(result.p, L"GetUpdateResult", DISPATCH_METHOD, { idx });
            const long code = item ? GetLong(item.p, L"ResultCode") : 0;
            if (code == 2 || code == 3)
                ++r.installed;
            else
                ++r.failed;
        }
    }
}

UpdateInstaller::~UpdateInstaller()
{
    Wait();
}

void UpdateInstaller::Join()
{
    if (m_thread.joinable())
        m_thread.join();
}

void UpdateInstaller::Wait()
{
    Join();
}

bool UpdateInstaller::StartAsync(HWND notifyWindow, std::vector<std::wstring> updateIds)
{
    if (updateIds.empty() || m_running.exchange(true))
        return false;
    Join();

    m_thread = std::thread([this, notifyWindow, ids = std::move(updateIds)]()
    {
        InstallResult result;
        if (!IsRunningAsAdmin())
        {
            result.error = L"Zum Installieren werden Administratorrechte benötigt";
        }
        else if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
        {
            Run(notifyWindow, ids, result);
            CoUninitialize();
        }
        else
        {
            result.error = L"COM-Initialisierung fehlgeschlagen";
        }

        const int installed = result.installed;
        const bool ok = result.error.empty();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_result = std::move(result);
        }
        m_running = false;
        if (notifyWindow && IsWindow(notifyWindow))
            PostMessageW(notifyWindow, WM_INSTALL_DONE, static_cast<WPARAM>(installed), ok ? 1 : 0);
    });
    return true;
}

InstallResult UpdateInstaller::GetResult() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_result;
}
