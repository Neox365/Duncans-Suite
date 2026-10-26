#include "DeviceRepair.h"

#include <setupapi.h>
#include <cfgmgr32.h>

#include "SuiteSafety.h"

#ifdef _MSC_VER
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")
#endif

namespace
{
    struct CodeText
    {
        unsigned long code;
        const wchar_t* title;
        const wchar_t* advice;
        bool fixable;
    };

    const CodeText kCodes[] = {
        { 1,  L"Das Gerät ist nicht richtig konfiguriert", L"Der Treiber ist unvollständig. Ein Neustart des Geräts oder eine Neuerkennung behebt das oft.", true },
        { 3,  L"Der Treiber ist beschädigt oder es fehlt Arbeitsspeicher", L"Ein Neustart des Geräts oder eine Neuerkennung hilft oft; sonst den PC neu starten.", true },
        { 10, L"Das Gerät kann nicht gestartet werden", L"Häufig bei USB-Geräten: ein Neustart oder eine Neuerkennung des Geräts löst das meist. Hilft das nicht, anderen USB-Anschluss oder anderes Kabel probieren.", true },
        { 12, L"Ressourcenkonflikt mit einem anderen Gerät", L"Ein Neustart des PCs löst Konflikte meist.", true },
        { 14, L"Das Gerät funktioniert erst nach einem Neustart", L"Bitte den PC neu starten.", false },
        { 18, L"Die Treiber müssen neu installiert werden", L"Das Gerät wird entfernt und von Windows neu erkannt.", true },
        { 19, L"Die Konfiguration ist in der Registry fehlerhaft", L"Das Gerät wird entfernt und von Windows neu erkannt.", true },
        { 21, L"Windows entfernt das Gerät gerade", L"Kurz warten oder den PC neu starten.", false },
        { 22, L"Das Gerät ist deaktiviert", L"Das Gerät wird wieder aktiviert.", true },
        { 24, L"Das Gerät ist nicht vorhanden oder funktioniert nicht", L"Gerät und Kabel prüfen, anderen Anschluss versuchen.", true },
        { 28, L"Für das Gerät ist kein Treiber installiert", L"Windows sucht beim Neuerkennen einen passenden Treiber. Sonst über „Nach Updates suchen“ oder die Herstellerseite installieren.", true },
        { 29, L"Das Gerät ist in der Firmware abgeschaltet", L"Im BIOS/UEFI aktivieren; per Software nicht lösbar.", false },
        { 31, L"Windows kann den Treiber nicht laden", L"Ein Neustart oder eine Neuerkennung des Geräts hilft oft.", true },
        { 32, L"Der Treiberdienst ist deaktiviert", L"Das Gerät wird entfernt und neu erkannt; der Dienst wird dabei neu eingerichtet.", true },
        { 33, L"Die Geräteressourcen konnten nicht ermittelt werden", L"Ein Neustart des PCs hilft meist.", true },
        { 37, L"Der Treiber konnte nicht initialisiert werden", L"Ein Neustart oder eine Neuerkennung hilft oft; sonst einen neueren Treiber installieren.", true },
        { 38, L"Ein früherer Treiber war noch geladen", L"Bitte den PC neu starten.", false },
        { 39, L"Der Treiber ist beschädigt oder fehlt", L"Das Gerät wird entfernt und neu erkannt, Windows lädt dabei den Treiber neu.", true },
        { 40, L"Die Dienstangaben des Treibers sind ungültig", L"Das Gerät wird entfernt und neu erkannt.", true },
        { 41, L"Der Treiber wurde geladen, aber das Gerät nicht gefunden", L"Gerät neu einstecken oder neu erkennen lassen.", true },
        { 42, L"Das Gerät ist doppelt vorhanden", L"Ein Neustart des PCs löst das meist.", false },
        { 43, L"Windows hat das Gerät angehalten, weil es Probleme meldete", L"Typisch bei USB-Geräten, auch bei defekten Sticks oder Kabeln. Neustart/Neuerkennung hilft oft; sonst anderen Anschluss oder anderes Kabel probieren.", true },
        { 44, L"Eine Anwendung oder ein Dienst hat das Gerät angehalten", L"Ein Neustart des Geräts hilft meist.", true },
        { 45, L"Das Gerät ist nicht angeschlossen", L"Gerät anschließen; per Software nichts zu tun.", false },
        { 47, L"Das Gerät wird zum Auswerfen vorbereitet", L"Gerät abziehen und neu einstecken.", false },
        { 48, L"Windows blockiert den Treiber (bekannte Probleme)", L"Einen neueren Treiber über „Nach Updates suchen“ oder die Herstellerseite installieren.", false },
        { 52, L"Die digitale Signatur des Treibers ist nicht prüfbar", L"Einen signierten, aktuellen Treiber des Herstellers installieren.", false },
    };

