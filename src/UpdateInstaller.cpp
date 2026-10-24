#include "UpdateInstaller.h"

#include "Dispatch.h"
#include "SuiteSafety.h"
#include "Utils.h"

#include <algorithm>

using namespace dispatch;

namespace
{
    // ---- Systemwiederherstellung: gemeinsame Implementierung (SuiteSafety, Name "Duncan's Suite Pre-Optimization") ----
    bool CreateRestorePoint(std::wstring& note)
    {
        const bool ok = suite_safety::CreateRestorePoint(10 /* DEVICE_DRIVER_INSTALL */, note);
        if (!ok && note.empty()) note = L"Wiederherstellungspunkt nicht erstellt";
        return ok;
    }

    // ---- Fehlerangaben -------------------------------------------------------------------------

    std::wstring Hex(long hr)
    {
        wchar_t buf[16];
        swprintf_s(buf, L"0x%08X", static_cast<unsigned long>(hr));
        return buf;
    }

    const wchar_t* HResultHint(long hr)
    {
        switch (static_cast<unsigned long>(hr))
        {
        case 0x80240016: return L"Windows installiert gerade etwas anderes oder wartet auf einen Neustart";
        case 0x80240017: return L"Der Treiber ist für dieses Gerät nicht anwendbar";
        case 0x8024001E: return L"Der Windows-Update-Dienst wurde währenddessen beendet";
        case 0x80240022: return L"Alle Updates sind fehlgeschlagen";
        case 0x8024200B: return L"Das Treiberpaket ließ sich nicht installieren (passt evtl. nicht zum Gerät oder ist bereits neuer vorhanden)";
        case 0x80070643: return L"Schwerwiegender Fehler bei der Installation";
        case 0x80070005: return L"Zugriff verweigert (Virenscanner oder fehlende Rechte)";
        case 0x80248007: return L"Update-Informationen fehlen (Windows Update neu starten)";
        case 0x8024402C:
        case 0x80072EE2:
        case 0x80072EFD: return L"Keine Verbindung zu Windows Update (Netzwerk, Proxy oder Firewall)";
        case 0x800F0922: return L"Installation nicht möglich (Systempartition zu klein oder beschädigt)";
        default: return nullptr;
        }
    }

    // Eine Problemzeile fuer Dialog und Log: "Titel – 0x… (Hinweis)"
    std::wstring Problem(const std::wstring& title, long code, long hr, const wchar_t* what)
    {
        std::wstring line = (title.empty() ? std::wstring(L"Treiber") : title) + L" – " + what;
        if (hr != 0)
            line += L" " + Hex(hr);
        else if (code != 0)
            line += L" (Code " + std::to_wstring(code) + L")";
        if (const wchar_t* h = HResultHint(hr))
            line += std::wstring(L": ") + h;
        return line;
    }

    // ---- Eigentlicher Ablauf -----------------------------------------------------------------

    void Run(HWND notify, const std::vector<std::wstring>& ids, InstallResult& r)
    {
        auto stage = [&](InstallStage s)
        {
            if (notify && IsWindow(notify))
                PostMessageW(notify, WM_INSTALL_STAGE, static_cast<WPARAM>(s), 0);
        };

        {
            Disp sysInfo = CreateByProgId(L"Microsoft.Update.SystemInfo");
            if (sysInfo && GetBool(sysInfo.p, L"RebootRequired"))
            {
                r.error = L"Windows wartet noch auf einen Neustart (zum Beispiel von einer früheren Treiber-Installation). Bitte den PC zuerst neu starten und die Suche danach wiederholen. Bis dahin schlagen weitere Installationen fehl und die Updates bleiben in der Liste.";
                return;
            }
        }

        stage(StageRestorePoint);
        if (suite_safety::AutoRestorePointEnabled())
            r.restorePointCreated = CreateRestorePoint(r.restorePointNote);
        else
        {
            r.restorePointCreated = false;
            r.restorePointNote = L"Automatischer Wiederherstellungspunkt ist in den Suite-Einstellungen ausgeschaltet";
            suite_safety::Log(L"DriverUpdater", r.restorePointNote);
        }

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

        std::vector<Disp> picked;
        std::vector<std::wstring> titles;
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
            const std::wstring title = GetString(update.p, L"Title");

            // Updates mit nicht akzeptierter Lizenz werden nicht automatisch bestaetigt.
            if (!GetBool(update.p, L"EulaAccepted"))
            {
                r.details.push_back(title + L" – Lizenz noch nicht bestätigt, bitte über Windows Update installieren");
                continue;
            }
            // Manche Treiber brauchen waehrend der Installation eine Eingabe; das geht ohne Fenster nicht
            Disp behavior = GetObject(update.p, L"InstallationBehavior");
            if (behavior && GetBool(behavior.p, L"CanRequestUserInput"))
            {
                r.details.push_back(title + L" – braucht eine Eingabe während der Installation, bitte über Windows Update installieren");
                continue;
            }
            VARIANT item;
            VariantInit(&item);
            item.vt = VT_DISPATCH;
            item.pdispVal = update.p;
            Invoke(selection.p, L"Add", DISPATCH_METHOD, nullptr, { item });
            titles.push_back(title);
            picked.push_back(std::move(update));
        }

