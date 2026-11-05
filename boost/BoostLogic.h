#pragma once
// Duncan's Boost - Logik des Zock-Modus: laufende Programme auflisten und beenden, Energiesparplan und Benachrichtigungen
// umstellen, Zustand sichern und wiederherstellen. Keine Oberflaeche, keine Administratorrechte noetig.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace boostmode {

// Laufendes Programm mit sichtbarem Fenster (alle Prozesse mit gleichem EXE-Namen zusammengefasst)
struct AppInfo {
    std::wstring exe;                 // z. B. "spotify.exe" (klein geschrieben)
    std::wstring title;               // Titel des ersten sichtbaren Fensters
    unsigned long long ramBytes = 0;  // Arbeitsspeicher aller Prozesse dieses Programms
    int procCount = 0;
};

// Sperrliste: Systemprozesse und die Suite selbst werden nie angeboten und nie beendet (exe in Kleinbuchstaben)
bool IsProtectedExe(const std::wstring& exeLower);

// Kleinbuchstaben-Kopie
std::wstring Lower(const std::wstring& s);

// Programme mit sichtbarem Hauptfenster im Fenster dieser Sitzung, ohne gesperrte und ohne den eigenen Prozess; nach RAM absteigend
std::vector<AppInfo> ListApps();

// Laeuft mindestens eines der Programme (EXE-Namen, Kleinbuchstaben)?
bool AnyRunning(const std::vector<std::wstring>& exes);

struct CloseResult {
    int closed = 0;                   // Programme, die sich beendet haben
    int forced = 0;                   // davon hart beendet (TerminateProcess)
    int failed = 0;                   // laufen noch
    std::vector<std::wstring> paths;  // vollstaendige Pfade der beendeten Programme (zum Neustarten)
    std::vector<std::wstring> names;  // EXE-Namen, die beendet wurden
};

// Beendet die Programme: erst freundlich (WM_CLOSE an alle Fenster), nach graceMs mit force=true hart.
// Gesperrte Programme und der eigene Prozess werden uebersprungen.
CloseResult CloseApps(const std::vector<std::wstring>& exes, int graceMs, bool force);

// Energiesparplan
bool GetActiveScheme(GUID& g);
bool SetActiveScheme(const GUID& g);
// Bevorzugt "Ultimative Leistung", sonst "Hoechstleistung"; false, wenn keiner vorhanden ist
bool FindPerformanceScheme(GUID& g);

// Windows-Toasts (HKCU\...\PushNotifications\ToastEnabled)
struct ToastState { bool exists = false; DWORD value = 1; };
ToastState GetToast();
bool SetToast(const ToastState& s);       // exists = false loescht den Wert

// ---- Persistenz (INI-Dateien unter %LOCALAPPDATA%\DuncansBoost) ----
struct Settings {
    std::vector<std::wstring> closeApps;  // zu beendende Programme
    std::vector<std::wstring> gameApps;   // erkannte Spiele (Auto-Start)
    bool optPower = true;
    bool optToast = false;
    bool optAuto = false;
    bool optRestart = true;               // beendete Programme nach dem Zock-Modus wieder starten
};

// Zustand waehrend aktivem Zock-Modus (ueberlebt Abstuerze: beim naechsten Start laesst sich alles wiederherstellen)
struct State {
    bool active = false;
    bool auto_ = false;                   // vom Spiel-Auto-Start ausgeloest
    bool powerChanged = false;
    GUID prevScheme{};
    bool toastChanged = false;
    ToastState prevToast;
    std::vector<std::wstring> closedPaths;
};

std::wstring DataDir();                   // %LOCALAPPDATA%\DuncansBoost (wird angelegt)
bool SettingsFileExists();                // false beim allerersten Start (dann Vorauswahl vorschlagen)
// Vorschlag fuer die Vorauswahl beim ersten Start: nur Hintergrundhelfer, die man beim Spielen nicht braucht (Cloud-Sync, Firmenchat).
// Bewusst NICHT dabei: Spiele-Launcher (Steam, Epic, EA, Battle.net: Spiele brauchen sie), Discord/Spotify (laufen oft beim Spielen), Browser.
std::vector<std::wstring> SuggestedHelpers();
void SetDataDirForTest(const std::wstring& dir);

Settings LoadSettings();
bool SaveSettings(const Settings& s);
State LoadState();
bool SaveState(const State& s);

// Teile einer "|"-getrennten Liste
std::wstring Join(const std::vector<std::wstring>& v);
std::vector<std::wstring> Split(const std::wstring& s);
std::wstring GuidToString(const GUID& g);
bool GuidFromString(const std::wstring& s, GUID& g);

// Zock-Modus ein-/ausschalten. Aktivieren sichert zuerst den Zustand (SaveState), dann wird umgestellt.
// note enthaelt eine kurze Zusammenfassung ("3 Programme beendet, Höchstleistung aktiv").
bool Activate(const Settings& cfg, bool autoTriggered, std::wstring& note);
// Stellt Energiesparplan und Benachrichtigungen wieder her und startet beendete Programme neu (wenn cfg.optRestart)
bool Deactivate(const Settings& cfg, std::wstring& note);

}  // namespace boostmode
