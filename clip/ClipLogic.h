#pragma once
// Duncan's Clip - Logik: Verlauf der Zwischenablage (Text und Bilder), Prompts/Textbausteine mit Ordnern, Platzhalter,
// Sperrregeln (inkl. Erkennung von Schluesseln/Tokens), Speichern (DPAPI), Export/Import, Zwischenablage lesen/schreiben,
// Markierung uebernehmen und Einfuegen per Strg+V. Keine Oberflaeche, keine Administratorrechte noetig.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace clip {

constexpr size_t kMaxHistory = 50;
constexpr size_t kMaxTextLen = 100000;      // laengere Texte werden nicht in den Verlauf aufgenommen
constexpr size_t kMaxImages = 10;           // hoechstens so viele Bilder im Verlauf (nur im Arbeitsspeicher)
constexpr size_t kMaxImageBytes = 8u << 20; // groessere Bilder (als PNG) werden uebersprungen
constexpr size_t kMaxPinned = 20;           // hoechstens so viele angepinnte Texte

struct Prompt {
    std::wstring title;
    std::wstring text;
    std::wstring folder;                    // optional, z. B. "Programmieren"
    unsigned long long lastUsed = 0;        // Sekunden (Windows-Zeit), 0 = nie benutzt; bestimmt die Reihenfolge in der Schnellauswahl
};

struct HistoryItem {
    unsigned id = 0;                        // stabile Nummer (fuer Vorschaubilder)
    std::wstring text;                      // Text; bei Bildern die Beschriftung ("Bild 1280 x 720")
    std::string png;                        // PNG-Bytes; leer = Textelement
    int width = 0, height = 0;
    unsigned long long time = 0;            // GetTickCount64 beim Aufnehmen (fuer die Aufbewahrung)
    bool pinned = false;                    // angepinnt: steht ganz oben und bleibt (Aufbewahrung, "Verlauf leeren", Obergrenze); nur Text
    bool IsImage() const { return !png.empty(); }
};

struct Store {
    std::vector<HistoryItem> history;       // neuester Eintrag zuerst
    std::vector<Prompt> prompts;
    unsigned nextId = 1;
};

// ---- Verlauf ----
// Nimmt einen kopierten Text auf: leere/zu lange Texte werden ignoriert, gleiche Texte wandern nach vorn, der Verlauf bleibt auf kMaxHistory begrenzt.
// true = Verlauf hat sich geaendert.
bool AddHistory(Store& s, const std::wstring& text, unsigned long long now = GetTickCount64());
// Nimmt ein Bild (PNG-Bytes) auf; gleiche Bilder wandern nach vorn, aeltere Bilder ueber kMaxImages fliegen raus, zu grosse werden ignoriert.
bool AddImage(Store& s, std::string png, int width, int height, unsigned long long now = GetTickCount64());
bool RemoveHistory(Store& s, size_t index);
// Anpinnen: angepinnte Texte stehen immer vorn, neue Eintraege kommen dahinter. Bilder lassen sich nicht anpinnen. false = nicht moeglich
// (Bild, ungueltiger Index, schon kMaxPinned Pins).
bool TogglePin(Store& s, size_t index);
size_t PinnedCount(const Store& s);
// Haengt einen gespeicherten Pin an das Ende der Pins (beim Start); false bei leerem/zu langem/doppeltem Text oder zu vielen Pins
bool AddPinned(Store& s, const std::wstring& text);
// Leert den Verlauf, nur Angepinntes bleibt. Rueckgabe: Anzahl entfernter Eintraege.
size_t ClearHistory(Store& s);
// Entfernt Eintraege, die aelter als maxAgeMs sind (0 = nichts entfernen). Rueckgabe: Anzahl entfernter Eintraege.
size_t PurgeHistory(Store& s, unsigned long long maxAgeMs, unsigned long long now = GetTickCount64());

// ---- Prompts ----
// Prompt aus einem Text; der Titel ist die erste nicht leere Zeile, gekuerzt auf 40 Zeichen
Prompt MakePrompt(const std::wstring& text);
// Ersetzt {clip} sowie {datum} (TT.MM.JJJJ), {zeit} (HH:MM) und {wochentag} (z. B. "Montag"), Gross-/Kleinschreibung egal.
// now = nullptr: aktuelle Ortszeit.
std::wstring Expand(const std::wstring& text, const std::wstring& clipText, const SYSTEMTIME* now = nullptr);
// Filter: jedes Wort muss im Text vorkommen (Gross-/Kleinschreibung egal); leerer Filter passt immer
bool Matches(const std::wstring& haystack, const std::wstring& filter);
// Einzeilige Vorschau fuer Listen (Umbrueche und Tabs werden zu Leerzeichen, maximal maxChars Zeichen)
std::wstring Preview(const std::wstring& text, size_t maxChars);
// Aktuelle Zeit in Sekunden (fuer Prompt::lastUsed)
unsigned long long NowSeconds();
// Prompts in der Reihenfolge fuer die Schnellauswahl: zuletzt benutzte zuerst, sonst die gespeicherte Reihenfolge (Indizes in prompts)
std::vector<size_t> PromptOrderByRecent(const std::vector<Prompt>& prompts);
// Ordnernamen (ohne Duplikate, in der Reihenfolge des ersten Auftretens, ohne leeren Namen)
std::vector<std::wstring> FolderNames(const std::vector<Prompt>& prompts);

