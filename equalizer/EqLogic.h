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
extern const int kFreqs[kBands];
const wchar_t* BandName(int band);   // kurzes Wort, z. B. "Waerme"
const wchar_t* BandHint(int band);   // ein Satz: was der Regler macht und was zu viel/zu wenig bewirkt

struct Settings {
    bool enabled = false;       // Equalizer an/aus
    int  gains[kBands] = {};    // dB je Band
    bool bass = false;          // zusaetzlicher Bass-Schub
    bool voice = false;         // Sprache klarer
    int  preamp = 0;            // Vorverstaerkung in dB (-12 ... +12), kommt zur automatischen Absenkung dazu
    std::vector<std::string> extra;   // eingelesene Filterzeilen (z. B. AutoEq-Kopfhoerer-Korrektur), nur "Preamp:"/"Filter:"
};

// Ein Profil = ein Satz Einstellungen (Regler, Schalter, Korrektur) mit Namen und optional einem Ausgabegeraet,
// bei dem es automatisch gewaehlt wird. "enabled" in s wird nicht benutzt (Ein/Aus gilt global).
struct Profile {
    std::wstring name;
    std::wstring device;        // Anzeigename des Ausgabegeraets oder leer
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

// Text der Konfigurationsdatei fuer Equalizer APO (ASCII, Zeilenende CRLF)
std::string BuildConfig(const Settings& s);

// Profile (%LOCALAPPDATA%\DuncansEqualizer\profiles.txt)
std::wstring ProfilesPath();
bool LoadProfiles(std::vector<Profile>& list, int& active, const std::wstring& path);   // false: Datei fehlt (list bleibt leer, active = 0)
bool SaveProfiles(const std::vector<Profile>& list, int active, const std::wstring& path);
std::wstring CleanName(std::wstring name);                                  // gekuerzt, ohne Steuer-/Sonderzeichen; leer = ungueltig
int FindProfile(const std::vector<Profile>& list, const std::wstring& name);   // ohne Gross-/Kleinschreibung, -1 wenn nicht da
int ProfileForDevice(const std::vector<Profile>& list, const std::wstring& device);   // -1 wenn keines gebunden

// Liest eine Parametric-EQ-Datei (z. B. von AutoEq): nur "Preamp:" und "Filter:"-Zeilen mit harmlosen Zeichen werden uebernommen
// ("Filter 3:" wird zu "Filter:"). ignored = Anzahl nicht uebernommener Zeilen.
std::vector<std::string> ParseFilterFile(const std::string& text, int& ignored);

// Anzeigename des Standard-Ausgabegeraets (leer, wenn nicht ermittelbar)
std::wstring DefaultOutputName();

// Eigene Einstellungen (%LOCALAPPDATA%\DuncansEqualizer\settings.txt)
std::wstring SettingsPath();
bool LoadSettings(Settings& s, const std::wstring& path);   // false: Datei fehlt/unlesbar (s bleibt Standard)
bool SaveSettings(const Settings& s, const std::wstring& path);

// Equalizer APO
extern const wchar_t* const kConfigFile;     // "duncans-equalizer.txt" im config-Ordner von Equalizer APO
std::wstring FindInstallDir();               // leer, wenn Equalizer APO nicht installiert ist
enum class ApplyResult { Applied = 0, NotInstalled = 1, AccessDenied = 2, Failed = 3 };
// Schreibt <installDir>\config\duncans-equalizer.txt und sorgt fuer die Include-Zeile in config.txt
ApplyResult ApplyToDir(const std::wstring& installDir, const Settings& s);

}  // namespace eq
