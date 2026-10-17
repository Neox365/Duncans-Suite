#pragma once
// UpdateInstaller.h - Installiert ausgewaehlte Treiber-Updates ueber den Windows Update Agent.
//
// Ablauf (im Worker-Thread, benoetigt Administratorrechte):
//   1. Systemwiederherstellungspunkt anlegen (srclient.dll / SRSetRestorePointW)
//   2. Updates erneut suchen und anhand der Update-ID auswaehlen
//   3. Herunterladen
//   4. Installieren
//
// Linker-Bibliotheken: ole32.lib, oleaut32.lib (srclient.dll wird dynamisch geladen)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// WM_INSTALL_STAGE: wParam = InstallStage
// WM_INSTALL_DONE:  wParam = Anzahl erfolgreich installiert, lParam = 1 wenn der Ablauf durchlief, 0 bei Fehler
constexpr UINT WM_INSTALL_STAGE = WM_APP + 4;
constexpr UINT WM_INSTALL_DONE  = WM_APP + 5;

enum InstallStage
{
    StageRestorePoint = 0,
    StageSearching    = 1,
    StageDownloading  = 2,
    StageInstalling   = 3
};

struct InstallResult
{
    int  installed = 0;
    int  failed = 0;
    int  skipped = 0;                // z. B. EULA nicht akzeptiert / nicht mehr verfuegbar
    bool rebootRequired = false;
    bool restorePointCreated = false;
    std::wstring restorePointNote;   // Hinweis, falls kein Wiederherstellungspunkt moeglich war
    std::wstring error;              // leer bei Erfolg
};

class UpdateInstaller
{
public:
    UpdateInstaller() = default;
    ~UpdateInstaller();

    UpdateInstaller(const UpdateInstaller&) = delete;
    UpdateInstaller& operator=(const UpdateInstaller&) = delete;

    // updateIds: Identity.UpdateID der zu installierenden Updates
    bool StartAsync(HWND notifyWindow, std::vector<std::wstring> updateIds);
    void Wait();
    bool IsRunning() const { return m_running.load(); }
    InstallResult GetResult() const;

private:
    void Join();

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    mutable std::mutex m_mutex;
    InstallResult m_result;
};