// ---- Export / Import (Textdatei, von Hand lesbar) ----
std::wstring ExportPrompts(const std::vector<Prompt>& prompts);
std::vector<Prompt> ImportPrompts(const std::wstring& fileText);
// Haengt neue Prompts an (gleicher Titel + Text + Ordner wird nicht doppelt aufgenommen); Rueckgabe: Anzahl neu hinzugefuegter
int MergePrompts(std::vector<Prompt>& dst, const std::vector<Prompt>& src);
bool WriteTextFile(const std::wstring& path, const std::wstring& text);   // UTF-8 mit BOM
bool ReadTextFile(const std::wstring& path, std::wstring& text);          // UTF-8 (mit/ohne BOM) oder UTF-16

// ---- Links saeubern ----
// Ist text genau ein Link (http/https, ohne Leerzeichen), entfernt das bekannte Tracking-Teile (utm_*, fbclid, gclid, msclkid ...,
// bei Amazon /ref=... und Werbe-Parameter, bei YouTube/Spotify "si") und liest %C3%A4 & Co. wieder als Umlaut.
// Rueckgabe: der gesaeuberte Link, oder leer, wenn es kein Link ist oder sich nichts aendert. Es wird nichts nachgeladen.
std::wstring CleanLink(const std::wstring& text);

// ---- Sperrregeln: was nie in den Verlauf kommt ----
// Kopien aus bekannten Passwort-Managern (EXE-Name in Kleinbuchstaben)
bool IsPasswordManagerExe(const std::wstring& exeLower);
// Erkennt Texte, die wie Schluessel, Tokens, Zugangsdaten oder Zahlungsdaten aussehen. Rueckgabe: Art ("API-Schluessel", "Kreditkartennummer" ...)
// oder leer. Das ist eine Mustererkennung, keine Garantie.
std::wstring DetectSecret(const std::wstring& text);
enum class SkipReason { None, Excluded, PasswordManager, UserExcluded, Blank, TooLong, Secret };
// userExcluded: vom Nutzer gesperrte Programme (EXE-Namen klein); nullptr = keine
SkipReason SkipReasonFor(const std::wstring& text, bool excludedByFormat, const std::wstring& ownerExeLower, const std::vector<std::wstring>* userExcluded = nullptr);

// Vom Nutzer gesperrte Programme (Liste in den Einstellungen): EXE-Namen klein, ohne Pfad, ohne Doppelte
std::wstring NormalizeExeName(const std::wstring& name);                          // "C:\\x\\Chrome.EXE" -> "chrome.exe"; leer bei ungueltigem Namen
bool AddExcludedExe(std::vector<std::wstring>& list, const std::wstring& name);   // true = neu aufgenommen (max. kMaxExcluded)
bool RemoveExcludedExe(std::vector<std::wstring>& list, const std::wstring& name);
bool IsExcludedExe(const std::vector<std::wstring>& list, const std::wstring& exeLower);
std::wstring JoinExcluded(const std::vector<std::wstring>& list);                 // "a.exe;b.exe"
std::vector<std::wstring> SplitExcluded(const std::wstring& text);                // Gegenstueck, bereinigt
constexpr size_t kMaxExcluded = 50;
// Entscheidet, ob eine Kopie uebersprungen wird (Windows-Markierung "nicht aufnehmen", Passwort-Manager, zu lang, leer, Geheimnis)
bool ShouldSkip(const std::wstring& text, bool excludedByFormat, const std::wstring& ownerExeLower, const std::vector<std::wstring>* userExcluded = nullptr);

