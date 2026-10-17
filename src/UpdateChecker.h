#pragma once
// UpdateChecker.h - Fragt ueber den Windows Update Agent (WUA) nach verfuegbaren Treiber-Updates
// und gleicht sie per Hardware-ID mit den gescannten Geraeten ab.
//
// Der WUA wird per COM-Automation (IDispatch) angesprochen; dadurch sind keine speziellen
// SDK-Header noetig. Die Suche laeuft in einem Worker-Thread (kann je nach Netz 10-60 s dauern).
//
// Linker-Bibliotheken: ole32.lib, oleaut32.lib

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "DriverInfo.h"

// WM_UPDATE_DONE: wParam = Anzahl gefundener Updates, lParam = 1 bei Erfolg, 0 bei Fehler
constexpr UINT WM_UPDATE_DONE = WM_APP + 3;

struct AvailableUpdate
{
    std::wstring id;           // Update-ID (Identity.UpdateID)
    std::wstring title;
    std::wstring hardwareId;   // GROSSBUCHSTABEN
    std::wstring provider;
    std::wstring dateText;     // TT.MM.JJJJ
    int dateKey = 0;           // JJJJMMTT
};

class UpdateChecker
{
public:
    UpdateChecker() = default;
    ~UpdateChecker();

    UpdateChecker(const UpdateChecker&) = delete;
    UpdateChecker& operator=(const UpdateChecker&) = delete;

    bool StartAsync(HWND notifyWindow);
    void Cancel();   // wartet, bis eine laufende Suche beendet ist
    bool IsRunning() const { return m_running.load(); }

    std::vector<AvailableUpdate> GetUpdates() const;
    std::wstring GetError() const;

    // Setzt bei passenden Geraeten status=UpdateAvailable (nur wenn der Update-Treiber neuer ist).
    // Gibt die Anzahl der markierten Geraete zurueck.
    static int Apply(std::vector<DriverInfo>& drivers, const std::vector<AvailableUpdate>& updates);

private:
    void Join();

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    mutable std::mutex m_mutex;
    std::vector<AvailableUpdate> m_updates;
    std::wstring m_error;
};
