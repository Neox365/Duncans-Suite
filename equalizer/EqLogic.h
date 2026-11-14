#pragma once
// Duncan's Equalizer - Logik: Regler/Voreinstellungen, Konfigurationstext fuer Equalizer APO, Speichern/Laden und
// Eintragen in den Equalizer-APO-Ordner. Keine Oberflaeche. Der Ton selbst wird von Equalizer APO (frei, eigener Audio-Filter)
// verarbeitet; dieses Programm schreibt nur dessen Konfiguration.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace eq {

constexpr int kBands = 10;      // 31 Hz ... 16 kHz
constexpr int kMaxGain = 12;    // Regler: -12 ... +12 dB
constexpr int kMaxBalance = 12; // Balance: -12 ... +12 dB (negativ: links lauter, positiv: rechts lauter)
constexpr unsigned kMaxIrBytes = 24u << 20;   // groesste erlaubte Impulsantwort (WAV)
extern const int kFreqs[kBands];
const wchar_t* BandName(int band);   // kurzes Wort, z. B. "Waerme"
const wchar_t* BandHint(int band);   // ein Satz: was der Regler macht und was zu viel/zu wenig bewirkt

struct Settings {
    bool enabled = false;       // Equalizer an/aus
    int  gains[kBands] = {};    // dB je Band
    bool bass = false;          // zusaetzlicher Bass-Schub
    bool voice = false;         // Sprache klarer
    int  preamp = 0;            // Vorverstaerkung in dB (-12 ... +12), kommt zur automatischen Absenkung dazu
    bool hotkeys = true;        // globale Tastenkuerzel: Strg+Alt+F9 an/aus, F10 naechstes, F8 voriges Profil
    bool autoGain = false;      // Auto-Lautstaerke (Beta): Vorverstaerkung passt sich der Musik an
    bool spectrum = true;       // Live-Spektrum im Frequenzgang-Diagramm
    bool keepRunning = true;    // Fenster schliessen = im Infobereich weiterlaufen (Profilwechsel pro Geraet, Symbol)
    std::vector<std::string> extra;   // eingelesene Filterzeilen (z. B. AutoEq-Kopfhoerer-Korrektur), nur "Preamp:"/"Filter:"
    int  balance = 0;           // Links/Rechts-Ausgleich in dB: negativ = links lauter (rechts wird leiser), positiv = rechts lauter
    bool mono = false;          // beide Seiten bekommen denselben Ton (Mittelwert aus links und rechts)
    std::wstring irPath;        // Impulsantwort (WAV) fuer Raum-/Kopfhoerer-Korrektur per Faltung; leer = keine. Nur auf diesem PC (nicht in weitergegebenen Profilen)
};

// Ein Profil = ein Satz Einstellungen (Regler, Schalter, Korrektur) mit Namen und optional einem Ausgabegeraet,
// bei dem es automatisch gewaehlt wird. "enabled" in s wird nicht benutzt (Ein/Aus gilt global).
struct Profile {
    std::wstring name;
    std::wstring device;        // Anzeigename des Ausgabegeraets oder leer
    std::vector<std::wstring> apps;   // Programme (kleingeschriebener Dateiname ohne .exe): spielt eines Ton, wird dieses Profil gewaehlt
    Settings s;
};
constexpr int kMaxProfiles = 6;
constexpr size_t kMaxNameLen = 24;
constexpr size_t kMaxExtraLines = 40;

struct Preset {
    const wchar_t* name;
    int gains[kBands];
};

int PresetCount();
const Preset& PresetAt(int index);
int MatchPreset(const Settings& s);          // Index der Voreinstellung, die zu den Reglern passt, sonst -1
int ClampGain(int db);

// Automatische Vorverstaerkung (<= 0 dB), damit die Lautstaerke durch Anhebungen, Bass und Sprache nicht uebersteuert
int PreampDb(const Settings& s);
// Gesamt: automatische Absenkung plus eigener Regler, begrenzt auf -30 ... +12 dB; das schreibt BuildConfig
int TotalPreampDb(const Settings& s);

// Text der Konfigurationsdatei fuer Equalizer APO (ASCII, Zeilenende CRLF). irFile: absoluter Pfad der Impulsantwort, wie APO ihn lesen soll
// (leer = keine Faltung; unsichere Pfade werden verworfen).
std::string BuildConfig(const Settings& s, const std::string& irFile = std::string());
// Prueft eine Impulsantwort: vorhanden, hoechstens kMaxIrBytes, WAV-Kopf. err = Meldung fuer den Nutzer.
bool CheckImpulseFile(const std::wstring& path, std::wstring& err);
extern const wchar_t* const kIrCopyName;      // "duncans-ir.wav": Kopie der Impulsantwort im config-Ordner von Equalizer APO

