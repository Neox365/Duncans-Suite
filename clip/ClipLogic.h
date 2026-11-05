#pragma once
// Duncan's Clip - Logik: Verlauf der Zwischenablage, Prompts/Textbausteine, Platzhalter, Sperrregeln, Speichern (DPAPI),
// Zwischenablage lesen/schreiben und Einfuegen per Strg+V. Keine Oberflaeche, keine Administratorrechte noetig.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace clip {

constexpr size_t kMaxHistory = 50;
constexpr size_t kMaxTextLen = 100000;      // laengere Texte werden nicht in den Verlauf aufgenommen

struct Prompt {
    std::wstring title;
    std::wstring text;
};

struct Store {
    std::vector<std::wstring> history;      // neuester Eintrag zuerst
    std::vector<Prompt> prompts;
};

// ---- Verlauf und Prompts ----
// Nimmt einen kopierten Text auf: leere/zu lange Texte werden ignoriert, gleiche Texte wandern nach vorn, der Verlauf bleibt auf kMaxHistory begrenzt.
// true = Verlauf hat sich geaendert.
bool AddHistory(Store& s, const std::wstring& text);
bool RemoveHistory(Store& s, size_t index);
// Prompt aus einem Text; der Titel ist die erste nicht leere Zeile, gekuerzt auf 40 Zeichen
Prompt MakePrompt(const std::wstring& text);
// Ersetzt {clip} (ohne Beachtung der Gross-/Kleinschreibung) durch den aktuellen Zwischenablage-Text
std::wstring Expand(const std::wstring& text, const std::wstring& clipText);
// Filter: jedes Wort muss im Text vorkommen (Gross-/Kleinschreibung egal); leerer Filter passt immer
bool Matches(const std::wstring& haystack, const std::wstring& filter);
// Einzeilige Vorschau fuer Listen (Umbrueche und Tabs werden zu Leerzeichen, maximal maxChars Zeichen)
std::wstring Preview(const std::wstring& text, size_t maxChars);

// ---- Sperrregeln: was nie in den Verlauf kommt ----
// Kopien aus bekannten Passwort-Managern (EXE-Name in Kleinbuchstaben)
bool IsPasswordManagerExe(const std::wstring& exeLower);
// Entscheidet, ob eine Kopie uebersprungen wird (Windows-Markierung "nicht aufnehmen", Passwort-Manager, zu lang, leer)
bool ShouldSkip(const std::wstring& text, bool excludedByFormat, const std::wstring& ownerExeLower);

// ---- Zwischenablage (Windows) ----
bool ReadClipboardText(std::wstring& out);                  // Text; false bei nicht-Text oder gesperrter Zwischenablage
bool WriteClipboardText(const std::wstring& text, bool markPrivate = true);  // Text setzen; markPrivate = mit "nicht in Verlauf/Cloud"-Markierung (Windows fuehrt ihn nicht doppelt)
bool ClipboardIsExcluded();                                  // hat die Quelle die Aufnahme in Verlaeufe untersagt?
std::wstring ClipboardOwnerExe();                            // EXE-Name (klein) des Programms, das zuletzt kopiert hat; leer = unbekannt
void SendPasteKeys();                                        // sendet Strg+V an das aktive Fenster

// ---- Einstellungen und Speicherung ----
struct Hotkey { UINT mods; UINT vk; const wchar_t* name; };
int HotkeyCount();
const Hotkey& HotkeyPreset(int index);                       // index wird auf den gueltigen Bereich begrenzt

struct Settings {
    bool autostart = false;                                  // mit Windows im Hintergrund starten (nur Anzeige, die Wahrheit steht in der Registry)
    bool saveHistory = false;                                // Verlauf verschluesselt speichern (sonst nur im Arbeitsspeicher)
    int hotkey = 0;                                          // Index in HotkeyPreset
};

std::wstring DataDir();                                      // %LOCALAPPDATA%\DuncansClip (wird angelegt)
void SetDataDirForTest(const std::wstring& dir);

Settings LoadSettings();
bool SaveSettings(const Settings& s);
bool SavePrompts(const std::vector<Prompt>& p);              // mit Windows-Schluessel (DPAPI) verschluesselt
std::vector<Prompt> LoadPrompts();
bool SaveHistory(const std::vector<std::wstring>& h);
std::vector<std::wstring> LoadHistory();
void DeleteHistoryFile();

// DPAPI (benutzergebunden); fuer Tests oeffentlich
bool Protect(const std::string& plain, std::string& cipher);
bool Unprotect(const std::string& cipher, std::string& plain);

// Autostart per HKCU\...\Run
bool GetAutostart();
bool SetAutostart(bool on, const std::wstring& exePath);

}  // namespace clip