// ---- Zwischenablage (Windows) ----
bool ReadClipboardText(std::wstring& out);                  // Text; false bei nicht-Text oder gesperrter Zwischenablage
bool WriteClipboardText(const std::wstring& text, bool markPrivate = true);  // Text setzen; markPrivate = mit "nicht in Verlauf/Cloud"-Markierung (Windows fuehrt ihn nicht doppelt)
bool ClipboardHasImage();                                    // Bild in der Zwischenablage (PNG, DIB)
bool ReadClipboardImage(std::string& png, int& width, int& height);   // als PNG-Bytes; false bei Fehler oder zu grossem Bild
bool WriteClipboardImage(const std::string& png, bool markPrivate = true);
// Vorschaubild (Bitmap-Handle, vom Aufrufer mit DeleteObject freizugeben), eingepasst in maxW x maxH; 0 bei Fehler
HBITMAP MakeThumbnail(const std::string& png, int maxW, int maxH, int& outW, int& outH);
bool ClipboardHasRichText();                                 // liegt neben dem Text auch Formatierung (HTML, RTF) in der Zwischenablage?
bool ClipboardIsExcluded();                                  // hat die Quelle die Aufnahme in Verlaeufe untersagt?
std::wstring ClipboardOwnerExe();                            // EXE-Name (klein) des Programms, das zuletzt kopiert hat; leer = unbekannt
void SendPasteKeys();                                        // sendet Strg+V an das aktive Fenster

// ---- Markierung uebernehmen ----
// Konsolen/Terminals werden nie angefasst (dort ist Strg+C "Abbrechen"); EXE-Name klein
bool IsTerminalExe(const std::wstring& exeLower);
bool IsTerminalWindow(HWND h);
// Kopiert die Markierung im Fenster target per Strg+C, liest sie und gibt die vorherige Zwischenablage zurueck (wenn sie Text oder leer war).
// Wartet zuerst, bis die Tasten des Tastenkuerzels losgelassen sind. false = nichts markiert/nicht moeglich.
bool CaptureSelection(HWND target, std::wstring& selection);

// ---- Einstellungen und Speicherung ----
struct Hotkey { UINT mods; UINT vk; const wchar_t* name; };
int HotkeyCount();
const Hotkey& HotkeyPreset(int index);                       // index wird auf den gueltigen Bereich begrenzt

struct Settings {
    bool autostart = false;                                  // mit Windows im Hintergrund starten (nur Anzeige, die Wahrheit steht in der Registry)
    bool saveHistory = false;                                // Verlauf verschluesselt speichern (sonst nur im Arbeitsspeicher)
    int hotkey = 0;                                          // Index in HotkeyPreset
    int retention = 0;                                       // 0 = nie loeschen, 1 = nach 1 Stunde, 2 = nach 1 Tag
    bool captureSelection = true;                            // Markierung beim Oeffnen der Schnellauswahl automatisch uebernehmen
    bool hideIntro = false;                                  // Anleitungskarte "Was ist Clip?" ausgeblendet
    bool plainText = false;                                  // Formatierung (HTML, RTF) beim Kopieren entfernen, nur reinen Text behalten (Menue "Mehr")
    bool cleanLinks = false;                                 // kopierte Links automatisch von Tracking saeubern (Menue "Mehr")
    std::vector<std::wstring> excludedExes;                  // Programme, aus denen Clip nie etwas aufnimmt (EXE-Namen klein)
};

unsigned long long RetentionMs(int retention);               // 0 = unbegrenzt
const wchar_t* RetentionName(int retention);

std::wstring DataDir();                                      // %LOCALAPPDATA%\DuncansClip (wird angelegt)
void SetDataDirForTest(const std::wstring& dir);

Settings LoadSettings();
bool SaveSettings(const Settings& s);
bool SavePrompts(const std::vector<Prompt>& p);              // mit Windows-Schluessel (DPAPI) verschluesselt
std::vector<Prompt> LoadPrompts();
constexpr size_t kMaxNoteLen = 50000;                        // Zettel: groesster gespeicherter Text (Zeichen)
bool SaveNote(const std::wstring& text);                     // Zettel (ein freier Textblock), mit Windows-Schluessel verschluesselt
std::wstring LoadNote();
bool SavePins(const std::vector<std::wstring>& pins);        // immer gespeichert (mit Windows-Schluessel), weil man sie bewusst anpinnt
std::vector<std::wstring> LoadPins();
bool SaveHistory(const std::vector<std::wstring>& h);        // nur Texte (Bilder werden nie gespeichert)
std::vector<std::wstring> LoadHistory();
void DeleteHistoryFile();

// DPAPI (benutzergebunden); fuer Tests oeffentlich
bool Protect(const std::string& plain, std::string& cipher);
bool Unprotect(const std::string& cipher, std::string& plain);

// Autostart per HKCU\...\Run
bool GetAutostart();
bool SetAutostart(bool on, const std::wstring& exePath);

}  // namespace clip
