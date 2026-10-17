#pragma once
// HardwareScanner.h - Liest per SetupAPI alle vorhandenen Geraete und deren Treiberdaten aus.
// Der Scan laeuft in einem Worker-Thread; das Fenster wird per PostMessage informiert,
// damit das UI nie blockiert.
//
// Linker-Bibliotheken: setupapi.lib, cfgmgr32.lib

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "DriverInfo.h"

// Nachrichten an das Zielfenster
//   WM_SCAN_PROGRESS: wParam = bisher verarbeitet, lParam = Gesamtzahl
//   WM_SCAN_DONE:     wParam = Anzahl Treiber,     lParam = 1 bei Erfolg, 0 bei Fehler/Abbruch
constexpr UINT WM_SCAN_PROGRESS = WM_APP + 1;
constexpr UINT WM_SCAN_DONE     = WM_APP + 2;

class HardwareScanner
{
public:
    HardwareScanner() = default;
    ~HardwareScanner();

    HardwareScanner(const HardwareScanner&) = delete;
    HardwareScanner& operator=(const HardwareScanner&) = delete;

    // Startet den asynchronen Scan. Gibt false zurueck, wenn bereits einer laeuft.
    bool StartAsync(HWND notifyWindow);

    // Bricht einen laufenden Scan ab und wartet auf das Thread-Ende.
    void Cancel();

    bool IsRunning() const { return m_running.load(); }

    // Ergebnisse des letzten abgeschlossenen Scans (Kopie, thread-sicher).
    std::vector<DriverInfo> GetResults() const;

    // Synchroner Scan (z. B. fuer Tests oder Kommandozeile).
    static std::vector<DriverInfo> ScanBlocking(HWND notifyWindow = nullptr,
                                                const std::atomic<bool>* cancel = nullptr);

private:
    void Join();

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_cancel{false};
    mutable std::mutex m_mutex;
    std::vector<DriverInfo> m_results;
};