// Profile (%LOCALAPPDATA%\DuncansEqualizer\profiles.txt)
std::wstring ProfilesPath();
bool LoadProfiles(std::vector<Profile>& list, int& active, const std::wstring& path);   // false: Datei fehlt (list bleibt leer, active = 0)
bool SaveProfiles(const std::vector<Profile>& list, int active, const std::wstring& path);
bool ParseProfiles(const std::string& data, std::vector<Profile>& list, int& active);   // Inhalt der Profildatei; false, wenn kein gueltiges Profil darin ist
// Ein einzelnes Profil als Textdatei weitergeben (ohne Geraetebindung) und wieder einlesen. Nur das erste Profil der Datei zaehlt; zu grosse Dateien (> 64 KB) werden abgelehnt.
std::string ProfileToText(const Profile& p);
bool ProfileFromText(const std::string& text, Profile& out);
// Name fuer ein eingelesenes Profil, der in list noch nicht vorkommt ("Name", "Name (2)", ...), auf kMaxNameLen gekuerzt
std::wstring UniqueProfileName(const std::vector<Profile>& list, const std::wstring& wanted);
std::wstring CleanName(std::wstring name);                                  // gekuerzt, ohne Steuer-/Sonderzeichen; leer = ungueltig
int FindProfile(const std::vector<Profile>& list, const std::wstring& name);   // ohne Gross-/Kleinschreibung, -1 wenn nicht da
int ProfileForDevice(const std::vector<Profile>& list, const std::wstring& device);   // -1 wenn keines gebunden

// Liest eine Parametric-EQ-Datei (z. B. von AutoEq): nur "Preamp:" und "Filter:"-Zeilen mit harmlosen Zeichen werden uebernommen
// ("Filter 3:" wird zu "Filter:"). ignored = Anzahl nicht uebernommener Zeilen.
std::vector<std::string> ParseFilterFile(const std::string& text, int& ignored);

// Profil fuer gerade Ton ausgebende Programme: das erste Profil, dessen Programmliste ein aktives Programm enthaelt; bleibt das
// bevorzugte Profil (preferred, meist das gerade aktive) passend, gewinnt es. -1, wenn keines passt.
int ProfileForApps(const std::vector<Profile>& list, const std::vector<std::wstring>& activeKeys, int preferred);
constexpr size_t kMaxProfileApps = 8;
// Auch eine einzelne "GraphicEQ: 20 -1.5; 25 ..."-Zeile (AutoEq GraphicEQ.txt) wird uebernommen.

// Fuer den A/B-Vergleich: Einstellungen "Original hoeren", nur mit der Vorverstaerkung (ungefaehr gleich laut wie mit Equalizer)
Settings BypassSettings(const Settings& s);

// Anzeigename des Standard-Ausgabegeraets (leer, wenn nicht ermittelbar)
std::wstring DefaultOutputName();

// Eigene Einstellungen (%LOCALAPPDATA%\DuncansEqualizer\settings.txt)
std::wstring SettingsPath();
bool LoadSettings(Settings& s, const std::wstring& path);   // false: Datei fehlt/unlesbar (s bleibt Standard)
bool SaveSettings(const Settings& s, const std::wstring& path);

// Autostart mit Windows (HKCU\Software\Microsoft\Windows\CurrentVersion\Run, Start mit --background)
bool GetAutostart(const wchar_t* valueName = L"DuncansEqualizer");
bool SetAutostart(bool on, const std::wstring& exePath, const wchar_t* valueName = L"DuncansEqualizer");

// Equalizer APO
extern const wchar_t* const kConfigFile;     // "duncans-equalizer.txt" im config-Ordner von Equalizer APO
std::wstring FindInstallDir();               // leer, wenn Equalizer APO nicht installiert ist
enum class ApplyResult { Applied = 0, NotInstalled = 1, AccessDenied = 2, Failed = 3 };
// Schreibt <installDir>\config\duncans-equalizer.txt und sorgt fuer die Include-Zeile in config.txt
ApplyResult ApplyToDir(const std::wstring& installDir, const Settings& s);

}  // namespace eq