    const CodeText* Find(unsigned long code)
    {
        for (const auto& c : kCodes)
            if (c.code == code)
                return &c;
        return nullptr;
    }

    enum class State { Gone, Problem, Ok };

    State Probe(const std::wstring& id, DEVINST& inst, ULONG& problem, ULONG& status)
    {
        problem = 0;
        status = 0;
        std::wstring copy = id;
        if (CM_Locate_DevNodeW(&inst, &copy[0], CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
            return State::Gone;
        if (CM_Get_DevNode_Status(&status, &problem, inst, 0) != CR_SUCCESS)
            return State::Gone;
        return problem != 0 ? State::Problem : State::Ok;
    }

    // Wartet bis zu maxMs, ob das Problem verschwindet bzw. das Geraet wieder auftaucht.
    State WaitForState(const std::wstring& id, unsigned long maxMs, ULONG& problem, ULONG& status)
    {
        DEVINST inst = 0;
        State s = Probe(id, inst, problem, status);
        for (unsigned long waited = 0; s != State::Ok && waited < maxMs; waited += 500)
        {
            Sleep(500);
            s = Probe(id, inst, problem, status);
        }
        return s;
    }

    bool RemoveDevice(const std::wstring& id)
    {
        HDEVINFO set = SetupDiCreateDeviceInfoList(nullptr, nullptr);
        if (set == INVALID_HANDLE_VALUE)
            return false;
        SP_DEVINFO_DATA dd{};
        dd.cbSize = sizeof(dd);
        bool ok = SetupDiOpenDeviceInfoW(set, id.c_str(), nullptr, 0, &dd) &&
                  SetupDiCallClassInstaller(DIF_REMOVE, set, &dd);
        SetupDiDestroyDeviceInfoList(set);
        return ok;
    }

    void ReenumerateAll()
    {
        DEVINST root = 0;
        if (CM_Locate_DevNodeW(&root, nullptr, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS)
            CM_Reenumerate_DevNode(root, CM_REENUMERATE_SYNCHRONOUS);
    }

    bool StartsWithNoCase(const std::wstring& s, const wchar_t* prefix)
    {
        const size_t n = wcslen(prefix);
        return s.size() >= n && CompareStringOrdinal(s.c_str(), static_cast<int>(n), prefix, static_cast<int>(n), TRUE) == CSTR_EQUAL;
    }
}

namespace devrepair
{
    std::wstring ProblemTitle(unsigned long code)
    {
        const CodeText* c = Find(code);
        return L"Code " + std::to_wstring(code) + L" – " + (c ? c->title : L"Das Gerät meldet ein Problem");
    }

    std::wstring ProblemAdvice(unsigned long code)
    {
        const CodeText* c = Find(code);
        return c ? c->advice : L"Ein Neustart oder eine Neuerkennung des Geräts hilft oft; sonst neueren Treiber installieren.";
    }

    bool CanTryRepair(unsigned long code)
    {
        const CodeText* c = Find(code);
        return c ? c->fixable : true;
    }

    bool MayReinstall(const DriverInfo& d)
    {
        if (StartsWithNoCase(d.instanceId, L"USB\\") || StartsWithNoCase(d.instanceId, L"USBSTOR\\"))
            return true;
        switch (d.icon)
        {
        case CategoryIcon::Usb: case CategoryIcon::Bluetooth: case CategoryIcon::Camera: case CategoryIcon::Audio:
        case CategoryIcon::Input: case CategoryIcon::Port: case CategoryIcon::Printer: case CategoryIcon::Network:
            return true;
        default:   // Grafik, Speicher-Controller, Prozessor, Systemgeraete, Monitor, Akku: Entfernen koennte das System stoeren
            return false;
        }
    }

    Result Repair(const DriverInfo& d)
    {
        Result r;
        r.deviceName = d.deviceName;
        const std::wstring& id = d.instanceId;
        auto note = [&](const std::wstring& s) { r.steps.push_back(s); };

        DEVINST inst = 0;
        ULONG problem = 0, status = 0;
        State s = Probe(id, inst, problem, status);
        if (id.empty() || s == State::Gone)
        {
            r.summary = L"Das Gerät ist nicht mehr vorhanden (abgezogen?).";
            return r;
        }
        if (s == State::Ok)
        {
            r.fixed = true;
            r.summary = L"Das Problem besteht nicht mehr.";
            return r;
        }
        if (!CanTryRepair(problem))
        {
            r.summary = ProblemTitle(problem) + L". " + ProblemAdvice(problem);
            return r;
        }

        if (suite_safety::AutoRestorePointEnabled())
        {
            std::wstring rp;
            note(suite_safety::CreateRestorePoint(10 /* DEVICE_DRIVER_INSTALL */, rp) ? L"Wiederherstellungspunkt erstellt"
                                                                                      : L"Kein Wiederherstellungspunkt: " + rp);
        }
        else
            note(L"Wiederherstellungspunkt in den Suite-Einstellungen ausgeschaltet");

        auto finish = [&](State st, const wchar_t* how) -> bool
        {
            if (st == State::Ok)
            {
                r.fixed = true;
                r.summary = std::wstring(L"Behoben: ") + how + L".";
                r.rebootNeeded = (status & DN_NEED_RESTART) != 0;
                if (r.rebootNeeded)
                    r.summary += L" Ein Neustart wird empfohlen.";
                return true;
            }
            return false;
        };

        // 1. Deaktiviert (22): nur aktivieren
        if (problem == 22)
        {
            const CONFIGRET cr = CM_Enable_DevNode(inst, 0);
            note(cr == CR_SUCCESS ? L"Gerät aktiviert" : L"Aktivieren fehlgeschlagen (CR " + std::to_wstring(cr) + L")");
            if (finish(WaitForState(id, 4000, problem, status), L"das Gerät wurde wieder aktiviert"))
                return r;
            r.summary = L"Aktivieren hat nicht geholfen: " + ProblemTitle(problem) + L".";
            return r;
        }

        // 2. Geraet neu starten (ausschalten, einschalten)
        {
            const CONFIGRET off = CM_Disable_DevNode(inst, 0);
            Sleep(800);
            const CONFIGRET on = CM_Enable_DevNode(inst, 0);
            note(L"Gerät neu gestartet" + std::wstring(off == CR_SUCCESS && on == CR_SUCCESS ? L"" : L" (teilweise fehlgeschlagen)"));
            if (finish(WaitForState(id, 5000, problem, status), L"das Gerät wurde neu gestartet"))
                return r;
        }

        // 3. Anschluss/Bus neu einlesen
        {
            DEVINST parent = 0;
            if (Probe(id, inst, problem, status) != State::Gone && CM_Get_Parent(&parent, inst, 0) == CR_SUCCESS)
                CM_Reenumerate_DevNode(parent, CM_REENUMERATE_SYNCHRONOUS);
            note(L"Anschluss neu eingelesen");
            if (finish(WaitForState(id, 5000, problem, status), L"der Anschluss wurde neu eingelesen"))
                return r;
        }

        // 4. Entfernen und von Windows neu erkennen lassen (laedt den Treiber neu)
        if (MayReinstall(d))
        {
            const bool removed = RemoveDevice(id);
            note(removed ? L"Gerät entfernt" : L"Entfernen fehlgeschlagen (Fehler " + std::to_wstring(GetLastError()) + L")");
            if (removed)
            {
                ReenumerateAll();
                note(L"Geräte neu erkannt");
                if (finish(WaitForState(id, 12000, problem, status), L"das Gerät wurde neu erkannt und der Treiber neu geladen"))
                    return r;
            }
        }
        else
            note(L"Entfernen und Neuerkennung bei diesem Gerätetyp bewusst übersprungen (könnte das System stören)");

        DEVINST dummy = 0;
        if (Probe(id, dummy, problem, status) == State::Gone)
            r.summary = L"Nach der Neuerkennung wurde das Gerät nicht mehr gefunden. Bitte neu einstecken oder den PC neu starten.";
        else
            r.summary = L"Nicht behoben: " + ProblemTitle(problem) + L". " + ProblemAdvice(problem) +
                        L" Bleibt das Problem, kann das Gerät, das Kabel oder der Anschluss defekt sein.";
        return r;
    }
}
