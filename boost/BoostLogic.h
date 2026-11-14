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

// Spiel-Einstellungen von Windows (HKCU): Spielmodus an, Hintergrundaufzeichnung der Xbox Game Bar aus. Der Zock-Modus stellt sie um und beim
// Ausschalten auf den vorherigen Wert zurueck (ein Eintrag, der vorher fehlte, wird wieder geloescht).
constexpr int kGameTweaks = 3;
struct RegDword { bool exists = false; DWORD value = 0; };
RegDword GetGameTweak(int index);
bool SetGameTweak(int index, const RegDword& v);   // exists = false loescht den Wert
DWORD GameTweakWanted(int index);                  // Zielwert beim Zock-Modus

// ---- Persistenz (INI-Dateien unter %LOCALAPPDATA%\DuncansBoost) ----
struct Settings {
    std::vector<std::wstring> closeApps;  // zu beendende Programme
    std::vector<std::wstring> gameApps;   // erkannte Spiele (Auto-Start)
    bool optPower = true;
    bool optToast = false;
    bool optAuto = false;
    bool optRestart = true;               // beendete Programme nach dem Zock-Modus wieder starten
    bool optDuck = false;                 // Ton: andere Programme leiser, solange ein angehaktes Spiel im Vordergrund ist
    bool optMuteGame = false;             // Ton: Spiel stumm, solange es nicht im Vordergrund ist
    bool optCross = false;                // Fadenkreuz-Overlay
    int crossStyle = 0, crossColor = 0, crossSize = 1;
    bool optClip = false;                 // Video-Clips: Rueckblick-Aufnahme (letzte Sekunden per Tastenkuerzel speichern)
    int clipSeconds = 30;                 // Rueckblick: 15, 30 oder 60 Sekunden
    int clipAudio = 0;                    // Video-Clips: Ton 0 aus, 1 Systemton, 2 Systemton + Mikrofon
    bool optMouse = false;                // Zock-Modus: Mausbeschleunigung ("Zeigerbeschleunigung verbessern") aus, solange er laeuft
    bool optOverlay = false;              // Overlay im Spiel (Statusleiste + Panel)
    int overlayCorner = 1;                // 0 oben links, 1 oben rechts, 2 unten links, 3 unten rechts
    bool overlayStats = true;             // Auslastung in der Statusleiste
};
constexpr int kClipLengths[3] = { 15, 30, 60 };
// Mausbeschleunigung: Windows-Werte (SPI_GETMOUSE: Schwelle 1, Schwelle 2, Beschleunigung 0/1/2). Nur fuer diese Sitzung, nichts wird dauerhaft geschrieben.
bool GetMouseAccel(int v[3]);
bool SetMouseAccel(const int v[3]);
void MouseAccelOff(const int cur[3], int out[3]);       // gleiche Schwellen, Beschleunigung 0
int ClampClipAudio(int a);                // 0 ... 2
const wchar_t* ClipAudioName(int a);      // "aus", "Systemton", "Systemton + Mikrofon"
int ClampClipSeconds(int s);              // auf den naechsten erlaubten Wert (15/30/60)
int NextClipSeconds(int s);               // 15 -> 30 -> 60 -> 15
// Soll gerade aufgenommen werden? Sind Spiele angehakt: nur wenn eines im Vordergrund ist; sonst immer.
bool ClipShouldRecord(const std::vector<std::wstring>& gameExes, const std::wstring& foregroundExe);

// Zustand waehrend aktivem Zock-Modus (ueberlebt Abstuerze: beim naechsten Start laesst sich alles wiederherstellen)
struct State {
    bool active = false;
    bool auto_ = false;                   // vom Spiel-Auto-Start ausgeloest
    bool powerChanged = false;
    GUID prevScheme{};
    bool toastChanged = false;
    ToastState prevToast;
    bool mouseChanged = false;            // Mausbeschleunigung wurde ausgeschaltet
    int prevMouse[3] = { 0, 0, 0 };       // vorherige Maus-Werte (Schwelle 1, Schwelle 2, Beschleunigung)
    unsigned gameMask = 0;                // Bit i gesetzt: Spiel-Einstellung i wurde umgestellt
    RegDword prevGame[kGameTweaks];       // vorheriger Wert je Spiel-Einstellung
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