        const long selected = static_cast<long>(picked.size());
        r.skipped = std::max(0, static_cast<int>(ids.size()) - static_cast<int>(selected));   // nicht mehr verfuegbar / EULA offen / Eingabe noetig
        if (selected == 0)
        {
            r.error = L"Keines der Updates ist (noch) verfügbar oder installierbar";
            for (const std::wstring& d : r.details)
                suite_safety::Log(L"DriverUpdater", d);
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
            r.error = L"Download der Treiber fehlgeschlagen (Code " + std::to_wstring(dlCode) + L", " + Hex(GetLong(dlResult.p, L"HResult")) + L")";
            return;
        }

        // Installieren: jedes Update einzeln, damit ein fehlschlagender Treiber die anderen nicht mitreisst und jeder Fehler seinen eigenen Code hat
        stage(StageInstalling);
        for (long i = 0; i < selected; ++i)
        {
            const std::wstring& title = titles[static_cast<size_t>(i)];
            Disp& update = picked[static_cast<size_t>(i)];
            if (!GetBool(update.p, L"IsDownloaded"))   // Download fuer dieses Update gescheitert (Gesamtergebnis war "mit Fehlern")
            {
                Disp dr = GetObject(dlResult.p, L"GetUpdateResult", DISPATCH_METHOD, { LongArg(i) });
                ++r.failed;
                r.details.push_back(Problem(title, dr ? GetLong(dr.p, L"ResultCode") : 0, dr ? GetLong(dr.p, L"HResult") : 0, L"Download fehlgeschlagen"));
                continue;
            }
            Disp one = CreateByProgId(L"Microsoft.Update.UpdateColl");
            Disp installer = GetObject(session.p, L"CreateUpdateInstaller", DISPATCH_METHOD);
            if (!one || !installer)
            {
                ++r.failed;
                r.details.push_back(title + L" – Installation konnte nicht vorbereitet werden");
                continue;
            }
            VARIANT item;
            VariantInit(&item);
            item.vt = VT_DISPATCH;
            item.pdispVal = update.p;
            Invoke(one.p, L"Add", DISPATCH_METHOD, nullptr, { item });
            if (FAILED(PutObject(installer.p, L"Updates", one.p)))
            {
                ++r.failed;
                r.details.push_back(title + L" – Installation konnte nicht vorbereitet werden");
                continue;
            }
            PutBool(installer.p, L"AllowSourcePrompts", false);
            PutBool(installer.p, L"ForceQuiet", true);
            Disp result = GetObject(installer.p, L"Install", DISPATCH_METHOD);
            if (!result)
            {
                ++r.failed;
                r.details.push_back(title + L" – Installation fehlgeschlagen (Administratorrechte?)");
                continue;
            }
            if (GetBool(result.p, L"RebootRequired"))
                r.rebootRequired = true;
            Disp ur = GetObject(result.p, L"GetUpdateResult", DISPATCH_METHOD, { LongArg(0) });
            const long code = ur ? GetLong(ur.p, L"ResultCode") : 0;
            const long hr = ur ? GetLong(ur.p, L"HResult") : GetLong(result.p, L"HResult");
            if (code == 2 || code == 3)
                ++r.installed;
            else
            {
                ++r.failed;
                r.details.push_back(Problem(title, code, hr, L"Installation fehlgeschlagen"));
            }
        }
        for (const std::wstring& d : r.details)
            suite_safety::Log(L"DriverUpdater", d);
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
        suite_safety::Log(L"DriverUpdater", ok ? L"Treiber-Installation abgeschlossen: " + std::to_wstring(result.installed) + L" installiert, " + std::to_wstring(result.failed) + L" fehlgeschlagen, " + std::to_wstring(result.skipped) + L" übersprungen" + (result.rebootRequired ? L", Neustart nötig" : L"")
                                         : L"Treiber-Installation FEHLER: " + result.error);
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
