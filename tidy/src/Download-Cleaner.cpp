// Download-Cleaner.cpp  ("Duncan's Tidy")
// Raeumt Downloads, Temp, Papierkorb und Systemdateien auf. Ohne Argumente oeffnet sich das Fenster
// (Sidebar mit Kategorien); mit --auto laeuft der stille 30-Tage-Check fuer den Downloads-Ordner.
//
// Kompilieren (Developer Command Prompt fuer Visual Studio):  siehe Release.ps1
//
// Start:
//   (ohne Argumente)  Fenster
//   --auto            stiller 30-Tage-Check (fuer die geplante Aufgabe)
//   --dry-run         zeigt nur an, was geloescht wuerde (Konsole)
//   --permanent       loescht endgueltig statt in den Papierkorb
//   --force           ignoriert die 30-Tage-Pruefung

#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
using std::min;
using std::max;
#include <objidl.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <ctime>
#include <winhttp.h>
#include <bcrypt.h>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <functional>
#include <vector>
#include <cmath>
#include <dwmapi.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <cstdio>
#include <tlhelp32.h>
#include <psapi.h>
#include <unordered_map>

#include "Dupes.h"  // doppelte Dateien im Downloads-Ordner finden
#include "public_key.inc"  // oeffentlicher Update-Schluessel, erzeugt von MakeSigningKey.ps1

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace fs = std::filesystem;

// Hintergrund-Thread, der keine Ausnahme nach aussen laesst: eine nicht abgefangene Ausnahme in einem Thread wuerde das ganze Programm beenden
static void Bg(std::function<void()> fn) {
    std::thread([fn = std::move(fn)] {
        try { fn(); } catch (...) {}
    }).detach();
}

static void RamStatus(unsigned long long& avail, unsigned long long& total);  // Arbeitsspeicher: frei / gesamt (siehe RAM optimieren)
static int AppBox(HWND owner, const wchar_t* text, const wchar_t* title, UINT flags);  // eigene MessageBox im App-Design (siehe unten)


// ---- Kategorien ----
enum { CAT_DL, CAT_TEMP, CAT_SYS, CAT_BROWSER, CAT_N };
static const wchar_t* kCatName[CAT_N] = {L"Downloads", L"Temp-Dateien", L"Systemdateien", L"Browser-Cache"};
static const wchar_t* kCatNote[CAT_N] = {L"landet im Papierkorb", L"wird endgültig gelöscht",
                                         L"Vorschau-Cache, Berichte",
                                         L"Zwischenspeicher"};
static const wchar_t* kCatGlyph0[3] = {L"", L"", L""};  // Segoe MDL2 Assets
// Symbol und Farbe pro Kachel der Uebersicht: 0..3 = Kategorien, 4 = Laufwerke, 5 = Autostart
static const wchar_t* kCatGlyph[8] = {kCatGlyph0[0], kCatGlyph0[1], kCatGlyph0[2], L"\xE774", L"\xEDA2", L"\xE7E8", L"\xE71D", L"\xE964"};  // E774 Globus, EDA2 Festplatte, E7E8 Ein/Aus
static const COLORREF kCatColor[8] = {RGB(59, 130, 246), RGB(249, 115, 22), RGB(168, 85, 247), RGB(20, 184, 166), RGB(234, 179, 8), RGB(236, 72, 153), RGB(99, 102, 241), RGB(14, 165, 233)};

static fs::path KnownFolder(const KNOWNFOLDERID& id) {
    PWSTR raw = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &raw))) result = raw;
    CoTaskMemFree(raw);
    return result;
}

// Programme aus dem Systemordner immer mit vollem Pfad starten: Ein blosser Name wuerde zuerst im Ordner des Programms
// (z. B. Downloads) gesucht, und dort koennte jemand eine eigene powershell.exe oder notepad.exe ablegen.
static std::wstring SystemFile(const wchar_t* rel) {
    wchar_t dir[MAX_PATH];
    const UINT n = GetSystemDirectoryW(dir, MAX_PATH);
    return (n && n < MAX_PATH) ? std::wstring(dir) + L"\\" + rel : std::wstring(rel);
}

static fs::path StateFile() {
    fs::path dir = KnownFolder(FOLDERID_LocalAppData) / L"DownloadsCleaner";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir / L"last_run.txt";
}

// ---- Auto-Clean (alle 30 Tage) ein- oder ausschalten: eine Marker-Datei bedeutet "aus" ----
static fs::path AutoOffFlag() { return StateFile().parent_path() / L"auto_off.flag"; }
static bool AutoCleanOn() { std::error_code ec; return !fs::exists(AutoOffFlag(), ec); }
static void SetAutoClean(bool on) {
    std::error_code ec;
    if (on) fs::remove(AutoOffFlag(), ec);
    else std::ofstream{AutoOffFlag()} << 1;
}

// ---- Einstellungen: kleine Ein/Aus-Schalter in der Registry (HKCU\Software\DuncansTidy) ----
static const wchar_t* kSettingsKey = L"Software\\DuncansTidy";

// Zahlen-Einstellungen (REG_DWORD): Auto-Clean-Intervall in Tagen (Standard 30) und Groessengrenze in MB (0 = alles loeschen)
static int GetInt(const wchar_t* name, int def) {
    DWORD v = 0, n = sizeof v;
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &v, &n) != ERROR_SUCCESS) return def;
    return (int)v;
}
static void SetInt(const wchar_t* name, int value) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    DWORD v = (DWORD)value;
    RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof v);
    RegCloseKey(k);
}
static bool GetFlag(const wchar_t* name, bool def) { return GetInt(name, def ? 1 : 0) != 0; }
static void SetFlag(const wchar_t* name, bool on) { SetInt(name, on ? 1 : 0); }
static int AutoDays() { return std::clamp(GetInt(L"AutoDays", 30), 1, 365); }
static int KeepBelowMB() { return std::clamp(GetInt(L"KeepBelowMB", 0), 0, 100000); }

// Mit Windows starten: Run-Eintrag des Benutzers; --tray startet unsichtbar im Hintergrund
static const wchar_t* kRunKeyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunValueName = L"DuncansTidy";
static fs::path InstalledExe();
static std::vector<int> ExeVersion(const fs::path& p);
static bool StartWithWindowsOn() {
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKeyPath, kRunValueName, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) != ERROR_SUCCESS) return false;
    // Im Task-Manager oder in den Windows-Einstellungen deaktiviert? Dann startet Windows den Eintrag trotz Run-Wert nicht
    BYTE b[12];
    DWORD n = sizeof b;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run", kRunValueName, RRF_RT_REG_BINARY, nullptr, b, &n) == ERROR_SUCCESS && n >= 1 && (b[0] & 1))
        return false;
    return true;
}
static void SetStartWithWindows(bool on) {
    HKEY k;
    if (on) {
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        std::error_code ec;
        fs::path exe = InstalledExe();
        // nicht installiert oder die installierte Kopie ist aelter (kennt --tray nicht): die laufende Datei verwenden
        if (!fs::exists(exe, ec) || ExeVersion(exe) < ExeVersion(self)) exe = self;
        const std::wstring cmd = L"\"" + exe.wstring() + L"\" --tray";
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
        RegSetValueExW(k, kRunValueName, 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
        // Wurde der Eintrag frueher im Task-Manager oder in den Windows-Einstellungen deaktiviert, ignoriert Windows ihn trotz Run-Eintrag.
        // Ein fehlender Wert unter StartupApproved heisst "aktiviert".
        HKEY ka;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run", 0, KEY_SET_VALUE, &ka) == ERROR_SUCCESS) {
            RegDeleteValueW(ka, kRunValueName);
            RegCloseKey(ka);
        }
    } else {
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
        RegDeleteValueW(k, kRunValueName);
    }
    RegCloseKey(k);
}

// ---- Sprachen (Deutsch / English) ----
// Die deutschen Texte stehen direkt im Code und sind zugleich der Schluessel: Tr(L"deutscher Text") liefert den Text in der
// gewaehlten Sprache aus kTranslations. Fehlt ein Eintrag, bleibt der deutsche Text stehen (nichts bricht).
// Die Uebersetzungen sind Teil der .exe und keine externen Dateien: Das Update prueft nur die .exe per Signatur,
// eine nachladbare Sprachdatei waere ungeprueft. Neue Sprache = Eintrag in kLangs + eine Spalte in kTranslations
// (und die Texte mit L2/Pick, siehe unten) .
struct LangInfo { const wchar_t* code; const wchar_t* name; };
static const LangInfo kLangs[] = {{L"de", L"Deutsch"}, {L"en", L"English"}, {L"fr", L"Français"}, {L"es", L"Español"}, {L"pl", L"Polski"},
                                        {L"pt", L"Português (Brasil)"}, {L"ja", L"日本語"}, {L"zh", L"繁體中文"}};
constexpr int kLangCount = (int)(sizeof kLangs / sizeof kLangs[0]);
static std::atomic<int> g_lang{0};  // Index in kLangs, 0 = Deutsch (Quelltext)

struct Translation { const wchar_t* de; const wchar_t* tr[kLangCount - 1]; };  // tr[0] = Englisch
#include "translations.inc"  // kTranslations (alle Sprachen) und kInfoTexts (lange Seitentexte)

// Uebersetzt einen deutschen Text in die aktuelle Sprache (Deutsch: unveraendert zurueck)
static const wchar_t* Tr(const wchar_t* de) {
    const int lang = g_lang;
    if (lang <= 0 || lang >= kLangCount) return de;
    static const std::unordered_map<std::wstring, const Translation*> index = [] {
        std::unordered_map<std::wstring, const Translation*> m;
        for (const auto& e : kTranslations) m.emplace(e.de, &e);
        return m;
    }();
    auto it = index.find(de);
    return it == index.end() ? de : it->second->tr[lang - 1];
}


// Sprache aus der Registry lesen/speichern (REG_SZ, z. B. "de"). Beim ersten Start gibt es noch keinen Wert:
// dann gilt die Windows-Sprache (Deutsch -> Deutsch, alles andere -> Englisch) und wird gleich gespeichert.
static int LangIndexOf(const std::wstring& code) {
    for (int i = 0; i < kLangCount; ++i) if (code == kLangs[i].code) return i;
    return -1;
}
static void SaveLanguage(int idx) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\DuncansTidy", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    const std::wstring c = kLangs[idx].code;
    RegSetValueExW(k, L"Language", 0, REG_SZ, (const BYTE*)c.c_str(), (DWORD)((c.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}
static int LoadLanguage() {
    wchar_t buf[16]{};
    DWORD n = sizeof buf;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\DuncansTidy", L"Language", RRF_RT_REG_SZ, nullptr, buf, &n) == ERROR_SUCCESS) {
        const int idx = LangIndexOf(buf);
        if (idx >= 0) return idx;
    }
    const LANGID ui = GetUserDefaultUILanguage();  // Anzeigesprache von Windows; nicht unterstuetzte Sprachen -> Englisch
    const WORD sub = (WORD)(ui >> 10);
    const wchar_t* code = L"en";
    switch (ui & 0x3FF) {
    case LANG_GERMAN: code = L"de"; break;
    case LANG_FRENCH: code = L"fr"; break;
    case LANG_SPANISH: code = L"es"; break;
    case LANG_POLISH: code = L"pl"; break;
    case LANG_PORTUGUESE: code = L"pt"; break;
    case LANG_JAPANESE: code = L"ja"; break;
    case LANG_CHINESE:  // nur traditionelles Chinesisch ist uebersetzt
        if (sub == SUBLANG_CHINESE_TRADITIONAL || sub == SUBLANG_CHINESE_HONGKONG || sub == SUBLANG_CHINESE_MACAU) code = L"zh";
        break;
    }
    const int idx = LangIndexOf(code);
    SaveLanguage(idx);
    return idx;
}
// ---- Schutzliste: Eintraege, die nie geloescht werden ----
static fs::path WhitelistFile() { return StateFile().parent_path() / L"schutzliste.txt"; }

static void EnsureWhitelistFile() {
    std::error_code ec;
    if (fs::exists(WhitelistFile(), ec)) return;
    std::ofstream f(WhitelistFile());
    f << "# Schutzliste: Diese Eintraege werden NIE geloescht.\n"
         "# Pro Zeile ein Dateiname, * als Platzhalter ist erlaubt (z.B. *.pdf oder Rechnung*).\n"
         "# Zeilen mit # am Anfang werden ignoriert.\n";
}

static std::vector<std::wstring> LoadWhitelist() {
    std::vector<std::wstring> out;
    std::ifstream f(WhitelistFile(), std::ios::binary);
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        size_t s = line.find_first_not_of(" \t");
        if (s != std::string::npos && line.compare(s, 3, "\xEF\xBB\xBF") == 0) s += 3;  // BOM
        if (s == std::string::npos || s >= line.size() || line[s] == '#') continue;
        line = line.substr(s);
        int n = MultiByteToWideChar(CP_UTF8, 0, line.data(), (int)line.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, line.data(), (int)line.size(), w.data(), n);
        out.push_back(w);
    }
    return out;
}

// Geschuetzt: Eintraege der Schutzliste; in Downloads zusaetzlich Systemdateien (desktop.ini usw.);
// ausserdem immer das Programm selbst.
static bool IsProtected(const fs::path& p, int cat, const std::vector<std::wstring>& wl) {
    std::wstring name = p.filename().wstring();
    for (const auto& pat : wl)
        if (PathMatchSpecW(name.c_str(), pat.c_str())) return true;
    if (cat == CAT_DL) {
        if (_wcsicmp(name.c_str(), L"desktop.ini") == 0 || _wcsicmp(name.c_str(), L"thumbs.db") == 0) return true;
        DWORD a = GetFileAttributesW(p.c_str());
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_SYSTEM)) return true;
    }
    static const std::wstring self = [] {  // Pfad des Programms nur einmal ermitteln, nicht bei jeder Datei
        wchar_t buf[MAX_PATH];
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        return std::wstring(buf);
    }();
    return _wcsicmp(p.c_str(), self.c_str()) == 0;
}

static std::atomic<int> g_protCount[CAT_N];  // Anzahl geschuetzter Eintraege beim letzten Auflisten

// Eintraege einer Kategorie ohne die geschuetzten
static std::vector<fs::path> ListItemsRaw(int cat, bool deep = false);
// deep = Ultra-Tiefenreinigung (nur Systemdateien, nur auf ausdruecklichen Wunsch der Oberflaeche)
static std::vector<fs::path> ListItems(int cat, bool deep = false) {
    auto v = ListItemsRaw(cat, deep);
    auto wl = LoadWhitelist();
    size_t before = v.size();
    v.erase(std::remove_if(v.begin(), v.end(), [&](const fs::path& p) { return IsProtected(p, cat, wl); }), v.end());
    g_protCount[cat] = (int)(before - v.size());
    return v;
}

// ---- Browser-Cache: nur Zwischenspeicher-Ordner von Chrome, Edge, Brave, Opera GX und Firefox ----
// Cookies, Passwoerter, Verlauf und Lesezeichen bleiben unberuehrt.
// only: 0 = Chrome, 1 = Edge, 2 = Brave, 3 = Firefox, 4 = Opera GX, -1 = alle
static void AddBrowserCaches(std::vector<fs::path>& v, int only = -1) {
    std::error_code ec;
    const fs::path lad = KnownFolder(FOLDERID_LocalAppData);
    const fs::path chromium[] = {lad / L"Google" / L"Chrome" / L"User Data", lad / L"Microsoft" / L"Edge" / L"User Data",
                                 lad / L"BraveSoftware" / L"Brave-Browser" / L"User Data"};
    static const wchar_t* kSubs[] = {L"Cache", L"Code Cache", L"GPUCache"};
    for (int bi = 0; bi < 3; ++bi) {
        if (only >= 0 && only != bi) continue;
        const fs::path& root = chromium[bi];
        for (const auto& prof : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
            std::wstring pn = prof.path().filename().wstring();
            if (_wcsicmp(pn.c_str(), L"Default") != 0 && _wcsnicmp(pn.c_str(), L"Profile ", 8) != 0) continue;
            for (const wchar_t* sub : kSubs) {
                fs::path c = prof.path() / sub;
                if (fs::is_directory(c, ec)) v.push_back(c);
            }
        }
        ec.clear();
    }
    if (only < 0 || only == 4) {  // Opera GX: Chromium mit eigener Ordnerstruktur. Cache und "System Cache" liegen direkt im Opera-Ordner und unter "Default"; Code Cache/GPUCache je nach Version auch im Roaming-Ordner.
        static const wchar_t* kOperaSubs[] = {L"Cache", L"Code Cache", L"GPUCache", L"System Cache"};
        const fs::path og = lad / L"Opera Software" / L"Opera GX Stable", ogRoam = KnownFolder(FOLDERID_RoamingAppData) / L"Opera Software" / L"Opera GX Stable";
        for (const fs::path& base : {og, og / L"Default", ogRoam, ogRoam / L"Default"})
            for (const wchar_t* sub : kOperaSubs) {
                fs::path c = base / sub;
                if (fs::is_directory(c, ec)) v.push_back(c);
            }
        ec.clear();
    }
    if (only < 0 || only == 3) for (const auto& prof : fs::directory_iterator(lad / L"Mozilla" / L"Firefox" / L"Profiles", fs::directory_options::skip_permission_denied, ec)) {
        fs::path c = prof.path() / L"cache2";
        if (fs::is_directory(c, ec)) v.push_back(c);
    }
    ec.clear();
}

// Verlauf (kind 1) und Cookies (kind 2) je Browser: einzelne Dateien in den Profilordnern. Passwoerter und Lesezeichen liegen woanders und bleiben erhalten.
// Firefox: nur Cookies (der Verlauf steckt dort in places.sqlite, zusammen mit den Lesezeichen, und wird nicht angefasst).
static void AddBrowserData(std::vector<fs::path>& v, int only, int kind) {
    std::error_code ec;
    const fs::path lad = KnownFolder(FOLDERID_LocalAppData), roam = KnownFolder(FOLDERID_RoamingAppData);
    auto addFiles = [&](const fs::path& dir, std::initializer_list<const wchar_t*> names) {
        for (const wchar_t* n : names) {
            const fs::path f = dir / n;
            if (fs::is_regular_file(f, ec)) v.push_back(f);
            ec.clear();
        }
    };
    auto profileFiles = [&](const fs::path& profile) {
        if (kind == 1) addFiles(profile, {L"History", L"History-journal", L"Visited Links", L"Top Sites", L"Top Sites-journal", L"Favicons", L"Favicons-journal", L"Shortcuts", L"Shortcuts-journal"});
        else addFiles(profile, {L"Cookies", L"Cookies-journal"});
        if (kind == 2) addFiles(profile / L"Network", {L"Cookies", L"Cookies-journal"});
    };
    const fs::path chromium[] = {lad / L"Google" / L"Chrome" / L"User Data", lad / L"Microsoft" / L"Edge" / L"User Data", lad / L"BraveSoftware" / L"Brave-Browser" / L"User Data"};
    for (int bi = 0; bi < 3; ++bi) {
        if (only != bi) continue;
        for (const auto& prof : fs::directory_iterator(chromium[bi], fs::directory_options::skip_permission_denied, ec)) {
            const std::wstring pn = prof.path().filename().wstring();
            if (_wcsicmp(pn.c_str(), L"Default") != 0 && _wcsnicmp(pn.c_str(), L"Profile ", 8) != 0) continue;
            profileFiles(prof.path());
        }
        ec.clear();
    }
    if (only == 4) {
        const fs::path og = lad / L"Opera Software" / L"Opera GX Stable", ogRoam = roam / L"Opera Software" / L"Opera GX Stable";
        for (const fs::path& base : {og, og / L"Default", ogRoam, ogRoam / L"Default"}) profileFiles(base);
    }
    if (only == 3 && kind == 2) {
        for (const auto& prof : fs::directory_iterator(roam / L"Mozilla" / L"Firefox" / L"Profiles", fs::directory_options::skip_permission_denied, ec))
            addFiles(prof.path(), {L"cookies.sqlite", L"cookies.sqlite-wal", L"cookies.sqlite-shm"});
        ec.clear();
    }
}

static std::wstring AppCacheLabel(const fs::path& p);   // leer, wenn p kein App-Cache der Ultra-Tiefenreinigung ist
// Anzeigename eines Eintrags: normalerweise der Dateiname, bei Browser-Caches "Browser · Profil · Ordner"
static std::wstring ItemLabel(int cat, const fs::path& p) {
    if (cat == CAT_SYS) {
        std::wstring l = AppCacheLabel(p);
        if (!l.empty()) return l;
    }
    if (cat != CAT_BROWSER) return p.filename().wstring();
    const std::wstring full = p.wstring();
    const wchar_t* br = StrStrIW(full.c_str(), L"\\Chrome\\") ? L"Chrome" : StrStrIW(full.c_str(), L"\\Edge\\") ? L"Edge"
                      : StrStrIW(full.c_str(), L"\\Brave-Browser\\") ? L"Brave" : StrStrIW(full.c_str(), L"\\Opera GX Stable") ? L"Opera GX" : L"Firefox";
    return std::wstring(br) + L"  ·  " + p.parent_path().filename().wstring() + L"  ·  " + p.filename().wstring();
}

// ---- Ultra-Tiefenreinigung (Deep Clean) ----
// Schalter in der Kopfzeile (Uebersicht, Systemdateien) und in den Einstellungen. Im Tiefen-Modus nehmen die Systemdateien zusaetzlich
// auf: Shader-Caches der Grafiktreiber, System-Fehlerberichte, Windows-Update-/Delivery-Optimization-Reste, Treiber-Installer-Reste
// (C:\NVIDIA, C:\AMD) und Windows\Temp. Es werden immer nur Inhalte fester Ordner aufgelistet (nie ganze Systemordner), und DeleteEntry
// verweigert alles ausserhalb dieser Bereiche (IsCriticalPath). Auto-Clean und die Schnell-Reinigung aus dem Tray bleiben beim Standard.
constexpr int kMaxGroups = 13;
enum { GRP_SHADER = 5, GRP_WUPDATE = 6, GRP_DRIVER = 7, GRP_SETUP = 8, GRP_APPCACHE = 9, GRP_STORE = 10, GRP_ETL = 11, GRP_DEVCACHE = 12 };
static bool DeepOn() { return GetFlag(L"DeepClean", false); }
// Ultra-Tiefenreinigung (Tidy 1.2.1): baut auf der Ultra-Tiefenreinigung auf. Dateigruppen: App-Caches (Discord, Slack, Steam, VS Code …), Store-Reste,
// ETL-Protokolle. Dazu drei Aktionen, die Windows selbst ausfuehrt (DISM, Schattenkopien, Ereignisprotokolle) und die immer einzeln bestaetigt werden.
static bool UltraOn() { return DeepOn(); }   // seit 1.2.3 gibt es nur noch Standard und Ultra: Ultra enthaelt alle Tiefen-Gruppen und die Windows-Aktionen

static fs::path SystemDriveRoot() {
    wchar_t b[16]{};
    const DWORD n = GetEnvironmentVariableW(L"SystemDrive", b, 16);
    return fs::path((n >= 2 && n < 16) ? std::wstring(b) + L"\\" : std::wstring(L"C:\\"));
}
static std::wstring LowerPath(const fs::path& p) {
    std::wstring s = p.wstring();
    for (auto& c : s) { if (c == L'/') c = L'\\'; c = (wchar_t)towlower(c); }
    while (s.size() > 3 && s.back() == L'\\') s.pop_back();
    return s;
}
// true, wenn low (kleingeschrieben) echt unterhalb von dir liegt (nicht gleich)
static bool UnderDir(const std::wstring& low, const fs::path& dir) {
    if (dir.empty()) return false;
    std::wstring d = LowerPath(dir);
    if (d.size() > 3 && d.back() == L'\\') d.pop_back();
    return low.size() > d.size() + 1 && low.compare(0, d.size(), d) == 0 && low[d.size()] == L'\\';
}
static void AddChildren(std::vector<fs::path>& v, const fs::path& dir) {
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return;
    for (const auto& e : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) v.push_back(e.path());
}
static void AddDeepItems(std::vector<fs::path>& v) {
    const fs::path lad = KnownFolder(FOLDERID_LocalAppData), low = KnownFolder(FOLDERID_LocalAppDataLow), pd = KnownFolder(FOLDERID_ProgramData);
    const fs::path win = KnownFolder(FOLDERID_Windows), root = SystemDriveRoot();
    if (!lad.empty()) {  // DirectX- und GPU-Shader-Caches (werden beim naechsten Start neu aufgebaut)
        for (const fs::path& d : {lad / L"D3DSCache", lad / L"NVIDIA" / L"DXCache", lad / L"NVIDIA" / L"GLCache", lad / L"NVIDIA" / L"ComputeCache",
                                  lad / L"AMD" / L"DxCache", lad / L"AMD" / L"DxcCache", lad / L"AMD" / L"GLCache", lad / L"AMD" / L"VkCache"})
            AddChildren(v, d);
        std::error_code ec;
        if (fs::exists(lad / L"IconCache.db", ec)) v.push_back(lad / L"IconCache.db");  // alter Symbol-Cache (Explorer baut ihn neu)
    }
    if (!low.empty()) {
        AddChildren(v, low / L"NVIDIA" / L"PerDriverVersion" / L"DXCache");
        AddChildren(v, low / L"NVIDIA" / L"PerDriverVersion" / L"GLCache");
    }
    if (!pd.empty()) {
        AddChildren(v, pd / L"NVIDIA Corporation" / L"NV_Cache");
        for (const wchar_t* sub : {L"ReportArchive", L"ReportQueue", L"Temp"}) AddChildren(v, pd / L"Microsoft" / L"Windows" / L"WER" / sub);  // System-Fehlerberichte
        AddChildren(v, pd / L"Microsoft" / L"Windows" / L"DeliveryOptimization" / L"Cache");
    }
    if (!win.empty()) {
        AddChildren(v, win / L"SoftwareDistribution" / L"Download");  // heruntergeladene Update-Pakete
        AddChildren(v, win / L"Logs" / L"WindowsUpdate");             // Update-Protokolle
        AddChildren(v, win / L"Temp");                                // System-Temp (Setup-Reste)
    }
    AddChildren(v, root / L"NVIDIA" / L"DisplayDriver");  // entpackte Treiber-Installer
    AddChildren(v, root / L"AMD" / L"Packages");
}
// Ultra-Tiefenreinigung: Cache-Ordner der Electron-/CEF-Apps. Nur diese Unterordner, nie Cookies, Anmeldung, Einstellungen oder Offline-Medien.
static const wchar_t* const kAppCacheSubs[] = {L"Cache", L"Code Cache", L"GPUCache", L"DawnGraphiteCache", L"DawnWebGPUCache", L"GrShaderCache", L"ShaderCache"};
struct AppBase { fs::path path; const wchar_t* label; bool whole = false; };  // whole: der Ordner selbst ist der Cache (statt Cache-Unterordner)
static std::vector<AppBase> AppCacheBases() {
    std::vector<AppBase> b;
    const fs::path ra = KnownFolder(FOLDERID_RoamingAppData), lad = KnownFolder(FOLDERID_LocalAppData);
    if (!ra.empty()) {
        struct R { const wchar_t* dir; const wchar_t* label; };
        static const R kRoaming[] = {{L"discord", L"Discord"}, {L"discordcanary", L"Discord Canary"}, {L"discordptb", L"Discord PTB"},
                                     {L"Code", L"VS Code"}, {L"Code - Insiders", L"VS Code Insiders"}, {L"Cursor", L"Cursor"},
                                     {L"Slack", L"Slack"}, {L"Signal", L"Signal"}, {L"Notion", L"Notion"}, {L"obsidian", L"Obsidian"},
                                     {L"GitHub Desktop", L"GitHub Desktop"}, {L"Claude", L"Claude"},
                                     {L"CurseForge", L"CurseForge"}, {L"r2modman", L"r2modman"}};
        for (const R& r : kRoaming) b.push_back({ra / r.dir, r.label});
        b.push_back({ra / L"Adobe" / L"Common" / L"Media Cache Files", L"Adobe Media Cache", true});
        b.push_back({ra / L"Adobe" / L"Common" / L"Media Cache", L"Adobe Media Cache", true});
        b.push_back({ra / L"Microsoft" / L"Teams", L"Teams"});
    }
    if (!lad.empty()) {
        b.push_back({lad / L"Spotify" / L"Browser", L"Spotify"});
        b.push_back({lad / L"Ubisoft Game Launcher" / L"cache", L"Ubisoft Connect", true});
        b.push_back({lad / L"GOG.com" / L"Galaxy" / L"webcache", L"GOG Galaxy", true});
        b.push_back({lad / L"Overwolf" / L"BrowserCache", L"Overwolf", true});
        b.push_back({lad / L"UnrealEngine" / L"Common" / L"DerivedDataCache", L"Unreal Engine (Derived Data Cache)", true});
        b.push_back({lad / L"Unity" / L"cache", L"Unity (Cache)", true});
        b.push_back({lad / L"Microsoft" / L"Office" / L"16.0" / L"OfficeFileCache", L"Microsoft Office", true});
        b.push_back({lad / L"Steam" / L"htmlcache", L"Steam"});
        b.push_back({lad / L"Steam" / L"htmlcache" / L"Default", L"Steam"});
        b.push_back({lad / L"Packages" / L"MSTeams_8wekyb3d8bbwe" / L"LocalCache" / L"Microsoft" / L"MSTeams" / L"EBWebView" / L"Default", L"Teams"});
        std::error_code ec;  // Epic Games Launcher: Web-Caches heissen webcache, webcache_4430 usw.
        for (const auto& e : fs::directory_iterator(lad / L"EpicGamesLauncher" / L"Saved", fs::directory_options::skip_permission_denied, ec)) {
            std::error_code e2;
            if (e.is_directory(e2) && LowerPath(e.path().filename()).rfind(L"webcache", 0) == 0) b.push_back({e.path(), L"Epic Games"});
        }
    }
    return b;
}
static std::wstring AppCacheLabel(const fs::path& p) {
    if (!UltraOn()) return L"";
    const std::wstring lp = LowerPath(p);
    for (const AppBase& ab : AppCacheBases())
        if (UnderDir(lp, ab.path)) return std::wstring(ab.label) + L"  ·  " + p.filename().wstring();
    return L"";
}
// Zwischenspeicher von Entwickler-Werkzeugen (alles wird bei Bedarf neu geladen, das naechste Installieren/Bauen dauert dann nur laenger).
// Bewusst nicht dabei: die NuGet-Paketablage (.nuget\\packages) und Gradle, weil Offline-Builds sonst brechen koennten.
static std::vector<fs::path> DevCacheDirs() {
    std::vector<fs::path> d;
    const fs::path lad = KnownFolder(FOLDERID_LocalAppData);
    if (lad.empty()) return d;
    d.push_back(lad / L"npm-cache");
    d.push_back(lad / L"pip" / L"Cache");
    d.push_back(lad / L"NuGet" / L"v3-cache");
    d.push_back(lad / L"NuGet" / L"plugins-cache");
    d.push_back(lad / L"Yarn" / L"Cache");
    return d;
}
static void AddUltraItems(std::vector<fs::path>& v) {
    std::error_code ec;
    for (const AppBase& ab : AppCacheBases()) {
        if (ab.whole) { if (fs::is_directory(ab.path, ec)) v.push_back(ab.path); ec.clear(); continue; }
        for (const wchar_t* sub : kAppCacheSubs) {
            const fs::path c = ab.path / sub;
            if (fs::is_directory(c, ec)) v.push_back(c);
            ec.clear();
        }
    }
    const fs::path lad = KnownFolder(FOLDERID_LocalAppData), pd = KnownFolder(FOLDERID_ProgramData), win = KnownFolder(FOLDERID_Windows);
    if (!lad.empty()) {  // Microsoft Store: zwischengespeicherte und voruebergehende Downloads
        const fs::path st = lad / L"Packages" / L"Microsoft.WindowsStore_8wekyb3d8bbwe";
        AddChildren(v, st / L"LocalCache");
        AddChildren(v, st / L"TempState");
        AddChildren(v, st / L"AC" / L"Temp");
    }
    auto addEtl = [&](const fs::path& d) {  // nur .etl-Dateien (Ablaufverfolgungs-Protokolle), keine Unterordner
        if (d.empty()) return;
        for (const auto& e : fs::directory_iterator(d, fs::directory_options::skip_permission_denied, ec)) {
            std::error_code e2;
            if (!e.is_regular_file(e2)) continue;
            std::wstring x = e.path().extension().wstring();
            for (auto& c : x) c = (wchar_t)towlower(c);
            if (x == L".etl") v.push_back(e.path());
        }
        ec.clear();
    };
    if (!pd.empty()) {
        const fs::path d = pd / L"Microsoft" / L"Diagnosis" / L"ETLLogs";
        addEtl(d);
        addEtl(d / L"AutoLogger");
        addEtl(d / L"ShutdownLogger");
    }
    for (const fs::path& d : DevCacheDirs()) AddChildren(v, d);
    if (!win.empty()) {  // Absturzabbilder und Wartungsprotokolle von Windows (ohne Administratorrechte bleibt Gesperrtes stehen)
        AddChildren(v, win / L"Minidump");
        AddChildren(v, win / L"LiveKernelReports");
        if (fs::is_regular_file(win / L"MEMORY.DMP", ec)) v.push_back(win / L"MEMORY.DMP");
        ec.clear();
        AddChildren(v, win / L"Logs" / L"CBS");
        AddChildren(v, win / L"Logs" / L"DISM");
    }
    if (!win.empty()) {
        addEtl(win / L"System32" / L"LogFiles" / L"WMI");
        AddChildren(v, win / L"System32" / L"LogFiles" / L"WMI" / L"RtBackup");
    }
}
// Zuordnung eines Eintrags zu den Tiefen-Gruppen; -1 = keine Tiefen-Gruppe
static int DeepGroupOf(const fs::path& p) {
    const std::wstring lp = LowerPath(p);
    const fs::path win = KnownFolder(FOLDERID_Windows), root = SystemDriveRoot();
    if (UnderDir(lp, root / L"NVIDIA") || UnderDir(lp, root / L"AMD")) return GRP_DRIVER;
    if (lp.find(L"\\microsoft.windowsstore_") != std::wstring::npos) return GRP_STORE;
    if (UnderDir(lp, win / L"System32" / L"LogFiles" / L"WMI") || lp.find(L"\\diagnosis\\etllogs\\") != std::wstring::npos) return GRP_ETL;
    for (const AppBase& ab : AppCacheBases())
        if (UnderDir(lp, ab.path)) return GRP_APPCACHE;
    if (UnderDir(lp, win / L"Minidump") || UnderDir(lp, win / L"LiveKernelReports") || lp == LowerPath(win / L"MEMORY.DMP")) return 2;   // Absturzabbilder
    if (UnderDir(lp, win / L"Logs" / L"CBS") || UnderDir(lp, win / L"Logs" / L"DISM")) return GRP_ETL;                                        // Windows-Wartungsprotokolle
    for (const fs::path& d : DevCacheDirs())
        if (UnderDir(lp, d)) return GRP_DEVCACHE;
    if (UnderDir(lp, win / L"SoftwareDistribution") || UnderDir(lp, win / L"Logs") || lp.find(L"\\deliveryoptimization\\") != std::wstring::npos) return GRP_WUPDATE;
    if (UnderDir(lp, win / L"Temp")) return GRP_SETUP;
    static const wchar_t* kShader[] = {L"\\d3dscache\\", L"\\dxcache\\", L"\\glcache\\", L"\\vkcache\\", L"\\dxccache\\", L"\\computecache\\", L"\\nv_cache\\"};
    for (const wchar_t* m : kShader)
        if (lp.find(m) != std::wstring::npos) return GRP_SHADER;
    return -1;
}

// Harte Sperre gegen Systemdateien: nur diese Bereiche duerfen unterhalb von Windows, Programme und ProgramData ueberhaupt geloescht werden,
// und auch dort nur Inhalte (nie der Ordner selbst). Laufwerkswurzeln und direkte Unterordner (C:\Foo), das Benutzerprofil und
// Programmordner sind immer gesperrt.
static bool IsCriticalPath(const fs::path& p) {
    const std::wstring lp = LowerPath(p);
    if (std::count(lp.begin(), lp.end(), L'\\') < 2) return true;  // "c:\" oder "c:\ordner"
    const fs::path win = KnownFolder(FOLDERID_Windows), pd = KnownFolder(FOLDERID_ProgramData), prof = KnownFolder(FOLDERID_Profile);
    if (!prof.empty() && lp == LowerPath(prof)) return true;
    if (!win.empty() && (lp == LowerPath(win) || UnderDir(lp, win))) {
        for (const fs::path& ok : {win / L"Temp", win / L"SoftwareDistribution" / L"Download", win / L"Logs" / L"WindowsUpdate", win / L"System32" / L"LogFiles" / L"WMI",
                                   win / L"Minidump", win / L"LiveKernelReports", win / L"Logs" / L"CBS", win / L"Logs" / L"DISM"})
            if (UnderDir(lp, ok)) return false;
        if (lp == LowerPath(win / L"MEMORY.DMP")) return false;   // das Speicherabbild nach einem Absturz (eine einzelne Datei)
        return true;
    }
    for (const KNOWNFOLDERID* id : {&FOLDERID_ProgramFiles, &FOLDERID_ProgramFilesX86, &FOLDERID_ProgramFilesCommon})
        if (const fs::path pf = KnownFolder(*id); !pf.empty() && (lp == LowerPath(pf) || UnderDir(lp, pf))) return true;
    if (!pd.empty() && (lp == LowerPath(pd) || UnderDir(lp, pd))) {
        for (const fs::path& ok : {pd / L"Microsoft" / L"Windows" / L"WER" / L"ReportArchive", pd / L"Microsoft" / L"Windows" / L"WER" / L"ReportQueue",
                                   pd / L"Microsoft" / L"Windows" / L"WER" / L"Temp", pd / L"Microsoft" / L"Windows" / L"DeliveryOptimization" / L"Cache",
                                   pd / L"NVIDIA Corporation" / L"NV_Cache", pd / L"Microsoft" / L"Diagnosis" / L"ETLLogs"})
            if (UnderDir(lp, ok)) return false;
        return true;
    }
    return false;
}

// Symbol- und Vorschau-Cache des Explorers (liegen im Explorer in Benutzung)
static bool IsExplorerCacheFile(const fs::path& p) {
    const std::wstring n = p.filename().wstring();
    return _wcsnicmp(n.c_str(), L"thumbcache_", 11) == 0 || _wcsnicmp(n.c_str(), L"iconcache_", 10) == 0 || _wcsicmp(n.c_str(), L"IconCache.db") == 0;
}
static std::atomic<bool> g_restartExplorer{false};

static std::vector<fs::path> ListItemsRaw(int cat, bool deep) {
    std::vector<fs::path> v;
    std::error_code ec;
    auto addDir = [&](const fs::path& d) {
        for (const auto& e : fs::directory_iterator(d, fs::directory_options::skip_permission_denied, ec)) v.push_back(e.path());
        ec.clear();
    };
    fs::path lad = KnownFolder(FOLDERID_LocalAppData);
    if (cat == CAT_DL) {
        addDir(KnownFolder(FOLDERID_Downloads));
    } else if (cat == CAT_TEMP) {
        addDir(lad / L"Temp");
    } else if (cat == CAT_SYS) {
        for (const auto& e : fs::directory_iterator(lad / L"Microsoft" / L"Windows" / L"Explorer", ec)) {
            std::wstring n = e.path().filename().wstring();
            if (_wcsnicmp(n.c_str(), L"thumbcache_", 11) == 0 || _wcsnicmp(n.c_str(), L"iconcache_", 10) == 0) v.push_back(e.path());
        }
        ec.clear();
        addDir(lad / L"CrashDumps");
        addDir(lad / L"Microsoft" / L"Windows" / L"WER" / L"ReportArchive");
        addDir(lad / L"Microsoft" / L"Windows" / L"WER" / L"ReportQueue");
        if (deep) {
            AddDeepItems(v);
            if (UltraOn()) AddUltraItems(v);
        }
    }
    return v;
}

static unsigned long long PathSize(const fs::path& p) {
    std::error_code ec;
    if (fs::is_symlink(p, ec)) return 0;
    if (fs::is_regular_file(p, ec)) {
        auto s = fs::file_size(p, ec);
        return ec ? 0 : s;
    }
    unsigned long long total = 0;
    if (fs::is_directory(p, ec)) {
        fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end;
        while (!ec && it != end) {
            std::error_code e2;
            if (it->is_regular_file(e2) && !e2) {
                auto s = it->file_size(e2);
                if (!e2) total += s;
            }
            it.increment(ec);
        }
    }
    return total;
}

struct CatStat {
    long long count = 0;
    unsigned long long bytes = 0;
};
static CatStat g_stat[CAT_N];
static std::mutex g_statMx;
static std::unordered_map<std::wstring, unsigned long long> g_sizeCache;  // Groesse pro Eintrag aus dem letzten Scan (fuer die Gruppen-Ueberschriften)
static std::atomic<bool> g_scanning{false};
static std::atomic<bool> g_scanned{false};  // vom Scan-Thread gesetzt, von der Oberflaeche gelesen

// Browser-Caches pro Browser (0 Chrome, 1 Edge, 2 Brave, 3 Firefox, 4 Opera GX) vermessen. Das Ergebnis wird zwischengespeichert,
// damit die Uebersicht und die Browser-Seite dieselbe Messung nutzen koennen und nicht doppelt rechnen.
struct BrowserScan {
    std::vector<fs::path> paths;
    unsigned long long bytes = 0;
};
constexpr int kBrowsers = 5;  // Chrome, Edge, Brave, Firefox, Opera GX
static BrowserScan g_browScan[kBrowsers];
static ULONGLONG g_browScanTick = 0;  // GetTickCount64 der letzten Messung, 0 = keine gueltige
static std::mutex g_browScanMx;

static void ScanBrowsersFresh() {
    BrowserScan tmp[kBrowsers];
    for (int b = 0; b < kBrowsers; ++b) {
        AddBrowserCaches(tmp[b].paths, b);
        for (const auto& p : tmp[b].paths) tmp[b].bytes += PathSize(p);
    }
    std::lock_guard<std::mutex> lk(g_browScanMx);
    for (int b = 0; b < kBrowsers; ++b) g_browScan[b] = std::move(tmp[b]);
    g_browScanTick = GetTickCount64();
}

static void ScanBrowsersCached(ULONGLONG maxAgeMs) {
    {
        std::lock_guard<std::mutex> lk(g_browScanMx);
        if (g_browScanTick && GetTickCount64() - g_browScanTick < maxAgeMs) return;
    }
    ScanBrowsersFresh();
}

static void InvalidateBrowserScan() {
    std::lock_guard<std::mutex> lk(g_browScanMx);
    g_browScanTick = 0;
}

static CatStat ScanCategory(int cat, std::unordered_map<std::wstring, unsigned long long>* sizes = nullptr) {
    CatStat s;
    if (cat == CAT_BROWSER) {
        ScanBrowsersFresh();
        std::lock_guard<std::mutex> lk(g_browScanMx);
        for (const auto& b : g_browScan) {
            s.count += (long long)b.paths.size();
            s.bytes += b.bytes;
        }
        return s;
    }
    for (const auto& p : ListItems(cat, cat == CAT_SYS && DeepOn())) {
        ++s.count;
        const unsigned long long sz = PathSize(p);
        s.bytes += sz;
        if (sizes) (*sizes)[p.wstring()] = sz;
    }
    return s;
}

static std::wstring FormatSize(unsigned long long b) {
    wchar_t buf[40];
    if (b >= (1ull << 40)) swprintf_s(buf, L"%.2f TB", b / 1099511627776.0);
    else if (b >= (1ull << 30)) swprintf_s(buf, L"%.1f GB", b / 1073741824.0);
    else if (b >= (1ull << 20)) swprintf_s(buf, L"%.0f MB", b / 1048576.0);
    else swprintf_s(buf, L"%.0f KB", b / 1024.0);
    return buf;
}

// Verzeichnis-Verknuepfungen (Junctions/Symlinks) nie verfolgen: sonst koennte ein Eintrag auf fremde Daten zeigen
static bool IsReparsePoint(const fs::path& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
}

// Entfernt nur die Verknuepfung selbst, nie das Ziel
static bool RemoveLinkOnly(const fs::path& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return (a & FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryW(p.c_str()) != 0 : DeleteFileW(p.c_str()) != 0;
}

// Endgueltiges Loeschen direkt ueber die Win32-Funktionen. SHFileOperation kostet pro Aufruf rund 25 ms Shell-Overhead:
// bei einigen tausend Temp-Eintraegen waere "Optimieren" minutenlang scheinbar eingefroren (gemessen: 1500 Dateien einzeln
// 38 s, direkt unter 1 s). Verknuepfungen werden nur selbst entfernt, nie das Ziel. false = etwas blieb (z. B. in Benutzung).
static std::wstring LongPath(const fs::path& p) {
    std::wstring s = p.wstring();
    if (s.size() >= 240 && s.rfind(L"\\\\?\\", 0) != 0 && s.size() > 2 && s[1] == L':') s = L"\\\\?\\" + s;   // Pfade ueber MAX_PATH
    return s;
}
// Sicheres Loeschen (Einstellung): der Inhalt einer Datei wird vor dem endgueltigen Loeschen einmal mit Zufallsdaten ueberschrieben.
// Auf SSDs und bei Dateisystemen, die Daten verschieben (Schattenkopien, Cloud-Ordner), schuetzt das nur begrenzt.
static std::atomic<int> g_secureDel{-1};
static bool SecureOn() {
    int v = g_secureDel.load();
    if (v < 0) { v = GetFlag(L"SecureDelete", false) ? 1 : 0; g_secureDel = v; }
    return v != 0;
}
static void ShredContents(const std::wstring& lp, DWORD attrs) {
    if (attrs & FILE_ATTRIBUTE_SPARSE_FILE) return;   // wuerde Gigabyte an Platz belegen
    HANDLE f = CreateFileW(lp.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;           // in Benutzung: das Loeschen scheitert danach ohnehin
    LARGE_INTEGER sz{};
    if (GetFileSizeEx(f, &sz) && sz.QuadPart > 0) {
        std::vector<BYTE> buf(1 << 16);
        long long left = sz.QuadPart;
        while (left > 0) {
            const DWORD n = (DWORD)std::min<long long>(left, (long long)buf.size());
            if (BCryptGenRandom(nullptr, buf.data(), n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) break;
            DWORD w = 0;
            if (!WriteFile(f, buf.data(), n, &w, nullptr) || w != n) break;
            left -= n;
        }
        FlushFileBuffers(f);
    }
    CloseHandle(f);
}
static bool DeleteTree(const fs::path& p, int depth = 0) {
    const std::wstring lp = LongPath(p);
    const DWORD a = GetFileAttributesW(lp.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;   // schon weg
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) return (a & FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryW(lp.c_str()) != 0 : DeleteFileW(lp.c_str()) != 0;
    if (!(a & FILE_ATTRIBUTE_DIRECTORY)) {
        if (a & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(lp.c_str(), a & ~FILE_ATTRIBUTE_READONLY);
        if (SecureOn()) ShredContents(lp, a);
        return DeleteFileW(lp.c_str()) != 0;
    }
    if (depth > 200) return false;   // Schutz vor extrem tiefen Baeumen
    bool ok = true;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((lp + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            if (!DeleteTree(p / fd.cFileName, depth + 1)) ok = false;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return ok && RemoveDirectoryW(lp.c_str()) != 0;
}

static bool DeleteEntry(const fs::path& p, bool permanent) {
    if (IsCriticalPath(p)) return false;  // Systemordner, Programme, Laufwerkswurzeln: nie
    if (IsReparsePoint(p)) return RemoveLinkOnly(p);
    if (permanent) return DeleteTree(p);
    // Papierkorb: SHFileOperation braucht einen doppelt nullterminierten String
    std::wstring from = p.wstring();
    from.push_back(L'\0');

    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    if (!permanent) op.fFlags |= FOF_ALLOWUNDO;
    return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}

static void MarkCleaned() { std::ofstream{StateFile()} << std::time(nullptr); }

// Verlauf der freigeraeumten Menge: eine Zeile "Zeitpunkt<Tab>Bytes" je Bereinigung in freigeraeumt.tsv (neben last_run.txt).
// Die Uebersicht zeigt daraus "Bisher freigeraeumt" und die letzten 30 Tage.
static fs::path FreedFile() { return StateFile().parent_path() / L"freigeraeumt.tsv"; }
static std::atomic<unsigned long long> g_freedTotal{0}, g_freed30{0};
static void LoadFreed() {
    std::ifstream f(FreedFile());
    unsigned long long total = 0, d30 = 0;
    const long long now = (long long)std::time(nullptr);
    long long t;
    unsigned long long b;
    while (f >> t >> b) {
        total += b;
        if (now - t <= 30ll * 86400) d30 += b;
    }
    g_freedTotal = total;
    g_freed30 = d30;
}
static void RecordFreed(unsigned long long bytes) {
    if (!bytes) return;
    { std::ofstream f(FreedFile(), std::ios::app); f << (long long)std::time(nullptr) << '\t' << bytes << '\n'; }
    LoadFreed();
}
static unsigned long long FreedTotal() {   // beim ersten Zugriff aus der Datei laden
    static bool loaded = false;
    if (!loaded) { loaded = true; LoadFreed(); }
    return g_freedTotal;
}

// Loescht die Eintraege; "failed" sind bei Temp meist von Programmen benutzte Dateien.
static void CleanPaths(const std::vector<fs::path>& items, bool permanent, int& ok, int& failed) {
    unsigned long long freed = 0;
    for (const auto& path : items) {
        const unsigned long long sz = PathSize(path);   // vor dem Loeschen messen (Auto-Clean ohne Fenster)
        if (DeleteEntry(path, permanent)) { ++ok; freed += sz; }
        else ++failed;
    }
    RecordFreed(freed);
}

// Kommandozeile/Auto: leert Downloads und setzt den Zaehler zurueck
static bool CleanDownloads(bool dryRun, bool permanent, int& ok, int& failed) {
    ok = failed = 0;
    if (KnownFolder(FOLDERID_Downloads).empty()) return false;
    auto items = ListItems(CAT_DL);
    if (const unsigned long long keep = (unsigned long long)KeepBelowMB() * 1048576ull)  // Eintraege unter der Groessengrenze bleiben erhalten
        items.erase(std::remove_if(items.begin(), items.end(), [&](const fs::path& p) { return PathSize(p) < keep; }), items.end());
    if (dryRun) {
        for (const auto& p : items) std::wcout << L"[dry-run] " << p.wstring() << L"\n";
        return true;
    }
    CleanPaths(items, permanent, ok, failed);
    MarkCleaned();
    return true;
}

// Auto-Clean, falls in den Einstellungen gewaehlt: zusaetzlich Temp und Systemdateien (immer endgueltig; Schutzliste gilt)
static void CleanExtras(bool dryRun, int& ok, int& failed) {
    if (!GetFlag(L"AutoExtra", false)) return;
    auto items = ListItems(CAT_TEMP);
    auto sys = ListItems(CAT_SYS);
    items.insert(items.end(), sys.begin(), sys.end());
    if (dryRun) {
        for (const auto& p : items) std::wcout << L"[dry-run] " << p.wstring() << L"\n";
        return;
    }
    CleanPaths(items, true, ok, failed);
}

// Prueft die 30 Tage. Rueckgabe: true, wenn jetzt aufgeraeumt werden soll.
static bool Due(bool force) {
    fs::path stateFile = StateFile();
    std::time_t now = std::time(nullptr), last = 0;
    if (std::ifstream in{stateFile}) in >> last;
    if (last == 0) {
        // Erster Start: nur Startdatum merken, noch nichts loeschen
        std::ofstream{stateFile} << now;
        std::cout << "Erster Start - Zaehler gestartet. Naechste Bereinigung in " << AutoDays() << " Tagen.\n";
        return force;
    }
    if (!force && std::difftime(now, last) < AutoDays() * 24.0 * 3600) {
        double left = AutoDays() - std::difftime(now, last) / (24 * 3600);
        std::cout << "Noch nicht faellig (ca. " << left << " Tage).\n";
        return false;
    }
    return true;
}

static std::wstring DaysLeftText() {
    if (!AutoCleanOn()) return Tr(L"Auto-Clean ist aus");
    std::time_t last = 0;
    if (std::ifstream in{StateFile()}) in >> last;
    if (last == 0) return Tr(L"Auto-Clean startet beim ersten Lauf");
    double left = AutoDays() - std::difftime(std::time(nullptr), last) / (24 * 3600);
    return left > 0 ? Tr(L"Auto-Clean in ") + std::to_wstring((int)std::ceil(left)) + Tr(L" Tagen")
                    : Tr(L"Auto-Clean fällig beim nächsten Login");
}

// Fragt nach, ob der Papierkorb endgueltig geleert werden soll (betrifft den ganzen Papierkorb).
static void AskEmptyRecycleBin(HWND owner) {
    if (AppBox(owner,
            (std::wstring(Tr(L"Soll der Papierkorb jetzt endgültig geleert werden?\n")) +
             Tr(L"(Das betrifft den ganzen Papierkorb und kann nicht rückgängig gemacht werden.)")).c_str(),
            L"Duncan's Tidy", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;
    HRESULT hr = SHEmptyRecycleBinW(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
    AppBox(owner, SUCCEEDED(hr) ? Tr(L"Papierkorb geleert.") : Tr(L"Papierkorb konnte nicht geleert werden."),
                L"Duncan's Tidy", MB_OK | MB_ICONINFORMATION);
}

// ---- Update ueber GitHub Releases ----
// Im Repo-Release liegen: DuncansTidy.exe, DuncansTidy.exe.sha256, version.txt und changelog.txt.
constexpr wchar_t kVersion[] = L"1.8.7";
static const wchar_t* kRepo = L"Neox365/Duncans-Tidy-ToolSet";  // "Besitzer/Repository" auf GitHub
constexpr UINT WM_UPDATE_FOUND = WM_APP + 1;
constexpr UINT WM_SHOW_WHATSNEW = WM_APP + 2;
constexpr UINT WM_SCAN_DONE = WM_APP + 3;
constexpr size_t kMaxDownload = 50u * 1024 * 1024;
static std::wstring g_newVersion;
// Beschriftung des Update-Knopfs; %s wird durch die Versionsnummer ersetzt (Satzbau je Sprache verschieden)
static std::wstring UpdateButtonText() {
    std::wstring s = Tr(L"Update auf v%s installieren");
    const size_t p = s.find(L"%s");
    if (p != std::wstring::npos) s.replace(p, 2, g_newVersion);
    return s;
}
static std::wstring g_notes;  // Aenderungen der neuen Version (aus changelog.txt)

// got/total (optional): Fortschritt des Downloads in Bytes, fuer den Fortschrittsbalken
static bool HttpGet(const std::wstring& path, std::string& out, std::atomic<unsigned long long>* got = nullptr,
                    std::atomic<unsigned long long>* total = nullptr) {
    bool ok = false;
    HINTERNET s = WinHttpOpen(L"DuncansTidy-Updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    if (s) WinHttpSetTimeouts(s, 5000, 8000, 10000, 15000);  // Aufloesen, Verbinden, Senden, Empfangen (ms)
    HINTERNET c = s ? WinHttpConnect(s, L"github.com", INTERNET_DEFAULT_HTTPS_PORT, 0) : nullptr;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path.c_str(), nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE) : nullptr;
    if (r && WinHttpSendRequest(r, nullptr, 0, nullptr, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
        DWORD status = 0, sz = sizeof status;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &sz, nullptr);
        if (status == 200) {
            ok = true;
            DWORD n;
            char buf[16384];
            DWORD len = 0, ls = sizeof len;
            if (total && WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &len, &ls, nullptr)) *total = len;
            while (WinHttpReadData(r, buf, sizeof buf, &n) && n) {
                out.append(buf, n);
                if (got) *got = out.size();
                if (out.size() > kMaxDownload) { ok = false; break; }
            }
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    return ok;
}

static std::wstring AssetPath(const wchar_t* file) {
    return std::wstring(L"/") + kRepo + L"/releases/latest/download/" + file;
}

// "0.5.1" -> {0,5,1}; true, wenn a neuer als b ist
static bool IsNewer(const std::string& a, const std::wstring& bw) {
    auto parse = [](const std::string& s) {
        std::vector<int> v;
        size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && !isdigit((unsigned char)s[i])) ++i;
            if (i >= s.size()) break;
            v.push_back(atoi(s.c_str() + i));
            while (i < s.size() && isdigit((unsigned char)s[i])) ++i;
        }
        return v;
    };
    std::string b;
    for (wchar_t ch : bw) b.push_back((char)ch);
    auto x = parse(a), y = parse(b);
    x.resize(std::max(x.size(), y.size()));
    y.resize(x.size());
    return x > y;
}

static std::string Sha256Hex(const std::string& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string hex;
    UCHAR digest[32];
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
        BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0 &&
        BCryptHashData(hash, (PUCHAR)data.data(), (ULONG)data.size(), 0) == 0 &&
        BCryptFinishHash(hash, digest, sizeof digest, 0) == 0) {
        for (UCHAR b : digest) {
            char t[3];
            sprintf_s(t, "%02x", b);
            hex += t;
        }
    }
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return hex;
}

// Prueft die Signatur (ECDSA P-256 ueber SHA-256) mit dem einkompilierten oeffentlichen Schluessel.
// Nur wer den privaten Schluessel besitzt, kann gueltige Updates signieren.
static bool VerifyUpdateSignature(const std::string& data, const std::string& sig) {
    if (sig.size() != 64) return false;
    BCRYPT_ALG_HANDLE sha = nullptr, ecc = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    UCHAR digest[32];
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&sha, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
        BCryptHash(sha, nullptr, 0, (PUCHAR)data.data(), (ULONG)data.size(), digest, sizeof digest) == 0 &&
        BCryptOpenAlgorithmProvider(&ecc, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) == 0 &&
        BCryptImportKeyPair(ecc, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key, (PUCHAR)kUpdatePublicKey, (ULONG)sizeof kUpdatePublicKey, 0) == 0 &&
        BCryptVerifySignature(key, nullptr, digest, sizeof digest, (PUCHAR)sig.data(), (ULONG)sig.size(), 0) == 0)
        ok = true;
    if (key) BCryptDestroyKey(key);
    if (ecc) BCryptCloseAlgorithmProvider(ecc, 0);
    if (sha) BCryptCloseAlgorithmProvider(sha, 0);
    return ok;
}

static std::wstring Utf8ToW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    if (!w.empty() && w[0] == 0xFEFF) w.erase(0, 1);  // BOM
    return w;
}

static fs::path WhatsNewFile() { return StateFile().parent_path() / L"whatsnew.txt"; }

// Laedt die neue .exe, prueft die SHA-256-Summe, tauscht sich selbst aus und startet neu.
// Laeuft im Hintergrund; die Oberflaeche zeigt einen Fortschrittsbalken (g_updPhase, g_updGot, g_updTotal)
constexpr UINT WM_UPDATE_RESULT = WM_APP + 7;  // wParam 1 = installiert (Programm beenden), 0 = Fehler (Text in g_updMsg)
static std::atomic<bool> g_updating{false};
static std::atomic<unsigned long long> g_updGot{0}, g_updTotal{0};
static std::atomic<int> g_updPhase{0};  // 0 = laden, 1 = pruefen, 2 = installieren
static std::wstring g_updMsg;
static std::mutex g_updMx;

static void UpdateWorker(HWND h) {
    auto fail = [h](const wchar_t* msg) {
        { std::lock_guard<std::mutex> lk(g_updMx); g_updMsg = msg; }
        PostMessageW(h, WM_UPDATE_RESULT, 0, 0);
    };
    g_updPhase = 0;
    g_updGot = 0;
    g_updTotal = 0;
    std::string exe, sum;
    if (!HttpGet(AssetPath(L"DuncansTidy.exe"), exe, &g_updGot, &g_updTotal) || !HttpGet(AssetPath(L"DuncansTidy.exe.sha256"), sum)) {
        fail(Tr(L"Download fehlgeschlagen. Bitte später erneut versuchen."));
        return;
    }
    g_updPhase = 1;
    std::string want = sum.substr(0, 64);
    for (auto& ch : want) ch = (char)tolower((unsigned char)ch);
    if (exe.size() < 1024 || Sha256Hex(exe) != want) {
        fail(Tr(L"Die heruntergeladene Datei ist ungültig (Prüfsumme stimmt nicht). Update abgebrochen."));
        return;
    }
    // Echtheit: nur von Duncan signierte Updates werden installiert
    std::string sig;
    if (!HttpGet(AssetPath(L"DuncansTidy.exe.sig"), sig) || !VerifyUpdateSignature(exe, sig)) {
        fail(Tr(L"Die Signatur des Updates ist ungültig oder fehlt. Aus Sicherheitsgründen wird es NICHT installiert."));
        return;
    }
    g_updPhase = 2;
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring cur = self, nw = cur + L".new", old = cur + L".old";
    {
        std::ofstream f(nw, std::ios::binary);
        f.write(exe.data(), (std::streamsize)exe.size());
        if (!f) {
            fail(Tr(L"Konnte die neue Datei nicht speichern."));
            return;
        }
    }
    // Kein Rueckschritt: nur eine echt neuere Version installieren (die Signatur allein wuerde auch eine alte, signierte Version zulassen)
    const auto newVer = ExeVersion(nw), curVer = ExeVersion(cur);
    if (newVer.empty() || !(newVer > curVer)) {
        DeleteFileW(nw.c_str());
        fail(Tr(L"Das Update ist nicht neuer als die installierte Version und wird nicht installiert."));
        return;
    }
    DeleteFileW(old.c_str());
    // Laufende .exe kann umbenannt, aber nicht ueberschrieben werden
    if (!MoveFileW(cur.c_str(), old.c_str()) || !MoveFileW(nw.c_str(), cur.c_str())) {
        MoveFileW(old.c_str(), cur.c_str());  // zurueckrollen
        DeleteFileW(nw.c_str());
        fail(Tr(L"Update konnte nicht installiert werden."));
        return;
    }
    // Aenderungen merken, damit die neue Version sie einmal anzeigt
    if (!g_notes.empty()) {
        std::wstring text = L"Version " + g_newVersion + L"\n\n" + g_notes;
        int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
        std::string u(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), u.data(), n, nullptr, nullptr);
        std::ofstream{WhatsNewFile(), std::ios::binary} << u;
    }
    ShellExecuteW(nullptr, L"open", cur.c_str(), nullptr, nullptr, SW_SHOW);
    PostMessageW(h, WM_UPDATE_RESULT, 1, 0);
}

static void DoUpdate(HWND h) {
    if (g_updating.exchange(true)) return;
    Bg([h] { UpdateWorker(h); });
}

// ---- Oberflaeche: Sidebar, Uebersicht mit Kacheln, Kategorie-Seiten (DPI-bewusst) ----
static const wchar_t* kApp = L"Duncan's Tidy";
// Farben des aktuellen Designs (dunkel oder hell); SetPalette() setzt sie um
static COLORREF kBg, kSide, kPanel, kCard, kCardHot, kText, kSub, kMuted, kStrong, kTrack, kBadgeBg, kNavSel, kNavHot, kSwitchOff, kSwitchOffHot,
                kGood, kWarn, kBad;
static const COLORREF kHdrSub = RGB(214, 220, 255);  // Untertitel auf dem Farbverlauf der Kopfzeile (in beiden Designs hell)
static bool g_dark = true;
constexpr int ID_CLEAN = 101, ID_EXIT = 102, ID_TOGGLE = 103, ID_UPDATE = 106, ID_PROTECT = 107, ID_ADMIN = 110, ID_RAM = 111, ID_NAV0 = 200, ID_MORE = 190;  // ID_MORE: Eintrag "Mehr" in der Seitenleiste, klappt Laufwerke bis Arbeitsspeicher auf
constexpr UINT_PTR ID_RAMTICK = 80;  // Zeitgeber der Arbeitsspeicher-Seite
// Alle Masse unten sind "logische" Einheiten (96 DPI); g_ui rechnet sie auf die Bildschirmskalierung um.
constexpr int kModeW1 = 120, kModeW2 = 120, kModeH = 28;  // Umschalter "Standard | Ultra" in der Kopfzeile
constexpr int kOptW = 200, kActH = 44;  // Aktionszeile der Uebersicht unter dem Banner: Hinweis/Fortschritt links, Umschalter und Optimieren-Knopf rechts
constexpr int kSideW = 224, kHeaderH = 124, kMargin = 28, kPages = 10, kAutoPage = 6, kBrowserPage = 4, kDrivePage = 5, kUninstPage = 7, kRamPage = 8, kSettingsPage = 9;  // Seite 0 = Uebersicht, 1..4 = Kategorien, 5 = Laufwerke, 6 = Autostart, 7 = Programme, 8 = Arbeitsspeicher, 9 = Einstellungen
constexpr int kActTop = kHeaderH + 14;  // Oberkante der Aktionszeile der Uebersicht
static const wchar_t* kPageName[kPages] = {L"Übersicht", L"Downloads", L"Temp-Dateien", L"Systemdateien", L"Browser", L"Laufwerke", L"Autostart", L"Programme", L"Arbeitsspeicher", L"Einstellungen"};
static const wchar_t* kNavGlyphBase0[4] = {L"", L"", L"", L""};

static const wchar_t* kNavGlyphBase[kPages] = {kNavGlyphBase0[0], kNavGlyphBase0[1], kNavGlyphBase0[2], kNavGlyphBase0[3], L"\xE774", L"\xEDA2", L"\xE7E8", L"\xE71D", L"\xE964", L"\xE9E9"};  // EDA2 = Festplatte, E7E8 = Power, E71D = Apps, E964 = Speicherriegel, E9E9 = Regler

static double g_ui = 1.0;
static HBRUSH g_bgBrush, g_panelBrush, g_sideBrush, g_cardBrush;
static COLORREF Tint(COLORREF c, COLORREF base, double t) {  // c mit Anteil t ueber base legen
    auto mix = [&](int a, int b) { return (int)std::lround(a * t + b * (1 - t)); };
    return RGB(mix(GetRValue(c), GetRValue(base)), mix(GetGValue(c), GetGValue(base)), mix(GetBValue(c), GetBValue(base)));
}
static void SetPalette(bool dark) {
    g_dark = dark;
    if (dark) {
        kBg = RGB(22, 23, 36); kSide = RGB(14, 15, 26); kPanel = RGB(15, 16, 26); kCard = RGB(30, 33, 56); kCardHot = RGB(40, 44, 74);
        kText = RGB(226, 229, 244); kSub = RGB(214, 220, 255); kMuted = RGB(140, 146, 175); kStrong = RGB(255, 255, 255);
        kTrack = RGB(42, 45, 74); kBadgeBg = RGB(40, 45, 82); kNavSel = RGB(40, 45, 82); kNavHot = RGB(28, 30, 52);
        kSwitchOff = RGB(62, 66, 98); kSwitchOffHot = RGB(92, 99, 140);
        kGood = RGB(74, 222, 128); kWarn = RGB(251, 191, 36); kBad = RGB(248, 113, 113);
    } else {
        kBg = RGB(241, 243, 250); kSide = RGB(228, 232, 245); kPanel = RGB(255, 255, 255); kCard = RGB(255, 255, 255); kCardHot = RGB(232, 237, 252);
        kText = RGB(30, 34, 56); kSub = RGB(70, 78, 110); kMuted = RGB(102, 110, 142); kStrong = RGB(16, 20, 42);
        kTrack = RGB(214, 220, 240); kBadgeBg = RGB(222, 231, 253); kNavSel = RGB(208, 220, 252); kNavHot = RGB(218, 224, 244);
        kSwitchOff = RGB(172, 180, 206); kSwitchOffHot = RGB(150, 159, 190);
        kGood = RGB(22, 150, 72); kWarn = RGB(196, 130, 0); kBad = RGB(214, 40, 40);
    }
    for (HBRUSH* b : {&g_bgBrush, &g_panelBrush, &g_sideBrush, &g_cardBrush}) if (*b) DeleteObject(*b);
    g_bgBrush = CreateSolidBrush(kBg);
    g_panelBrush = CreateSolidBrush(kPanel);
    g_sideBrush = CreateSolidBrush(kSide);
    g_cardBrush = CreateSolidBrush(kCard);
}
static const wchar_t* ThemeName() { return g_dark ? L"DarkMode_Explorer" : L"Explorer"; }
static HFONT g_fSm, g_fMed, g_fNav, g_fBig, g_fTitle, g_fIcon, g_fIconBig, g_fList;
static RECT g_listRect{};  // Bereich der Liste auf den Kategorie-Seiten (logisch); dort erscheint bei leerer Liste die Leer-Ansicht
static HICON g_logo;
static HWND g_main;  // Hauptfenster (fuer Hilfsfunktionen, die Meldungen dorthin schicken)
static int g_hover = 0, g_page = 0, g_tileHot = -1;
static bool g_navMore = false;  // Seitenleiste: Seiten 5..8 (Laufwerke, Autostart, Programme, Arbeitsspeicher) aufgeklappt
static bool NavGrouped(int i) { return i >= 5 && i <= 8; }
static std::vector<fs::path> g_items;
constexpr int kTiles = 8;  //Kacheln der Uebersicht: eine pro Seite ausser der Uebersicht selbst
static_assert(kTiles == kPages - 2, "Eine Kachel pro Seite (ohne Uebersicht und Einstellungen)");
constexpr int kShownTiles = CAT_N;  // auf der Uebersicht nur die vier Kernbereiche (Downloads, Temp, Systemdateien, Browser-Cache); der Rest ist ueber die Seitenleiste erreichbar
static RECT g_tile[kTiles];
constexpr int kSettings = 9;  // Schalter auf der Einstellungen-Seite
constexpr int kSetAutoRow = 2, kSetTextExtra = 36, kSetPillBar = 50, kSetExtra = kSetTextExtra + kSetPillBar;  // Auto-Clean-Zeile: Platz fuer 2 Textzeilen plus Leiste mit den Auswahlmenues (34 hoch, 16 Abstand unten)
static RECT g_setRow[kSettings];
static bool g_set[kSettings];
static int g_setHot = -1;
static bool g_trayOnClose = false, g_trayShown = false, g_animOn = true;  // g_animOn: Besen-Animation erlaubt
static UINT g_msgTaskbarCreated = 0;
static std::atomic<int> g_ovProgs{0};   // Anzahl installierter Programme fuer die Kachel
static RECT g_ramCard{};  // Karte der Arbeitsspeicher-Seite
static std::atomic<int> g_ovDrives{0};  // Laufwerke und Speicher fuer die Kachel (beim Scan ermittelt)
static std::atomic<unsigned long long> g_ovFree{0}, g_ovTotal{0};
static RECT g_noteRect{};  // Hinweisleiste unter der Liste (Temp, Systemdateien), leer = keine
static RECT g_btnRect{};  // Position des Clean-Buttons auf Listen-Seiten (dort erscheint waehrenddessen der Fortschrittsbalken)

// Zustand der Hintergrund-Bereinigung
constexpr UINT WM_CLEAN_TICK = WM_APP + 4, WM_CLEAN_DONE = WM_APP + 5, WM_BROWSER_DONE = WM_APP + 6, WM_DRIVE_DONE = WM_APP + 8;
static std::atomic<bool> g_busy{false};
static std::atomic<bool> g_optDl{false};  // Optimieren hat auch die Downloads (in den Papierkorb) bereinigt
static std::atomic<int> g_progDone{0}, g_progTotal{0}, g_resOk{0}, g_resFail{0};
static std::atomic<ULONGLONG> g_ultraStart{0};   // Beginn der Ultra-Aktionen (GetTickCount64), 0 = keine laufen
static std::atomic<unsigned long long> g_resFreed{0};
static std::mutex g_jobMx;
static int g_linkHot = 0;  // Maus ueber der Versionszeile in der Seitenleiste (Klick oeffnet das Ueber-Fenster)
static std::vector<std::wstring> g_skipped;  // Namen der ersten uebersprungenen Eintraege (max. 5)
static std::wstring g_curName;            // Datei, die gerade geloescht wird
static std::wstring g_doneText, g_doneSub;  // Zusammenfassung nach Optimieren (Uebersicht), zwei Zeilen

static int D(int v) { return (int)std::lround(v * g_ui); }
static void MW(HWND w, int x, int y, int cw, int ch) { MoveWindow(w, D(x), D(y), D(cw), D(ch), TRUE); }
static int PageCat(int page) { return page >= 1 && page <= CAT_N ? page - 1 : -1; }

static void SetLogical(HDC dc) {
    SetGraphicsMode(dc, GM_ADVANCED);
    XFORM xf{(FLOAT)g_ui, 0, 0, (FLOAT)g_ui, 0, 0};
    SetWorldTransform(dc, &xf);
}
static void ResetLogical(HDC dc) { ModifyWorldTransform(dc, nullptr, MWT_IDENTITY); }

static COLORREF Shade(COLORREF c, double f) {
    auto s = [f](int v) { return (int)std::min(255.0, v * f); };
    return RGB(s(GetRValue(c)), s(GetGValue(c)), s(GetBValue(c)));
}

// Weich geglaettetes Rechteck mit runden Ecken (GDI+), Koordinaten in logischen Einheiten
static void FillRound(HDC dc, const RECT& r, COLORREF c, int diameter) {
    int saved = SaveDC(dc);
    ResetLogical(dc);
    {
        using namespace Gdiplus;
        Graphics g(dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.ScaleTransform((REAL)g_ui, (REAL)g_ui);
        REAL x = (REAL)r.left, y = (REAL)r.top, w = (REAL)(r.right - r.left), h = (REAL)(r.bottom - r.top);
        REAL d = std::min((REAL)diameter, std::min(w, h));
        GraphicsPath path;
        path.AddArc(x, y, d, d, 180, 90);
        path.AddArc(x + w - d, y, d, d, 270, 90);
        path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
        path.AddArc(x, y + h - d, d, d, 90, 90);
        path.CloseFigure();
        SolidBrush b(Color(255, GetRValue(c), GetGValue(c), GetBValue(c)));
        g.FillPath(&b, &path);
    }
    RestoreDC(dc, saved);
}

static void DrawTxt(HDC dc, HFONT f, COLORREF c, int x, int y, const std::wstring& s) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    TextOutW(dc, x, y, s.c_str(), (int)s.size());
}

// Text, der nicht ueber "right" hinausragt (wird mit ... gekuerzt), z.B. damit er nicht in den Button daneben laeuft
static void DrawFit(HDC dc, HFONT f, COLORREF c, int x, int y, int right, const std::wstring& s) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    RECT r{x, y, right, y + 40};
    DrawTextW(dc, s.c_str(), -1, &r, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

static COLORREF ButtonColor(int id) {
    switch (id) {
    case ID_CLEAN:  return RGB(34, 197, 94);    // gruen
    case ID_EXIT:   return RGB(150, 60, 78);    // gedecktes rot
    case ID_TOGGLE: return RGB(70, 76, 110);    // blaugrau
    case ID_UPDATE: return RGB(168, 85, 247);   // violett
    case ID_RAM:    return RGB(14, 165, 233);   // himmelblau
    case 230:       return RGB(168, 85, 247);   // "GitHub oeffnen" im Ueber-Fenster
    case 240:       return RGB(34, 197, 94);    // "Jetzt aktualisieren" im Update-Fenster
    case 250:       return RGB(34, 197, 94);    // "Hinzufuegen" im Schutzlisten-Fenster
    case 260:       return RGB(34, 197, 94);    // "Browser schliessen" in der Browser-Abfrage
    case 270:       return RGB(34, 197, 94);    // erster Knopf (OK / Ja) der App-MessageBox
    }
    return RGB(70, 76, 110);
}

// ---- Autostart-Manager: Programme, die mit Windows starten, ein- und ausschalten ----
// Wie im Task-Manager: Nichts wird geloescht, nur der Schalter unter StartupApproved (Benutzerbereich) umgelegt.
struct AutoEntry {
    std::wstring name, cmd, approvedSub, approvedValue;  // approvedSub: Run, Run32 oder StartupFolder
    std::wstring sub;                      // nur Browser-Eintraege: Untertitel (leer = Autostart-Eintrag)
    std::vector<fs::path> paths;           // nur Browser-Eintraege: die Cache-Ordner (bzw. bei Verlauf/Cookies die Dateien)
    int bidx = -1, bkind = 0;              // nur Browser-Eintraege: Browser (0 Chrome ... 4 Opera GX) und Art (0 Cache, 1 Verlauf, 2 Cookies)
    unsigned long long bytes = 0;          // nur Browser-Eintraege: Groesse des Caches
    bool drive = false;                    // nur Laufwerks-Karten:
    int level = 0;                         //   Gesundheit: 0 = unbekannt, 1 = gut, 2 = Warnung, 3 = schlecht
    unsigned long long freeB = 0, totalB = 0;
    std::wstring health, disk;             //   Gesundheitszeile, Name der physischen Platte
    bool avail = true, warn = false, missing = false;      // avail = gefunden/schaltbar, warn = Browser laeuft gerade
    bool allUsers = false, orig = true, want = true;  // orig = gespeicherter Zustand, want = gewuenschter (Schalter)
    HICON icon = nullptr;                  // nur Autostart: Symbol des Programms (liegt im Cache und wird nicht freigegeben)
    std::wstring exePath, publisher;       // Autostart und Programme: aufgeloeste Datei (bzw. Symbolquelle) und Herausgeber
    std::wstring uninst;                   // nur Programme: Deinstallationsbefehl aus der Registry
    int impact = 0;                        // nur Autostart: 0 = unbekannt, 1 = niedrig, 2 = mittel, 3 = hoch (Schaetzung nach Programmgroesse)
    bool open = false;                     // nur Autostart: Details aufgeklappt
    const wchar_t* glyph = nullptr;        // nur Gruppen-Kacheln (Temp, Systemdateien): Symbol und Farbe links
    COLORREF col = 0;
};
static std::vector<AutoEntry> g_auto;
static std::vector<int> g_autoVis;  // Indizes in g_auto (bzw. g_brow), die der Filter durchlaesst
static std::vector<AutoEntry> g_brow, g_browTmp;  // Browser-Seite: eine Karte pro Browser (nutzt dieselbe Kartenliste wie Autostart)
static std::atomic<bool> g_browLoading{false};
static std::vector<AutoEntry> g_drv, g_drvTmp;  // Laufwerke-Seite: eine Karte pro Laufwerk
static std::vector<AutoEntry> g_grp;            // Temp- und Systemdateien-Seite: eine Karte pro Gruppe (level = Gruppennummer)
static std::vector<AutoEntry> g_unin, g_uninTmp;  // Programme-Seite: eine Karte pro installiertem Programm
static std::atomic<bool> g_uninLoading{false};
static ULONGLONG g_uninTick = 0;                // GetTickCount64 des letzten Abschlusses, damit die Liste nicht bei jedem Fensterwechsel neu gelesen wird
static bool g_unSortSize = true;                // true = nach Groesse, false = nach Name
static std::atomic<bool> g_drvLoading{false};
static std::vector<AutoEntry>& Cur() { return g_page == kBrowserPage ? g_brow : g_page == kDrivePage ? g_drv : g_page == kUninstPage ? g_unin : (g_page >= 1 && g_page <= 3) ? g_grp : g_auto; }

static const wchar_t* kBrowserName[kBrowsers] = {L"Google Chrome", L"Microsoft Edge", L"Brave", L"Mozilla Firefox", L"Opera GX"};
static const wchar_t* kBrowserExe[kBrowsers] = {L"chrome.exe", L"msedge.exe", L"brave.exe", L"firefox.exe", L"opera.exe"};

// Winzige Partitionen (unter 1 GB) und die "System-reserviert"-Partition verwirren nur und lassen sich nicht aufraeumen
static bool IsHiddenDrive(const wchar_t* root, unsigned long long total) {
    if (total < (1ull << 30)) return true;
    wchar_t label[64]{};
    GetVolumeInformationW(root, label, 64, nullptr, nullptr, nullptr, nullptr, 0);
    return _wcsicmp(label, L"System-reserviert") == 0 || _wcsicmp(label, L"System Reserved") == 0 || _wcsicmp(label, L"Systemreserviert") == 0;
}

static bool IsRunning(const wchar_t* exe) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (s == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{sizeof pe};
    bool found = false;
    for (BOOL ok = Process32FirstW(s, &pe); ok; ok = Process32NextW(s, &pe))
        if (_wcsicmp(pe.szExeFile, exe) == 0) { found = true; break; }
    CloseHandle(s);
    return found;
}

// ---- Laufwerke: Belegung ueber die Windows-API, Gesundheit ueber die Windows-Speicherverwaltung (nur lesend) ----

// Skript als Base64 fuer powershell -EncodedCommand (so gibt es keine Anfuehrungszeichen-Probleme); leer bei Fehler
static std::wstring PsEncode(const std::wstring& script) {
    DWORD n = 0;
    CryptBinaryToStringW((const BYTE*)script.data(), (DWORD)(script.size() * 2), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &n);
    std::wstring b64(n, L'\0');
    if (!CryptBinaryToStringW((const BYTE*)script.data(), (DWORD)(script.size() * 2), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64.data(), &n)) return L"";
    b64.resize(wcslen(b64.c_str()));
    return b64;
}
static const wchar_t* kPsArgs = L" -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -EncodedCommand ";

// Fuehrt ein PowerShell-Skript unsichtbar aus und sammelt die Ausgabe (UTF-8); bricht nach 25 Sekunden ab
static bool RunPowerShellCapture(const std::wstring& script, std::string& out) {
    const std::wstring b64 = PsEncode(script);
    if (b64.empty()) return false;
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    std::wstring cmd = L"\"" + SystemFile(L"WindowsPowerShell\\v1.0\\powershell.exe") + L"\"" + kPsArgs + b64;
    STARTUPINFOW si{sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    PROCESS_INFORMATION pi{};
    bool started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != 0;
    CloseHandle(wr);
    if (!started) { CloseHandle(rd); return false; }
    auto drain = [&] {
        DWORD avail = 0;
        while (PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) && avail) {
            char buf[4096];
            DWORD got = 0;
            if (!ReadFile(rd, buf, std::min<DWORD>(avail, sizeof buf), &got, nullptr) || !got) break;
            out.append(buf, got);
        }
    };
    const ULONGLONG t0 = GetTickCount64();
    bool done = false;
    for (;;) {
        drain();
        if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) { drain(); done = true; break; }
        if (GetTickCount64() - t0 > 25000) { TerminateProcess(pi.hProcess, 1); break; }
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(rd);
    return done;
}

// ---- Speicherfresser-Finder: die 20 groessten Dateien eines Laufwerks (nur Anzeige, es wird nichts geloescht) ----
constexpr UINT WM_TOP_FILES = WM_APP + 12, WM_TOP_DONE = WM_APP + 13;  // WM_TOP_FILES: wParam = Laufwerksbuchstabe
constexpr UINT WM_ANALYZE_PATH = WM_APP + 20;   // Start mit --analyze <Ordner>: Ordner liegt in g_pendingAnalyze
constexpr ULONG_PTR kAnalyzeMagic = 0x7D7A0001;  // WM_COPYDATA eines zweiten Starts an die laufende Instanz
static std::wstring g_pendingAnalyze;
static std::atomic<bool> g_topBusy{false};
static std::wstring g_topLabel = L"C:";  // Anzeigename des gerade durchsuchten Laufwerks/Ordners
struct TopEntry { unsigned long long size = 0; std::wstring name, dir, full; FILETIME mt{}; };
static std::wstring g_topRoot;           // zuletzt durchsuchte Wurzel (fuer den Umschalter im Ergebnisfenster)
static bool g_topForgotten = false;      // true = nur grosse Dateien, die lange niemand angefasst hat
static std::vector<TopEntry> g_topList;
static std::wstring g_topTitle, g_topSub;  // Ergebnis, vom Thread vor WM_TOP_DONE gesetzt
static std::mutex g_topMx;

// root: "C:" (Laufwerk) oder voller Ordnerpfad ohne abschliessenden Backslash; label: Anzeigename fuer Titel und Statuszeile
// forgotten: nur Dateien ab 50 MB, die seit ueber einem Jahr weder geaendert noch benutzt wurden (Datei-Zugriffszeit, wenn Windows sie fuehrt);
// Programmordner, Windows, AppData und Spiele-Bibliotheken werden dabei uebersprungen (dort ist "alt" nicht "vergessen")
static void StartTopFilesAt(HWND h, std::wstring root, std::wstring label, bool forgotten = false) {
    if (g_topBusy.exchange(true)) return;
    g_topLabel = label;
    g_topRoot = root;
    g_topForgotten = forgotten;
    InvalidateRect(h, nullptr, FALSE);
    Bg([h, root, label, forgotten] {
        struct F { unsigned long long size; std::wstring path; FILETIME mt; };
        ULARGE_INTEGER cutoff{};
        {
            FILETIME nowFt;
            GetSystemTimeAsFileTime(&nowFt);
            ULARGE_INTEGER n;
            n.LowPart = nowFt.dwLowDateTime; n.HighPart = nowFt.dwHighDateTime;
            cutoff.QuadPart = n.QuadPart - 365ull * 24 * 3600 * 10000000ull;
        }
        constexpr unsigned long long kForgotMin = 50ull * 1048576ull;
        static const wchar_t* const kSkipDirs[] = {L"Windows", L"Program Files", L"Program Files (x86)", L"ProgramData", L"$Recycle.Bin", L"System Volume Information", L"AppData",
                                                   L"steamapps", L"Epic Games", L"WindowsApps", L"GOG Galaxy", L"Ubisoft Game Launcher", L"Riot Games"};
        std::vector<F> top;  // absteigend nach Groesse, hoechstens 20
        unsigned long long sum = 0;
        // \\?\-Praefix: auch sehr lange Pfade; Netzwerkpfade werden zu \\?\UNC\...
        const std::wstring base = root.rfind(L"\\\\?\\", 0) == 0 ? root
                                : root.rfind(L"\\\\", 0) == 0 ? L"\\\\?\\UNC\\" + root.substr(2)
                                                              : L"\\\\?\\" + root;
        auto display = [](const std::wstring& full) {  // Praefix fuer die Anzeige wieder entfernen
            return full.rfind(L"\\\\?\\UNC\\", 0) == 0 ? L"\\\\" + full.substr(8) : full.substr(4);
        };
        std::vector<std::wstring> stack{base};
        while (!stack.empty()) {
            const std::wstring dir = std::move(stack.back());
            stack.pop_back();
            WIN32_FIND_DATAW fd;
            HANDLE f = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
            if (f == INVALID_HANDLE_VALUE) continue;
            do {
                const std::wstring name = fd.cFileName;
                if (name == L"." || name == L"..") continue;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;  // Verknuepfungen nie verfolgen
                const std::wstring full = dir + L"\\" + name;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    if (dir == base && _wcsicmp(name.c_str(), L"Windows") == 0) continue;  // Windows selbst: nichts, was man loeschen sollte
                    if (forgotten) {
                        bool skip = false;
                        for (const wchar_t* sk : kSkipDirs) if (_wcsicmp(name.c_str(), sk) == 0) skip = true;
                        if (skip) continue;
                    }
                    stack.push_back(full);
                    continue;
                }
                if (_wcsicmp(name.c_str(), L"pagefile.sys") == 0 || _wcsicmp(name.c_str(), L"hiberfil.sys") == 0 || _wcsicmp(name.c_str(), L"swapfile.sys") == 0) continue;
                const unsigned long long sz = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                if (forgotten) {
                    if (sz < kForgotMin) continue;
                    ULARGE_INTEGER lw, la;
                    lw.LowPart = fd.ftLastWriteTime.dwLowDateTime; lw.HighPart = fd.ftLastWriteTime.dwHighDateTime;
                    la.LowPart = fd.ftLastAccessTime.dwLowDateTime; la.HighPart = fd.ftLastAccessTime.dwHighDateTime;
                    if (std::max(lw.QuadPart, la.QuadPart) >= cutoff.QuadPart) continue;   // vor kurzem geaendert oder benutzt
                }
                sum += sz;
                if (top.size() < 20 || sz > top.back().size) {
                    top.push_back({sz, display(full), fd.ftLastWriteTime});
                    std::sort(top.begin(), top.end(), [](const F& a, const F& b) { return a.size > b.size; });
                    if (top.size() > 20) top.resize(20);
                }
            } while (FindNextFileW(f, &fd));
            FindClose(f);
        }
        std::vector<TopEntry> list;
        for (const auto& e : top) {
            const fs::path p = e.path;
            list.push_back({e.size, p.filename().wstring(), p.parent_path().wstring(), e.path, e.mt});
        }
        {
            std::lock_guard<std::mutex> lk(g_topMx);
            g_topTitle = (forgotten ? std::wstring(Tr(L"Vergessene Dateien auf ")) : std::wstring(Tr(L"Größte Dateien auf "))) + label;
            g_topSub = top.empty() ? std::wstring(forgotten ? Tr(L"Nichts gefunden: keine Datei über 50 MB, die über ein Jahr unberührt ist.") : Tr(L"Keine Dateien gefunden."))
                                   : std::wstring(forgotten ? Tr(L"Größte, über 1 Jahr unberührt  ·  zusammen ") : Tr(L"Die 20 größten  ·  zusammen ")) + FormatSize(sum);
            g_topList = std::move(list);
        }
        g_topBusy = false;
        PostMessageW(h, WM_TOP_DONE, 0, 0);
    });
}

static void StartTopFiles(HWND h, wchar_t letter) {
    const std::wstring drive = std::wstring(1, letter) + L":";
    StartTopFilesAt(h, drive, drive);
}

// Physische Platten samt Gesundheitswerten; eine Zeile pro Platte: Nr|Name|Typ|Bus|Status|Betrieb|Temp|Abnutzung|Std|Lesefehler|Schreibfehler|Laufwerksbuchstaben
static const wchar_t* kDiskScript =
    L"[Console]::OutputEncoding=[Text.Encoding]::UTF8; $ErrorActionPreference='SilentlyContinue';"
    L"foreach($d in Get-PhysicalDisk){ $r=$d | Get-StorageReliabilityCounter;"
    L"$l=(Get-Partition -DiskNumber ([int]$d.DeviceId) | Where-Object {$_.DriveLetter} | ForEach-Object {[string]$_.DriveLetter}) -join ',';"
    L"'{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}|{8}|{9}|{10}|{11}' -f $d.DeviceId,$d.FriendlyName,$d.MediaType,$d.BusType,$d.HealthStatus,($d.OperationalStatus -join ','),$r.Temperature,$r.Wear,$r.PowerOnHours,$r.ReadErrorsTotal,$r.WriteErrorsTotal,$l }";

// Viele Laufwerke (z. B. SATA-SSDs) melden ihre Werte nur mit Administrator-Rechten. Der Knopf "Details (Admin)" fragt deshalb einmal per UAC
// nach und liest die Werte nur lesend aus; das Ergebnis bleibt bis zum Beenden im Speicher.
constexpr UINT WM_ADMIN_DONE = WM_APP + 14;  // wParam 1 = Werte gelesen
static std::atomic<bool> g_adminBusy{false};
static std::string g_adminRaw;
static std::mutex g_adminMx;
static void StartDriveScan(HWND h);

static void StartAdminHealth(HWND h) {
    if (g_adminBusy.exchange(true)) return;
    Bg([h] {
        bool ok = false;
        const fs::path out = StateFile().parent_path() / (L"health_" + std::to_wstring(GetTickCount64()) + L".tmp");
        { std::ofstream f{out}; }  // die Datei gehoert uns; das erhoehte Skript fuellt sie nur
        std::wstring q;
        for (wchar_t c : out.wstring()) { q += c; if (c == L'\'') q += c; }
        const std::wstring script = L"$o=@(& { " + std::wstring(kDiskScript) + L" }); $o | Out-File -LiteralPath '" + q + L"' -Encoding utf8";
        const std::wstring b64 = PsEncode(script);
        if (!b64.empty()) {
            const std::wstring exe = SystemFile(L"WindowsPowerShell\\v1.0\\powershell.exe");
            const std::wstring args = std::wstring(kPsArgs + 1) + b64;
            SHELLEXECUTEINFOW sei{sizeof sei};
            sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
            sei.lpVerb = L"runas";
            sei.lpFile = exe.c_str();
            sei.lpParameters = args.c_str();
            sei.nShow = SW_HIDE;
            if (ShellExecuteExW(&sei) && sei.hProcess) {  // bei "Abbrechen" in der UAC-Abfrage schlaegt der Aufruf fehl
                WaitForSingleObject(sei.hProcess, 60000);
                CloseHandle(sei.hProcess);
                std::ifstream in(out, std::ios::binary);
                std::string raw(std::istreambuf_iterator<char>(in), {});
                if (!raw.empty()) {
                    std::lock_guard<std::mutex> lk(g_adminMx);
                    g_adminRaw = std::move(raw);
                    ok = true;
                }
            }
        }
        std::error_code ec;
        fs::remove(out, ec);
        g_adminBusy = false;
        PostMessageW(h, WM_ADMIN_DONE, ok ? 1 : 0, 0);
    });
}

static void StartDriveScan(HWND h) {
    g_drv.clear();
    g_autoVis.clear();
    g_drvLoading = true;
    Bg([h] {
        std::string raw;
        {
            std::lock_guard<std::mutex> lk(g_adminMx);
            raw = g_adminRaw;
        }
        const bool haveAdmin = !raw.empty();  // schon mit Administrator-Rechten gelesen
        if (!haveAdmin) RunPowerShellCapture(kDiskScript, raw);
        std::vector<std::vector<std::wstring>> disks;
        {
            const std::wstring all = Utf8ToW(raw);
            size_t pos = 0;
            while (pos < all.size()) {
                size_t e = all.find(L'\n', pos);
                if (e == std::wstring::npos) e = all.size();
                std::wstring line = all.substr(pos, e - pos);
                pos = e + 1;
                while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
                std::vector<std::wstring> f;
                size_t s = 0;
                for (;;) {
                    size_t b = line.find(L'|', s);
                    f.push_back(line.substr(s, b == std::wstring::npos ? std::wstring::npos : b - s));
                    if (b == std::wstring::npos) break;
                    s = b + 1;
                }
                if (f.size() >= 12) disks.push_back(std::move(f));
            }
        }
        std::vector<AutoEntry> v;
        const DWORD mask = GetLogicalDrives();
        for (int i = 2; i < 26; ++i) {  // A: und B: (Disketten) ueberspringen
            if (!(mask & (1u << i))) continue;
            wchar_t root[4] = {(wchar_t)(L'A' + i), L':', L'\\', 0};
            const UINT type = GetDriveTypeW(root);
            if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) continue;
            ULARGE_INTEGER fr{}, tot{}, fa{};
            if (!GetDiskFreeSpaceExW(root, &fr, &tot, &fa) || tot.QuadPart == 0 || IsHiddenDrive(root, tot.QuadPart)) continue;
            wchar_t label[64]{}, fsn[32]{};
            GetVolumeInformationW(root, label, 64, nullptr, nullptr, nullptr, fsn, 32);
            AutoEntry e;
            e.drive = true;
            e.name = std::wstring(1, root[0]) + L":  ·  " + (label[0] ? std::wstring(label) : type == DRIVE_REMOVABLE ? Tr(L"Wechseldatenträger") : Tr(L"Lokaler Datenträger"));
            e.freeB = fr.QuadPart;
            e.totalB = tot.QuadPart;
            e.bytes = tot.QuadPart - fr.QuadPart;
            const std::wstring letter(1, root[0]);
            const std::vector<std::wstring>* d = nullptr;
            for (const auto& f : disks) {
                std::wstring ls = L"," + f[11] + L",";
                if (ls.find(L"," + letter + L",") != std::wstring::npos) { d = &f; break; }
            }
            if (!d) {
                e.sub = std::wstring(fsn);
                e.level = 0;
                e.health = Tr(L"Gesundheit nicht verfügbar (z. B. bei USB-Laufwerken)");
                e.cmd = e.name + Tr(L"\nWindows meldet für dieses Laufwerk keine Gesundheitswerte.");
            } else {
                const auto& f = *d;
                auto num = [&](size_t k) { return f[k].empty() ? -1.0 : _wtof(f[k].c_str()); };
                const double temp = num(6), wear = num(7), poh = num(8), rerr = num(9), werr = num(10);
                std::wstring base;
                if (_wcsicmp(f[4].c_str(), L"Healthy") == 0) { e.level = 1; base = Tr(L"Gesund"); }
                else if (_wcsicmp(f[4].c_str(), L"Warning") == 0) { e.level = 2; base = Tr(L"Warnung"); }
                else if (_wcsicmp(f[4].c_str(), L"Unhealthy") == 0) { e.level = 3; base = Tr(L"Fehlerhaft"); }
                else { e.level = 0; base = Tr(L"Gesundheit nicht verfügbar"); }
                std::vector<std::wstring> ex;
                wchar_t b[64];
                if (temp > 0) { swprintf_s(b, L"%.0f °C", temp); ex.push_back(b); }
                if (wear >= 0) { swprintf_s(b, Tr(L"Abnutzung %.0f %%"), wear); ex.push_back(b); }
                if (poh >= 0) { swprintf_s(b, Tr(L"%.0f Std. in Betrieb"), poh); ex.push_back(b); }
                if (rerr > 0) { swprintf_s(b, Tr(L"%.0f Lesefehler"), rerr); ex.push_back(b); }
                if (werr > 0) { swprintf_s(b, Tr(L"%.0f Schreibfehler"), werr); ex.push_back(b); }
                if (!f[5].empty() && _wcsicmp(f[5].c_str(), L"OK") != 0) ex.push_back(Tr(L"Betriebsstatus: ") + f[5]);
                if (ex.empty()) ex.push_back(haveAdmin ? Tr(L"Windows meldet keine weiteren Werte") : Tr(L"mehr Werte mit „Details (Admin)“"));
                if (e.level == 1 && (wear >= 80 || rerr > 0 || werr > 0 || temp >= 70)) { e.level = 2; base = Tr(L"Gesund, aber auffällig"); }
                e.health = base;
                for (const auto& x : ex) e.health += L"  ·  " + x;
                std::wstring typ = _wcsicmp(f[2].c_str(), L"SSD") == 0 || _wcsicmp(f[2].c_str(), L"HDD") == 0 ? f[2] : L"";
                if (!f[3].empty() && _wcsicmp(f[3].c_str(), L"Unknown") != 0) typ += (typ.empty() ? L"" : L" ") + f[3];
                e.disk = f[1];
                e.sub = f[1] + (typ.empty() ? L"" : L"  ·  " + typ);
                auto val = [&](double x, const wchar_t* unit) {
                    if (x < 0) return std::wstring(Tr(L"nicht verfügbar"));
                    wchar_t t[48];
                    swprintf_s(t, L"%.0f%s", x, unit);
                    return std::wstring(t);
                };
                e.cmd = e.name + Tr(L"\nDatenträger ") + f[0] + L": " + f[1] + Tr(L"\nTyp: ") + (typ.empty() ? Tr(L"unbekannt") : typ) + Tr(L"\nDateisystem: ") + fsn +
                        Tr(L"\nStatus: ") + base + Tr(L"\nTemperatur: ") + (temp > 0 ? val(temp, L" °C") : Tr(L"nicht verfügbar")) + Tr(L"\nAbnutzung: ") + val(wear, L" %") +
                        Tr(L"\nBetriebsstunden: ") + val(poh, L"") + Tr(L"\nLesefehler: ") + val(rerr, L"") + Tr(L"\nSchreibfehler: ") + val(werr, L"");
            }
            if (e.sub.empty()) e.sub = type == DRIVE_REMOVABLE ? Tr(L"Wechseldatenträger") : Tr(L"Lokaler Datenträger");
            e.sub = e.sub.empty() ? std::wstring(fsn) : e.sub;
            v.push_back(std::move(e));
        }
        for (auto& e : v) e.sub = e.sub.empty() ? Tr(L"Laufwerk") : e.sub;
        {
            std::lock_guard<std::mutex> lk(g_statMx);
            g_drvTmp = std::move(v);
        }
        g_drvLoading = false;
        PostMessageW(h, WM_DRIVE_DONE, 0, 0);
    });
}

// Misst die Browser-Caches im Hintergrund (kann bei grossen Caches dauern) und meldet sich per WM_BROWSER_DONE
static void StartBrowserScan(HWND h) {
    g_brow.clear();
    g_autoVis.clear();
    g_browLoading = true;
    Bg([h] {
        ScanBrowsersCached(30000);  // frische Messung der Uebersicht wiederverwenden
        std::vector<AutoEntry> v;
        for (int b = 0; b < kBrowsers; ++b) {
            AutoEntry e;
            e.bidx = b;
            e.name = kBrowserName[b];
            unsigned long long bytes = 0;
            {
                std::lock_guard<std::mutex> lk(g_browScanMx);
                e.paths = g_browScan[b].paths;
                bytes = g_browScan[b].bytes;
            }
            e.avail = !e.paths.empty();
            e.warn = e.avail && IsRunning(kBrowserExe[b]);
            e.orig = e.want = e.avail && bytes > 0;
            if (!e.avail) e.sub = Tr(L"Nicht installiert oder noch nie benutzt");
            else {
                e.sub = bytes ? FormatSize(bytes) + Tr(L" Cache  ·  ") + std::to_wstring(e.paths.size()) + Tr(L" Ordner")
                              : std::wstring(Tr(L"Cache ist leer"));
                if (e.warn) e.sub += Tr(L"  ·  läuft gerade, bitte schließen");
                size_t n = 0;
                for (const auto& p : e.paths) {
                    if (n++ == 8) { e.cmd += Tr(L"\n… und ") + std::to_wstring(e.paths.size() - 8) + Tr(L" weitere"); break; }
                    e.cmd += (e.cmd.empty() ? L"" : L"\n") + p.wstring();
                }
            }
            e.bytes = bytes;
            v.push_back(std::move(e));
        }
        for (int kind = 1; kind <= 2; ++kind)   // danach je Browser Verlauf und Cookies, standardmaessig aus (Cookies loeschen meldet dich bei Webseiten ab)
            for (int b : {0, 1, 2, 4, 3}) {
                if (b == 3 && kind == 1) continue;   // Firefox-Verlauf nicht (liegt zusammen mit den Lesezeichen)
                AutoEntry e;
                e.bidx = b;
                e.bkind = kind;
                e.name = std::wstring(kBrowserName[b]) + L" · " + (kind == 1 ? Tr(L"Verlauf") : Tr(L"Cookies"));
                AddBrowserData(e.paths, b, kind);
                unsigned long long bytes = 0;
                std::error_code ec;
                for (const auto& f : e.paths) { const auto sz = fs::file_size(f, ec); if (!ec) bytes += sz; ec.clear(); }
                e.bytes = bytes;
                e.avail = !e.paths.empty();
                e.orig = e.want = false;
                e.warn = e.avail && IsRunning(kBrowserExe[b]);
                e.sub = !e.avail ? std::wstring(Tr(L"Nichts gefunden"))
                                 : FormatSize(bytes) + L"  ·  " + (kind == 1 ? Tr(L"Besuchte Seiten, Favoriten-Symbole und Top-Sites; Lesezeichen bleiben") : Tr(L"Du wirst bei Webseiten abgemeldet"));
                if (e.warn) e.sub += Tr(L"  ·  läuft gerade, bitte schließen");
                for (const auto& f : e.paths) e.cmd += (e.cmd.empty() ? L"" : L"\n") + f.wstring();
                v.push_back(std::move(e));
            }
        {
            std::lock_guard<std::mutex> lk(g_statMx);
            g_browTmp = std::move(v);
        }
        g_browLoading = false;
        PostMessageW(h, WM_BROWSER_DONE, 0, 0);
    });
}
static HWND g_alist, g_search, g_ttList;
static int g_aScroll = 0, g_aHover = -1;  // Scrollposition (Pixel) und Zeile unter der Maus
static bool g_aHotToggle = false;
static RECT g_infoRect{}, g_searchCard{};  // logische Koordinaten; Info-Icon im Seitenkopf und Rahmen des Suchfelds
static bool g_infoHot = false;
static RECT g_modeRect{};   // Umschalter Standard | Ultra-Tiefenreinigung in der Kopfzeile (logische Koordinaten, leer = nicht sichtbar)
static int g_modeHot = 0;   // 1 = Maus ueber "Standard", 2 = ueber "Ultra"
constexpr int ID_SEARCH = 109, kARowBase = 60, kAGap = 8;
static int RowH() { return g_page == kDrivePage ? 104 : (g_page >= 1 && g_page <= 3) ? 72 : g_page == kUninstPage ? 68 : kARowBase; }  // Laufwerks-Karten sind hoeher (Balken + Gesundheitszeile)
// Autostart: eine angeklickte Karte klappt Pfad und Herausgeber auf und ist dann um kDetailH hoeher; alle anderen Seiten haben gleich hohe Karten
constexpr int kDetailH = 48;
static int RowHOf(int vis) {
    int h = RowH();
    if (g_page == kAutoPage && vis >= 0 && vis < (int)g_autoVis.size() && g_auto[g_autoVis[vis]].open) h += kDetailH;
    return h;
}
static int RowTopL(int vis) {  // Oberkante der Karte vis in logischen Einheiten (ohne Scrollen)
    int y = 0;
    for (int i = 0; i < vis; ++i) y += RowHOf(i) + kAGap;
    return y;
}
static const wchar_t* kRunPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kApprovedPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\";

static bool ReadApproved(const std::wstring& sub, const std::wstring& value) {
    bool enabled = true;
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, (kApprovedPath + sub).c_str(), 0, KEY_READ, &k) == ERROR_SUCCESS) {
        BYTE b[12];
        DWORD n = sizeof b, type = 0;
        if (RegQueryValueExW(k, value.c_str(), nullptr, &type, b, &n) == ERROR_SUCCESS && type == REG_BINARY && n >= 1)
            enabled = !(b[0] & 1);  // 02/06 = an, 03/07 = aus
        RegCloseKey(k);
    }
    return enabled;
}

static bool WriteApproved(const std::wstring& sub, const std::wstring& value, bool enable) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, (kApprovedPath + sub).c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return false;
    BYTE b[12]{};
    b[0] = enable ? 2 : 3;
    if (!enable) {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        memcpy(b + 4, &ft, sizeof ft);
    }
    bool ok = RegSetValueExW(k, value.c_str(), 0, REG_BINARY, b, sizeof b) == ERROR_SUCCESS;
    RegCloseKey(k);
    return ok;
}

static bool IsOwnEntry(const std::wstring& s) {
    std::wstring l = s;
    std::transform(l.begin(), l.end(), l.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return l.find(L"duncanstidy") != std::wstring::npos || l.find(L"duncan's tidy") != std::wstring::npos;
}

// Zeigt der Autostart-Befehl auf eine Datei, die es nicht mehr gibt (Rest einer deinstallierten Software)?
// Nur eindeutige Faelle mit absolutem Pfad zaehlen; alles andere gilt als vorhanden, damit nichts falsch markiert wird.
static std::wstring LowerW(std::wstring s);
static std::wstring ParseCommandPath(const std::wstring& cmd) {  // Programmdatei aus einem Autostart-Befehl (Umgebungsvariablen aufgeloest)
    wchar_t buf[2048];
    DWORD n = ExpandEnvironmentStringsW(cmd.c_str(), buf, 2048);
    std::wstring ex = (n && n <= 2048) ? std::wstring(buf) : cmd;
    const size_t s = ex.find_first_not_of(L" \t");
    if (s == std::wstring::npos) return L"";
    ex = ex.substr(s);
    if (ex[0] == L'"') {
        const size_t e = ex.find(L'"', 1);
        return ex.substr(1, e == std::wstring::npos ? std::wstring::npos : e - 1);
    }
    const size_t x = LowerW(ex).find(L".exe");
    return x != std::wstring::npos ? ex.substr(0, x + 4) : ex.substr(0, ex.find(L' '));
}
static bool IsAbsolutePath(const std::wstring& p) {
    return (p.size() > 3 && p[1] == L':' && (p[2] == L'\\' || p[2] == L'/')) || p.rfind(L"\\\\", 0) == 0;
}
// Nur feste lokale Laufwerke (C:\ ...) werden abgefragt; Netzlaufwerke und getrennte Freigaben koennten einen Zugriff lange blockieren
static bool IsFixedLocalPath(const std::wstring& p) {
    if (p.size() < 3 || p[1] != L':') return false;
    wchar_t root[4] = {p[0], L':', L'\\', 0};
    return GetDriveTypeW(root) == DRIVE_FIXED;
}
static bool CommandTargetMissing(const std::wstring& cmd) {
    const std::wstring p = ParseCommandPath(cmd);
    if (p.empty() || !IsAbsolutePath(p) || !IsFixedLocalPath(p)) return false;
    if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return false;
    const DWORD err = GetLastError();
    return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
}

// Vollstaendiger Pfad der Datei, die ein Autostart-Befehl startet (leer, wenn es sie nicht gibt)
static std::wstring ResolveCommandPath(const std::wstring& cmd) {
    const std::wstring p = ParseCommandPath(cmd);
    if (p.empty()) return L"";
    if (!IsAbsolutePath(p)) {
        wchar_t full[MAX_PATH];
        return SearchPathW(nullptr, p.c_str(), L".exe", MAX_PATH, full, nullptr) ? std::wstring(full) : L"";
    }
    return (IsFixedLocalPath(p) && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) ? p : L"";
}

// Ziel einer Verknuepfung im Autostart-Ordner; andere Dateien zeigen auf sich selbst
static std::wstring StartupTarget(const fs::path& file) {
    std::wstring ext = file.extension().wstring();
    for (auto& c : ext) c = (wchar_t)towlower(c);
    if (ext != L".lnk") return file.wstring();
    static std::unordered_map<std::wstring, std::wstring> cache;
    auto it = cache.find(file.wstring());
    if (it != cache.end()) return it->second;
    static const bool com = (CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), true);
    (void)com;
    std::wstring out;
    IShellLinkW* sl = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) {
        IPersistFile* pf = nullptr;
        if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) {
            wchar_t t[MAX_PATH]{}, x[MAX_PATH]{};
            if (SUCCEEDED(pf->Load(file.c_str(), STGM_READ)) && SUCCEEDED(sl->GetPath(t, MAX_PATH, nullptr, SLGP_RAWPATH)) && t[0] &&
                ExpandEnvironmentStringsW(t, x, MAX_PATH))
                out = x;
            pf->Release();
        }
        sl->Release();
    }
    if (out.empty() || !IsFixedLocalPath(out) || GetFileAttributesW(out.c_str()) == INVALID_FILE_ATTRIBUTES) out = file.wstring();  // Ziel unbekannt: die Verknuepfung selbst zeigen
    cache[file.wstring()] = out;
    return out;
}

// Symbol, Herausgeber (CompanyName aus den Versionsinfos) und Dateigroesse eines Programms; wird pro Datei einmal ermittelt
struct ExeInfo {
    HICON icon = nullptr;
    std::wstring publisher;
    unsigned long long size = 0;
};
static ExeInfo GetExeInfo(const std::wstring& path) {
    static std::unordered_map<std::wstring, ExeInfo> cache;
    static std::mutex mx;  // wird vom UI-Thread (Autostart) und vom Programme-Scan benutzt
    {
        std::lock_guard<std::mutex> lk(mx);
        auto it = cache.find(path);
        if (it != cache.end()) return it->second;
    }
    ExeInfo e;
    SHFILEINFOW sfi{};
    if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof sfi, SHGFI_ICON | SHGFI_LARGEICON)) e.icon = sfi.hIcon;
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) e.size = ((unsigned long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
    DWORD ignored;
    const DWORD n = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (n) {
        std::vector<char> buf(n);
        struct Lang { WORD lang, page; }* lt = nullptr;
        UINT len = 0;
        if (GetFileVersionInfoW(path.c_str(), 0, n, buf.data()) && VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", (LPVOID*)&lt, &len) && len >= sizeof(Lang)) {
            wchar_t q[64];
            swprintf_s(q, L"\\StringFileInfo\\%04x%04x\\CompanyName", lt->lang, lt->page);
            LPWSTR v = nullptr;
            UINT vl = 0;
            if (VerQueryValueW(buf.data(), q, (LPVOID*)&v, &vl) && v) e.publisher = v;
        }
    }
    {
        std::lock_guard<std::mutex> lk(mx);
        cache[path] = e;
    }
    return e;
}

// Trägt Symbol, Herausgeber und die Startauswirkung ein. Die Auswirkung ist eine grobe Schaetzung nach Dateigroesse (keine Messung).
static void FillExeInfo(AutoEntry& e, const std::wstring& path) {
    if (path.empty()) return;
    e.exePath = path;
    const ExeInfo ei = GetExeInfo(path);
    e.icon = ei.icon;
    e.publisher = ei.publisher;
    e.impact = ei.size == 0 ? 0 : ei.size < (4ull << 20) ? 1 : ei.size < (40ull << 20) ? 2 : 3;
}

// ---- Programme (Deinstallieren): klassische Windows-Programme aus der Registry; der Knopf startet das eigene Deinstallationsprogramm.
// Gelesen wird nur; es werden weder Registry-Eintraege noch Reste-Ordner angefasst. ----
struct InstalledApp {
    std::wstring name, publisher, version, date, loc, iconSrc, uninst;
    unsigned long long size = 0;
};

static std::vector<InstalledApp> ReadInstalled(bool withSizes) {
    std::vector<InstalledApp> out;
    std::unordered_map<std::wstring, bool> seen;
    auto readRoot = [&](HKEY root, REGSAM wow) {
        HKEY k;
        if (RegOpenKeyExW(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ | wow, &k) != ERROR_SUCCESS) return;
        for (DWORD i = 0;; ++i) {
            wchar_t sub[256];
            DWORD sl = 256;
            const LONG r = RegEnumKeyExW(k, i, sub, &sl, nullptr, nullptr, nullptr, nullptr);
            if (r == ERROR_NO_MORE_ITEMS) break;
            if (r != ERROR_SUCCESS) continue;
            HKEY a;
            if (RegOpenKeyExW(k, sub, 0, KEY_READ | wow, &a) != ERROR_SUCCESS) continue;
            auto str = [&](const wchar_t* n) {
                wchar_t b[1024]{};
                DWORD sz = sizeof b;
                return RegGetValueW(a, nullptr, n, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, b, &sz) == ERROR_SUCCESS ? std::wstring(b) : std::wstring();
            };
            auto num = [&](const wchar_t* n) {
                DWORD v = 0, sz = sizeof v;
                return RegGetValueW(a, nullptr, n, RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS ? v : 0u;
            };
            InstalledApp e;
            e.name = str(L"DisplayName");
            e.uninst = str(L"UninstallString");
            const std::wstring rel = str(L"ReleaseType");
            // Updates, Systemkomponenten und Eintraege ohne Deinstallation gehoeren nicht in die Liste
            const bool skip = e.name.empty() || e.uninst.empty() || num(L"SystemComponent") == 1 || num(L"NoRemove") == 1 || !str(L"ParentKeyName").empty() ||
                              rel == L"Update" || rel == L"Hotfix" || rel == L"Security Update" || rel == L"Service Pack" || IsOwnEntry(e.name);
            if (!skip) {
                e.publisher = str(L"Publisher");
                e.version = str(L"DisplayVersion");
                const std::wstring d = str(L"InstallDate");
                if (d.size() == 8 && std::all_of(d.begin(), d.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; }))
                    e.date = d.substr(0, 4) + L"-" + d.substr(4, 2) + L"-" + d.substr(6, 2);
                e.loc = str(L"InstallLocation");
                e.iconSrc = str(L"DisplayIcon");
                e.size = (unsigned long long)num(L"EstimatedSize") * 1024ull;
                if (seen.emplace(LowerW(e.name) + L"|" + e.version, true).second) out.push_back(std::move(e));
            }
            RegCloseKey(a);
        }
        RegCloseKey(k);
    };
    readRoot(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY);
    readRoot(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY);
    readRoot(HKEY_CURRENT_USER, 0);
    if (withSizes)  // fehlt die Groessenangabe, den Programmordner messen (aber nie "C:\" oder "C:\Program Files" selbst)
        for (auto& e : out) {
            if (e.size || e.loc.empty() || !IsFixedLocalPath(e.loc)) continue;
            const fs::path rp = fs::path(e.loc).relative_path();
            if (std::distance(rp.begin(), rp.end()) >= 2) e.size = PathSize(e.loc);
        }
    return out;
}

// Datei, aus der das Symbol des Programms stammt: DisplayIcon, sonst das Deinstallationsprogramm
static std::wstring IconPathOf(const InstalledApp& a) {
    std::wstring s = a.iconSrc;
    if (!s.empty()) {
        if (s[0] == L'"') {
            const size_t e = s.find(L'"', 1);
            s = s.substr(1, e == std::wstring::npos ? std::wstring::npos : e - 1);
        } else if (const size_t c = s.rfind(L','); c != std::wstring::npos) {
            s = s.substr(0, c);
        }
        if (IsFixedLocalPath(s) && GetFileAttributesW(s.c_str()) != INVALID_FILE_ATTRIBUTES) return s;
    }
    return ResolveCommandPath(a.uninst);
}

constexpr UINT WM_UNINST_DONE = WM_APP + 16, WM_UNINSTALL = WM_APP + 17;  // WM_UNINSTALL: wParam = Index in g_unin

static void StartUninstScan(HWND h) {
    if (g_uninLoading.exchange(true)) return;
    g_unin.clear();
    g_autoVis.clear();
    Bg([h] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // fuer die Symbol-Ermittlung (SHGetFileInfo) in diesem Thread
        std::vector<AutoEntry> v;
        for (const auto& a : ReadInstalled(true)) {
            AutoEntry e;
            e.name = a.name;
            e.publisher = a.publisher;
            e.uninst = a.uninst;
            e.bytes = a.size;
            e.exePath = IconPathOf(a);
            e.cmd = a.loc.empty() ? a.uninst : a.loc;  // Tooltip: Installationsordner
            std::wstring sub;
            for (const std::wstring& part : {a.publisher, a.version.empty() ? std::wstring() : L"v" + a.version, a.date})
                if (!part.empty()) sub += (sub.empty() ? L"" : L"  ·  ") + part;
            e.sub = sub.empty() ? std::wstring(Tr(L"Unbekannt")) : sub;
            if (!e.exePath.empty()) e.icon = GetExeInfo(e.exePath).icon;  // im Hintergrund, nicht beim Anzeigen
            v.push_back(std::move(e));
        }
        {
            std::lock_guard<std::mutex> lk(g_statMx);
            g_uninTmp = std::move(v);
        }
        CoUninitialize();
        g_uninLoading = false;
        PostMessageW(h, WM_UNINST_DONE, 0, 0);
    });
}

static void SortUninst() {
    std::sort(g_unin.begin(), g_unin.end(), [](const AutoEntry& a, const AutoEntry& b) {
        if (g_unSortSize && a.bytes != b.bytes) return a.bytes > b.bytes;
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
}

// Startet das Deinstallationsprogramm eines Eintrags (mit Windows' eigener Rechteabfrage, falls es Administratorrechte braucht)
static bool RunUninstall(const std::wstring& cmd) {
    wchar_t buf[2048];
    const DWORD n = ExpandEnvironmentStringsW(cmd.c_str(), buf, 2048);
    std::wstring ex = (n && n <= 2048) ? std::wstring(buf) : cmd;
    const size_t s0 = ex.find_first_not_of(L" \t");
    if (s0 == std::wstring::npos) return false;
    ex = ex.substr(s0);
    std::wstring exe, args;
    if (ex[0] == L'"') {
        const size_t e = ex.find(L'"', 1);
        exe = ex.substr(1, e == std::wstring::npos ? std::wstring::npos : e - 1);
        args = e == std::wstring::npos ? L"" : ex.substr(e + 1);
    } else if (const size_t x = LowerW(ex).find(L".exe"); x != std::wstring::npos) {
        exe = ex.substr(0, x + 4);
        args = ex.substr(x + 4);
    } else {
        const size_t sp = ex.find(L' ');
        exe = ex.substr(0, sp);
        args = sp == std::wstring::npos ? L"" : ex.substr(sp);
    }
    if (_wcsicmp(exe.c_str(), L"msiexec.exe") == 0 || _wcsicmp(exe.c_str(), L"msiexec") == 0) {
        exe = SystemFile(L"msiexec.exe");  // immer das echte aus dem Systemordner
    } else if (!IsAbsolutePath(exe)) {
        wchar_t full[MAX_PATH];
        if (!SearchPathW(nullptr, exe.c_str(), L".exe", MAX_PATH, full, nullptr)) return false;
        exe = full;
    }
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    const std::wstring dir = fs::path(exe).parent_path().wstring();
    SHELLEXECUTEINFOW sei{sizeof sei};
    sei.fMask = SEE_MASK_NOASYNC;
    sei.lpFile = exe.c_str();
    sei.lpParameters = args.empty() ? nullptr : args.c_str();
    sei.lpDirectory = dir.empty() ? nullptr : dir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

static void LoadAutostart() {
    g_auto.clear();
    auto addRun = [&](HKEY root, REGSAM wow, const wchar_t* sub, bool all) {
        HKEY k;
        if (RegOpenKeyExW(root, kRunPath, 0, KEY_READ | wow, &k) != ERROR_SUCCESS) return;
        for (DWORD i = 0;; ++i) {
            wchar_t name[512];
            DWORD nl = 512, type = 0, dl = 0;
            LONG r = RegEnumValueW(k, i, name, &nl, nullptr, &type, nullptr, &dl);
            if (r == ERROR_NO_MORE_ITEMS) break;
            if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) continue;
            std::vector<wchar_t> buf(dl / 2 + 2, 0);
            DWORD sz = dl;
            if (RegQueryValueExW(k, name, nullptr, &type, (BYTE*)buf.data(), &sz) != ERROR_SUCCESS) continue;
            AutoEntry e;
            e.name = name;
            e.cmd = buf.data();
            if (IsOwnEntry(e.name) || IsOwnEntry(e.cmd)) continue;  // die eigene automatische Bereinigung nicht anbieten
            e.approvedSub = sub;
            e.approvedValue = name;
            e.allUsers = all;
            e.missing = CommandTargetMissing(e.cmd);
            FillExeInfo(e, ResolveCommandPath(e.cmd));
            e.orig = e.want = ReadApproved(e.approvedSub, e.approvedValue);
            g_auto.push_back(std::move(e));
        }
        RegCloseKey(k);
    };
    addRun(HKEY_CURRENT_USER, 0, L"Run", false);
    addRun(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, L"Run", true);
    addRun(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, L"Run32", true);
    for (bool all : {false, true}) {
        fs::path dir = KnownFolder(all ? FOLDERID_CommonStartup : FOLDERID_Startup);
        std::error_code ec;
        for (const auto& f : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
            std::wstring fn = f.path().filename().wstring();
            if (!f.is_regular_file(ec) || _wcsicmp(fn.c_str(), L"desktop.ini") == 0 || IsOwnEntry(fn)) continue;
            AutoEntry e;
            e.name = f.path().stem().wstring();
            e.cmd = fn;
            e.approvedSub = L"StartupFolder";
            e.approvedValue = fn;
            FillExeInfo(e, StartupTarget(f.path()));
            e.allUsers = all;
            e.orig = e.want = ReadApproved(e.approvedSub, e.approvedValue);
            g_auto.push_back(std::move(e));
        }
    }
    // Aktive zuerst (die startet Windows wirklich), innerhalb davon alphabetisch
    std::sort(g_auto.begin(), g_auto.end(), [](const AutoEntry& a, const AutoEntry& b) {
        if (a.orig != b.orig) return a.orig;
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
}

static std::wstring LowerW(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return s;
}

// Gemeinsam fuer die Kartenlisten (Autostart/Browser/Laufwerke, Groesste Dateien, Schutzliste):
// neue Scrollposition aus einer WM_VSCROLL-Meldung, und Bildlaufleiste samt Position an die Zeilenzahl anpassen
static int ScrollPosFrom(HWND h, WPARAM w, int step) {
    SCROLLINFO si{sizeof si, SIF_ALL};
    GetScrollInfo(h, SB_VERT, &si);
    int pos = si.nPos;
    switch (LOWORD(w)) {
    case SB_LINEUP: pos -= step / 2; break;
    case SB_LINEDOWN: pos += step / 2; break;
    case SB_PAGEUP: pos -= (int)si.nPage; break;
    case SB_PAGEDOWN: pos += (int)si.nPage; break;
    case SB_TOP: pos = 0; break;
    case SB_BOTTOM: pos = si.nMax; break;
    case SB_THUMBTRACK: case SB_THUMBPOSITION: pos = si.nTrackPos; break;
    }
    return pos;
}
static void SyncScroll(HWND wnd, int total, int& scroll) {
    RECT cr;
    GetClientRect(wnd, &cr);
    scroll = std::clamp(scroll, 0, std::max(0, total - (int)cr.bottom));
    SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, 0, std::max(0, total - 1), (UINT)cr.bottom, scroll};
    SetScrollInfo(wnd, SB_VERT, &si, TRUE);
    InvalidateRect(wnd, nullptr, FALSE);
}

static void UpdateAutoScroll() {
    static bool busy = false;  // SetScrollInfo kann die Breite aendern und damit WM_SIZE ausloesen
    if (busy || !g_alist) return;
    busy = true;
    RECT cr;
    GetClientRect(g_alist, &cr);
    const int total = D(RowTopL((int)g_autoVis.size()));
    g_aScroll = std::clamp(g_aScroll, 0, std::max(0, total - (int)cr.bottom));
    SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, 0, std::max(0, total - 1), (UINT)cr.bottom, g_aScroll};
    SetScrollInfo(g_alist, SB_VERT, &si, TRUE);
    if (g_ttList) {
        GetClientRect(g_alist, &cr);
        TOOLINFOW ti{sizeof ti};
        ti.hwnd = g_alist;
        ti.uId = 1;
        ti.rect = cr;
        SendMessageW(g_ttList, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
    }
    InvalidateRect(g_alist, nullptr, FALSE);
    busy = false;
}

static void FilterAutostart() {
    wchar_t q[128]{};
    if (g_search) GetWindowTextW(g_search, q, 128);
    const std::wstring ql = LowerW(q);
    g_autoVis.clear();
    for (int i = 0; i < (int)Cur().size(); ++i)
        if (ql.empty() || LowerW(Cur()[i].name).find(ql) != std::wstring::npos) g_autoVis.push_back(i);
    g_aHover = -1;
}

static void PopulateAutostart() {
    LoadAutostart();
    g_aScroll = 0;
    FilterAutostart();
    UpdateAutoScroll();
}

// Eigene Liste: pro Programm eine Karte mit grossem Namen links und einem Schalter (An/Aus) rechts
static int AutoRowAt(int y) {
    if (y < 0) return -1;
    const double yl = (y + g_aScroll) / g_ui;
    int top = 0;
    for (int i = 0; i < (int)g_autoVis.size(); ++i) {
        const int h = RowHOf(i);
        if (yl < top + h) return i;
        top += h + kAGap;
        if (yl < top) return -1;  // Luecke zwischen den Karten
    }
    return -1;
}
// Freien Speicher eines Laufwerks mit Windows' eigenem Programm cipher /w ueberschreiben (drei Durchgaenge: 0x00, 0xFF, Zufall).
// Es laeuft in einem eigenen Konsolenfenster mit Fortschritt; das Fenster zu schliessen bricht ab. Dateien auf dem Laufwerk bleiben unberuehrt.
static bool StartFreeSpaceWipe(HWND owner, wchar_t letter) {
    wchar_t up = (wchar_t)towupper(letter);
    std::wstring q = std::wstring(Tr(L"Der freie Speicher von Laufwerk ")) + up + Tr(L": wird dreimal überschrieben (0x00, 0xFF, Zufall), damit gelöschte Dateien nicht wiederherstellbar sind. Deine Dateien bleiben unberührt.\n@NDas kann je nach Größe Stunden dauern, belastet das Laufwerk und füllt es während des Laufs vorübergehend. Ein Konsolenfenster zeigt den Fortschritt; schließt du es, bricht der Vorgang ab.\n@NBei SSDs bringt das wenig (der Speicher verschiebt Daten intern) und verbraucht Schreibzyklen; dort hilft Verschlüsselung (BitLocker) mehr.\n@NStarten?");
    if (AppBox(owner, q.c_str(), Tr(L"Freien Speicher überschreiben"), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return false;
    wchar_t sys[MAX_PATH]{};
    GetSystemDirectoryW(sys, MAX_PATH);
    const std::wstring cmd = L"cmd.exe /c title Tidy - Freien Speicher ueberschreiben & \"" + std::wstring(sys) + L"\\cipher.exe\" /w:" + up + L":\\ & echo. & echo Fertig. & pause";
    std::wstring line = cmd;
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi)) {
        AppBox(owner, Tr(L"Windows konnte das Überschreiben nicht starten."), kApp, MB_OK | MB_ICONWARNING);
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

static LRESULT CALLBACK AutoListProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    const int step = D(RowH() + kAGap);
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        UpdateAutoScroll();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC wdc = BeginPaint(h, &ps);
        RECT cr;
        GetClientRect(h, &cr);
        HDC dc = CreateCompatibleDC(wdc);
        HBITMAP bm = CreateCompatibleBitmap(wdc, cr.right, cr.bottom);
        HGDIOBJ ob = SelectObject(dc, bm);
        FillRect(dc, &cr, g_bgBrush);
        SetBkMode(dc, TRANSPARENT);
        SetLogical(dc);
        const int W = (int)std::ceil(cr.right / g_ui);
        const double scrollL = g_aScroll / g_ui;
        const int n = (int)g_autoVis.size();
        if (n == 0) {
            SetTextAlign(dc, TA_CENTER);
            DrawTxt(dc, g_fMed, kMuted, W / 2, 40, g_page == kDrivePage ? (g_drvLoading ? Tr(L"Analysiere Laufwerke …") : Tr(L"Keine Laufwerke gefunden")) : g_page == kBrowserPage ? Tr(L"Analysiere Browser …") : g_page == kUninstPage ? (g_uninLoading ? Tr(L"Analysiere Programme …") : g_unin.empty() ? Tr(L"Keine Programme gefunden") : Tr(L"Keine Treffer")) : g_auto.empty() ? Tr(L"Keine Autostart-Programme gefunden") : Tr(L"Keine Treffer"));
            SetTextAlign(dc, TA_LEFT);
        }
        int yAcc = 0;  // Oberkante der Karte i in logischen Einheiten, fortlaufend summiert
        for (int i = 0; i < n; ++i) {
            const int rowH = RowHOf(i);
            const int top = (int)std::lround(yAcc - scrollL);
            yAcc += rowH + kAGap;
            if (top + rowH < 0) continue;
            if (top * g_ui > cr.bottom) break;
            const AutoEntry& e = Cur()[g_autoVis[i]];
            const bool hot = g_aHover == i;
            RECT card{0, top, W - 2, top + RowHOf(i)};
            FillRound(dc, card, hot ? kCardHot : kCard, 18);
            if (e.drive) {  // Laufwerks-Karte: Name, Belegung als Balken, Gesundheitszeile
                const int lx = 22, rx = card.right - 22;
                const double used = e.totalB ? (double)e.bytes / (double)e.totalB : 0.0;
                wchar_t pc[32];
                swprintf_s(pc, Tr(L"%d %% belegt"), (int)std::lround(used * 100));
                RECT pr{rx - 200, top + 12, rx, top + 34};
                SelectObject(dc, g_fSm);
                SetTextColor(dc, kSub);
                DrawTextW(dc, pc, -1, &pr, DT_RIGHT | DT_SINGLELINE);
                DrawFit(dc, g_fMed, kStrong, lx, top + 10, rx - 210, e.name);
                RECT track{lx, top + 42, rx, top + 50};
                FillRound(dc, track, kTrack, 8);
                RECT fill{track.left, track.top, track.left + std::max(8, (int)((track.right - track.left) * used)), track.bottom};
                FillRound(dc, fill, used > 0.9 ? RGB(248, 113, 113) : used > 0.75 ? RGB(251, 191, 36) : RGB(79, 140, 255), 8);
                DrawFit(dc, g_fSm, kSub, lx, top + 56, rx, FormatSize(e.freeB) + Tr(L" frei von ") + FormatSize(e.totalB) + L"  ·  " + e.sub);
                const COLORREF hc = e.level == 1 ? kGood : e.level == 2 ? kWarn : e.level == 3 ? kBad : kMuted;
                DrawFit(dc, g_fSm, hc, lx, top + 78, rx - 440, L"●  " + e.health);
                RECT hr2{rx - 430, top + 78, rx - 215, top + 100};
                SelectObject(dc, g_fSm);
                SetTextColor(dc, hot ? kText : kMuted);
                DrawTextW(dc, Tr(L"Klick: größte Dateien"), -1, &hr2, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
                RECT wp{rx - 205, top + 72, rx, top + 98};   // Knopf: freien Speicher ueberschreiben (Klick siehe WM_LBUTTONDOWN)
                FillRound(dc, wp, hot ? kSwitchOffHot : kSwitchOff, 26);
                SetTextColor(dc, kText);
                DrawTextW(dc, Tr(L"Leerraum überschreiben"), -1, &wp, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                continue;
            }
            if (g_page == kUninstPage) {  // Programm: Symbol, Name, Herausgeber/Version/Datum, Groesse und Deinstallieren-Knopf
                const int ur = card.right - 20;
                RECT ub{ur - 150, top + (RowH() - 34) / 2, ur, top + (RowH() - 34) / 2 + 34};
                FillRound(dc, ub, Tint(kBad, kCard, (hot && g_aHotToggle) ? 1.0 : 0.78), 34);
                SelectObject(dc, g_fSm);
                SetTextColor(dc, RGB(255, 255, 255));
                DrawTextW(dc, Tr(L"Deinstallieren"), -1, &ub, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                RECT zr{ub.left - 12 - 100, top, ub.left - 12, top + RowH()};
                SelectObject(dc, g_fMed);
                SetTextColor(dc, kSub);
                DrawTextW(dc, e.bytes ? FormatSize(e.bytes).c_str() : L"–", -1, &zr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                ResetLogical(dc);
                if (e.icon) DrawIconEx(dc, D(22), D(top + (RowH() - 32) / 2), e.icon, D(32), D(32), 0, nullptr, DI_NORMAL);
                SetLogical(dc);
                if (!e.icon) {
                    RECT gr{22, top + (RowH() - 32) / 2, 54, top + (RowH() - 32) / 2 + 32};
                    SelectObject(dc, g_fIcon);
                    SetTextColor(dc, kMuted);
                    DrawTextW(dc, L"\xE71D", -1, &gr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                DrawFit(dc, g_fMed, kStrong, 66, top + 12, zr.left - 8, e.name);
                DrawFit(dc, g_fSm, kMuted, 66, top + 38, zr.left - 8, e.sub);
                continue;
            }
            const int right = card.right - 20;
            RECT pill{right - 48, top + (RowH() - 28) / 2, right, top + (RowH() - 28) / 2 + 28};
            const bool toggleHot = hot && g_aHotToggle;
            FillRound(dc, pill, e.want ? (toggleHot ? RGB(52, 211, 110) : RGB(34, 197, 94)) : (toggleHot ? kSwitchOffHot : kSwitchOff), 28);
            RECT knob{e.want ? pill.right - 4 - 20 : pill.left + 4, pill.top + 4, e.want ? pill.right - 4 : pill.left + 4 + 20, pill.top + 24};
            FillRound(dc, knob, RGB(255, 255, 255), 20);
            RECT lab{pill.left - 72, top, pill.left - 14, top + RowH()};
            SelectObject(dc, g_fSm);
            SetTextColor(dc, e.want ? kGood : kMuted);
            DrawTextW(dc, e.want ? (e.sub.empty() ? Tr(L"An") : Tr(L"Leeren")) : Tr(L"Aus"), -1, &lab, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            const bool autoPage = g_page == kAutoPage;
            int textRight = lab.left - 8;
            if (autoPage && e.want && !e.missing && e.impact) {  // Startauswirkung als kleines Etikett links vom Schalter
                static const wchar_t* kImp[] = {L"", L"Niedrig", L"Mittel", L"Hoch"};
                const COLORREF ic = e.impact == 1 ? kGood : e.impact == 2 ? kWarn : kBad;
                RECT chip{lab.left - 12 - 78, top + (RowH() - 24) / 2, lab.left - 12, top + (RowH() - 24) / 2 + 24};
                FillRound(dc, chip, Tint(ic, kCard, 0.18), 24);
                SelectObject(dc, g_fSm);
                SetTextColor(dc, ic);
                DrawTextW(dc, Tr(kImp[e.impact]), -1, &chip, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                textRight = chip.left - 8;
            }
            const int tx = (e.glyph || autoPage) ? (e.glyph ? 78 : 66) : 22, dy = (RowH() - kARowBase) / 2;  // Gruppen-Kacheln: Symbol links; Autostart: Programm-Icon links
            if (autoPage) {
                ResetLogical(dc);
                if (e.icon) DrawIconEx(dc, D(22), D(top + (RowH() - 32) / 2), e.icon, D(32), D(32), 0, nullptr, DI_NORMAL);
                SetLogical(dc);
                if (!e.icon) {  // kein Symbol gefunden: neutrales Platzhalter-Zeichen
                    RECT gr{22, top + (RowH() - 32) / 2, 54, top + (RowH() - 32) / 2 + 32};
                    SelectObject(dc, g_fIcon);
                    SetTextColor(dc, kMuted);
                    DrawTextW(dc, L"\xE7B8", -1, &gr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
            }
            if (e.glyph) {
                RECT bd{18, top + (RowH() - 44) / 2, 18 + 44, top + (RowH() - 44) / 2 + 44};
                FillRound(dc, bd, e.want ? e.col : Tint(e.col, kCard, 0.3), 44);
                SelectObject(dc, g_fIcon);
                SetTextColor(dc, RGB(255, 255, 255));
                DrawTextW(dc, e.glyph, -1, &bd, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            DrawFit(dc, g_fMed, e.want ? kStrong : kMuted, tx, top + 10 + dy, textRight, e.name);
            std::wstring sub = !e.sub.empty() ? e.sub : e.approvedSub == L"StartupFolder" ? Tr(L"Autostart-Ordner") : e.allUsers ? Tr(L"Für alle Benutzer") : Tr(L"Nur für dich");
            const bool changed = e.sub.empty() && e.want != e.orig;
            if (changed) sub += Tr(L"  ·  nicht gespeichert");
            if (e.missing) sub = Tr(L"Programm nicht gefunden  ·  ") + sub;  // Eintrag zeigt auf eine Datei, die es nicht mehr gibt
            DrawFit(dc, g_fSm, e.missing ? kBad : (changed || e.warn) ? kWarn : kMuted, tx, top + 34 + dy, textRight, sub);
            if (autoPage && e.open) {  // Details: Pfad, Herausgeber und die Einschaetzung der Startauswirkung
                RECT sep{tx, top + RowH() - 2, card.right - 20, top + RowH() - 1};
                FillRound(dc, sep, kTrack, 1);
                static const wchar_t* kImpName[] = {L"", L"Niedrig", L"Mittel", L"Hoch"};
                std::wstring l2 = Tr(L"Herausgeber:  ") + (e.publisher.empty() ? std::wstring(Tr(L"Unbekannt")) : e.publisher);
                if (e.impact) l2 += L"   ·   " + std::wstring(Tr(L"Startauswirkung: ")) + Tr(kImpName[e.impact]) + Tr(L" (Schätzung nach Programmgröße)");
                DrawFit(dc, g_fSm, kSub, tx, top + RowH() + 2, card.right - 20, Tr(L"Pfad:  ") + (e.exePath.empty() ? e.cmd : e.exePath));
                DrawFit(dc, g_fSm, kMuted, tx, top + RowH() + 24, card.right - 20, l2);
            }
        }
        ResetLogical(dc);
        BitBlt(wdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, ob);
        DeleteObject(bm);
        DeleteDC(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_VSCROLL:
        g_aScroll = ScrollPosFrom(h, w, step);
        UpdateAutoScroll();
        return 0;
    case WM_MOUSEWHEEL:
        g_aScroll -= GET_WHEEL_DELTA_WPARAM(w) * step / 120;
        UpdateAutoScroll();
        return 0;
    case WM_MOUSEMOVE: {
        RECT cr;
        GetClientRect(h, &cr);
        const int x = (short)LOWORD(l), y = (short)HIWORD(l);
        const int row = AutoRowAt(y);
        const bool tg = row >= 0 && g_page != kDrivePage && x >= cr.right - D(g_page == kUninstPage ? 176 : 140) && (y + g_aScroll - D(RowTopL(row))) < D(RowH());  // nicht im aufgeklappten Detailbereich
        if (row != g_aHover || tg != g_aHotToggle) {
            if (row != g_aHover && g_ttList) SendMessageW(g_ttList, TTM_POP, 0, 0);  // Tooltip fuer die neue Zeile neu aufbauen
            g_aHover = row;
            g_aHotToggle = tg;
            InvalidateRect(h, nullptr, FALSE);
        }
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE:
        g_aHover = -1;
        g_aHotToggle = false;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR:
        if ((g_aHotToggle || ((g_page == kDrivePage || g_page == kAutoPage) && g_aHover >= 0)) && LOWORD(l) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649))); return TRUE; }
        break;
    case WM_LBUTTONDOWN:
        if (g_page == kDrivePage && g_aHover >= 0 && g_aHover < (int)g_autoVis.size()) {   // Knopf "Leerraum ueberschreiben" rechts unten auf der Karte
            RECT cr2;
            GetClientRect(h, &cr2);
            const int px = (short)LOWORD(l), py = (short)HIWORD(l) + g_aScroll - D(RowTopL(g_aHover));
            if (px >= cr2.right - D(24 + 205) && px <= cr2.right - D(24) && py >= D(72) && py <= D(98)) {
                const AutoEntry& we = Cur()[g_autoVis[g_aHover]];
                if (!we.name.empty()) StartFreeSpaceWipe(GetParent(h), we.name[0]);
                return 0;
            }
        }
        if (g_page == kDrivePage && g_aHover >= 0 && g_aHover < (int)g_autoVis.size() && !g_topBusy) {  // Laufwerks-Karte: groesste Dateien suchen
            const AutoEntry& de = Cur()[g_autoVis[g_aHover]];
            if (!de.name.empty()) PostMessageW(GetParent(h), WM_TOP_FILES, (WPARAM)de.name[0], 0);
            return 0;
        }
        if (g_page == kUninstPage) {  // Programme: nur der Deinstallieren-Knopf reagiert
            if (g_aHover >= 0 && g_aHotToggle && g_aHover < (int)g_autoVis.size()) PostMessageW(GetParent(h), WM_UNINSTALL, (WPARAM)g_autoVis[g_aHover], 0);
            return 0;
        }
        if (g_aHover >= 0 && g_aHotToggle && g_aHover < (int)g_autoVis.size()) {
            AutoEntry& e = Cur()[g_autoVis[g_aHover]];
            if (e.avail) e.want = !e.want;
            InvalidateRect(h, nullptr, FALSE);
            InvalidateRect(GetParent(h), nullptr, FALSE);
        } else if (g_page == kAutoPage && g_aHover >= 0 && g_aHover < (int)g_autoVis.size()) {  // Klick auf die Karte: Details ein- oder ausblenden
            AutoEntry& e = Cur()[g_autoVis[g_aHover]];
            e.open = !e.open;
            UpdateAutoScroll();
        }
        return 0;
    case WM_NOTIFY: {
        auto* hdr = (NMHDR*)l;
        if (hdr->code == TTN_GETDISPINFOW) {
            static std::wstring tip;
            auto* di = (NMTTDISPINFOW*)l;
            tip.clear();
            if (g_aHover >= 0 && g_aHover < (int)g_autoVis.size()) tip = Cur()[g_autoVis[g_aHover]].cmd;
            di->lpszText = tip.data();
            return 0;
        }
        break;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

// Legt die Haken der Liste als Schalter ab; Rueckgabe: Anzahl geaenderter Eintraege (failed = nicht gelungen)
static int ApplyAutostart(int& failed) {
    int changed = 0;
    failed = 0;
    for (int i = 0; i < (int)g_auto.size(); ++i) {
        bool now = g_auto[i].want;
        if (now == g_auto[i].orig) continue;
        if (WriteApproved(g_auto[i].approvedSub, g_auto[i].approvedValue, now)) { g_auto[i].orig = now; ++changed; }
        else ++failed;
    }
    return changed;
}

// Fuellt die Liste mit dem aktuellen Inhalt der gewaehlten Kategorie (alles angehakt)
// ---- Gruppen ("Ordner") in der Liste von Temp und Systemdateien ----
// Statt hunderter kryptischer Namen sieht man ein paar verstaendliche Gruppen, die sich per Klick aufklappen lassen.
// Es werden keine Dateien verschoben; die Gruppen gibt es nur in der Anzeige.
static std::vector<int> g_itemGroup;  // Gruppe pro Eintrag in g_items (parallel), leer = keine Gruppen

static bool LooksRandom(const std::wstring& n) {  // GUID oder lange Hex-Zeichenfolge
    int hex = 0;
    for (wchar_t c : n) {
        if (iswxdigit(c)) ++hex;
        else if (c != L'-' && c != L'{' && c != L'}' && c != L'_' && c != L'.') return false;
    }
    return hex >= 8;
}

static int GroupOf(int cat, const fs::path& p) {
    const std::wstring n = p.filename().wstring();
    if (cat == CAT_TEMP) {
        std::error_code ec;
        if (fs::is_directory(p, ec)) return LooksRandom(n) ? 1 : 0;
        std::wstring e = p.extension().wstring();
        for (auto& c : e) c = (wchar_t)towlower(c);
        if (e == L".tmp") return 2;
        if (e == L".log" || e == L".txt" || e == L".etl" || e == L".dmp") return 3;
        if (e == L".msi" || e == L".msp" || e == L".msu" || e == L".exe" || e == L".cab") return 4;
        return 5;
    }
    if (cat == CAT_SYS) {
        if (_wcsnicmp(n.c_str(), L"thumbcache_", 11) == 0) return 0;
        if (_wcsnicmp(n.c_str(), L"iconcache_", 10) == 0 || _wcsicmp(n.c_str(), L"IconCache.db") == 0) return 1;
        if (const int dg = DeepGroupOf(p); dg >= 0) return dg;
        if (_wcsicmp(p.parent_path().filename().wstring().c_str(), L"CrashDumps") == 0) return 2;
        if (StrStrIW(p.c_str(), L"\\WER\\")) return 3;
        return 4;
    }
    if (cat == CAT_DL) {  // Downloads: nach Dateityp
        std::error_code ec;
        if (fs::is_directory(p, ec)) return 6;
        std::wstring e = p.extension().wstring();
        for (auto& c : e) c = (wchar_t)towlower(c);
        auto in = [&](std::initializer_list<const wchar_t*> l) { for (const wchar_t* x : l) if (e == x) return true; return false; };
        if (in({L".pdf", L".doc", L".docx", L".xls", L".xlsx", L".ppt", L".pptx", L".txt", L".rtf", L".odt", L".ods", L".csv", L".md"})) return 0;
        if (in({L".jpg", L".jpeg", L".png", L".gif", L".bmp", L".webp", L".svg", L".heic", L".tif", L".tiff", L".ico"})) return 1;
        if (in({L".mp4", L".mkv", L".avi", L".mov", L".wmv", L".webm", L".flv", L".m4v"})) return 2;
        if (in({L".mp3", L".wav", L".flac", L".aac", L".ogg", L".m4a", L".wma"})) return 3;
        if (in({L".zip", L".rar", L".7z", L".tar", L".gz", L".iso", L".img"})) return 4;
        if (in({L".exe", L".msi", L".msix", L".bat", L".cmd", L".ps1", L".apk", L".jar"})) return 5;
        return 7;
    }
    return -1;
}

static const wchar_t* GroupName(int cat, int g) {
    static const wchar_t* t[] = {L"Programm-Ordner", L"Ordner mit Zufallsnamen", L"Zwischendateien (.tmp)", L"Protokolle und Berichte",
                                 L"Installationsdateien", L"Sonstige Dateien"};
    static const wchar_t* s[] = {L"Vorschaubilder", L"Symbol-Cache", L"Absturzabbilder", L"Windows-Fehlerberichte", L"Sonstiges",
                                 L"Shader-Caches (DirectX, NVIDIA, AMD)", L"Windows-Update-Reste", L"Treiber-Installer-Reste", L"Setup-Reste (Windows-Temp)",
                                 L"App-Caches (Discord, Slack, Steam, VS Code …)", L"Microsoft-Store-Reste", L"Diagnose- und ETL-Protokolle",
                                 L"Entwickler-Caches (npm, pip, NuGet, Yarn)"};
    static const wchar_t* d[] = {L"Dokumente", L"Bilder", L"Videos", L"Musik", L"Archive", L"Programme und Installer", L"Ordner", L"Sonstiges", L"Doppelte Dateien"};
    return Tr(cat == CAT_TEMP ? t[g] : cat == CAT_SYS ? s[g] : d[g]);
}

static const wchar_t* GroupNote(int cat, int g) {
    static const wchar_t* t[] = {L"Zwischenordner einzelner Programme mit lesbaren Namen",
                                 L"Reste von Installationen und Updates (Namen wie {3F2A19C4-…})",
                                 L"Kurzlebige Arbeitsdateien von Programmen",
                                 L"Logdateien und Textprotokolle",
                                 L"Entpackte Installer und Update-Pakete",
                                 L"Alles andere, was sich nicht eindeutig zuordnen lässt"};
    static const wchar_t* s[] = {L"Zwischenspeicher für Vorschaubilder im Explorer, wird neu aufgebaut",
                                 L"Zwischenspeicher für Symbole im Explorer, wird neu aufgebaut",
                                 L"Speicherabbilder abgestürzter Programme",
                                 L"Von Windows gesammelte Fehlerberichte (Benutzer und System)",
                                 L"Weitere Dateien",
                                 L"Zwischengespeicherte Shader der Grafiktreiber, werden beim nächsten Programm- oder Spielstart neu aufgebaut",
                                 L"Heruntergeladene Update-Pakete, Übermittlungsoptimierung und Update-Protokolle. Gesperrtes bleibt (ohne Administratorrechte)",
                                 L"Entpackte Grafiktreiber-Installer in C:\\NVIDIA und C:\\AMD – der installierte Treiber bleibt unberührt",
                                 L"Temporäre Setup-Dateien des Systems. Gesperrtes bleibt (ohne Administratorrechte)",
                                 L"Zwischenspeicher von Discord, Slack, Steam, VS Code, Teams, Spotify und weiteren Programmen – Anmeldung und Einstellungen bleiben, die Programme bauen den Cache neu auf",
                                 L"Zwischengespeicherte und vorübergehende Downloads des Microsoft Store",
                                 L"Ablaufverfolgungs-Protokolle (.etl) und Wartungsprotokolle (CBS, DISM) von Windows. Von Windows benutzte Dateien bleiben (ohne Administratorrechte)",
                                 L"Heruntergeladene Paket-Zwischenspeicher von npm, pip, NuGet und Yarn – werden bei Bedarf neu geladen, das nächste Installieren dauert dann etwas länger"};
    static const wchar_t* d[] = {L"PDF, Word, Excel, PowerPoint, Text …", L"JPG, PNG, GIF, WebP …", L"MP4, MKV, AVI, MOV …", L"MP3, WAV, FLAC, M4A …",
                                 L"ZIP, RAR, 7z, ISO …", L"Installer, EXE, MSI, Skripte …", L"Ganze Ordner im Downloads-Ordner",
                                 L"Alles andere, was sich nicht eindeutig zuordnen lässt",
                                 L"Inhaltlich gleiche Dateien – die älteste Kopie bleibt, die übrigen kommen in den Papierkorb"};
    return Tr(cat == CAT_TEMP ? t[g] : cat == CAT_SYS ? s[g] : d[g]);
}

// Gruppen-Kacheln (Downloads, Temp, Systemdateien): pro Gruppe eine Karte mit Symbol, Anzahl, Groesse, Schalter und (per Maus darueber) Beispielen.
// keepWant: Schalterstellungen behalten (nach einem Scan), sonst alles an.
static void BuildGroupCards(int cat, bool keepWant) {
    static const wchar_t* kTempGlyph[] = {L"\xE8B7", L"\xE7B8", L"\xE823", L"\xE8A5", L"\xE896", L"\xE712"};  // Ordner, Paket, Uhr, Dokument, Download, mehr
    static const wchar_t* kSysGlyph[] = {L"\xE91B", L"\xE734", L"\xE7BA", L"\xE783", L"\xE712", L"\xE7F4", L"\xE895", L"\xE7B8", L"\xE74C", L"\xE71D", L"\xE719", L"\xE9D9", L"\xE943"};  // Bild, Stern, Warnung, Fehler, mehr, Monitor, Sync, Paket, OEM, Apps, Store, Diagnose
    static const wchar_t* kDlGlyph[] = {L"\xE8A5", L"\xE91B", L"\xE714", L"\xE8D6", L"\xE7B8", L"\xE756", L"\xE8B7", L"\xE712", L"\xE8C8"};  // Dokument, Bild, Video, Musik, Paket, Konsole, Ordner, mehr, Kopie
    int count[kMaxGroups]{};
    unsigned long long bytes[kMaxGroups]{};
    bool known[kMaxGroups];
    std::fill(std::begin(known), std::end(known), true);
    std::vector<std::wstring> names[kMaxGroups];
    {
        std::lock_guard<std::mutex> lk(g_statMx);
        for (size_t i = 0; i < g_items.size() && i < g_itemGroup.size(); ++i) {
            const int g = g_itemGroup[i];
            if (g < 0 || g >= kMaxGroups) continue;
            ++count[g];
            auto it = g_sizeCache.find(g_items[i].wstring());
            if (it == g_sizeCache.end()) known[g] = false;
            else bytes[g] += it->second;
            if (names[g].size() < 8) names[g].push_back(ItemLabel(cat, g_items[i]));
        }
    }
    std::vector<AutoEntry> old = std::move(g_grp);
    g_grp.clear();
    for (int g = 0; g < kMaxGroups; ++g) {
        if (!count[g]) continue;
        AutoEntry e;
        e.name = GroupName(cat, g);
        e.sub = std::to_wstring(count[g]) + (count[g] == 1 ? Tr(L" Eintrag") : Tr(L" Einträge"));
        if (known[g]) e.sub += L"  ·  " + FormatSize(bytes[g]);
        e.sub += L"  ·  " + std::wstring(GroupNote(cat, g));
        e.level = g;
        e.glyph = (cat == CAT_TEMP ? kTempGlyph : cat == CAT_SYS ? kSysGlyph : kDlGlyph)[g];
        e.col = kCatColor[g % 6];
        e.orig = e.want = true;
        if (keepWant) for (const auto& o : old) if (o.level == g) e.want = o.want;
        for (const auto& n : names[g]) e.cmd += (e.cmd.empty() ? L"" : L"\n") + n;
        if (count[g] > 8) e.cmd += Tr(L"\n… und ") + std::to_wstring(count[g] - 8) + Tr(L" weitere");
        g_grp.push_back(std::move(e));
    }
}
// Dateiseiten (Downloads, Temp, Systemdateien): Auflisten, Gruppieren und Sortieren laeuft im Hintergrund, damit das Fenster auch bei sehr vielen
// Eintraegen bedienbar bleibt. Das Ergebnis kommt per WM_LIST_DONE; veraltete Ergebnisse (andere Seite, neuer Scan) werden verworfen.
constexpr UINT WM_LIST_DONE = WM_APP + 18;
struct ListResult {
    int cat = -1, gen = 0;
    std::vector<fs::path> items;
    std::vector<int> groups;
};
static ListResult g_listTmp;
static std::atomic<int> g_listGen{0};
static std::atomic<bool> g_listLoading{false};

static void StartListScan(HWND h, int cat) {
    const int gen = ++g_listGen;
    g_listLoading = true;
    Bg([h, cat, gen] {
        struct Row { fs::path p; std::wstring label; int group; };
        std::vector<Row> rows;
        for (auto& p : ListItems(cat, cat == CAT_SYS && DeepOn())) {
            std::wstring label = ItemLabel(cat, p);
            const int group = GroupOf(cat, p);
            rows.push_back({std::move(p), std::move(label), group});
        }
        if (cat == CAT_DL && GetFlag(L"DupeScan", true)) {  // inhaltlich gleiche Dateien (nur Dateien, keine Ordner/Verknuepfungen) in die Gruppe "Doppelte Dateien"
            std::vector<dupes::File> fl;
            std::vector<size_t> idx;
            for (size_t i = 0; i < rows.size(); ++i) {
                WIN32_FILE_ATTRIBUTE_DATA a;
                if (!GetFileAttributesExW(rows[i].p.c_str(), GetFileExInfoStandard, &a)) continue;
                if (a.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
                dupes::File f;
                f.path = rows[i].p.wstring();
                f.size = (static_cast<unsigned long long>(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
                f.mtime = (static_cast<unsigned long long>(a.ftLastWriteTime.dwHighDateTime) << 32) | a.ftLastWriteTime.dwLowDateTime;
                fl.push_back(std::move(f));
                idx.push_back(i);
            }
            for (size_t k : dupes::FindRedundant(fl, [](const std::wstring& path) { return dupes::HashFile(path); }, [gen] { return g_listGen.load() != gen; }))
                rows[idx[k]].group = 8;
        }
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {  // erst nach Gruppe, dann alphabetisch
            if (a.group != b.group) return a.group < b.group;
            return _wcsicmp(a.label.c_str(), b.label.c_str()) < 0;
        });
        ListResult r;
        r.cat = cat;
        r.gen = gen;
        for (auto& row : rows) {
            r.items.push_back(std::move(row.p));
            r.groups.push_back(row.group);
        }
        {
            std::lock_guard<std::mutex> lk(g_statMx);
            g_listTmp = std::move(r);
        }
        PostMessageW(h, WM_LIST_DONE, 0, 0);
    });
}

static void PopulateList() {
    g_items.clear();
    g_itemGroup.clear();
    g_grp.clear();
    g_listLoading = false;
    ++g_listGen;  // laufende Auflistungen anderer Seiten verwerfen
    if (g_page == kAutoPage) { PopulateAutostart(); return; }
    if (g_page == kBrowserPage) { g_aScroll = 0; StartBrowserScan(g_main); UpdateAutoScroll(); return; }
    if (g_page == kUninstPage) { g_aScroll = 0; StartUninstScan(g_main); UpdateAutoScroll(); return; }
    if (g_page == kDrivePage) { g_aScroll = 0; StartDriveScan(g_main); UpdateAutoScroll(); return; }
    const int cat = PageCat(g_page);
    if (cat < 0) return;
    g_aScroll = 0;
    FilterAutostart();  // alte Kacheln weg
    UpdateAutoScroll();
    ShowWindow(g_alist, SW_HIDE);  // waehrend der Analyse zeigt das Fenster "Analyse laeuft ..."
    StartListScan(g_main, cat);
    InvalidateRect(g_main, nullptr, FALSE);
}
static void ScanAllAsync(HWND h) {
    if (g_scanning.exchange(true)) return;
    Bg([h] {
        CatStat local[CAT_N];
        std::unordered_map<std::wstring, unsigned long long> sizes;
        for (int c = 0; c < CAT_N; ++c) local[c] = ScanCategory(c, &sizes);
        {   // Laufwerke fuer die Uebersicht: Anzahl und freier Speicher
            unsigned long long fr = 0, tt = 0;
            int n = 0;
            const DWORD mask = GetLogicalDrives();
            for (int i = 2; i < 26; ++i) {
                if (!(mask & (1u << i))) continue;
                wchar_t root[4] = {(wchar_t)(L'A' + i), L':', L'\\', 0};
                const UINT ty = GetDriveTypeW(root);
                if (ty != DRIVE_FIXED && ty != DRIVE_REMOVABLE) continue;
                ULARGE_INTEGER a{}, b{}, c{};
                if (GetDiskFreeSpaceExW(root, &a, &b, &c) && b.QuadPart && !IsHiddenDrive(root, b.QuadPart)) { ++n; fr += a.QuadPart; tt += b.QuadPart; }
            }
            g_ovDrives = n;
            g_ovFree = fr;
            g_ovTotal = tt;
            g_ovProgs = (int)ReadInstalled(false).size();
        }
        {
            std::lock_guard<std::mutex> lk(g_statMx);
            for (int c = 0; c < CAT_N; ++c) g_stat[c] = local[c];
            g_sizeCache = std::move(sizes);
            g_scanned = true;
        }
        g_scanning = false;
        PostMessageW(h, WM_SCAN_DONE, 0, 0);
    });
}

static CatStat Stat(int c) {
    std::lock_guard<std::mutex> lk(g_statMx);
    return g_stat[c];
}

// Standard <-> Ultra-Tiefenreinigung: Schalter merken, Uebersicht und (falls offen) die Systemdateien-Liste neu vermessen
static void SetDeep(HWND h, bool on) {
    SetFlag(L"DeepClean", on);
    if (!on) SetFlag(L"UltraClean", false);   // Ultra baut auf der Ultra-Tiefenreinigung auf
    g_scanned = false;
    ScanAllAsync(h);
    if (g_page == 3) PopulateList();
    InvalidateRect(h, nullptr, FALSE);
}

// Umschalter in der Kopfzeile: 0 = Standard, 1 = Ultra
static void SetMode(HWND h, int mode) {
    SetFlag(L"DeepClean", mode >= 1);
    g_scanned = false;
    ScanAllAsync(h);
    if (g_page == 3) PopulateList();
    InvalidateRect(h, nullptr, FALSE);
}

static void DrawButton(const DRAWITEMSTRUCT* d) {
    int id = (int)d->CtlID;
    bool down = d->itemState & ODS_SELECTED, hot = g_hover == id;
    bool nav = (id >= ID_NAV0 && id < ID_NAV0 + kPages) || id == ID_MORE;
    wchar_t text[64];
    GetWindowTextW(d->hwndItem, text, 64);
    // Erst im Speicher zeichnen und dann in einem Rutsch auf den Bildschirm: kein Flackern beim Hovern
    const int bw = d->rcItem.right - d->rcItem.left, bh = d->rcItem.bottom - d->rcItem.top;
    HDC screenDc = d->hDC;
    HDC dc = CreateCompatibleDC(screenDc);
    HBITMAP bmp = CreateCompatibleBitmap(screenDc, bw, bh);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    RECT rc{0, 0, (LONG)std::ceil(bw / g_ui), (LONG)std::ceil(bh / g_ui)};
    SetBkMode(dc, TRANSPARENT);
    SetLogical(dc);
    if (nav) {
        bool sel = id != ID_MORE && g_page == id - ID_NAV0;
        FillRect(dc, &rc, g_sideBrush);
        if (sel || hot) FillRound(dc, rc, sel ? kNavSel : kNavHot, 14);
        if (sel) {
            RECT bar{0, 11, 4, rc.bottom - 11};
            FillRound(dc, bar, RGB(79, 140, 255), 4);
        }
        COLORREF col = sel ? kStrong : hot ? kText : kMuted;
        RECT rg{18, 0, 50, rc.bottom}, rt{54, 0, rc.right, rc.bottom};
        SelectObject(dc, g_fIcon);
        SetTextColor(dc, sel ? RGB(79, 140, 255) : col);
        DrawTextW(dc, id == ID_MORE ? (g_navMore ? L"\xE70E" : L"\xE70D") : kNavGlyphBase[id - ID_NAV0], -1, &rg, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, g_fNav);
        SetTextColor(dc, col);
        DrawTextW(dc, text, -1, &rt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        COLORREF c = ButtonColor(id);
        c = down ? Shade(c, 0.75) : hot ? Shade(c, 1.2) : c;
        if (id == ID_UPDATE) {
            // Der Update-Button sitzt auf dem Farbverlauf der Kopfzeile: Hinter den runden Ecken denselben Verlauf zeichnen
            RECT wr;
            GetWindowRect(d->hwndItem, &wr);
            HWND par = GetParent(d->hwndItem);
            POINT pt{wr.left, wr.top};
            ScreenToClient(par, &pt);
            RECT pc;
            GetClientRect(par, &pc);
            const float leftL = (float)(pt.x / g_ui), widthL = (float)(pc.right / g_ui);
            int saved = SaveDC(dc);
            ResetLogical(dc);
            {
                Gdiplus::Graphics g(dc);
                g.ScaleTransform((Gdiplus::REAL)g_ui, (Gdiplus::REAL)g_ui);
                Gdiplus::LinearGradientBrush gb(Gdiplus::PointF((Gdiplus::REAL)kSideW - leftL, 0), Gdiplus::PointF(widthL + 2 - leftL, 0),
                                                Gdiplus::Color(255, 112, 72, 240), Gdiplus::Color(255, 24, 168, 232));
                g.FillRectangle(&gb, -2.f, -2.f, (Gdiplus::REAL)rc.right + 4, (Gdiplus::REAL)rc.bottom + 4);
            }
            RestoreDC(dc, saved);
        } else {
            FillRect(dc, &rc, g_bgBrush);
        }
        FillRound(dc, rc, c, 14);
        SelectObject(dc, g_fMed);
        SetTextColor(dc, RGB(255, 255, 255));
        DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    ResetLogical(dc);
    BitBlt(screenDc, d->rcItem.left, d->rcItem.top, bw, bh, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

static LRESULT CALLBACK ButtonProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    int id = GetDlgCtrlID(h);
    if (m == WM_ERASEBKGND) return 1;  // der Button zeichnet alles selbst; Hintergrund loeschen wuerde kurz schwarz aufblitzen
    if (m == WM_MOUSEMOVE && g_hover != id) {
        g_hover = id;
        InvalidateRect(h, nullptr, FALSE);
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
    } else if (m == WM_MOUSELEAVE) {
        g_hover = 0;
        InvalidateRect(h, nullptr, FALSE);
    }
    return DefSubclassProc(h, m, w, l);
}

static void ClientLogical(HWND h, int& W, int& H) {
    RECT rc;
    GetClientRect(h, &rc);
    W = (int)std::ceil(rc.right / g_ui);
    H = (int)std::ceil(rc.bottom / g_ui);
}

// Info-Symbol (i) sitzt direkt hinter dem Seitentitel; x0 = linker Rand des Titels
static RECT TitleInfoRect(HWND h, int x0, int page) {
    HDC mdc = GetDC(h);
    HGDIOBJ of = SelectObject(mdc, g_fTitle);
    SIZE tsz{};
    GetTextExtentPoint32W(mdc, Tr(kPageName[page]), (int)wcslen(Tr(kPageName[page])), &tsz);
    SelectObject(mdc, of);
    ReleaseDC(h, mdc);
    return {x0 + (int)tsz.cx + 14, 16, x0 + (int)tsz.cx + 40, 42};
}

static void Layout(HWND h) {
    int W, H;
    ClientLogical(h, W, H);
    const int cx = kSideW + kMargin, cw = W - kSideW - 2 * kMargin, btnH = 52;
    g_noteRect = {};
    const int navStep = 48, navH = 42;  // 9 Eintraege oben plus Einstellungen unten
    int row = 0;
    for (int i = 0; i < kPages - 1; ++i) {
        if (i == 5) { MW(GetDlgItem(h, ID_MORE), 10, 112 + row * navStep, kSideW - 20, navH); ++row; }  // "Mehr" gruppiert die selteneren Seiten
        HWND b = GetDlgItem(h, ID_NAV0 + i);
        bool show = !NavGrouped(i) || g_navMore;
        ShowWindow(b, show ? SW_SHOWNA : SW_HIDE);
        if (show) { MW(b, 10, 112 + row * navStep, kSideW - 20, navH); ++row; }
    }
    MW(GetDlgItem(h, ID_NAV0 + kSettingsPage), 10, std::max(112 + row * navStep, H - 92 - 62), kSideW - 20, navH);  // Einstellungen sitzen unten, ueber Exit
    MW(GetDlgItem(h, ID_EXIT), 14, H - 92, kSideW - 28, 40);
    {   // Update-Button: Breite richtet sich nach dem Text, damit nichts abgeschnitten wird
        HWND ub = GetDlgItem(h, ID_UPDATE);
        wchar_t t[96];
        GetWindowTextW(ub, t, 96);
        HDC mdc = GetDC(h);
        HGDIOBJ of = SelectObject(mdc, g_fMed);
        SIZE sz{};
        GetTextExtentPoint32W(mdc, t, (int)wcslen(t), &sz);
        SelectObject(mdc, of);
        ReleaseDC(h, mdc);
        int bw = std::clamp((int)sz.cx + 56, 250, std::max(250, W / 2));
        MW(ub, W - kMargin - bw, 14, bw, 40);
    }
    HWND clean = GetDlgItem(h, ID_CLEAN), toggle = GetDlgItem(h, ID_TOGGLE);
    if (g_page == 0) {
        MW(clean, W - kMargin - kOptW, kActTop, kOptW, kActH);  // Aktionszeile unter dem Banner: der Besen im Banner hat die ganze Breite fuer sich
        const int gap = 24, top = kActTop + kActH + 22, areaH = H - kMargin - top, rows = (kShownTiles + 1) / 2;
        const int th = std::clamp((areaH - (rows - 1) * gap) / rows, 110, 260), colW = (cw - gap) / 2;  // vier Kacheln in zwei Spalten
        for (int i = 0; i < kTiles; ++i) {
            if (i >= kShownTiles) { g_tile[i] = {}; continue; }  // ausgeblendete Kacheln: leeres Rechteck, damit Hover und Klick sie ignorieren
            const int x = cx + (i % 2) * (colW + gap), y = top + (i / 2) * (th + gap);
            g_tile[i] = {x, y, x + colW, y + th};
        }
    } else {
        MW(toggle, W - kMargin - 150, kHeaderH + 14, 150, 38);
        MW(GetDlgItem(h, ID_PROTECT), W - kMargin - 150 - 12 - 160, kHeaderH + 14, 160, 38);
        g_infoRect = {};
        if (g_page == kAutoPage) {
            const int sw = cw - 150 - 12, btnY2 = H - kMargin - btnH, listY2 = kHeaderH + 64;
            const int barY = kHeaderH + 12, barH = 40;  // Suchfeld und "Alle / Keine" haben dieselbe Oberkante, Hoehe und Mitte
            g_searchCard = {cx, barY, cx + sw, barY + barH};
            MW(g_search, cx + 44, barY + (barH - 22) / 2, sw - 60, 22);
            MW(toggle, W - kMargin - 150, barY, 150, barH);
            MW(g_alist, cx, listY2, cw, std::max(60, btnY2 - 12 - listY2));
            MW(clean, cx, btnY2, cw, btnH);
            g_btnRect = {cx, btnY2, cx + cw, btnY2 + btnH};
            g_infoRect = TitleInfoRect(h, cx, kAutoPage);
        } else if (g_page == kDrivePage) {
            const int listY2 = kHeaderH + 64;
            MW(GetDlgItem(h, ID_ADMIN), W - kMargin - 190, kHeaderH + 14, 190, 38);
            g_searchCard = {};
            g_btnRect = {};
            MW(g_alist, cx, listY2, cw, std::max(60, H - kMargin - listY2));
            g_infoRect = TitleInfoRect(h, cx, kDrivePage);
        } else if (g_page == kBrowserPage) {
            const int btnY2 = H - kMargin - btnH, listY2 = kHeaderH + 64;
            g_searchCard = {};
            MW(g_alist, cx, listY2, cw, std::max(60, btnY2 - 12 - listY2));
            MW(clean, cx, btnY2, cw, btnH);
            g_btnRect = {cx, btnY2, cx + cw, btnY2 + btnH};
            g_infoRect = TitleInfoRect(h, cx, kBrowserPage);
        } else if (g_page == kUninstPage) {  // Programme: Suchfeld, Sortier-Knopf und Liste bis nach unten
            const int sw = cw - 170 - 12, barY = kHeaderH + 12, barH = 40, listY2 = kHeaderH + 64;
            g_searchCard = {cx, barY, cx + sw, barY + barH};
            MW(g_search, cx + 44, barY + (barH - 22) / 2, sw - 60, 22);
            MW(toggle, W - kMargin - 170, barY, 170, barH);
            g_btnRect = {};
            MW(g_alist, cx, listY2, cw, std::max(60, H - kMargin - listY2));
            g_infoRect = TitleInfoRect(h, cx, kUninstPage);
        } else if (g_page >= 1 && g_page <= 3) {  // Downloads, Temp, Systemdateien: Kacheln (Temp/System mit Hinweisleiste)
            const int btnY2 = H - kMargin - btnH, listY2 = kHeaderH + 64;
            const bool note = g_page != 1;
            const int listH2 = std::max(60, std::max(60, btnY2 - 12 - listY2) - (note ? 52 : 0));
            g_searchCard = {};
            if (note) g_noteRect = {cx, listY2 + listH2 + 8, cx + cw, listY2 + listH2 + 8 + 44};
            MW(g_alist, cx, listY2, cw, listH2);
            g_listRect = {cx, listY2, cx + cw, listY2 + listH2};
            MW(clean, cx, btnY2, cw, btnH);
            g_btnRect = {cx, btnY2, cx + cw, btnY2 + btnH};
            g_infoRect = TitleInfoRect(h, cx, g_page);
        } else if (g_page == kRamPage) {
            g_searchCard = {};
            const int rw = std::min(cw, 860), top0 = kHeaderH + 18;
            g_ramCard = {cx, top0, cx + rw, top0 + 250};
            g_btnRect = {g_ramCard.right - 24 - 230, g_ramCard.bottom - 24 - 48, g_ramCard.right - 24, g_ramCard.bottom - 24};
            MW(GetDlgItem(h, ID_RAM), g_btnRect.left, g_btnRect.top, 230, 48);
        } else if (g_page == kSettingsPage) {
            g_searchCard = {};
            g_btnRect = {};
            const int rw = std::min(cw, 860), gap = 10, top0 = kHeaderH + 18;
            // Die Zeile "Downloads automatisch aufraeumen" ist um eine Leiste mit den beiden Auswahlmenues hoeher
            const int rh = std::clamp((H - kMargin - top0 - (kSettings - 1) * gap - kSetExtra) / kSettings, 64, 96);  // bei kleinem Fenster etwas flacher
            int y = top0;
            for (int i = 0; i < kSettings; ++i) {
                const int hh = rh + (i == kSetAutoRow ? kSetExtra : 0);
                g_setRow[i] = {cx, y, cx + rw, y + hh};
                y += hh + gap;
            }
        }
    }
    if (g_page == 0) g_infoRect = {};
    InvalidateRect(h, nullptr, FALSE);
}

// Zeigt den echten Zustand der Schalter (Registry, Run-Eintrag, Marker-Datei)
static void LoadSettings() {
    g_set[0] = StartWithWindowsOn();
    g_set[1] = g_trayOnClose;
    g_set[2] = AutoCleanOn();
    g_set[3] = !g_dark;  // an = helles Design
    g_set[4] = g_animOn;
    g_set[5] = DeepOn();
    g_set[6] = GetFlag(L"DupeScan", true);
    g_set[7] = SecureOn();
}

static void SetPage(HWND h, int p) {
    g_page = p;
    if (NavGrouped(p) && !g_navMore) { g_navMore = true; SetWindowTextW(GetDlgItem(h, ID_MORE), Tr(L"Weniger")); Layout(h); }
    g_setHot = -1;
    if (p == kSettingsPage) LoadSettings();
    SetWindowTextW(GetDlgItem(h, ID_CLEAN), p == 0 ? Tr(L"Optimieren") : p == kAutoPage ? Tr(L"Änderungen übernehmen") : Tr(L"Clean Now"));
    SetWindowTextW(GetDlgItem(h, ID_TOGGLE), p == kUninstPage ? (g_unSortSize ? Tr(L"Sortieren: Größe") : Tr(L"Sortieren: Name")) : Tr(L"Alle / Keine"));
    bool listPage = p != 0 && p != kDrivePage && p != kSettingsPage && p != kRamPage;
    ShowWindow(GetDlgItem(h, ID_CLEAN), (p == kDrivePage || p == kUninstPage || p == kSettingsPage || p == kRamPage) ? SW_HIDE : SW_SHOW);  // Laufwerke/Einstellungen/Arbeitsspeicher: nichts zu bereinigen
    const bool plainList = PageCat(p) >= 0 && p != kBrowserPage;  // Seiten mit Dateiliste und Protokoll
    ShowWindow(GetDlgItem(h, ID_TOGGLE), listPage ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(h, ID_PROTECT), plainList ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(h, ID_ADMIN), p == kDrivePage ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(h, ID_RAM), p == kRamPage ? SW_SHOW : SW_HIDE);
    if (p == kRamPage) SetTimer(h, ID_RAMTICK, 1500, nullptr);  // Anzeige alle 1,5 s auffrischen
    else KillTimer(h, ID_RAMTICK);
    if (p == 0) LoadAutostart();  // fuer die Autostart-Kachel der Uebersicht
    ShowWindow(g_alist, (p == kAutoPage || p == kBrowserPage || p == kDrivePage || p == kUninstPage || (p >= 1 && p <= 3)) ? SW_SHOW : SW_HIDE);
    ShowWindow(g_search, (p == kAutoPage || p == kUninstPage) ? SW_SHOW : SW_HIDE);
    SetWindowTextW(g_search, L"");  // sonst filtert ein alter Suchtext die Kacheln der anderen Seiten
    PopulateList();
    for (int i = 0; i < kPages; ++i) InvalidateRect(GetDlgItem(h, ID_NAV0 + i), nullptr, FALSE);
    Layout(h);
}

// Animation des Besens: g_sceneP = Fortschritt des Intros (0 = links am Start, 1 = Ruheposition),
// g_sceneSwing = Schwung beim Aufraeumen (-1..1, 0 = ruhig)
static float g_sceneP = 1.f, g_sceneSwing = 0.f;

// Banner-Szene: Besen faegt eine rote Schmutzspur weg und hinterlaesst eine saubere gruene Linie
static void DrawScene(HDC dc, int xStart, int W, int yOff = 0) {  // yOff: Verschiebung nach unten (Splash-Fenster), 0 = Kopfzeile
    using namespace Gdiplus;
    int saved = SaveDC(dc);
    ResetLogical(dc);
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.ScaleTransform((REAL)g_ui, (REAL)g_ui);
    g.TranslateTransform(0.f, (REAL)yOff);
    const float x0 = (float)(xStart + kMargin), x1 = (float)(W - kMargin), base = (float)(kHeaderH - 24);
    if (x1 - x0 < 300) { RestoreDC(dc, saved); return; }
    const float kPi = 3.14159265f;
    // sanft anfahren und abbremsen (ease-in-out), damit es als gewollte Bewegung wirkt und nicht als Ruckler
    const float ease = g_sceneP < 0.5f ? 4.f * g_sceneP * g_sceneP * g_sceneP : 1.f - powf(-2.f * g_sceneP + 2.f, 3.f) / 2.f;
    const float restX = x0 + (x1 - x0) * 0.68f;
    const float bx = x0 + (restX - x0) * ease + g_sceneSwing * 26.f;  // beim Fegen deutlich hin und her
    const float tilt = 16.f + 12.f * sinf(g_sceneP * 9.4f) * (1.f - g_sceneP * g_sceneP) + g_sceneSwing * 17.f;  // Wippen beim Fegen

    if (bx - 22 > x0 + 8) {
        LinearGradientBrush fade(PointF(x0, 0), PointF(bx - 20, 0), Color(0, 74, 222, 128), Color(255, 74, 222, 128));
        Pen green(&fade, 4.5f);
        green.SetStartCap(LineCapRound);
        green.SetEndCap(LineCapRound);
        std::vector<PointF> gp;
        for (float x = x0; x <= bx - 22; x += 6) gp.push_back(PointF(x, base + 1.5f * sinf((x - x0) / 60.f)));
        if (gp.size() > 2) g.DrawCurve(&green, gp.data(), (INT)gp.size());
    }

    Pen red(Color(255, 251, 113, 133), 3.5f);
    red.SetLineJoin(LineJoinRound);
    red.SetStartCap(LineCapRound);
    red.SetEndCap(LineCapRound);
    std::vector<PointF> rp;
    const float rs = bx + 34;
    for (float x = rs; x <= x1; x += 4) {
        float t = (x - rs) / (x1 - rs);
        rp.push_back(PointF(x, base - 2 - 12.f * sinf(t * kPi) + 5.f * t * sinf(x * 0.85f)));
    }
    if (rp.size() > 2) g.DrawLines(&red, rp.data(), (INT)rp.size());

    g.TranslateTransform(bx, base);
    g.RotateTransform(tilt);
    Pen wood(Color(255, 214, 160, 110), 5.5f);
    wood.SetStartCap(LineCapRound);
    wood.SetEndCap(LineCapRound);
    g.DrawLine(&wood, 0.f, -24.f, 0.f, -78.f);
    SolidBrush ferrule(Color(255, 251, 146, 60));
    g.FillRectangle(&ferrule, -13.f, -32.f, 26.f, 13.f);
    SolidBrush head(Color(255, 251, 191, 36));
    PointF hp[4] = {PointF(-13, -19), PointF(13, -19), PointF(27, 2), PointF(-27, 2)};
    g.FillPolygon(&head, hp, 4);
    Pen bristle(Color(255, 217, 119, 6), 1.6f);
    for (int i = 0; i < 7; ++i) g.DrawLine(&bristle, -11.f + i * 22.f / 6.f, -19.f, -25.f + i * 50.f / 6.f, 1.f);
    // Staub liegt im Koordinatensystem des Besens: er dreht sich mit und huepft beim Fegen
    SolidBrush dust(Color(200, 255, 255, 255));
    for (int i = 0; i < 7; ++i) {
        const float r = 1.6f + (i % 3) * 0.7f;
        const float ph = g_sceneSwing * 6.f + i * 1.7f;
        const float hop = g_sceneSwing != 0.f ? 4.f * sinf(ph) : 0.f;
        const float drift = g_sceneSwing != 0.f ? 3.f * cosf(ph) : 0.f;
        g.FillEllipse(&dust, 30.f + i * 6.f + drift, -32.f - (i % 3) * 8.f + hop, r * 2, r * 2);
    }
    RestoreDC(dc, saved);
}

// Kreis mit Symbol (Kategorie-Icon)
static void DrawBadge(HDC dc, int x, int y, int size, int cat) {
    RECT r{x, y, x + size, y + size};
    FillRound(dc, r, kCatColor[cat], size);
    SelectObject(dc, g_fIcon);
    SetTextColor(dc, RGB(255, 255, 255));
    DrawTextW(dc, kCatGlyph[cat], -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// Waehrend die Dateiseiten im Hintergrund aufgelistet werden: ruhige Anzeige statt der Leer-Ansicht
static void DrawLoading(HDC dc, const RECT& area) {
    FillRound(dc, area, kPanel, 18);
    SetTextAlign(dc, TA_CENTER);
    DrawTxt(dc, g_fMed, kMuted, (area.left + area.right) / 2, (area.top + area.bottom) / 2 - 12, Tr(L"Analyse l\u00E4uft \u2026"));
    SetTextAlign(dc, TA_LEFT);
}

// Leer-Ansicht fuer eine Kategorie ohne Eintraege: Haken-Abzeichen in der Kategoriefarbe und ein paar freundliche Zeilen
static void DrawEmpty(HDC dc, const RECT& area, int cat) {
    FillRound(dc, area, kPanel, 18);
    const int cx = (area.left + area.right) / 2, cy = (area.top + area.bottom) / 2;
    const int lines = 2 + (g_protCount[cat] > 0 ? 1 : 0);
    const int blockH = 96 + 16 + 34 + 8 + lines * 24;  // Abzeichen + Abstand + Titel + Abstand + Zeilen
    const int top = std::max((int)area.top + 10, cy - blockH / 2);
    const COLORREF col = kCatColor[cat];

    RECT halo{cx - 48, top, cx + 48, top + 96};  // zarter Hof hinter dem Abzeichen
    FillRound(dc, halo, Tint(col, kPanel, 0.22), 96);
    RECT badge{cx - 33, top + 15, cx + 33, top + 81};
    FillRound(dc, badge, col, 66);
    SelectObject(dc, g_fIconBig);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT gr = badge;
    DrawTextW(dc, L"", -1, &gr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);  // Haken

    SetTextAlign(dc, TA_CENTER);
    DrawTxt(dc, g_fBig, kStrong, cx, top + 112, Tr(L"Alles sauber"));
    std::wstring l1, l2;
    if (cat == CAT_BROWSER) {
        l1 = Tr(L"Keine Browser-Caches gefunden.");
        l2 = Tr(L"Chrome, Edge, Brave, Opera GX und Firefox sind aufgeräumt.");
    } else
    if (cat == CAT_DL) {
        l1 = Tr(L"Dein Downloads-Ordner ist leer.");
        l2 = AutoCleanOn() ? Tr(L"Auto-Clean räumt ihn regelmäßig für dich auf.") : Tr(L"Auto-Clean ist aus – er wird nur von Hand geleert.");
    } else if (cat == CAT_TEMP) {
        l1 = Tr(L"Keine temporären Dateien gefunden.");
        l2 = Tr(L"Dein PC ist an dieser Stelle aufgeräumt.");
    } else {
        l1 = Tr(L"Keine Vorschau-Caches oder Absturzberichte gefunden.");
        l2 = Tr(L"Hier gibt es nichts aufzuräumen.");
    }
    DrawTxt(dc, g_fSm, kMuted, cx, top + 158, l1);
    DrawTxt(dc, g_fSm, kMuted, cx, top + 182, l2);
    if (g_protCount[cat] > 0)
        DrawTxt(dc, g_fSm, RGB(140, 110, 220), cx, top + 206,
                g_protCount[cat] == 1 ? std::wstring(Tr(L"1 geschützter Eintrag bleibt erhalten."))
                                      : std::to_wstring(g_protCount[cat].load()) + Tr(L" geschützte Einträge bleiben erhalten."));
    SetTextAlign(dc, TA_LEFT);
}

// Zustands-Ring: Hintergrundring plus farbiger Bogen, der sich je nach Punktzahl (0-100) fuellt
static void DrawHealth(HDC dc, int cx, int cy, int r, int score, COLORREF col) {
    int saved = SaveDC(dc);
    ResetLogical(dc);
    {
        using namespace Gdiplus;
        Graphics g(dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.ScaleTransform((REAL)g_ui, (REAL)g_ui);
        Pen track(Color(255, GetRValue(kTrack), GetGValue(kTrack), GetBValue(kTrack)), 7.f);
        g.DrawEllipse(&track, (REAL)(cx - r), (REAL)(cy - r), (REAL)(2 * r), (REAL)(2 * r));
        if (score > 0) {
            Pen arc(Color(255, GetRValue(col), GetGValue(col), GetBValue(col)), 7.f);
            arc.SetStartCap(LineCapRound);
            arc.SetEndCap(LineCapRound);
            g.DrawArc(&arc, (REAL)(cx - r), (REAL)(cy - r), (REAL)(2 * r), (REAL)(2 * r), -90.f, (REAL)(3.6 * std::min(score, 100)));
        }
    }
    RestoreDC(dc, saved);
}

// Fortschrittsbalken: gruener Fuellstand, Prozent und Zaehler in der Mitte, darunter (falls Platz) die aktuelle Datei
static void DrawProgress(HDC dc, const RECT& r, COLORREF nameCol = kMuted) {
    int total = g_progTotal, done = g_progDone;
    double f = total > 0 ? std::min(1.0, (double)done / total) : 1.0;
    FillRound(dc, r, kCard, 14);
    int w = (int)((r.right - r.left) * f);
    if (w > 0) {
        RECT fr{r.left, r.top, r.left + std::max(w, 14), r.bottom};
        FillRound(dc, fr, RGB(34, 197, 94), 14);
    }
    wchar_t buf[128];
    if (const ULONGLONG us = g_ultraStart.load()) {  // Windows-Aktionen: kein Prozentwert, dafuer die Laufzeit (DISM meldet keinen Fortschritt)
        const unsigned sec = (unsigned)((GetTickCount64() - us) / 1000);
        swprintf_s(buf, Tr(L"Windows-Aktionen laufen …  %u:%02u Min."), sec / 60, sec % 60);
    } else {
        swprintf_s(buf, Tr(L"Bereinige …  %d %%   (%d / %d)"), (int)(f * 100), done, total);
    }
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, g_fMed);
    SetTextColor(dc, kStrong);
    RECT t = r;
    DrawTextW(dc, buf, -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    std::wstring name;
    { std::lock_guard<std::mutex> lk(g_jobMx); name = g_curName; }
    if (!name.empty()) {
        RECT nr{r.left, r.bottom + 6, r.right, r.bottom + 28};
        SelectObject(dc, g_fSm);
        SetTextColor(dc, nameCol);
        std::wstring s = Tr(L"Löscht: ") + name;
        DrawTextW(dc, s.c_str(), -1, &nr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

// Punktzahl (0-100), Wort und Farbe aus der Menge an Ballast (Temp + Systemdateien)
static void HealthOf(unsigned long long junk, int& score, const wchar_t*& label, COLORREF& col) {
    const double gb = junk / 1073741824.0;
    score = gb <= 1 ? (int)(100 - 6 * gb) : gb <= 8 ? (int)(94 - (gb - 1) * (33.0 / 7)) : (int)std::max(5.0, 61 - (gb - 8) * 7);
    if (gb < 1) { label = Tr(L"Gut"); col = kGood; }
    else if (gb < 8) { label = Tr(L"Aufräumen"); col = kWarn; }
    else { label = Tr(L"Viel Ballast"); col = kBad; }
}

// Umschalter "Standard | Ultra" rechts in der Kopfzeile (nur Uebersicht und Systemdateien)
static void DrawModePill(HDC dc, int W) {
    g_modeRect = {};
    if ((g_page != 0 && g_page != 3) || g_updating) return;
    const int py = g_page == 0 ? kActTop + (kActH - kModeH) / 2 : 84;  // Uebersicht: in der Aktionszeile unter dem Banner, Systemdateien: im Banner
    const int px = g_page == 0 ? W - kMargin - kOptW - 16 - (kModeW1 + kModeW2) : W - kMargin - (kModeW1 + kModeW2);
    RECT all{px, py, px + kModeW1 + kModeW2, py + kModeH};
    g_modeRect = all;
    FillRound(dc, all, RGB(20, 22, 44), kModeH);
    const int mode = DeepOn() ? 1 : 0;
    RECT a{all.left + 3, all.top + 3, all.left + kModeW1, all.bottom - 3}, b{all.left + kModeW1, all.top + 3, all.right - 3, all.bottom - 3};
    FillRound(dc, mode == 1 ? b : a, mode == 1 ? RGB(236, 100, 75) : RGB(79, 140, 255), kModeH - 6);
    SelectObject(dc, g_fSm);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, mode == 0 || g_modeHot == 1 ? RGB(255, 255, 255) : RGB(190, 198, 235));
    DrawTextW(dc, Tr(L"Standard"), -1, &a, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SetTextColor(dc, mode == 1 || g_modeHot == 2 ? RGB(255, 255, 255) : RGB(190, 198, 235));
    DrawTextW(dc, Tr(L"Ultra"), -1, &b, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
static int ModeSegAt(const POINT& p) {
    if (!PtInRect(&g_modeRect, p)) return 0;
    return p.x < g_modeRect.left + kModeW1 ? 1 : 2;
}

// Dezentes Info-Symbol hinter dem Seitentitel (Hover = kurzer Tooltip, Klick = ausfuehrliche Uebersicht)
static void DrawInfoIcon(HDC dc) {
    if (g_infoRect.right <= g_infoRect.left) return;
    {
        int saved = SaveDC(dc);
        ResetLogical(dc);
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.ScaleTransform((Gdiplus::REAL)g_ui, (Gdiplus::REAL)g_ui);
        Gdiplus::Pen pen(Gdiplus::Color(g_infoHot ? 255 : 170, 255, 255, 255), 1.8f);
        g.DrawEllipse(&pen, (Gdiplus::REAL)g_infoRect.left + 1, (Gdiplus::REAL)g_infoRect.top + 1,
                      (Gdiplus::REAL)(g_infoRect.right - g_infoRect.left - 2), (Gdiplus::REAL)(g_infoRect.bottom - g_infoRect.top - 2));
        RestoreDC(dc, saved);
    }
    RECT ir = g_infoRect;
    SelectObject(dc, g_fMed);
    SetTextColor(dc, RGB(255, 255, 255));
    DrawTextW(dc, L"i", -1, &ir, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static const wchar_t* PageTip();

// Eigener Hinweis-Kasten unter dem Info-Symbol (statt des Windows-Tooltips, der bei manchen Rechnern nicht erschien):
// wird direkt ins Fenster gezeichnet und ist deshalb immer sichtbar
static void DrawInfoTip(HDC dc, int W) {
    if (!g_infoHot || g_infoRect.right <= g_infoRect.left) return;
    const wchar_t* tip = PageTip();
    const int maxW = std::min(560, W - (kSideW + kMargin) - 2 * kMargin);
    SelectObject(dc, g_fSm);
    RECT tr{0, 0, maxW, 0};
    DrawTextW(dc, tip, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    const int bw = tr.right + 32, bh = tr.bottom + 24;
    int x = std::min((int)g_infoRect.left - 6, W - kMargin - bw);
    x = std::max(x, kSideW + kMargin);
    const int y = g_infoRect.bottom + 8;
    RECT outer{x - 1, y - 1, x + bw + 1, y + bh + 1}, box{x, y, x + bw, y + bh};
    FillRound(dc, outer, RGB(79, 140, 255), 14);
    FillRound(dc, box, RGB(26, 28, 48), 14);
    RECT txt{x + 16, y + 12, x + bw - 16, y + bh - 12};
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(240, 242, 252));
    DrawTextW(dc, tip, -1, &txt, DT_WORDBREAK | DT_NOPREFIX);
}

// ---- Auswahlmenues der Auto-Clean-Zeile in den Einstellungen: Intervall und Groessengrenze ----
static const int kIntervalChoices[] = {7, 14, 30, 60, 90};
static const int kSizeChoices[] = {0, 1, 5, 10, 50, 100};

static std::wstring IntervalLabel(int days) {
    wchar_t b[64];
    swprintf_s(b, Tr(L"Alle %d Tage"), days);
    return b;
}
static std::wstring KeepLabel(int mb) {
    if (!mb) return Tr(L"Behalten: nichts");
    wchar_t b[64];
    swprintf_s(b, Tr(L"Behalten: < %d MB"), mb);
    return b;
}

// Position der beiden Auswahlfelder in der Leiste unter dem Text der Zeile (logische Einheiten)
static void SettingPills(const RECT& r, RECT& a, RECT& b, RECT& c) {
    const int y0 = r.bottom - kSetPillBar;
    a = {r.left + 22, y0, r.left + 22 + 180, y0 + 34};
    b = {a.right + 10, y0, a.right + 10 + 220, y0 + 34};
    c = {b.right + 10, y0, b.right + 10 + 210, y0 + 34};
}

static void DrawPill(HDC dc, const RECT& p, const std::wstring& text, bool enabled, bool menu = true) {
    FillRound(dc, p, kBadgeBg, 34);
    RECT tr{p.left + 16, p.top, p.right - (menu ? 34 : 16), p.bottom};
    SelectObject(dc, g_fSm);
    SetTextColor(dc, enabled ? kStrong : kMuted);
    DrawTextW(dc, text.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (!menu) return;  // reiner Ein/Aus-Schalter ohne Pfeil
    RECT cr{p.right - 32, p.top, p.right - 8, p.bottom};
    SelectObject(dc, g_fIcon);
    SetTextColor(dc, kMuted);
    DrawTextW(dc, L"\xE70D", -1, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);  // Pfeil nach unten
}

// Klappt unter dem Feld ein Auswahlmenue auf; Rueckgabe: Index des gewaehlten Eintrags oder -1
static int PickFromMenu(HWND h, const RECT& pill, const std::vector<std::wstring>& labels, int checked) {
    HMENU menu = CreatePopupMenu();
    for (int i = 0; i < (int)labels.size(); ++i) AppendMenuW(menu, MF_STRING | (i == checked ? MF_CHECKED : 0), 1000 + i, labels[i].c_str());
    POINT pt{D(pill.left), D(pill.bottom) + 2};
    ClientToScreen(h, &pt);
    SetForegroundWindow(h);
    const int cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, h, nullptr);
    DestroyMenu(menu);
    return cmd >= 1000 ? cmd - 1000 : -1;
}

static void PaintWindow(HWND h) {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(h, &ps);
    RECT crc;
    GetClientRect(h, &crc);
    int W, H;
    ClientLogical(h, W, H);
    const int cx = kSideW + kMargin;
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bm = CreateCompatibleBitmap(wdc, crc.right, crc.bottom);
    HGDIOBJ obm = SelectObject(dc, bm);
    SetBkMode(dc, TRANSPARENT);
    SetLogical(dc);
    RECT all{0, 0, W + 2, H + 2}, sb{0, 0, kSideW, H + 2};
    FillRect(dc, &all, g_bgBrush);
    FillRect(dc, &sb, g_sideBrush);
    // Waehrend der Besen-Animation wird nur der Banner neu gezeichnet: Der Rest wird gar nicht erst aufgebaut
    const bool bannerOnly = ps.rcPaint.left >= D(kSideW) && ps.rcPaint.bottom <= D(kHeaderH) + 2;

    // Sidebar oben: Zustands-Ring mit Punktzahl und freier Speicher des Systemlaufwerks
    if (!bannerOnly) {
        unsigned long long sideJunk = Stat(CAT_TEMP).bytes + Stat(CAT_SYS).bytes;
        int sideScore = 0;
        const wchar_t* sideLabel = Tr(L"Analyse …");
        COLORREF sideCol = kMuted;
        if (g_scanned) HealthOf(sideJunk, sideScore, sideLabel, sideCol);
        const int rcx = 46, rcy = 48, rr = 24;
        DrawHealth(dc, rcx, rcy, rr, g_scanned ? sideScore : 0, sideCol);
        RECT rs{rcx - rr, rcy - 12, rcx + rr, rcy + 12};
        SelectObject(dc, g_fMed);
        SetTextColor(dc, kStrong);
        DrawTextW(dc, g_scanned ? std::to_wstring(sideScore).c_str() : L"…", -1, &rs, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawFit(dc, g_fMed, sideCol, 86, 28, kSideW - 10, sideLabel);
        DrawFit(dc, g_fSm, kMuted, 86, 52, kSideW - 10, Tr(L"Zustand"));
    }
    DrawTxt(dc, g_fSm, g_linkHot ? kText : kMuted, 18, H - 34, std::wstring(L"© 2026 Duncan · v") + kVersion);

    // Kopfzeile mit Farbverlauf (GDI+), Titel, Status und Besen-Szene
    {
        int saved = SaveDC(dc);
        ResetLogical(dc);
        Gdiplus::Graphics g(dc);
        g.ScaleTransform((Gdiplus::REAL)g_ui, (Gdiplus::REAL)g_ui);
        Gdiplus::LinearGradientBrush gb(Gdiplus::RectF((Gdiplus::REAL)kSideW, 0, (Gdiplus::REAL)(W - kSideW + 2), (Gdiplus::REAL)kHeaderH),
                                        Gdiplus::Color(255, 112, 72, 240), Gdiplus::Color(255, 24, 168, 232),
                                        Gdiplus::LinearGradientModeHorizontal);
        g.FillRectangle(&gb, kSideW, 0, W - kSideW + 2, kHeaderH);
        RestoreDC(dc, saved);
    }
    DrawScene(dc, kSideW, W);  // vor den Texten, damit der Besen beim Fegen hinter Titel und Status laeuft
    std::wstring status;
    if (g_page == 0) {
        status = (g_scanning || !g_scanned) ? Tr(L"Analyse läuft …") : Tr(L"Analyse abgeschlossen  ·  ") + DaysLeftText();
    } else if (g_page == kAutoPage) {
        int active = 0;
        for (const auto& e : g_auto) active += (e.orig && !e.missing) ? 1 : 0;
        status = std::to_wstring(g_auto.size()) + Tr(L" Programme starten mit Windows  ·  ") + std::to_wstring(active) + Tr(L" aktiv");
    } else if (g_page == kUninstPage) {
        unsigned long long tot = 0;
        for (const auto& e : g_unin) tot += e.bytes;
        status = g_uninLoading ? std::wstring(Tr(L"Analyse läuft …")) : std::to_wstring(g_unin.size()) + Tr(L" Programme installiert  ·  ") + FormatSize(tot) + Tr(L" belegt");
    } else if (g_page == kSettingsPage) {
        status = Tr(L"Verhalten des Programms  ·  Änderungen gelten sofort");
    } else if (g_page == kRamPage) {
        unsigned long long fr = 0, tt = 0;
        RamStatus(fr, tt);
        status = FormatSize(fr) + Tr(L" frei von ") + FormatSize(tt) + Tr(L"  ·  nur Anzeige, es wird nichts gelöscht");
    } else {
        int c = PageCat(g_page);
        CatStat s = Stat(c);
        const std::wstring note = (c == CAT_SYS && DeepOn()) ? std::wstring(Tr(L"Ultra: alles inklusive Shader-, Update-, Treiber-, App-Cache-, Store- und ETL-Reste")) : std::wstring(Tr(kCatNote[c]));
        status = g_scanned ? std::to_wstring(s.count) + Tr(L" Einträge  ·  ") + FormatSize(s.bytes) + L"  ·  " + note
                           : std::wstring(Tr(L"Analyse läuft …  ·  ")) + note;
        if (c == CAT_DL) status += L"  ·  " + DaysLeftText();
        if (g_protCount[c] > 0) status += L"  ·  " + std::to_wstring(g_protCount[c].load()) + Tr(L" geschützt");
    }
    if (g_page == kDrivePage) {
        unsigned long long fr = 0, tt = 0;
        for (const auto& e : g_drv) { fr += e.freeB; tt += e.totalB; }
        status = g_drvLoading ? std::wstring(Tr(L"Analyse läuft …  ·  nur Anzeige, es wird nichts verändert"))
                              : std::to_wstring(g_drv.size()) + Tr(L" Laufwerke  ·  ") + FormatSize(fr) + Tr(L" frei von ") + FormatSize(tt);
    }
    if (g_page == kDrivePage && g_topBusy) status = Tr(L"Suche größte Dateien auf ") + g_topLabel + L": …";
    if (g_page == kBrowserPage) {
        unsigned long long bb = 0;
        int found = 0;
        for (const auto& e : g_brow) { if (e.bkind) continue; bb += e.avail ? e.bytes : 0; found += e.avail ? 1 : 0; }
        status = g_browLoading ? std::wstring(Tr(L"Analyse läuft …  ·  nur Cache, nichts Persönliches"))
                               : std::to_wstring(found) + Tr(L" von 5 Browsern gefunden  ·  ") + FormatSize(bb) + Tr(L" Cache  ·  wird endgültig gelöscht");
    }
    DrawTxt(dc, g_fTitle, RGB(255, 255, 255), cx, 8, Tr(kPageName[g_page]));
    if (g_updating) status = Tr(L"Update läuft – bitte das Fenster nicht schließen");
    if (g_page == 0 && !g_updating) {
        // Uebersicht: Fund-Menge, Hinweis und Fortschritt sitzen im Banner (eine Kopfzeile statt Banner plus Textblock)
        long long totalN = 0;
        unsigned long long totalB = 0;
        for (int c = 0; c < CAT_N; ++c) {
            const CatStat cs = Stat(c);
            totalN += cs.count;
            totalB += cs.bytes;
        }
        DrawFit(dc, g_fMed, RGB(255, 255, 255), cx, 50, W - kMargin - 280,  // Update-Knopf sitzt oben rechts

                g_busy ? Tr(L"Bereinigung läuft …")
                       : !g_doneText.empty() ? g_doneText
                       : g_scanned ? std::to_wstring(totalN) + Tr(L" Objekte (") + FormatSize(totalB) + Tr(L") gefunden") : Tr(L"Analyse läuft …"));
    } else {
        DrawTxt(dc, g_fSm, kHdrSub, cx, 50, status);
    }
    if (g_updating) {  // Fortschrittsbalken des Updates: Download 0-90 %, Pruefen bis 95 %, Installieren bis 100 %
        const int phase = g_updPhase;
        const unsigned long long got = g_updGot, tot = g_updTotal;
        double f = phase == 0 ? (tot ? 0.9 * std::min(1.0, (double)got / (double)tot) : 0.0) : phase == 1 ? 0.95 : 1.0;
        wchar_t txt[96];
        if (phase == 0 && tot) swprintf_s(txt, Tr(L"Update lädt …  %d %%  ·  %.1f / %.1f MB"), (int)(f / 0.9 * 100), got / 1048576.0, tot / 1048576.0);
        else if (phase == 0) swprintf_s(txt, Tr(L"Update lädt …  %.1f MB"), got / 1048576.0);
        else if (phase == 1) swprintf_s(txt, Tr(L"Prüfe Signatur …"));
        else swprintf_s(txt, Tr(L"Installiere …"));
        RECT ur{cx, 76, std::min(W - kMargin, cx + 400), 100};
        FillRound(dc, ur, RGB(20, 22, 44), 12);
        const int fw = (int)((ur.right - ur.left) * f);
        if (fw > 0) {
            RECT fr{ur.left, ur.top, ur.left + std::max(fw, 12), ur.bottom};
            FillRound(dc, fr, RGB(34, 197, 94), 12);
        }
        SelectObject(dc, g_fSm);
        SetTextColor(dc, RGB(255, 255, 255));
        DrawTextW(dc, txt, -1, &ur, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    DrawModePill(dc, W);
    if (bannerOnly && g_page != kAutoPage && g_page != kBrowserPage && g_page != kDrivePage && g_page != kUninstPage) {
        // Banner-Animation: Kacheln, Liste und Seitenleiste bleiben unveraendert
        DrawInfoIcon(dc);  // der Banner wurde neu aufgebaut, das Symbol liegt darauf und muss mit
    } else if (g_page == 0) {
        CatStat st[CAT_N];
        unsigned long long junk = 0;
        for (int c = 0; c < CAT_N; ++c) {
            st[c] = Stat(c);
            if (c != CAT_BROWSER) junk += st[c].bytes;  // Optimieren: Downloads, Temp, Systemdateien; Browser-Cache hat einen eigenen Reiter
        }
        {   // Aktionszeile: links Hinweis bzw. Fortschritt, rechts Umschalter und (als Fenster) der Optimieren-Knopf
            const int textRight = W - kMargin - kOptW - 16 - (kModeW1 + kModeW2) - 16;
            if (g_busy) {
                RECT pr{cx, kActTop, textRight, kActTop + 18};  // der Dateiname steht darunter
                DrawProgress(dc, pr);
            } else if (!g_doneText.empty()) {
                DrawFit(dc, g_fMed, kGood, cx, kActTop - 2, textRight, g_doneText);
                DrawFit(dc, g_fSm, kMuted, cx, kActTop + 24, textRight, g_doneSub);
            } else {
                DrawFit(dc, g_fMed, kSub, cx, kActTop + 8, textRight,
                        g_scanned ? Tr(L"Mit „Optimieren“ entfernbar: ") + FormatSize(junk) + Tr(L" (ohne Browser-Cache)") +
                                        (FreedTotal() ? Tr(L"  ·  Bisher freigeräumt: ") + FormatSize(FreedTotal()) + Tr(L" (letzte 30 Tage: ") + FormatSize(g_freed30.load()) + L")"
                                                      : std::wstring())
                                  : Tr(L"Bitte einen Moment warten"));
            }
        }
        int autoActive = 0;
        for (const auto& e : g_auto) autoActive += (e.orig && !e.missing) ? 1 : 0;
        for (int i = 0; i < kShownTiles; ++i) {
            const RECT& t = g_tile[i];
            const int th = t.bottom - t.top;
            FillRound(dc, t, g_tileHot == i ? kCardHot : kCard, 20);
            DrawBadge(dc, t.left + 22, t.top + (th - 52) / 2, 52, i);
            std::wstring name, note, cnt, sz;
            if (i < CAT_N) {
                name = Tr(kCatName[i]);
                note = i == CAT_DL ? DaysLeftText() : (i == CAT_SYS && DeepOn()) ? std::wstring(Tr(L"Ultra-Tiefenreinigung aktiv")) : std::wstring(Tr(kCatNote[i]));
                cnt = g_scanned ? std::to_wstring(st[i].count) : L"…";
                sz = g_scanned ? FormatSize(st[i].bytes) : L"";
            } else if (i == CAT_N) {  // Laufwerke
                name = Tr(L"Laufwerke");
                note = Tr(L"Belegung und Gesundheit");
                cnt = g_scanned ? std::to_wstring(g_ovDrives.load()) : L"…";
                sz = g_scanned && g_ovTotal ? FormatSize(g_ovFree) + Tr(L" frei") : L"";
            } else if (i == CAT_N + 2) {  // Programme
                name = Tr(L"Programme");
                note = Tr(L"Installierte Programme");
                cnt = g_scanned ? std::to_wstring(g_ovProgs.load()) : L"…";
                sz = L"";
            } else if (i == kTiles - 1) {  // Arbeitsspeicher
                unsigned long long fr = 0, tt = 0;
                RamStatus(fr, tt);
                name = Tr(L"Arbeitsspeicher");
                note = Tr(L"Belegung und Optimierung");
                cnt = std::to_wstring(tt ? (int)((tt - fr) * 100 / tt) : 0) + L" %";
                sz = FormatSize(fr) + Tr(L" frei");
            } else {  // Autostart
                name = Tr(L"Autostart");
                note = Tr(L"Programme mit Windows");
                cnt = std::to_wstring(autoActive);
                wchar_t of[64];
                swprintf_s(of, Tr(L"von %d aktiv"), (int)g_auto.size());
                sz = of;
            }
            DrawFit(dc, g_fMed, kStrong, t.left + 92, t.top + th / 2 - 28, t.right - 130, name);
            DrawFit(dc, g_fSm, kMuted, t.left + 92, t.top + th / 2 + 2, t.right - 130, note);
            RECT rc1{t.left, t.top + th / 2 - 36, t.right - 26, t.top + th / 2 + 4}, rc2{t.left, t.top + th / 2 + 6, t.right - 26, t.top + th / 2 + 34};
            SelectObject(dc, g_fBig);
            SetTextColor(dc, kStrong);
            DrawTextW(dc, cnt.c_str(), -1, &rc1, DT_RIGHT | DT_SINGLELINE | DT_BOTTOM);
            SelectObject(dc, g_fSm);
            SetTextColor(dc, kSub);
            DrawTextW(dc, sz.c_str(), -1, &rc2, DT_RIGHT | DT_SINGLELINE);
            RECT bar{t.left + 22, t.bottom - 9, t.right - 22, t.bottom - 6};
            FillRound(dc, bar, kCatColor[i], 3);
        }
    } else if (g_page == kSettingsPage) {
        static const wchar_t* kSetTitle[kSettings] = {L"Mit Windows starten", L"Im Hintergrund weiterlaufen", L"Downloads automatisch aufräumen",
                                                          L"Helles Design", L"Besen-Animation", L"Ultra-Tiefenreinigung", L"Doppelte Downloads erkennen", L"Sicher löschen (überschreiben)", L"Sprache / Language"};
        static const wchar_t* kSetText[kSettings] = {
            L"Startet beim Anmelden unsichtbar im System-Tray (neben der Uhr).",
            L"X minimiert in den Tray. Beenden über „Exit“ oder Rechtsklick auf das Symbol.",
            L"Räumt Downloads im gewählten Intervall auf (erst in den Papierkorb, kleine Dateien ausnehmbar). Optional auch Temp-Dateien, Vorschau-/Symbol-Cache sowie Absturz- und Fehlerberichte – die werden endgültig gelöscht; benutzte Dateien und Schutzliste bleiben.",
            L"Aus = dunkles Design (wie bisher), an = helles Design. Der Wechsel gilt sofort.",
            L"Der Besen in der Kopfzeile fegt beim Start und beim Aufräumen.",
            L"Nimmt bei den Systemdateien und beim Optimieren alles mit auf, was geht: Shader-Caches, Windows-Update- und Treiber-Installer-Reste, Fehlerberichte, Windows-Temp, App-Caches, Store-Reste und ETL-Protokolle. Auf der Seite Systemdateien kommen auf Wunsch DISM, Schattenkopien und Ereignisprotokolle dazu (jeweils mit Rückfrage und Administratorrechten). Vorher zeigt Tidy eine Vorschau. Auto-Clean und Schnell-Reinigung bleiben beim Standard.",
            L"Sucht im Downloads-Ordner nach Dateien mit gleichem Inhalt (nicht nur gleichem Namen) und zeigt die überzähligen Kopien als eigene Gruppe „Doppelte Dateien“. Die älteste Kopie bleibt immer, die übrigen kommen wie alle Downloads in den Papierkorb. Große Downloads brauchen beim Öffnen der Seite etwas länger.",
            L"Überschreibt Dateien vor dem endgültigen Löschen einmal mit Zufallsdaten, damit sie sich nicht wiederherstellen lassen. Gilt für alles, was Tidy endgültig löscht (Temp, Systemdateien, Browser-Daten, Auto-Clean), nicht für den Papierkorb. Dauert deutlich länger. Auf SSDs schützt es nur begrenzt, weil der Speicher Daten intern verschiebt; dort hilft Verschlüsselung (BitLocker) mehr.",
            L"Sprache der Oberfläche. Ein Klick öffnet die Auswahl."};
        static const wchar_t* kSetGlyph[kSettings] = {L"\xE7E8", L"\xE73F", L"\xE777", L"\xE706", L"\xE768", L"\xE74D", L"\xE8C8", L"\xE72E", L"\xE774"};  // E706 = Sonne, E768 = Play, E8C8 = Kopie
        for (int i = 0; i < kSettings; ++i) {
            const RECT& r = g_setRow[i];
            const int rh = r.bottom - r.top - (i == kSetAutoRow ? kSetExtra : 0);  // Hoehe ohne die Leiste mit den Auswahlmenues
            FillRound(dc, r, g_setHot == i ? kCardHot : kCard, 20);
            RECT bd{r.left + 22, r.top + (rh - 48) / 2, r.left + 70, r.top + (rh - 48) / 2 + 48};
            FillRound(dc, bd, kBadgeBg, 16);
            SelectObject(dc, g_fIcon);
            SetTextColor(dc, RGB(79, 140, 255));
            DrawTextW(dc, kSetGlyph[i], -1, &bd, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            RECT sw{r.right - 24 - 56, r.top + (rh - 30) / 2, r.right - 24, r.top + (rh - 30) / 2 + 30};
            if (i == kSettings - 1) {  // Sprache: Schaltflaeche mit dem Namen der aktuellen Sprache
                sw = {r.right - 24 - 190, r.top + (rh - 34) / 2, r.right - 24, r.top + (rh - 34) / 2 + 34};
                DrawPill(dc, sw, kLangs[g_lang].name, true);
            } else {
                FillRound(dc, sw, g_set[i] ? RGB(34, 197, 94) : kSwitchOff, 30);
                const int kx = g_set[i] ? sw.right - 4 - 22 : sw.left + 4;
                RECT knob{kx, sw.top + 4, kx + 22, sw.top + 26};
                FillRound(dc, knob, RGB(255, 255, 255), 22);
            }
            DrawFit(dc, g_fMed, kStrong, r.left + 92, r.top + 10, sw.left - 18, Tr(kSetTitle[i]));
            RECT tr{r.left + 92, r.top + 36, sw.left - 18, r.top + rh + (i == kSetAutoRow ? kSetTextExtra : 0) - 6};
            SelectObject(dc, g_fSm);
            SetTextColor(dc, kMuted);
            DrawTextW(dc, Tr(kSetText[i]), -1, &tr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
            if (i == kSetAutoRow) {  // Auswahlmenues: Intervall und Groessengrenze
                RECT pa, pb, pc;
                SettingPills(r, pa, pb, pc);
                DrawPill(dc, pa, IntervalLabel(AutoDays()), g_set[i]);
                DrawPill(dc, pb, KeepLabel(KeepBelowMB()), g_set[i]);
                DrawPill(dc, pc, GetFlag(L"AutoExtra", false) ? Tr(L"Temp & System: An") : Tr(L"Temp & System: Aus"), g_set[i], false);
            }
        }
    } else if (g_page == kRamPage) {
        unsigned long long fr = 0, tt = 0;
        RamStatus(fr, tt);
        const unsigned long long used = tt > fr ? tt - fr : 0;
        const double usedF = tt ? (double)used / (double)tt : 0.0;
        const RECT& c = g_ramCard;
        FillRound(dc, c, kCard, 20);
        RECT bd{c.left + 24, c.top + 24, c.left + 24 + 56, c.top + 24 + 56};
        FillRound(dc, bd, kCatColor[7], 56);
        SelectObject(dc, g_fIcon);
        SetTextColor(dc, RGB(255, 255, 255));
        DrawTextW(dc, kCatGlyph[7], -1, &bd, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawFit(dc, g_fBig, kStrong, c.left + 96, c.top + 18, c.right - 24, FormatSize(fr) + Tr(L" frei von ") + FormatSize(tt));
        wchar_t pct[96];
        swprintf_s(pct, Tr(L"%d %% belegt  ·  %s in Benutzung"), (int)(usedF * 100.0 + 0.5), FormatSize(used).c_str());
        DrawFit(dc, g_fSm, kMuted, c.left + 96, c.top + 58, c.right - 24, pct);
        RECT bar{c.left + 24, c.top + 108, c.right - 24, c.top + 108 + 18};  // Belegung: farbiger Teil = in Benutzung
        FillRound(dc, bar, kTrack, 18);
        const int fw = (int)((bar.right - bar.left) * usedF);
        if (fw > 0) {
            RECT fb{bar.left, bar.top, bar.left + std::max(fw, 18), bar.bottom};
            FillRound(dc, fb, usedF > 0.9 ? RGB(239, 68, 68) : usedF > 0.75 ? RGB(245, 158, 11) : kCatColor[7], 18);
        }
        RECT tr{c.left + 24, c.top + 140, c.right - 24 - 230 - 20, c.bottom - 20};
        SelectObject(dc, g_fSm);
        SetTextColor(dc, kMuted);
        DrawTextW(dc, Tr(L"„RAM optimieren“ verkleinert den Arbeitsspeicher laufender Programme. Windows holt sich benötigte Daten bei Bedarf zurück; mit Administrator-Rechten wird zusätzlich die Standby-Liste geleert."),
                  -1, &tr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
    } else if (g_page == kAutoPage || g_page == kBrowserPage || g_page == kDrivePage || g_page == kUninstPage) {
        if (g_page == kBrowserPage) {
            DrawTxt(dc, g_fMed, kText, cx, kHeaderH + 22, Tr(L"Browser auswählen"));
            if (g_busy) DrawProgress(dc, g_btnRect);
        }
        FillRound(dc, g_searchCard, kPanel, 14);  // Rahmen des Suchfelds (das Eingabefeld liegt darauf)
        SelectObject(dc, g_fIcon);
        SetTextColor(dc, kMuted);
        RECT mg{g_searchCard.left + 14, g_searchCard.top, g_searchCard.left + 40, g_searchCard.bottom};
        DrawTextW(dc, L"", -1, &mg, DT_LEFT | DT_VCENTER | DT_SINGLELINE);  // Lupe
        DrawInfoIcon(dc);
    } else {
        DrawTxt(dc, g_fMed, kText, cx, kHeaderH + 22, Tr(L"Auswahl"));
        DrawInfoIcon(dc);
        if (PageCat(g_page) >= 0) {
            if (g_listLoading) DrawLoading(dc, g_listRect);
            else if (g_items.empty()) DrawEmpty(dc, g_listRect, PageCat(g_page));
        }
        if (g_noteRect.right > g_noteRect.left) {  // Hinweisleiste
            FillRound(dc, g_noteRect, kCard, 14);
            SelectObject(dc, g_fIcon);
            SetTextColor(dc, kGood);
            RECT gi{g_noteRect.left + 16, g_noteRect.top, g_noteRect.left + 46, g_noteRect.bottom};
            DrawTextW(dc, L"\xE73E", -1, &gi, DT_LEFT | DT_VCENTER | DT_SINGLELINE);  // Haken
            RECT nt{g_noteRect.left + 52, g_noteRect.top, g_noteRect.right - 16, g_noteRect.bottom};
            SelectObject(dc, g_fSm);
            SetTextColor(dc, kSub);
            DrawTextW(dc, (g_page == 3 && DeepOn()) ? Tr(L"Ultra-Tiefenreinigung: Vor dem Löschen kommt eine Vorschau je Gruppe. Systemordner, Schutzliste und benutzte Dateien bleiben erhalten.")
                                                    : Tr(L"Sicherer Modus: Nur dein Benutzerkonto, benutzte Dateien bleiben erhalten."), -1, &nt,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        if (g_busy) DrawProgress(dc, g_btnRect);
    }

    DrawInfoTip(dc, W);  // zuletzt, damit der Kasten ueber allem liegt
    ResetLogical(dc);
    BitBlt(wdc, 0, 0, crc.right, crc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, obm);
    DeleteObject(bm);
    DeleteDC(dc);
    EndPaint(h, &ps);
}

// Besen-Animation: Timer laeuft nur, solange etwas in Bewegung ist (Intro oder Aufraeumen)
constexpr UINT_PTR ID_ANIM = 77, ID_UPDT = 78, ID_TRAYRETRY = 79;
static bool g_trayWanted = false;  // das Tray-Symbol soll da sein (auch wenn das Hinzufuegen noch scheiterte)
constexpr float kIntroMs = 1000.f, kIntroDelayMs = 250.f;  // Anlaufpause, damit das Auge die Bewegung findet
static bool g_introOn = false;
static ULONGLONG g_introStart = 0;

static bool MotionAllowed() {
    if (!g_animOn) return false;  // in den Einstellungen ausgeschaltet
    BOOL on = TRUE;  // Windows-Einstellung "Animationseffekte"
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0);
    return on != FALSE;
}

static bool g_fineTimer = false;  // Systemtakt auf 1 ms, solange animiert wird (sonst kommen Timer nur alle ~16-31 ms an)

static void StartAnim(HWND h, bool intro) {
    if (!MotionAllowed()) return;
    if (intro) {
        g_introOn = true;
        g_introStart = GetTickCount64() + (ULONGLONG)kIntroDelayMs;
        g_sceneP = 0.f;
    }
    if (!g_fineTimer) { timeBeginPeriod(1); g_fineTimer = true; }
    SetTimer(h, ID_ANIM, g_introOn ? 10 : 33, nullptr);  // ~60 FPS im Intro, ~30 FPS beim Aufraeumen
}

static void TickAnim(HWND h) {
    const ULONGLONG now = GetTickCount64();
    bool keep = false;
    if (g_introOn) {
        float p = std::max(0.f, (float)((long long)now - (long long)g_introStart) / kIntroMs);
        if (p >= 1.f) { p = 1.f; g_introOn = false; }
        g_sceneP = p;
        keep = g_introOn;
    }
    if (g_busy) {
        g_sceneSwing = sinf((float)now / 140.f);
        keep = true;
    } else {
        g_sceneSwing = 0.f;
    }
    RECT cr;
    GetClientRect(h, &cr);
    RECT banner{D(kSideW), 0, cr.right, D(kHeaderH)};
    InvalidateRect(h, &banner, FALSE);
    if (g_ultraStart.load()) {
        static ULONGLONG lastSec = 0;
        if (now - lastSec >= 1000) { lastSec = now; InvalidateRect(h, nullptr, FALSE); }
    }
    if (!keep) {
        KillTimer(h, ID_ANIM);
        if (g_fineTimer) { timeEndPeriod(1); g_fineTimer = false; }
    }
}

// Loescht im Hintergrund, damit das Fenster bedienbar bleibt und der Fortschritt sichtbar ist
// Explorer fuer das Leeren des Symbol-/Vorschau-Caches kurz beenden (nur eigene Sitzung) und danach sicherstellen, dass er wieder laeuft
static void KillExplorer() {
    DWORD mine = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &mine);
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (s == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{sizeof pe};
    for (BOOL ok = Process32FirstW(s, &pe); ok; ok = Process32NextW(s, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"explorer.exe") != 0) continue;
        DWORD sess = 0;
        if (!ProcessIdToSessionId(pe.th32ProcessID, &sess) || sess != mine) continue;
        if (HANDLE hp = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID)) { TerminateProcess(hp, 0); CloseHandle(hp); }
    }
    CloseHandle(s);
}
static void EnsureExplorer() {
    for (int i = 0; i < 16 && !IsRunning(L"explorer.exe"); ++i) Sleep(250);  // Windows startet die Shell normalerweise selbst neu
    if (IsRunning(L"explorer.exe")) return;
    wchar_t win[MAX_PATH]{};
    GetWindowsDirectoryW(win, MAX_PATH);
    const std::wstring exe = std::wstring(win) + L"\\explorer.exe";
    ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---- Ultra-Aktionen: von Windows selbst ausgefuehrt, mit Administratorrechten (eine UAC-Abfrage) ----
// Das Skript laeuft ueber -EncodedCommand (keine Datei, die vor dem Start veraendert werden koennte) und nutzt nur Programme aus dem Systemordner.
enum { ULTRA_DISM = 1, ULTRA_VSS = 2, ULTRA_EVT = 4, ULTRA_WINOLD = 8 };
static fs::path WinOldPath() {  // Ordner der vorherigen Windows-Installation (nur wenn er ein echter Ordner und keine Verknuepfung ist)
    const fs::path p = KnownFolder(FOLDERID_Windows).parent_path() / L"Windows.old";
    std::error_code ec;
    if (!fs::is_directory(p, ec) || fs::is_symlink(p, ec)) return {};
    return p;
}
static std::atomic<int> g_ultraMask{0}, g_ultraDone{0}, g_ultraResult{0};

// Rueckgabe: -1 = Administrator-Abfrage abgelehnt oder Start fehlgeschlagen, sonst Maske der Aktionen, die NICHT gelungen sind
static int RunUltraActions(int mask, HWND owner) {
    std::wstring s = L"$ErrorActionPreference='Continue'; $f=0\n";
    if (mask & ULTRA_DISM)  // ersetzte Komponentenversionen entfernen; installierte Updates lassen sich danach nicht mehr deinstallieren
        s += L"& (Join-Path $env:windir 'System32\\dism.exe') /Online /Cleanup-Image /StartComponentCleanup /ResetBase | Out-Null; if ($LASTEXITCODE -ne 0) { $f+=1 }\n";
    if (mask & ULTRA_VSS)  // je Laufwerk alle Schattenkopien ausser der neuesten
        s += L"try { Get-CimInstance Win32_ShadowCopy | Group-Object VolumeName | ForEach-Object { $_.Group | Sort-Object InstallDate | Select-Object -SkipLast 1 | ForEach-Object { Remove-CimInstance -InputObject $_ -ErrorAction Stop } } } catch { $f+=2 }\n";
    if (mask & ULTRA_EVT)  // alle Ereignisprotokolle ausser Sicherheit; manche Kanaele lassen sich nicht leeren, das ist normal
        s += L"$w=Join-Path $env:windir 'System32\\wevtutil.exe'; $t=0; $bad=0; & $w el | Where-Object { $_ -and $_ -ne 'Security' } | ForEach-Object { $t++; & $w cl $_ 2>$null; if ($LASTEXITCODE -ne 0) { $bad++ } }; if ($t -gt 0 -and $bad -eq $t) { $f+=4 }\n";
    if (mask & ULTRA_WINOLD) {  // Windows.old: Besitz uebernehmen (takeown fragt je nach Sprache nach J oder Y), Rechte setzen, dann loeschen
        const fs::path wo = WinOldPath();
        if (wo.empty()) s += L"$f+=8\n";
        else {
            std::wstring q = wo.wstring();
            for (size_t i = 0; (i = q.find(L'\'', i)) != std::wstring::npos; i += 2) q.insert(i, L"'");
            s += L"$p='" + q + L"'; $sys=Join-Path $env:windir 'System32'; & (Join-Path $sys 'takeown.exe') /F $p /R /A /D Y 2>$null | Out-Null; & (Join-Path $sys 'takeown.exe') /F $p /R /A /D J 2>$null | Out-Null; "
                 L"& (Join-Path $sys 'icacls.exe') $p /grant '*S-1-5-32-544:(OI)(CI)F' /T /C /Q 2>$null | Out-Null; Remove-Item -LiteralPath $p -Recurse -Force -ErrorAction SilentlyContinue; if (Test-Path -LiteralPath $p) { $f+=8 }\n";
        }
    }
    s += L"exit $f";
    const std::wstring b64 = PsEncode(s);
    if (b64.empty()) return -1;
    const std::wstring exe = SystemFile(L"WindowsPowerShell\\v1.0\\powershell.exe");
    const std::wstring args = std::wstring(kPsArgs + 1) + b64;
    SHELLEXECUTEINFOW sei{sizeof sei};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.hwnd = owner;   // die UAC-Abfrage soll vor dem Tidy-Fenster erscheinen und nicht dahinter
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) return -1;  // bei "Nein" in der UAC-Abfrage schlaegt der Aufruf fehl
    DWORD code = 0xFFFF;
    if (WaitForSingleObject(sei.hProcess, 60 * 60 * 1000) == WAIT_OBJECT_0) GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return code == 0xFFFF ? (ULTRA_DISM | ULTRA_VSS | ULTRA_EVT | ULTRA_WINOLD) & mask : (int)(code & 15);
}

static void StartClean(HWND h, std::vector<fs::path> items, bool permanent, bool all, int cat, std::vector<fs::path> recycle = {}) {
    g_busy = true;
    g_progDone = 0;
    g_progTotal = std::max<int>(1, (int)(items.size() + recycle.size()));
    g_resOk = 0;
    g_resFail = 0;
    g_resFreed = 0;
    { std::lock_guard<std::mutex> lk(g_jobMx); g_skipped.clear(); }
    g_doneText.clear();
    g_doneSub.clear();
    ShowWindow(GetDlgItem(h, ID_CLEAN), SW_HIDE);  // an dieser Stelle zeichnet das Fenster den Balken
    InvalidateRect(h, nullptr, FALSE);
    StartAnim(h, false);
    std::vector<std::pair<fs::path, bool>> work;  // Eintrag + endgueltig (true) oder in den Papierkorb (false)
    for (auto& p : items) work.push_back({std::move(p), permanent});
    for (auto& p : recycle) work.push_back({std::move(p), false});
    Bg([=] {
        const bool restartExplorer = g_restartExplorer.exchange(false);
        if (restartExplorer) { KillExplorer(); Sleep(900); }
        auto last = std::chrono::steady_clock::now();
        for (const auto& [p, perm] : work) {
            std::wstring name = ItemLabel(cat, p);
            { std::lock_guard<std::mutex> lk(g_jobMx); g_curName = name; }
            unsigned long long sz = PathSize(p);
            bool ok = DeleteEntry(p, perm);
            if (ok) { ++g_resOk; g_resFreed += sz; } else ++g_resFail;
            {
                std::lock_guard<std::mutex> lk(g_jobMx);
                if (!ok && g_skipped.size() < 5) g_skipped.push_back(name);
            }
            ++g_progDone;
            auto now = std::chrono::steady_clock::now();
            if (now - last > std::chrono::milliseconds(40)) {
                last = now;
                PostMessageW(h, WM_CLEAN_TICK, 0, 0);
            }
        }
        if (restartExplorer) EnsureExplorer();
        if (const int um = g_ultraMask.exchange(0)) {  // Ultra-Aktionen laufen nach dem Loeschen; DISM braucht mehrere Minuten
            { std::lock_guard<std::mutex> lk(g_jobMx); g_curName = Tr(L"Windows-Komponentenspeicher, Schattenkopien und Ereignisprotokolle – das kann bis zu einer halben Stunde dauern, Tidy bitte offen lassen …"); }
            PostMessageW(h, WM_CLEAN_TICK, 0, 0);
            g_ultraStart = GetTickCount64();
            g_ultraResult = RunUltraActions(um, h);
            g_ultraStart = 0;
            g_ultraDone = um;
        }
        PostMessageW(h, WM_CLEAN_DONE, all ? 1 : 0, (LPARAM)cat);
    });
}

// Ultra-Tiefenreinigung, Vorschau (Dry Run): pro Gruppe Anzahl und Groesse der Eintraege, die geloescht wuerden
static std::wstring DeepPreview(const std::vector<fs::path>& sys) {
    int cnt[kMaxGroups]{};
    unsigned long long bytes[kMaxGroups]{};
    std::vector<std::pair<int, fs::path>> misses;
    {
        std::lock_guard<std::mutex> lk(g_statMx);
        for (const auto& p : sys) {
            const int g = GroupOf(CAT_SYS, p);
            if (g < 0 || g >= kMaxGroups) continue;
            ++cnt[g];
            auto it = g_sizeCache.find(p.wstring());
            if (it != g_sizeCache.end()) bytes[g] += it->second;
            else misses.push_back({g, p});
        }
    }
    for (const auto& m : misses) bytes[m.first] += PathSize(m.second);  // nicht vermessene Eintraege nachmessen
    std::wstring out;
    for (int g = 0; g < kMaxGroups; ++g)
        if (cnt[g]) out += L"\n   · " + std::wstring(GroupName(CAT_SYS, g)) + L": " + std::to_wstring(cnt[g]) + L" (" + FormatSize(bytes[g]) + L")";
    return out;
}

// Vorschau fuer die Rueckfrage: die groessten Eintraege einer Liste (Name und Groesse), der Rest als Anzahl. Bisher nicht vermessene Eintraege
// werden (hoechstens 60) nachgemessen und im Cache abgelegt, damit die Reihenfolge stimmt.
static std::wstring TopPreview(const std::vector<fs::path>& items, int cat, size_t n = 3) {
    if (items.empty()) return L"";
    std::vector<std::pair<unsigned long long, size_t>> sz;
    std::vector<size_t> misses;
    {
        std::lock_guard<std::mutex> lk(g_statMx);
        for (size_t i = 0; i < items.size(); ++i) {
            auto it = g_sizeCache.find(items[i].wstring());
            sz.push_back({it != g_sizeCache.end() ? it->second : 0, i});
            if (it == g_sizeCache.end() && misses.size() < 60) misses.push_back(i);
        }
    }
    for (size_t i : misses) {
        const unsigned long long b = PathSize(items[i]);
        sz[i].first = b;
        std::lock_guard<std::mutex> lk(g_statMx);
        g_sizeCache[items[i].wstring()] = b;
    }
    std::sort(sz.begin(), sz.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::wstring out;
    for (size_t k = 0; k < sz.size() && k < n; ++k) {
        out += L"\n      – " + ItemLabel(cat, items[sz[k].second]);
        if (sz[k].first) out += L" (" + FormatSize(sz[k].first) + L")";
    }
    if (sz.size() > n) out += std::wstring(L"\n      … ") + Tr(L"und ") + std::to_wstring(sz.size() - n) + Tr(L" weitere");
    return out;
}

// Symbol- und Vorschau-Cache liegen im Explorer in Benutzung: nur mit kurzem Explorer-Neustart loeschbar. Ja = neu starten, Nein = diese Caches auslassen.
static void AskExplorerReset(HWND h, std::vector<fs::path>& items) {
    g_restartExplorer = false;
    if (!std::any_of(items.begin(), items.end(), [](const fs::path& p) { return IsExplorerCacheFile(p); })) return;
    if (AppBox(h, Tr(L"Symbol- und Vorschau-Cache des Explorers lassen sich nur löschen, wenn der Windows-Explorer dabei kurz beendet wird (Ordnerfenster und Taskleiste verschwinden für einen Moment und kommen von selbst zurück).\n\nExplorer dafür kurz neu starten?\n\nNein = diese Caches diesmal auslassen."),
               Tr(L"Ultra-Tiefenreinigung"), MB_YESNO | MB_ICONQUESTION) == IDYES) {
        g_restartExplorer = true;
        return;
    }
    items.erase(std::remove_if(items.begin(), items.end(), [](const fs::path& p) { return IsExplorerCacheFile(p); }), items.end());
}

// Ultra-Aktionen: jede wird einzeln erklaert und bestaetigt (Ja = ausfuehren, Nein/Esc = auslassen). Rueckgabe: Maske der bestaetigten Aktionen
static int AskUltraActions(HWND h) {
    struct Q { int bit; const wchar_t* text; };
    static const Q kQ[] = {
        {ULTRA_DISM, L"Windows-Komponentenspeicher (WinSxS) bereinigen?\n\nStartet DISM mit /StartComponentCleanup /ResetBase und entfernt ersetzte Komponentenversionen. Das kann von einigen Minuten bis zu einer halben Stunde dauern; Tidy bitte so lange geöffnet lassen. Danach lassen sich bereits installierte Windows-Updates NICHT mehr deinstallieren. Nicht rückgängig zu machen.\n\nWindows fragt dafür einmal nach Administratorrechten."},
        {ULTRA_VSS, L"Alte Schattenkopien löschen?\n\nLöscht auf jedem Laufwerk alle Schattenkopien (Wiederherstellungspunkte, „Vorgängerversionen“) außer der neuesten. Ältere Wiederherstellungspunkte sind danach weg. Nicht rückgängig zu machen.\n\nBenötigt Administratorrechte."},
        {ULTRA_EVT, L"Windows-Ereignisprotokolle leeren?\n\nLeert alle Ereignisprotokolle (Anwendung, System, Setup …); das Sicherheitsprotokoll bleibt unberührt. Die Einträge helfen bei der Fehlersuche und sind danach weg. Nicht rückgängig zu machen.\n\nBenötigt Administratorrechte."},
        {ULTRA_WINOLD, L"Vorherige Windows-Installation (Windows.old) löschen?\n\nLöscht den Ordner Windows.old. Er enthält die Dateien der alten Windows-Version und braucht oft 10 bis 30 GB. Danach ist ein Zurückgehen auf die vorherige Windows-Version NICHT mehr möglich. Windows löscht den Ordner nach einigen Tagen auch selbst. Nicht rückgängig zu machen.\n\nBenötigt Administratorrechte."}};
    int mask = 0;
    for (const Q& q : kQ) {
        if (q.bit == ULTRA_WINOLD && WinOldPath().empty()) continue;  // nur anbieten, wenn der Ordner existiert
        if (AppBox(h, Tr(q.text), Tr(L"Ultra-Tiefenreinigung"), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) mask |= q.bit;   // Nein, Esc oder X = auslassen
    }
    return mask;
}

// Schliesst einen Browser freundlich (WM_CLOSE an seine Fenster, wie ein Klick auf das X) und wartet bis zu 8 Sekunden, bis er beendet ist.
// Nichts wird erzwungen: Laeuft ein Hintergrundprozess weiter, bleibt er bestehen.
static void CloseBrowserGracefully(const wchar_t* exe) {
    std::vector<DWORD> pids;
    if (HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); s != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{sizeof pe};
        for (BOOL ok = Process32FirstW(s, &pe); ok; ok = Process32NextW(s, &pe))
            if (_wcsicmp(pe.szExeFile, exe) == 0) pids.push_back(pe.th32ProcessID);
        CloseHandle(s);
    }
    struct Ctx { const std::vector<DWORD>* pids; } ctx{&pids};
    EnumWindows([](HWND w, LPARAM l) -> BOOL {
        auto* c = (Ctx*)l;
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        if (IsWindowVisible(w) && std::find(c->pids->begin(), c->pids->end(), pid) != c->pids->end()) PostMessageW(w, WM_CLOSE, 0, 0);
        return TRUE;
    }, (LPARAM)&ctx);
    for (int i = 0; i < 32 && IsRunning(exe); ++i) Sleep(250);
}
static int g_mbExtraH = 0;  // zusaetzliche Hoehe (px) fuer das naechste AppBox-Fenster, z. B. wenn die letzte Zeile sonst abgeschnitten wird
static int ShowBrowserPrompt(HWND owner, const std::wstring& names);
static void DoClean(HWND h) {
    if (g_page == 0) {  // Optimieren: Temp und Systemdateien
        if (!g_scanned) {
            AppBox(h, Tr(L"Die Analyse läuft noch. Bitte einen Moment warten."), kApp, MB_OK | MB_ICONINFORMATION);
            return;
        }
        CatStat t = Stat(CAT_TEMP), s = Stat(CAT_SYS), d = Stat(CAT_DL);
        if (t.count + s.count + d.count == 0) {
            AppBox(h, Tr(L"Es gibt nichts zu bereinigen."), kApp, MB_OK | MB_ICONINFORMATION);
            return;
        }
        auto items = ListItems(CAT_TEMP);
        auto sys = ListItems(CAT_SYS, DeepOn());
        auto dl = ListItems(CAT_DL);
        std::wstring q = Tr(L"Folgendes wird bereinigt:\n\n • Downloads: ") + std::to_wstring(d.count) + L" (" + FormatSize(d.bytes) +
                         Tr(L") – landen im Papierkorb") + TopPreview(dl, CAT_DL) + Tr(L"\n • Temp-Dateien: ") + std::to_wstring(t.count) + L" (" + FormatSize(t.bytes) +
                         Tr(L") – endgültig") + TopPreview(items, CAT_TEMP) + Tr(L"\n • Systemdateien: ") + std::to_wstring(s.count) + L" (" + FormatSize(s.bytes) +
                         Tr(L") – endgültig") + (DeepOn() ? std::wstring() : TopPreview(sys, CAT_SYS)) +
                         Tr(L"\n\nGeschützte Dateien (Schutzliste) bleiben erhalten. Temp- und Systemdateien lassen sich nicht wiederherstellen.");
        if (DeepOn()) q += Tr(L"\n\nUltra-Tiefenreinigung – Systemdateien im Detail:") + DeepPreview(ListItems(CAT_SYS, true));
        g_mbExtraH = 40;   // die Rueckfrage ist lang: etwas Luft, damit die letzte Zeile bei anderer Schrift/Sprache nicht abgeschnitten wird
        if (AppBox(h, q.c_str(), Tr(L"Optimieren"), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
        if (DeepOn()) AskExplorerReset(h, sys);
        items.insert(items.end(), sys.begin(), sys.end());
        g_optDl = !dl.empty();
        StartClean(h, std::move(items), true, false, -1, std::move(dl));
        return;
    }
    if (g_page == kAutoPage) {
        int failed = 0, changed = ApplyAutostart(failed);
        std::wstring msg = changed ? std::to_wstring(changed) + Tr(L" Änderung(en) gespeichert. Sie gelten ab dem nächsten Start von Windows.")
                                   : std::wstring(Tr(L"Es gab keine Änderungen."));
        if (failed) msg += L"\n\n" + std::to_wstring(failed) + Tr(L" Eintrag/Einträge konnten nicht geändert werden.");
        InvalidateRect(g_alist, nullptr, FALSE);
        InvalidateRect(h, nullptr, FALSE);
        AppBox(h, msg.c_str(), kApp, MB_OK | (failed ? MB_ICONWARNING : MB_ICONINFORMATION));
        return;
    }
    if (g_page == kBrowserPage) {
        if (g_browLoading) {
            AppBox(h, Tr(L"Die Analyse läuft noch. Bitte einen Moment warten."), kApp, MB_OK | MB_ICONINFORMATION);
            return;
        }
        std::vector<fs::path> picked;
        std::wstring names, running;
        std::vector<int> runIdx;  // gewaehlte Browser, die gerade laufen (live geprueft, nicht vom letzten Scan)
        bool anyHistory = false, anyCookies = false;
        for (int b = 0; b < (int)g_brow.size(); ++b) {
            const auto& e = g_brow[b];
            if (!e.avail || !e.want) continue;
            picked.insert(picked.end(), e.paths.begin(), e.paths.end());
            names += L"\n • " + e.name + L" (" + FormatSize(e.bytes) + L")";
            anyHistory = anyHistory || e.bkind == 1;
            anyCookies = anyCookies || e.bkind == 2;
            const int bi = e.bidx;
            if (bi >= 0 && bi < kBrowsers && IsRunning(kBrowserExe[bi]) && std::find(runIdx.begin(), runIdx.end(), bi) == runIdx.end()) {
                running += (running.empty() ? L"" : L", ") + std::wstring(kBrowserName[bi]);
                runIdx.push_back(bi);
            }
        }
        if (picked.empty()) {
            AppBox(h, Tr(L"Es ist kein Browser ausgewählt."), kApp, MB_OK | MB_ICONINFORMATION);
            return;
        }
        if (!runIdx.empty()) {  // Laufende Browser sperren ihre Cache-Dateien: vorher anbieten, sie zu schliessen
            const int a = ShowBrowserPrompt(h, running);
            if (a == IDCANCEL) return;
            if (a == IDYES) {
                SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32514)));
                for (int b : runIdx) CloseBrowserGracefully(kBrowserExe[b]);
                SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
                running.clear();  // nur die, die sich nicht schliessen liessen (z. B. Hintergrundprozess)
                for (int b : runIdx)
                    if (IsRunning(kBrowserExe[b])) running += (running.empty() ? L"" : L", ") + std::wstring(kBrowserName[b]);
            }
        }
        std::wstring q = (anyHistory || anyCookies) ? std::wstring(Tr(L"Diese Browser-Daten werden ENDGÜLTIG gelöscht:\n")) + names
                               : std::wstring(Tr(L"Der Cache folgender Browser wird ENDGÜLTIG gelöscht:\n")) + names;
        if (anyCookies) q += Tr(L"\n\nCookies: Du wirst bei Webseiten abgemeldet und musst dich neu anmelden.");
        if (anyHistory) q += Tr(L"\n\nVerlauf: Besuchte Seiten, Favoriten-Symbole und Top-Sites verschwinden.");
        q += (anyHistory || anyCookies) ? std::wstring(Tr(L"\n\nPasswörter und Lesezeichen bleiben erhalten.")) : std::wstring(Tr(L"\n\nCookies, Passwörter, Verlauf und Lesezeichen bleiben erhalten."));
        if (!running.empty()) q += Tr(L"\n\nHinweis: ") + running + Tr(L" läuft noch. Benutzte Dateien bleiben dann erhalten; am besten vorher schließen.");
        if (AppBox(h, q.c_str(), kApp, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
        StartClean(h, std::move(picked), true, false, CAT_BROWSER);
        return;
    }
    if (g_listLoading) {
        AppBox(h, Tr(L"Die Analyse l\u00E4uft noch. Bitte einen Moment warten."), kApp, MB_OK | MB_ICONINFORMATION);
        return;
    }
    int cat = PageCat(g_page);
    std::vector<fs::path> chosen;
    for (int i = 0; i < (int)g_items.size(); ++i) {
        bool on = false;  // Kacheln: der Schalter der Gruppe gilt fuer alle ihre Eintraege
        for (const auto& e : g_grp) if (e.level == g_itemGroup[i]) on = e.want;
        if (on) chosen.push_back(g_items[i]);
    }
    const bool ultra = cat == CAT_SYS && UltraOn();  // Ultra: zusaetzlich DISM, Schattenkopien, Ereignisprotokolle (auch ohne Dateien)
    if (chosen.empty() && !ultra) {
        AppBox(h, Tr(L"Es ist nichts ausgewählt."), kApp, MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (cat == CAT_SYS && DeepOn()) {  // Ultra-Tiefenreinigung: erst die Vorschau je Gruppe, dann loeschen
        if (!chosen.empty()) {
            const std::wstring prev = Tr(L"Ultra-Tiefenreinigung – diese Systemdateien werden ENDGÜLTIG gelöscht:") + DeepPreview(chosen) +
                                      Tr(L"\n\nGeschützte Dateien (Schutzliste) und Systemordner bleiben erhalten; benutzte Dateien werden übersprungen. Nicht wiederherstellbar.");
            if (AppBox(h, prev.c_str(), Tr(L"Ultra-Tiefenreinigung"), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
            AskExplorerReset(h, chosen);
        }
        const int um = ultra ? AskUltraActions(h) : 0;
        if (chosen.empty() && um == 0) {
            AppBox(h, Tr(L"Es ist nichts ausgewählt."), kApp, MB_OK | MB_ICONINFORMATION);
            return;
        }
        g_ultraMask = um;
        StartClean(h, std::move(chosen), true, false, cat);
        return;
    }
    wchar_t q[256];  // Satzbau ist je Sprache verschieden: ganzer Satz als Format
    swprintf_s(q, cat == CAT_DL ? Tr(L"%d von %d Einträgen in den Papierkorb verschieben?") : Tr(L"%d von %d Einträgen ENDGÜLTIG löschen? (nicht wiederherstellbar)"),
               (int)chosen.size(), (int)g_items.size());

    if (AppBox(h, q, kApp, MB_YESNO | (cat == CAT_DL ? MB_ICONQUESTION : MB_ICONWARNING)) != IDYES) return;
    bool all = chosen.size() >= g_items.size();
    StartClean(h, std::move(chosen), cat != CAT_DL, all, cat);  // Ergebnis kommt per WM_CLEAN_DONE
}

// Info-Text des Symbols neben dem Seitentitel (Tooltip und Klick)

// Ausfuehrlicher Text (Klick aufs Info-Symbol)
static const wchar_t* PageInfo() {
    const int idx = g_page == 1 ? 0 : g_page == 2 ? 1 : g_page == 3 ? 2 : g_page == kBrowserPage ? 3 : g_page == kDrivePage ? 4 : g_page == kUninstPage ? 6 : 5;
    const int lang = g_lang;
    return kInfoTexts[idx][lang >= 0 && lang < kLangCount ? lang : 0];
}
// Kurzer Text (Mouseover)
static const wchar_t* PageTip() {
    switch (g_page) {
    case 1: return Tr(L"Dateien in deinem Downloads-Ordner. Beim Löschen landen sie im Papierkorb.\nKlick für die genaue Übersicht.");
    case 2: return Tr(L"Temporäre Dateien von Programmen. Werden endgültig gelöscht.\nKlick für die genaue Übersicht.");
    case 3: return Tr(L"Vorschaubild-Cache und Absturzberichte von Windows.\nKlick für die genaue Übersicht.");
    case kBrowserPage: return Tr(L"Zwischenspeicher (Cache) deiner Browser. Keine Passwörter oder Cookies.\nKlick für die genaue Übersicht.");
    case kDrivePage: return Tr(L"Belegung und Zustand deiner Laufwerke. Nur Anzeige.\nKlick für die genaue Übersicht.");
    case kUninstPage: return Tr(L"Installierte Programme. Deinstallieren startet den Uninstaller des Programms.\nKlick für die genaue Übersicht.");
    default: return Tr(L"Programme, die mit Windows starten. Hier ein- und ausschaltbar.\nKlick für die genaue Übersicht.");
    }
}

// Kopf der Dialoge: Farbverlauf und Logo (iconX < 0 = mittig); danach ist der DC wieder logisch skaliert
// App-Symbol in einer Pixelgroesse; pro Groesse nur einmal geladen, nicht bei jedem Neuzeichnen (auch der Splash zeichnet ~60x pro Sekunde)
static HICON AppIcon(int px) {
    static std::unordered_map<int, HICON> cache;
    auto it = cache.find(px);
    if (it != cache.end()) return it->second;
    HICON ic = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, px, px, 0);
    cache[px] = ic;
    return ic;
}

static void DrawDlgHeader(HDC dc, int W, int iconX = 28) {
    {
        int saved = SaveDC(dc);
        ResetLogical(dc);
        Gdiplus::Graphics g(dc);
        g.ScaleTransform((Gdiplus::REAL)g_ui, (Gdiplus::REAL)g_ui);
        Gdiplus::LinearGradientBrush gb(Gdiplus::RectF(0, 0, (Gdiplus::REAL)W, 120), Gdiplus::Color(255, 112, 72, 240),
                                        Gdiplus::Color(255, 24, 168, 232), Gdiplus::LinearGradientModeHorizontal);
        g.FillRectangle(&gb, 0.f, 0.f, (Gdiplus::REAL)W, 120.f);
        RestoreDC(dc, saved);
    }
    SetLogical(dc);
    if (HICON ic = AppIcon(D(64))) {
        ResetLogical(dc);
        DrawIconEx(dc, D(iconX < 0 ? (W - 64) / 2 : iconX), D(28), ic, D(64), D(64), 0, nullptr, DI_NORMAL);
        SetLogical(dc);
    }
}

// Zeigt ein Fenster modal mittig ueber owner und laeuft, bis es sich schliesst (open wird dabei vom Fenster selbst zurueckgesetzt).
// Die Fensterklasse wird beim ersten Aufruf registriert; ein erneutes Registrieren schlaegt harmlos fehl.
static bool ShowModal(HWND owner, const wchar_t* cls, WNDPROC proc, const wchar_t* title, int lw, int lh, bool& open) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = cls;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hIcon = g_logo;
    RegisterClassW(&wc);
    RECT r{0, 0, D(lw), D(lh)};
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRect(&r, style, FALSE);
    const bool free = !owner || !IsWindowVisible(owner);  // kein sichtbares Hauptfenster (Start, Tray, Kommandozeile): mittig auf dem Bildschirm, immer oben
    RECT o;
    if (free) SystemParametersInfoW(SPI_GETWORKAREA, 0, &o, 0);
    else GetWindowRect(owner, &o);
    const int w = r.right - r.left, hgt = r.bottom - r.top;
    open = true;
    HWND d = CreateWindowExW(WS_EX_DLGMODALFRAME | (free ? WS_EX_TOPMOST : 0), cls, title, style, o.left + ((o.right - o.left) - w) / 2,
                             o.top + ((o.bottom - o.top) - hgt) / 2, w, hgt, free ? nullptr : owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!d) { open = false; return false; }
    if (!free) EnableWindow(owner, FALSE);
    ShowWindow(d, SW_SHOW);
    if (free) SetForegroundWindow(d);
    MSG msg{};
    BOOL got = 0;
    while (open && (got = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && GetAncestor(msg.hwnd, GA_ROOT) == d) { DestroyWindow(d); continue; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (got == 0) {  // WM_QUIT: Fenster schliessen und die Beenden-Meldung weitergeben, sonst bliebe das Programm offen
        if (open) DestroyWindow(d);
        PostQuitMessage((int)msg.wParam);
    }
    if (!free) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    return true;
}

// ---- Ueber-Fenster ----

// ---- Update- und Neuigkeiten-Fenster (statt eines schlichten Meldungsfensters) ----
constexpr int ID_UD_OK = 240, ID_UD_NO = 241;
struct UpdDlg {
    std::wstring title, sub, notes, hint, heading;
    const wchar_t *okText = L"", *noText = nullptr;
    bool result = false, open = false;
};
static UpdDlg g_ud;

// "- Punkt" je Zeile wird zu "•  Punkt" mit Leerzeile dazwischen
static std::wstring NotesToBullets(const std::wstring& notes) {
    std::wstring out, line;
    auto flush = [&] {
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
        size_t s = line.find_first_not_of(L" \t");
        line = s == std::wstring::npos ? L"" : line.substr(s);
        if (!line.empty() && (line[0] == L'-' || line[0] == L'*')) {
            size_t t = line.find_first_not_of(L"-* \t");
            line = t == std::wstring::npos ? L"" : line.substr(t);
        }
        if (!line.empty()) {
            if (!out.empty()) out += L"\r\n";
            out += L"•  " + line;
        }
        line.clear();
    };
    for (wchar_t c : notes) {
        if (c == L'\n') flush();
        else line.push_back(c);
    }
    flush();
    return out;
}

static LRESULT CALLBACK UpdDlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    constexpr int kW = 500, kH = 520;
    switch (m) {
    case WM_CREATE: {
        BOOL dark = g_dark;
        DwmSetWindowAttribute(h, 20, &dark, sizeof dark);
        const int by = kH - 74;
        auto btn = [&](int id, const wchar_t* text, int x, int bw) {
            HWND b = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, D(x), D(by), D(bw), D(46), h, (HMENU)(INT_PTR)id, nullptr, nullptr);
            SetWindowSubclass(b, ButtonProc, (UINT_PTR)id, 0);
        };
        if (g_ud.noText) {
            btn(ID_UD_NO, g_ud.noText, 28, 150);
            btn(ID_UD_OK, g_ud.okText, kW - 28 - 270, 270);
        } else {
            btn(ID_UD_OK, g_ud.okText, (kW - 270) / 2, 270);
        }
        HWND ed = CreateWindowW(L"EDIT", NotesToBullets(g_ud.notes).c_str(), WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                D(44), D(188), D(kW - 88), D(kH - 188 - 168), h, nullptr, nullptr, nullptr);
        SendMessageW(ed, WM_SETFONT, (WPARAM)g_fList, TRUE);
        SendMessageW(ed, EM_SETSEL, 0, 0);
        SetWindowTheme(ed, ThemeName(), nullptr);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC screen = BeginPaint(h, &ps);
        RECT cr;
        GetClientRect(h, &cr);
        HDC dc = CreateCompatibleDC(screen);
        HBITMAP bmp = CreateCompatibleBitmap(screen, cr.right, cr.bottom);
        HGDIOBJ old = SelectObject(dc, bmp);
        FillRect(dc, &cr, g_bgBrush);
        SetBkMode(dc, TRANSPARENT);
        const int W = (int)std::ceil(cr.right / g_ui);
        DrawDlgHeader(dc, W);
        DrawFit(dc, g_fBig, RGB(255, 255, 255), 112, 26, W - 24, g_ud.title);
        DrawFit(dc, g_fMed, kHdrSub, 112, 72, W - 24, g_ud.sub);
        DrawTxt(dc, g_fMed, kText, 28, 140, g_ud.heading);
        RECT card{28, 174, W - 28, kH - 150};
        FillRound(dc, card, kCard, 16);
        RECT hr{30, kH - 138, W - 30, kH - 84};  // Hinweis darf auf zwei Zeilen umbrechen
        SelectObject(dc, g_fSm);
        SetTextColor(dc, kMuted);
        DrawTextW(dc, g_ud.hint.c_str(), -1, &hr, DT_WORDBREAK | DT_NOPREFIX);
        ResetLogical(dc);
        BitBlt(screen, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old);
        DeleteObject(bmp);
        DeleteDC(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC:  // das schreibgeschuetzte Textfeld meldet sich so
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        SetTextColor(dc, kText);
        SetBkColor(dc, kCard);
        return (LRESULT)g_cardBrush;
    }
    case WM_DRAWITEM:
        DrawButton((const DRAWITEMSTRUCT*)l);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(w) == ID_UD_OK) { g_ud.result = true; DestroyWindow(h); }
        else if (LOWORD(w) == ID_UD_NO) DestroyWindow(h);
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        g_ud.open = false;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// Zeigt das Fenster modal. Rueckgabe: true, wenn der "OK"-Knopf gedrueckt wurde.
static bool ShowUpdateDialog(HWND owner, const std::wstring& title, const std::wstring& sub, const std::wstring& notes,
                             const std::wstring& hint, const wchar_t* okText, const wchar_t* noText, const wchar_t* heading = nullptr) {
    if (g_ud.open) return false;
    g_ud.title = title;
    g_ud.sub = sub;
    g_ud.notes = notes;
    g_ud.hint = hint;
    g_ud.okText = okText;
    g_ud.noText = noText;
    g_ud.heading = heading ? heading : Tr(L"Das ist neu");
    g_ud.result = false;
    ShowModal(owner, L"DuncansTidyUpdDlg", UpdDlgProc, title.c_str(), 500, 520, g_ud.open);
    return g_ud.result;
}

// ---- Fenster "Groesste Dateien": zweizeilige Karten (Name gross, Pfad klein), Klick zeigt die Datei im Explorer ----
constexpr int ID_TOP_OK = 242, ID_TOP_MODE = 243, kTopRow = 62, kTopGap = 10;
static bool g_topOpen = false;
static HWND g_topListWnd;
static int g_topScroll = 0, g_topHover = -1;

static void UpdateTopScroll() {
    if (g_topListWnd) SyncScroll(g_topListWnd, (int)g_topList.size() * D(kTopRow + kTopGap), g_topScroll);
}

static int TopRowAt(int y) {
    const int step = D(kTopRow + kTopGap);
    const int row = (y + g_topScroll) / step;
    if (y < 0 || (y + g_topScroll) % step >= D(kTopRow) || row < 0 || row >= (int)g_topList.size()) return -1;
    return row;
}

// Zeigt die Datei markiert im Windows-Explorer (explorer.exe liegt im Windows-Ordner, nicht in System32)
static void ShowInExplorer(const std::wstring& full) {
    wchar_t wd[MAX_PATH];
    const UINT n = GetWindowsDirectoryW(wd, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    const std::wstring exe = std::wstring(wd) + L"\\explorer.exe";
    const std::wstring args = L"/select,\"" + full + L"\"";
    ShellExecuteW(nullptr, L"open", exe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL);
}

static LRESULT CALLBACK TopListProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    const int step = D(kTopRow + kTopGap);
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        UpdateTopScroll();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC wdc = BeginPaint(h, &ps);
        RECT cr;
        GetClientRect(h, &cr);
        HDC dc = CreateCompatibleDC(wdc);
        HBITMAP bm = CreateCompatibleBitmap(wdc, cr.right, cr.bottom);
        HGDIOBJ ob = SelectObject(dc, bm);
        FillRect(dc, &cr, g_bgBrush);
        SetBkMode(dc, TRANSPARENT);
        SetLogical(dc);
        const int W = (int)std::ceil(cr.right / g_ui);
        const double scrollL = g_topScroll / g_ui;
        const int n = (int)g_topList.size();
        for (int i = std::max(0, (int)(scrollL / (kTopRow + kTopGap))); i < n; ++i) {
            const int top = (int)std::lround(i * (kTopRow + kTopGap) - scrollL);
            if (top * g_ui > cr.bottom) break;
            const TopEntry& e = g_topList[i];
            RECT card{0, top, W - 2, top + kTopRow};
            FillRound(dc, card, g_topHover == i ? kCardHot : kCard, 16);
            SelectObject(dc, g_fMed);
            SetTextColor(dc, kSub);
            RECT sr{W - 2 - 130, top + 10, W - 2 - 18, top + 36};
            DrawTextW(dc, FormatSize(e.size).c_str(), -1, &sr, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
            DrawFit(dc, g_fMed, kStrong, 20, top + 10, W - 2 - 140, e.name);
            std::wstring dirLine = e.dir;
            if (g_topForgotten && (e.mt.dwLowDateTime || e.mt.dwHighDateTime)) {   // wann zuletzt geaendert
                SYSTEMTIME st{};
                FILETIME lf;
                FileTimeToLocalFileTime(&e.mt, &lf);
                if (FileTimeToSystemTime(&lf, &st)) {
                    wchar_t d[48];
                    swprintf_s(d, L"%02d.%02d.%04d", (int)st.wDay, (int)st.wMonth, (int)st.wYear);
                    dirLine = std::wstring(Tr(L"zuletzt geändert ")) + d + L"  ·  " + e.dir;
                }
            }
            DrawFit(dc, g_fSm, kMuted, 20, top + 36, W - 2 - 20, dirLine);
        }
        ResetLogical(dc);
        BitBlt(wdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, ob);
        DeleteObject(bm);
        DeleteDC(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_VSCROLL:
        g_topScroll = ScrollPosFrom(h, w, step);
        UpdateTopScroll();
        return 0;
    case WM_MOUSEWHEEL:
        g_topScroll -= GET_WHEEL_DELTA_WPARAM(w) * step / 120;
        UpdateTopScroll();
        return 0;
    case WM_MOUSEMOVE: {
        const int row = TopRowAt((short)HIWORD(l));
        if (row != g_topHover) {
            g_topHover = row;
            InvalidateRect(h, nullptr, FALSE);
        }
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE:
        g_topHover = -1;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (g_topHover >= 0 && LOWORD(l) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649))); return TRUE; }
        break;
    case WM_LBUTTONDOWN:
        if (g_topHover >= 0 && g_topHover < (int)g_topList.size()) ShowInExplorer(g_topList[g_topHover].full);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static LRESULT CALLBACK TopDlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    constexpr int kW = 640, kH = 640;
    switch (m) {
    case WM_CREATE: {
        BOOL dark = g_dark;
        DwmSetWindowAttribute(h, 20, &dark, sizeof dark);
        g_topScroll = 0;
        g_topHover = -1;
        g_topListWnd = CreateWindowW(L"TidyTopList", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL, D(28), D(140), D(kW - 56), D(kH - 140 - 100), h, nullptr,
                                     GetModuleHandleW(nullptr), nullptr);
        SetWindowTheme(g_topListWnd, ThemeName(), nullptr);
        HWND b = CreateWindowW(L"BUTTON", Tr(L"Schließen"), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, D(kW / 2 + 8), D(kH - 62), D(180), D(46), h,
                               (HMENU)(INT_PTR)ID_TOP_OK, nullptr, nullptr);
        SetWindowSubclass(b, ButtonProc, (UINT_PTR)ID_TOP_OK, 0);
        HWND bm = CreateWindowW(L"BUTTON", g_topForgotten ? Tr(L"Alle größten zeigen") : Tr(L"Vergessene suchen (über 1 Jahr)"), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                D(kW / 2 - 8 - 300), D(kH - 62), D(300), D(46), h, (HMENU)(INT_PTR)ID_TOP_MODE, nullptr, nullptr);
        SetWindowSubclass(bm, ButtonProc, (UINT_PTR)ID_TOP_MODE, 0);
        UpdateTopScroll();
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC screen = BeginPaint(h, &ps);
        RECT cr;
        GetClientRect(h, &cr);
        HDC dc = CreateCompatibleDC(screen);
        HBITMAP bmp = CreateCompatibleBitmap(screen, cr.right, cr.bottom);
        HGDIOBJ old = SelectObject(dc, bmp);
        FillRect(dc, &cr, g_bgBrush);
        SetBkMode(dc, TRANSPARENT);
        const int W = (int)std::ceil(cr.right / g_ui);
        DrawDlgHeader(dc, W);
        std::wstring title, sub;
        { std::lock_guard<std::mutex> lk(g_topMx); title = g_topTitle; sub = g_topSub; }
        DrawFit(dc, g_fBig, RGB(255, 255, 255), 112, 26, W - 24, title);
        DrawFit(dc, g_fMed, kHdrSub, 112, 72, W - 24, sub);
        DrawFit(dc, g_fSm, kMuted, 30, kH - 98, W - 30, Tr(L"Klick auf einen Eintrag zeigt die Datei im Explorer. Es wird nichts gelöscht."));
        ResetLogical(dc);
        BitBlt(screen, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old);
        DeleteObject(bmp);
        DeleteDC(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DRAWITEM:
        DrawButton((const DRAWITEMSTRUCT*)l);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(w) == ID_TOP_OK) DestroyWindow(h);
        else if (LOWORD(w) == ID_TOP_MODE && !g_topBusy) {   // zwischen "groesste Dateien" und "gross und lange unberuehrt" wechseln: Fenster zu, neue Suche, Ergebnis kommt wieder per WM_TOP_DONE
            const std::wstring root = g_topRoot, label = g_topLabel;
            const bool next = !g_topForgotten;
            DestroyWindow(h);
            StartTopFilesAt(g_main, root, label, next);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        g_topOpen = false;
        g_topListWnd = nullptr;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void ShowTopDialog(HWND owner) {
    if (g_topOpen) return;
    WNDCLASSW lc{};
    lc.lpfnWndProc = TopListProc;
    lc.hInstance = GetModuleHandleW(nullptr);
    lc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    lc.lpszClassName = L"TidyTopList";
    RegisterClassW(&lc);
    std::wstring title;
    { std::lock_guard<std::mutex> lk(g_topMx); title = g_topTitle; }
    ShowModal(owner, L"DuncansTidyTopDlg", TopDlgProc, title.c_str(), 640, 640, g_topOpen);
}

// ---- Schutzliste bearbeiten: Fenster mit Eingabefeld und Liste statt Texteditor ----
// Die Datei schutzliste.txt bleibt das Speicherformat (eine Zeile pro Eintrag); das Fenster liest und schreibt sie.
constexpr int ID_WL_ADD = 250, ID_WL_PICK = 251, ID_WL_OK = 252, kWlRow = 48, kWlGap = 8;
static std::wstring ShowPickDialog(HWND owner);
static std::vector<std::wstring> g_wlItems;
static HWND g_wlListWnd, g_wlEdit;
static int g_wlScroll = 0, g_wlHover = -1;
static bool g_wlOpen = false, g_wlHoverDel = false;

static void SaveWhitelist(const std::vector<std::wstring>& v) {
    std::ofstream f(WhitelistFile(), std::ios::binary | std::ios::trunc);
    f << "# Schutzliste: Diese Eintraege werden NIE geloescht.\n"
         "# Pro Zeile ein Dateiname, * als Platzhalter ist erlaubt (z.B. *.pdf oder Rechnung*).\n"
         "# Die Liste wird in Duncan's Tidy unter \"Schutzliste\" bearbeitet.\n";
    for (const auto& w : v) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string u(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), u.data(), n, nullptr, nullptr);
        f << u << "\n";
    }
}

static void UpdateWlScroll() {
    if (g_wlListWnd) SyncScroll(g_wlListWnd, (int)g_wlItems.size() * D(kWlRow + kWlGap), g_wlScroll);
}

static int WlRowAt(int y) {
    const int step = D(kWlRow + kWlGap);
    const int row = (y + g_wlScroll) / step;
    if (y < 0 || (y + g_wlScroll) % step >= D(kWlRow) || row < 0 || row >= (int)g_wlItems.size()) return -1;
    return row;
}

// Neuen Eintrag pruefen und aufnehmen. Es zaehlt nur der Dateiname, deshalb keine Pfade.
static void AddWhitelistEntry(HWND dlg, std::wstring s) {
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t')) s.pop_back();
    const size_t st = s.find_first_not_of(L" \t");
    s = st == std::wstring::npos ? L"" : s.substr(st);
    if (s.empty()) return;
    if (s.find_first_of(L"\\/") != std::wstring::npos) {
        AppBox(dlg, Tr(L"Bitte nur einen Dateinamen oder ein Muster eintragen, keinen Pfad (z. B. Rechnung.pdf oder *.pdf)."), kApp, MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (s[0] == L'#') s = L"?" + s.substr(1);  // Zeilen mit # am Anfang waeren in der Datei Kommentare
    for (const auto& e : g_wlItems)
        if (_wcsicmp(e.c_str(), s.c_str()) == 0) { SetWindowTextW(g_wlEdit, L""); return; }  // schon vorhanden
    g_wlItems.push_back(s);
    SaveWhitelist(g_wlItems);
    SetWindowTextW(g_wlEdit, L"");
    g_wlScroll = 1 << 20;  // ans Ende, damit der neue Eintrag sichtbar ist
    UpdateWlScroll();
    InvalidateRect(dlg, nullptr, FALSE);
}

static LRESULT CALLBACK WlListProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    const int step = D(kWlRow + kWlGap);
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        UpdateWlScroll();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC wdc = BeginPaint(h, &ps);
        RECT cr;
        GetClientRect(h, &cr);
        HDC dc = CreateCompatibleDC(wdc);
        HBITMAP bm = CreateCompatibleBitmap(wdc, cr.right, cr.bottom);
        HGDIOBJ ob = SelectObject(dc, bm);
        FillRect(dc, &cr, g_bgBrush);
        SetBkMode(dc, TRANSPARENT);
        SetLogical(dc);
        const int W = (int)std::ceil(cr.right / g_ui);
        const double scrollL = g_wlScroll / g_ui;
        const int n = (int)g_wlItems.size();
        if (n == 0) {
            SetTextAlign(dc, TA_CENTER);
            DrawTxt(dc, g_fMed, kMuted, W / 2, 30, Tr(L"Noch nichts geschützt"));
            DrawTxt(dc, g_fSm, kMuted, W / 2, 62, Tr(L"Trage oben einen Dateinamen oder ein Muster ein."));
            SetTextAlign(dc, TA_LEFT);
        }
        for (int i = std::max(0, (int)(scrollL / (kWlRow + kWlGap))); i < n; ++i) {
            const int top = (int)std::lround(i * (kWlRow + kWlGap) - scrollL);
            if (top * g_ui > cr.bottom) break;
            RECT card{0, top, W - 2, top + kWlRow};
            FillRound(dc, card, g_wlHover == i ? kCardHot : kCard, 14);
            DrawFit(dc, g_fMed, kStrong, 20, top + 12, W - 2 - 70, g_wlItems[i]);
            RECT xr{W - 2 - 52, top, W - 2 - 12, top + kWlRow};
            SelectObject(dc, g_fIcon);
            SetTextColor(dc, (g_wlHover == i && g_wlHoverDel) ? kBad : kMuted);
            DrawTextW(dc, L"\xE711", -1, &xr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);  // X zum Entfernen
        }
        ResetLogical(dc);
        BitBlt(wdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, ob);
        DeleteObject(bm);
        DeleteDC(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_VSCROLL:
        g_wlScroll = ScrollPosFrom(h, w, step);
        UpdateWlScroll();
        return 0;
    case WM_MOUSEWHEEL:
        g_wlScroll -= GET_WHEEL_DELTA_WPARAM(w) * step / 120;
        UpdateWlScroll();
        return 0;
    case WM_MOUSEMOVE: {
        RECT cr;
        GetClientRect(h, &cr);
        const int row = WlRowAt((short)HIWORD(l));
        const bool del = row >= 0 && (short)LOWORD(l) >= cr.right - D(64);
        if (row != g_wlHover || del != g_wlHoverDel) {
            g_wlHover = row;
            g_wlHoverDel = del;
            InvalidateRect(h, nullptr, FALSE);
        }
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE:
        g_wlHover = -1;
        g_wlHoverDel = false;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (g_wlHoverDel && LOWORD(l) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649))); return TRUE; }
        break;
    case WM_LBUTTONDOWN:
        if (g_wlHoverDel && g_wlHover >= 0 && g_wlHover < (int)g_wlItems.size()) {
            g_wlItems.erase(g_wlItems.begin() + g_wlHover);
            SaveWhitelist(g_wlItems);
            g_wlHover = -1;
            g_wlHoverDel = false;
            UpdateWlScroll();
            InvalidateRect(GetParent(h), nullptr, FALSE);
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// Eingabefeld: Enter fuegt hinzu
static LRESULT CALLBACK WlEditProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (m == WM_CHAR && w == VK_RETURN) {
        PostMessageW(GetParent(h), WM_COMMAND, ID_WL_ADD, 0);
        return 0;
    }
    return DefSubclassProc(h, m, w, l);
}

static LRESULT CALLBACK WlDlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    constexpr int kW = 640, kH = 700;
    switch (m) {
    case WM_CREATE: {
        BOOL dark = g_dark;
        DwmSetWindowAttribute(h, 20, &dark, sizeof dark);
        g_wlScroll = 0;
        g_wlHover = -1;
        g_wlHoverDel = false;
        g_wlItems = LoadWhitelist();
        g_wlEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, D(28 + 16), D(140 + 10), D(290 - 32), D(22), h,
                                 (HMENU)(INT_PTR)253, nullptr, nullptr);
        SendMessageW(g_wlEdit, WM_SETFONT, (WPARAM)g_fList, TRUE);
        SendMessageW(g_wlEdit, EM_SETCUEBANNER, TRUE, (LPARAM)Tr(L"Dateiname oder Muster, z. B. *.pdf"));
        SendMessageW(g_wlEdit, EM_SETLIMITTEXT, 200, 0);
        SetWindowSubclass(g_wlEdit, WlEditProc, 1, 0);
        auto btn = [&](int id, const wchar_t* text, int x, int y, int bw, int bh) {
            HWND b = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, D(x), D(y), D(bw), D(bh), h, (HMENU)(INT_PTR)id, nullptr, nullptr);
            SetWindowSubclass(b, ButtonProc, (UINT_PTR)id, 0);
        };
        btn(ID_WL_ADD, Tr(L"Hinzufügen"), 330, 140, 116, 42);
        btn(ID_WL_PICK, Tr(L"Aus Downloads …"), 456, 140, 156, 42);
        btn(ID_WL_OK, Tr(L"Schließen"), (kW - 220) / 2, kH - 62, 220, 46);
        g_wlListWnd = CreateWindowW(L"TidyWlList", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL, D(28), D(224), D(kW - 56), D(kH - 224 - 118), h, nullptr,
                                    GetModuleHandleW(nullptr), nullptr);
        SetWindowTheme(g_wlListWnd, ThemeName(), nullptr);
        UpdateWlScroll();
        SetFocus(g_wlEdit);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC screen = BeginPaint(h, &ps);
        RECT cr;
        GetClientRect(h, &cr);
        HDC dc = CreateCompatibleDC(screen);
        HBITMAP bmp = CreateCompatibleBitmap(screen, cr.right, cr.bottom);
        HGDIOBJ old = SelectObject(dc, bmp);
        FillRect(dc, &cr, g_bgBrush);
        SetBkMode(dc, TRANSPARENT);
        const int W = (int)std::ceil(cr.right / g_ui);
        DrawDlgHeader(dc, W);
        DrawFit(dc, g_fBig, RGB(255, 255, 255), 112, 26, W - 24, Tr(L"Schutzliste"));
        DrawFit(dc, g_fMed, kHdrSub, 112, 72, W - 24,
                std::to_wstring(g_wlItems.size()) + (g_wlItems.size() == 1 ? Tr(L" Eintrag") : Tr(L" Einträge")) + Tr(L"  ·  werden nie gelöscht"));
        RECT card{28, 140, 28 + 290, 140 + 42};  // Rahmen des Eingabefelds
        FillRound(dc, card, kPanel, 14);
        DrawFit(dc, g_fSm, kMuted, 30, 192, W - 30, Tr(L"Gilt in allen Bereichen. Platzhalter: * (z. B. *.pdf oder Rechnung*)"));
        DrawFit(dc, g_fSm, kMuted, 30, kH - 106, W - 30, Tr(L"Immer geschützt: desktop.ini, Thumbs.db, Systemdateien und das Programm selbst."));
        ResetLogical(dc);
        BitBlt(screen, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old);
        DeleteObject(bmp);
        DeleteDC(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        SetTextColor(dc, kText);
        SetBkColor(dc, kPanel);
        return (LRESULT)g_panelBrush;
    }
    case WM_DRAWITEM:
        DrawButton((const DRAWITEMSTRUCT*)l);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(w) == ID_WL_ADD) {
            wchar_t buf[256]{};
            GetWindowTextW(g_wlEdit, buf, 256);
            AddWhitelistEntry(h, buf);
            SetFocus(g_wlEdit);
        } else if (LOWORD(w) == ID_WL_PICK) {  // Datei im Downloads-Ordner waehlen: nur ihr Name wird uebernommen
            const std::wstring name = ShowPickDialog(h);
            if (!name.empty()) AddWhitelistEntry(h, name);
        } else if (LOWORD(w) == ID_WL_OK) {
            DestroyWindow(h);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        g_wlOpen = false;
        g_wlListWnd = nullptr;
        g_wlEdit = nullptr;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void ShowProtectDialog(HWND owner) {
    if (g_wlOpen) return;
    WNDCLASSW lc{};
    lc.lpfnWndProc = WlListProc;
    lc.hInstance = GetModuleHandleW(nullptr);
    lc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    lc.lpszClassName = L"TidyWlList";
    RegisterClassW(&lc);
    ShowModal(owner, L"DuncansTidyWlDlg", WlDlgProc, Tr(L"Schutzliste"), 640, 700, g_wlOpen);
}

// ---- Gemeinsamer UI-Start: Theme, Skalierung, Schriften, GDI+ und dunkle Menues. Wird von allen Fenstern angefordert,
// auch von Dialogen, die vor dem Hauptfenster oder in der Kommandozeile erscheinen (Installationsfrage, Auto-Lauf). ----
static ULONG_PTR g_gdiToken = 0;

// Popup-Menues (Tray, Auswahlfelder) im Theme der App; nutzt die Windows-Funktion SetPreferredAppMode (uxtheme, Ordinal 135/136). Fehlt sie, bleibt das Menue wie es ist.
static void ApplyMenuTheme() {
    HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
    auto setMode = ux ? (int(WINAPI*)(int))GetProcAddress(ux, MAKEINTRESOURCEA(135)) : nullptr;
    auto flush = ux ? (void(WINAPI*)())GetProcAddress(ux, MAKEINTRESOURCEA(136)) : nullptr;
    if (setMode) setMode(g_dark ? 2 : 3);  // 2 = dunkel erzwingen, 3 = hell erzwingen
    if (flush) flush();
}

static void EnsureUi() {
    static bool done = false;
    if (done) return;
    done = true;
    SetPalette(!GetFlag(L"LightTheme", false));
    // Scharf auf skalierten Bildschirmen: DPI-bewusst starten statt von Windows strecken zu lassen
    SetProcessDPIAware();
    HDC sdc = GetDC(nullptr);
    g_ui = GetDeviceCaps(sdc, LOGPIXELSX) / 96.0;
    ReleaseDC(nullptr, sdc);
    Gdiplus::GdiplusStartupInput gsi;
    Gdiplus::GdiplusStartup(&g_gdiToken, &gsi, nullptr);
    auto font = [](double px, int weight, const wchar_t* face, DWORD pitch = 0) {
        return CreateFontW(-(int)std::lround(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, pitch, face);
    };
    // Gezeichneter Text: logische Groesse (die DC-Skalierung rechnet auf die Bildschirm-DPI um)
    g_fSm = font(15, FW_NORMAL, L"Segoe UI");
    g_fNav = font(17, FW_NORMAL, L"Segoe UI");
    g_fMed = font(17, FW_SEMIBOLD, L"Segoe UI");
    g_fBig = font(28, FW_SEMIBOLD, L"Segoe UI");
    g_fTitle = font(32, FW_SEMIBOLD, L"Segoe UI");
    g_fIcon = font(20, FW_NORMAL, L"Segoe MDL2 Assets");
    // Steuerelemente (Liste, Log) brauchen echte Pixelgroessen
    g_fList = font(17 * g_ui, FW_NORMAL, L"Segoe UI");
    g_fIconBig = font(34, FW_NORMAL, L"Segoe MDL2 Assets");
    g_logo = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, D(40), D(40), 0);
    ApplyMenuTheme();
}

// ---- Dateiwahl aus Downloads: Liste im App-Design statt des Windows-Dateidialogs; nur der Dateiname wird uebernommen ----
constexpr int ID_PK_OK = 254, kPkRow = 48, kPkGap = 8;
static std::vector<std::wstring> g_pkItems;
static std::wstring g_pkChosen;
static HWND g_pkListWnd;
static int g_pkScroll = 0, g_pkHover = -1;
static bool g_pkOpen = false;

static void UpdatePkScroll() {
    if (g_pkListWnd) SyncScroll(g_pkListWnd, (int)g_pkItems.size() * D(kPkRow + kPkGap), g_pkScroll);
}

static int PkRowAt(int y) {
    const int step = D(kPkRow + kPkGap);
    const int row = (y + g_pkScroll) / step;
    if (y < 0 || (y + g_pkScroll) % step >= D(kPkRow) || row < 0 || row >= (int)g_pkItems.size()) return -1;
    return row;
}

static LRESULT CALLBACK PkListProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    const int step = D(kPkRow + kPkGap);
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        UpdatePkScroll();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC wdc = BeginPaint(h, &ps);
        RECT cr;
        GetClientRect(h, &cr);
        HDC dc = CreateCompatibleDC(wdc);
        HBITMAP bm = CreateCompatibleBitmap(wdc, cr.right, cr.bottom);
        HGDIOBJ ob = SelectObject(dc, bm);
        FillRect(dc, &cr, g_bgBrush);
        SetBkMode(dc, TRANSPARENT);
        SetLogical(dc);
        const int W = (int)std::ceil(cr.right / g_ui);
        const double scrollL = g_pkScroll / g_ui;
        const int n = (int)g_pkItems.size();
        if (n == 0) {
            SetTextAlign(dc, TA_CENTER);
            DrawTxt(dc, g_fMed, kMuted, W / 2, 30, Tr(L"Keine Dateien in Downloads."));
            SetTextAlign(dc, TA_LEFT);
        }
        for (int i = std::max(0, (int)(scrollL / (kPkRow + kPkGap))); i < n; ++i) {
            const int top = (int)std::lround(i * (kPkRow + kPkGap) - scrollL);
            if (top * g_ui > cr.bottom) break;
            RECT card{0, top, W - 2, top + kPkRow};
            FillRound(dc, card, g_pkHover == i ? kCardHot : kCard, 14);
            DrawFit(dc, g_fMed, kStrong, 20, top + 12, W - 2 - 20, g_pkItems[i]);
        }
        ResetLogical(dc);
        BitBlt(wdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, ob);
        DeleteObject(bm);
        DeleteDC(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_VSCROLL:
        g_pkScroll = ScrollPosFrom(h, w, step);
        UpdatePkScroll();
        return 0;
    case WM_MOUSEWHEEL:
        g_pkScroll -= GET_WHEEL_DELTA_WPARAM(w) * step / 120;
        UpdatePkScroll();
        return 0;
    case WM_MOUSEMOVE: {
        const int row = PkRowAt((short)HIWORD(l));
        if (row != g_pkHover) {
            g_pkHover = row;
            InvalidateRect(h, nullptr, FALSE);
        }
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE:
        g_pkHover = -1;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (g_pkHover >= 0 && LOWORD(l) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649))); return TRUE; }
        break;
    case WM_LBUTTONDOWN:
        if (g_pkHover >= 0 && g_pkHover < (int)g_pkItems.size()) {
            g_pkChosen = g_pkItems[g_pkHover];
            DestroyWindow(GetParent(h));
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static LRESULT CALLBACK PkDlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    constexpr int kW = 640, kH = 640;
    switch (m) {
    case WM_CREATE: {
        BOOL dark = g_dark;
        DwmSetWindowAttribute(h, 20, &dark, sizeof dark);
        g_pkScroll = 0;
        g_pkHover = -1;
        g_pkListWnd = CreateWindowW(L"TidyPkList", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL, D(28), D(140), D(kW - 56), D(kH - 140 - 100), h, nullptr,
                                    GetModuleHandleW(nullptr), nullptr);
        SetWindowTheme(g_pkListWnd, ThemeName(), nullptr);
        HWND b = CreateWindowW(L"BUTTON", Tr(L"Schließen"), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, D((kW - 220) / 2), D(kH - 62), D(220), D(46), h,
                               (HMENU)(INT_PTR)ID_PK_OK, nullptr, nullptr);
        SetWindowSubclass(b, ButtonProc, (UINT_PTR)ID_PK_OK, 0);
        UpdatePkScroll();
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC screen = BeginPaint(h, &ps);
        RECT cr;
        GetClientRect(h, &cr);
        HDC dc = CreateCompatibleDC(screen);
        HBITMAP bmp = CreateCompatibleBitmap(screen, cr.right, cr.bottom);
        HGDIOBJ old = SelectObject(dc, bmp);
        FillRect(dc, &cr, g_bgBrush);
        SetBkMode(dc, TRANSPARENT);
        const int W = (int)std::ceil(cr.right / g_ui);
        DrawDlgHeader(dc, W);
        DrawFit(dc, g_fBig, RGB(255, 255, 255), 112, 26, W - 24, Tr(L"Aus Downloads wählen"));
        DrawFit(dc, g_fMed, kHdrSub, 112, 72, W - 24, std::to_wstring(g_pkItems.size()) + (g_pkItems.size() == 1 ? Tr(L" Datei") : Tr(L" Dateien")));
        DrawFit(dc, g_fSm, kMuted, 30, kH - 98, W - 30, Tr(L"Ein Klick auf eine Datei übernimmt ihren Namen in die Schutzliste."));
        ResetLogical(dc);
        BitBlt(screen, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old);
        DeleteObject(bmp);
        DeleteDC(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DRAWITEM:
        DrawButton((const DRAWITEMSTRUCT*)l);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(w) == ID_PK_OK) DestroyWindow(h);
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        g_pkOpen = false;
        g_pkListWnd = nullptr;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// Rueckgabe: gewaehlter Dateiname, leer bei Abbruch
static std::wstring ShowPickDialog(HWND owner) {
    g_pkItems.clear();
    g_pkChosen.clear();
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(KnownFolder(FOLDERID_Downloads), fs::directory_options::skip_permission_denied, ec)) {
        const DWORD a = GetFileAttributesW(e.path().c_str());
        if (a == INVALID_FILE_ATTRIBUTES || (a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) continue;
        g_pkItems.push_back(e.path().filename().wstring());
    }
    std::sort(g_pkItems.begin(), g_pkItems.end(), [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    WNDCLASSW lc{};
    lc.lpfnWndProc = PkListProc;
    lc.hInstance = GetModuleHandleW(nullptr);
    lc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    lc.lpszClassName = L"TidyPkList";
    RegisterClassW(&lc);
    ShowModal(owner, L"DuncansTidyPickDlg", PkDlgProc, Tr(L"Aus Downloads wählen"), 640, 640, g_pkOpen);
    return g_pkChosen;
}

// ---- Eigene MessageBox im App-Design: gleiche Aufrufe und Rueckgabewerte wie MessageBoxW (OK, OK/Abbrechen, Ja/Nein; Symbol je nach MB_ICON...) ----
constexpr int ID_MB_1 = 270, ID_MB_2 = 271, kMbW = 540, kMbTextW = 412;
struct MsgDlg {
    std::wstring text, title;
    UINT flags = 0;
    int result = IDOK, textH = 0, lh = 0;
    bool open = false;
};
static MsgDlg g_md;
// g_mbExtraH: siehe weiter oben (vor DoClean)

static LRESULT CALLBACK MsgBoxProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    const UINT type = g_md.flags & 0xF;
    switch (m) {
    case WM_CREATE: {
        BOOL dark = g_dark;
        DwmSetWindowAttribute(h, 20, &dark, sizeof dark);
        const int by = g_md.lh - 70, bw = 130;
        const bool yn = type == MB_YESNO;
        auto btn = [&](int id, const wchar_t* t, int x) {
            HWND c = CreateWindowW(L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, D(x), D(by), D(bw), D(46), h, (HMENU)(INT_PTR)id, nullptr, nullptr);
            SetWindowSubclass(c, ButtonProc, (UINT_PTR)id, 0);
        };
        if (type == MB_OK) {
            btn(ID_MB_1, Tr(L"OK"), kMbW - 28 - bw);
        } else {
            btn(ID_MB_1, yn ? Tr(L"Ja") : Tr(L"OK"), kMbW - 28 - 2 * bw - 12);
            btn(ID_MB_2, yn ? Tr(L"Nein") : Tr(L"Abbrechen"), kMbW - 28 - bw);
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC screen = BeginPaint(h, &ps);
        RECT cr;
        GetClientRect(h, &cr);
        HDC dc = CreateCompatibleDC(screen);
        HBITMAP bmp = CreateCompatibleBitmap(screen, cr.right, cr.bottom);
        HGDIOBJ old = SelectObject(dc, bmp);
        FillRect(dc, &cr, g_bgBrush);
        SetBkMode(dc, TRANSPARENT);
        SetLogical(dc);
        const UINT icon = g_md.flags & 0xF0;
        const wchar_t* glyph = icon == MB_ICONERROR ? L"\xEA39" : icon == MB_ICONWARNING ? L"\xE7BA" : icon == MB_ICONQUESTION ? L"\xE897" : L"\xE946";
        const COLORREF col = icon == MB_ICONERROR ? RGB(239, 68, 68) : icon == MB_ICONWARNING ? RGB(245, 158, 11) : icon == MB_ICONQUESTION ? RGB(168, 85, 247) : RGB(59, 130, 246);
        RECT badge{28, 28, 84, 84};
        FillRound(dc, badge, col, 56);
        SelectObject(dc, g_fIconBig);
        SetTextColor(dc, RGB(255, 255, 255));
        DrawTextW(dc, glyph, -1, &badge, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT tr{100, 28, 100 + kMbTextW, 28 + std::max(56, g_md.textH)};
        SelectObject(dc, g_fNav);
        SetTextColor(dc, kText);
        DrawTextW(dc, g_md.text.c_str(), -1, &tr, DT_WORDBREAK | DT_NOPREFIX);
        ResetLogical(dc);
        BitBlt(screen, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old);
        DeleteObject(bmp);
        DeleteDC(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DRAWITEM:
        DrawButton((const DRAWITEMSTRUCT*)l);
        return TRUE;
    case WM_COMMAND: {
        const int id = LOWORD(w);
        if (id == ID_MB_1 || id == ID_MB_2) {
            g_md.result = id == ID_MB_1 ? (type == MB_YESNO ? IDYES : IDOK) : (type == MB_YESNO ? IDNO : IDCANCEL);
            DestroyWindow(h);
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        g_md.open = false;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static int AppBox(HWND owner, const wchar_t* text, const wchar_t* title, UINT flags) {
    EnsureUi();
    const UINT type = flags & 0xF;
    g_md.text = text;
    g_md.title = title ? title : kApp;
    g_md.flags = flags;
    g_md.result = type == MB_OK ? IDOK : type == MB_YESNO ? IDNO : IDCANCEL;  // Esc oder X
    HDC mdc = GetDC(nullptr);  // Texthoehe vorab messen, damit das Fenster passt
    HGDIOBJ of = SelectObject(mdc, g_fNav);
    RECT tr{0, 0, kMbTextW, 0};
    DrawTextW(mdc, text, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(mdc, of);
    ReleaseDC(nullptr, mdc);
    g_md.textH = tr.bottom + g_mbExtraH;  // Zusatzhoehe gilt auch fuer das Textfeld, sonst wuerde die letzte Zeile trotzdem abgeschnitten
    g_mbExtraH = 0;
    g_md.lh = 28 + std::max(56, g_md.textH) + 28 + 46 + 24;
    ShowModal(owner, L"DuncansTidyMsgDlg", MsgBoxProc, g_md.title.c_str(), kMbW, g_md.lh, g_md.open);
    return g_md.result;
}

// ---- Browser-Abfrage: eigenes Fenster im Stil der App statt der Windows-MessageBox ----
constexpr int ID_BP_CLOSE = 260, ID_BP_KEEP = 261, ID_BP_CANCEL = 262, kBpW = 620, kBpH = 380;
struct BrowserPrompt {
    std::wstring names;
    int result = IDCANCEL;  // IDYES = schliessen, IDNO = ohne Schliessen, IDCANCEL = abbrechen
    bool open = false;
};
static BrowserPrompt g_bp;

static LRESULT CALLBACK BrowserPromptProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        BOOL dark = g_dark;
        DwmSetWindowAttribute(h, 20, &dark, sizeof dark);
        struct B { int id; const wchar_t* text; int x, bw; };
        for (const B& b : {B{ID_BP_CLOSE, Tr(L"Browser schließen"), 28, 170}, B{ID_BP_KEEP, Tr(L"Ohne Schließen fortfahren"), 208, 250},
                           B{ID_BP_CANCEL, Tr(L"Abbrechen"), 468, 124}}) {
            HWND c = CreateWindowW(L"BUTTON", b.text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, D(b.x), D(kBpH - 74), D(b.bw), D(46), h,
                                   (HMENU)(INT_PTR)b.id, nullptr, nullptr);
            SetWindowSubclass(c, ButtonProc, (UINT_PTR)b.id, 0);
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC screen = BeginPaint(h, &ps);
        RECT cr;
        GetClientRect(h, &cr);
        HDC dc = CreateCompatibleDC(screen);
        HBITMAP bmp = CreateCompatibleBitmap(screen, cr.right, cr.bottom);
        HGDIOBJ old = SelectObject(dc, bmp);
        FillRect(dc, &cr, g_bgBrush);
        SetBkMode(dc, TRANSPARENT);
        const int W = (int)std::ceil(cr.right / g_ui);
        DrawDlgHeader(dc, W);
        DrawFit(dc, g_fBig, RGB(255, 255, 255), 112, 26, W - 24, Tr(L"Browser laufen noch"));
        DrawFit(dc, g_fMed, kHdrSub, 112, 72, W - 24, g_bp.names);
        RECT card{28, 140, W - 28, kBpH - 100};
        FillRound(dc, card, kCard, 16);
        RECT tr{card.left + 20, card.top + 16, card.right - 20, card.bottom - 16};
        SelectObject(dc, g_fSm);
        SetTextColor(dc, kText);
        DrawTextW(dc, Tr(L"Laufende Browser sperren ihre Cache-Dateien. Jetzt schließen, damit der Cache komplett geleert werden kann?\n\nUngespeicherte Eingaben auf Webseiten können dabei verloren gehen. Ohne Schließen bleiben benutzte Dateien erhalten."), -1, &tr, DT_WORDBREAK | DT_NOPREFIX);
        ResetLogical(dc);
        BitBlt(screen, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old);
        DeleteObject(bmp);
        DeleteDC(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DRAWITEM:
        DrawButton((const DRAWITEMSTRUCT*)l);
        return TRUE;
    case WM_COMMAND: {
        const int id = LOWORD(w);
        if (id == ID_BP_CLOSE || id == ID_BP_KEEP || id == ID_BP_CANCEL) {
            g_bp.result = id == ID_BP_CLOSE ? IDYES : id == ID_BP_KEEP ? IDNO : IDCANCEL;
            DestroyWindow(h);
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        g_bp.open = false;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static int ShowBrowserPrompt(HWND owner, const std::wstring& names) {
    g_bp.names = names;
    g_bp.result = IDCANCEL;  // Esc oder X = abbrechen
    ShowModal(owner, L"DuncansTidyBrowserDlg", BrowserPromptProc, kApp, kBpW, kBpH, g_bp.open);
    return g_bp.result;
}

// ---- RAM optimieren: Arbeitssaetze verkleinern, mit Administrator-Rechten auch die Standby-Liste leeren ----
constexpr UINT WM_RAM_DONE = WM_APP + 19;
static std::atomic<bool> g_ramBusy{false};
struct RamResult { unsigned long long freed = 0, avail = 0, total = 0; int trimmed = 0; bool standby = false; };
static RamResult g_ramRes;

static void RamStatus(unsigned long long& avail, unsigned long long& total) {
    MEMORYSTATUSEX ms{sizeof ms};
    GlobalMemoryStatusEx(&ms);
    avail = ms.ullAvailPhys;
    total = ms.ullTotalPhys;
}

// Standby-Liste leeren (NtSetSystemInformation, SystemMemoryListInformation / MemoryPurgeStandbyList). Braucht Admin-Rechte.
static bool PurgeStandbyList() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return false;
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = LookupPrivilegeValueW(nullptr, L"SeProfileSingleProcessPrivilege", &tp.Privileges[0].Luid) &&
              AdjustTokenPrivileges(tok, FALSE, &tp, 0, nullptr, nullptr) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(tok);
    if (!ok) return false;
    using NtSetFn = LONG(NTAPI*)(ULONG, PVOID, ULONG);
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    auto fn = nt ? reinterpret_cast<NtSetFn>(reinterpret_cast<void*>(GetProcAddress(nt, "NtSetSystemInformation"))) : nullptr;
    if (!fn) return false;
    ULONG command = 4;  // MemoryPurgeStandbyList
    return fn(80 /* SystemMemoryListInformation */, &command, sizeof command) >= 0;
}

static void RamWorker(HWND h) {
    RamResult r;
    unsigned long long before = 0;
    RamStatus(before, r.total);
    std::vector<DWORD> ids(8192);
    DWORD bytes = 0;
    if (EnumProcesses(ids.data(), (DWORD)(ids.size() * sizeof(DWORD)), &bytes)) {
        const DWORD self = GetCurrentProcessId();
        for (DWORD i = 0; i < bytes / sizeof(DWORD); ++i) {
            if (ids[i] == 0 || ids[i] == self) continue;
            HANDLE p = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_INFORMATION, FALSE, ids[i]);  // geschuetzte Prozesse scheitern still
            if (!p) continue;
            if (EmptyWorkingSet(p)) ++r.trimmed;
            CloseHandle(p);
        }
    }
    r.standby = PurgeStandbyList();
    Sleep(600);  // Windows braucht einen Moment, bis die Zaehler stimmen
    RamStatus(r.avail, r.total);
    r.freed = r.avail > before ? r.avail - before : 0;
    g_ramRes = r;
    PostMessageW(h, WM_RAM_DONE, 0, 0);
}

static void StartRamOptimize(HWND h) {
    if (g_ramBusy.exchange(true)) return;
    EnableWindow(GetDlgItem(h, ID_RAM), FALSE);
    std::thread(RamWorker, h).detach();
}

// ---- System-Tray: Symbol neben der Uhr, solange das Fenster versteckt ----
constexpr UINT WM_TRAY = WM_APP + 11, WM_SHOW_FROM_OTHER = WM_APP + 10;
constexpr UINT kTrayId = 1, kTrayOpen = 300, kTrayExit = 301, kTrayQuick = 302;
constexpr UINT WM_QUICK_DONE = WM_APP + 15;  // wParam = Anzahl geloeschter Eintraege
static std::atomic<bool> g_quickBusy{false};
static std::atomic<unsigned long long> g_quickFreed{0};

static NOTIFYICONDATAW TrayData(HWND h) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof nid;
    nid.hWnd = h;
    nid.uID = kTrayId;
    return nid;
}

static void TrayAdd(HWND h, bool hint) {
    static HICON icon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
                                          GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    NOTIFYICONDATAW nid = TrayData(h);
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = icon;
    wcscpy_s(nid.szTip, L"Duncan's Tidy");
    g_trayWanted = true;
    g_trayShown = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    // Beim Start mit Windows ist die Taskleiste oft noch nicht bereit und das Hinzufuegen scheitert: alle 2 Sekunden erneut versuchen
    if (g_trayShown) KillTimer(h, ID_TRAYRETRY);
    else SetTimer(h, ID_TRAYRETRY, 2000, nullptr);
    if (g_trayShown && hint && !GetFlag(L"TrayHintShown", false)) {  // beim ersten Mal erklaeren, wohin das Fenster verschwunden ist
        SetFlag(L"TrayHintShown", true);
        nid.uFlags = NIF_INFO;
        wcscpy_s(nid.szInfoTitle, Tr(L"Duncan's Tidy läuft weiter"));
        wcscpy_s(nid.szInfo, Tr(L"Das Programm läuft im Hintergrund. Ein Klick auf das Symbol öffnet es wieder."));
        nid.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
}

static void TrayRemove(HWND h) {
    g_trayWanted = false;
    KillTimer(h, ID_TRAYRETRY);
    if (!g_trayShown) return;
    NOTIFYICONDATAW nid = TrayData(h);
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_trayShown = false;
}

static void RestoreFromTray(HWND h) {
    TrayRemove(h);
    ShowWindow(h, IsIconic(h) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(h);
    if (!g_busy) ScanAllAsync(h);  // nach langer Zeit im Hintergrund sind die Zahlen veraltet
}

// Explorer-Kontextmenue "Mit Duncan's Tidy analysieren": die 20 groessten Dateien eines Ordners oder Laufwerks (nur Anzeige).
// path: voller Ordnerpfad oder Laufwerk ("C:"); es wird nur gelesen, nichts geloescht.
static void AnalyzePath(HWND h, std::wstring path) {
    while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();   // "C:\Ordner\" -> "C:\Ordner"
    if (path.size() == 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) path.pop_back();   // "C:\" -> "C:"
    const bool drive = path.size() == 2 && path[1] == L':';
    const DWORD attr = GetFileAttributesW(drive ? (path + L"\\").c_str() : path.c_str());
    if (path.empty() || path.size() >= 32000 || attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        AppBox(h, Tr(L"Dieser Ordner wurde nicht gefunden oder ist nicht lesbar."), kApp, MB_OK | MB_ICONWARNING);
        return;
    }
    if (g_busy || g_updating) {
        AppBox(h, Tr(L"Die Analyse läuft noch. Bitte einen Moment warten."), kApp, MB_OK | MB_ICONINFORMATION);
        return;
    }
    SetPage(h, kDrivePage);  // dort steht waehrend der Suche "Suche groesste Dateien auf ..."
    StartTopFilesAt(h, path, path);
}

// Schnell-Reinigung aus dem Tray: Temp und Systemdateien (endgueltig, Schutzliste gilt, benutzte Dateien bleiben); das Ergebnis meldet eine Sprechblase
static void QuickCleanAsync(HWND h) {
    if (g_busy || g_quickBusy.exchange(true)) return;
    Bg([h] {
        auto items = ListItems(CAT_TEMP);
        auto sys = ListItems(CAT_SYS);
        items.insert(items.end(), sys.begin(), sys.end());
        int ok = 0;
        unsigned long long freed = 0;
        for (const auto& p : items) {
            const unsigned long long sz = PathSize(p);
            if (DeleteEntry(p, true)) { ++ok; freed += sz; }
        }
        g_quickFreed = freed;
        g_quickBusy = false;
        PostMessageW(h, WM_QUICK_DONE, (WPARAM)ok, 0);
    });
}

static void ShowTrayMenu(HWND h) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kTrayOpen, Tr(L"Duncan's Tidy öffnen"));
    AppendMenuW(menu, MF_STRING, kTrayQuick, Tr(L"Jetzt schnell reinigen"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayExit, Tr(L"Beenden"));
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(h);  // sonst schliesst sich das Menue nicht beim Klick daneben
    const int cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, nullptr);
    DestroyMenu(menu);
    if (cmd == (int)kTrayOpen) RestoreFromTray(h);
    else if (cmd == (int)kTrayQuick) QuickCleanAsync(h);
    else if (cmd == (int)kTrayExit) DestroyWindow(h);
}

// Design wechseln: Titelleiste, Listen und alle Steuerelemente auf die neue Palette umstellen
static void ApplyThemeToWindow(HWND h) {
    ApplyMenuTheme();
    BOOL dark = g_dark;
    DwmSetWindowAttribute(h, 20, &dark, sizeof dark);
    SetWindowTheme(g_alist, ThemeName(), nullptr);
    RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
}

// Sprache zur Laufzeit wechseln: speichern, Beschriftungen der Knoepfe setzen, Seite und Listen neu aufbauen
static void ApplyLanguage(HWND h, int idx) {
    g_lang = idx;
    SaveLanguage(idx);
    for (int i = 0; i < kPages; ++i) SetWindowTextW(GetDlgItem(h, ID_NAV0 + i), Tr(kPageName[i]));
    SetWindowTextW(GetDlgItem(h, ID_MORE), g_navMore ? Tr(L"Weniger") : Tr(L"Mehr"));
    SetWindowTextW(GetDlgItem(h, ID_EXIT), Tr(L"Exit"));
    SetWindowTextW(GetDlgItem(h, ID_TOGGLE), Tr(L"Alle / Keine"));
    SetWindowTextW(GetDlgItem(h, ID_PROTECT), Tr(L"Schutzliste"));
    SetWindowTextW(GetDlgItem(h, ID_ADMIN), Tr(L"Details (Admin)"));
    SetWindowTextW(GetDlgItem(h, ID_RAM), Tr(L"RAM optimieren"));
    SetWindowTextW(GetDlgItem(h, ID_UPDATE), g_newVersion.empty() ? Tr(L"Update verfügbar") : UpdateButtonText().c_str());
    SendMessageW(g_search, EM_SETCUEBANNER, TRUE, (LPARAM)Tr(L"Programm suchen …"));
    g_doneText.clear();  // alte Ergebnismeldung der Uebersicht waere in der falschen Sprache
    g_doneSub.clear();
    LoadAutostart();
    SetPage(h, g_page);  // setzt Clean-Button-Text, baut Listen und Gruppen in der neuen Sprache neu auf
    RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

// Schalter auf der Einstellungen-Seite umlegen; danach den echten Zustand neu lesen
static void ToggleSetting(HWND h, int i) {
    if (i == 0) {
        SetStartWithWindows(!StartWithWindowsOn());
    } else if (i == 1) {
        g_trayOnClose = !g_trayOnClose;
        SetFlag(L"TrayOnClose", g_trayOnClose);
    } else if (i == 2) {
        SetAutoClean(!AutoCleanOn());
    } else if (i == 5) {
        SetDeep(h, !DeepOn());
    } else if (i == 6) {
        SetFlag(L"DupeScan", !GetFlag(L"DupeScan", true));
    } else if (i == 7) {
        const bool on = !SecureOn();
        if (on && AppBox(h, Tr(L"Beim endgültigen Löschen werden Dateien vorher einmal mit Zufallsdaten überschrieben.\n\nDas dauert bei großen Mengen deutlich länger. Auf SSDs schützt es nur begrenzt, weil der Speicher Daten intern verschiebt; dort hilft Verschlüsselung (BitLocker) mehr. Der Papierkorb ist nicht betroffen.\n\nEinschalten?"),
                         kApp, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
        SetFlag(L"SecureDelete", on);
        g_secureDel = on ? 1 : 0;
    } else if (i == kSettings - 1) {
        const RECT& r = g_setRow[i];  // Auswahlfeld mit allen Sprachen (gleiche Lage wie beim Zeichnen)
        const int rh = r.bottom - r.top;
        const RECT pill{r.right - 24 - 190, r.top + (rh - 34) / 2, r.right - 24, r.top + (rh - 34) / 2 + 34};
        std::vector<std::wstring> labels;
        for (const auto& l : kLangs) labels.push_back(l.name);
        const int pick = PickFromMenu(h, pill, labels, g_lang);
        if (pick >= 0 && pick != g_lang) ApplyLanguage(h, pick);
    } else if (i == 3) {
        SetFlag(L"LightTheme", g_dark);  // g_dark = aktuell dunkel -> jetzt hell speichern
        SetPalette(!g_dark);
        ApplyThemeToWindow(h);
    } else if (i == 4) {
        g_animOn = !g_animOn;
        SetFlag(L"AnimOff", !g_animOn);
        if (!g_animOn) {  // laufende Animation sofort anhalten und den Besen in Ruhe stellen
            KillTimer(h, ID_ANIM);
            if (g_fineTimer) { timeEndPeriod(1); g_fineTimer = false; }
            g_introOn = false;
            g_sceneP = 1.f;
            g_sceneSwing = 0.f;
            InvalidateRect(h, nullptr, FALSE);
        } else {
            StartAnim(h, !g_busy);  // zur Vorschau einmal fegen
        }
    }
    LoadSettings();
    RECT r{D(g_setRow[i].left), D(g_setRow[i].top), D(g_setRow[i].right) + 1, D(g_setRow[i].bottom) + 1};
    InvalidateRect(h, &r, FALSE);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g_msgTaskbarCreated && m == g_msgTaskbarCreated) {  // Explorer wurde neu gestartet: Symbol wieder anlegen
        if (g_trayWanted) { g_trayShown = false; TrayAdd(h, false); }
        return 0;
    }
    switch (m) {
    case WM_CLOSE:
        if (g_trayOnClose && !g_updating) {  // X minimiert in den Tray statt zu beenden
            ShowWindow(h, SW_HIDE);
            TrayAdd(h, true);
            return 0;
        }
        break;
    case WM_TRAY:
        if (l == WM_LBUTTONUP || l == WM_LBUTTONDBLCLK) RestoreFromTray(h);
        else if (l == WM_RBUTTONUP) ShowTrayMenu(h);
        return 0;
    case WM_RAM_DONE: {
        g_ramBusy = false;
        EnableWindow(GetDlgItem(h, ID_RAM), TRUE);
        const RamResult r = g_ramRes;
        wchar_t msg[512];
        swprintf_s(msg, Tr(L"Arbeitsspeicher optimiert.\n\nFreigegeben: %s\nVerfügbar jetzt: %s von %s\nVerkleinerte Prozesse: %d"),
                   FormatSize(r.freed).c_str(), FormatSize(r.avail).c_str(), FormatSize(r.total).c_str(), r.trimmed);
        std::wstring text = msg;
        text += r.standby ? Tr(L"\n\nDie Standby-Liste wurde geleert.")
                          : Tr(L"\n\nHinweis: Ohne Administrator-Rechte wird nur der Arbeitsspeicher der Programme verkleinert, nicht die Standby-Liste.");
        g_mbExtraH = 30;  // die Standby-Zeile war unten abgeschnitten
        AppBox(h, text.c_str(), kApp, MB_OK | MB_ICONINFORMATION);
        if (g_page == kRamPage) InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_QUICK_DONE:
        if (g_trayShown) {
            NOTIFYICONDATAW nid = TrayData(h);
            nid.uFlags = NIF_INFO;
            wcscpy_s(nid.szInfoTitle, Tr(L"Schnell-Reinigung fertig"));
            swprintf_s(nid.szInfo, Tr(L"%s freigegeben (%d Einträge)"), FormatSize(g_quickFreed).c_str(), (int)w);
            nid.dwInfoFlags = NIIF_INFO;
            Shell_NotifyIconW(NIM_MODIFY, &nid);
        }
        return 0;
    case WM_SHOW_FROM_OTHER:  // ein zweiter Start hat uns gebeten, das Fenster zu zeigen
        RestoreFromTray(h);
        return 0;
    case WM_CREATE: {
        BOOL dark = g_dark;
        DwmSetWindowAttribute(h, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);

        auto btn = [&](const wchar_t* t, int id, bool visible = true) {
            HWND b = CreateWindowW(L"BUTTON", t, WS_CHILD | (visible ? WS_VISIBLE : 0) | BS_OWNERDRAW, 0, 0, 10, 10, h,
                                   (HMENU)(INT_PTR)id, nullptr, nullptr);
            SetWindowSubclass(b, ButtonProc, (UINT_PTR)id, 0);
        };
        for (int i = 0; i < kPages; ++i) btn(Tr(kPageName[i]), ID_NAV0 + i);
        btn(Tr(L"Mehr"), ID_MORE);
        for (int i = 5; i <= 8; ++i) ShowWindow(GetDlgItem(h, ID_NAV0 + i), SW_HIDE);
        btn(Tr(L"Exit"), ID_EXIT);
        btn(Tr(L"Clean Now"), ID_CLEAN);
        btn(Tr(L"Alle / Keine"), ID_TOGGLE, false);
        btn(Tr(L"Schutzliste"), ID_PROTECT, false);
        btn(Tr(L"Update verfügbar"), ID_UPDATE, false);
        btn(Tr(L"Details (Admin)"), ID_ADMIN, false);
        btn(Tr(L"RAM optimieren"), ID_RAM, false);

        g_main = h;
        INITCOMMONCONTROLSEX icc{sizeof icc, ICC_TAB_CLASSES};  // TAB_CLASSES schliesst die Tooltips ein
        InitCommonControlsEx(&icc);
        {   // Autostart-Seite: eigene Liste, Suchfeld und Tooltips
            WNDCLASSW ac{};
            ac.lpfnWndProc = AutoListProc;
            ac.hInstance = GetModuleHandleW(nullptr);
            ac.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
            ac.lpszClassName = L"TidyAutoList";
            RegisterClassW(&ac);
            g_alist = CreateWindowW(L"TidyAutoList", L"", WS_CHILD | WS_VSCROLL, 0, 0, 10, 10, h, nullptr, GetModuleHandleW(nullptr), nullptr);
            SetWindowTheme(g_alist, ThemeName(), nullptr);
            g_search = CreateWindowW(L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, h, (HMENU)(INT_PTR)ID_SEARCH, nullptr, nullptr);
            SendMessageW(g_search, WM_SETFONT, (WPARAM)g_fList, TRUE);
            SendMessageW(g_search, EM_SETCUEBANNER, TRUE, (LPARAM)Tr(L"Programm suchen …"));
            g_ttList = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, g_alist, nullptr,
                                       GetModuleHandleW(nullptr), nullptr);
            SendMessageW(g_ttList, TTM_SETMAXTIPWIDTH, 0, D(640));
            TOOLINFOW ti{sizeof ti, TTF_SUBCLASS, g_alist, 1, {0, 0, 10, 10}, nullptr, (LPWSTR)LPSTR_TEXTCALLBACKW};
            SendMessageW(g_ttList, TTM_ADDTOOLW, 0, (LPARAM)&ti);
            for (HWND tt : {g_ttList}) {  // dunkle Tooltips im Stil der App, gut lesbar
                SetWindowTheme(tt, L"", L"");
                SendMessageW(tt, TTM_SETTIPBKCOLOR, (WPARAM)kCard, 0);
                SendMessageW(tt, TTM_SETTIPTEXTCOLOR, (WPARAM)RGB(255, 255, 255), 0);
                SendMessageW(tt, WM_SETFONT, (WPARAM)g_fList, TRUE);
            }
        }

        SetPage(h, 0);
        ScanAllAsync(h);
        // Keine eigene Update-Pruefung mehr beim Start: Duncan's Suite prueft zentral alle Module (Tidy liest dort seine Version aus der EXE).
        PostMessageW(h, WM_SHOW_WHATSNEW, 0, 0);
        // Der Fege-Effekt startet erst, wenn das Fenster sichtbar ist (RunWindow); Analyse und Update-Pruefung laufen schon
        return 0;
    }
    case WM_ACTIVATE: {  // Schutzliste im Editor geaendert? Dann Liste neu aufbauen
        static fs::file_time_type lastWl{};
        std::error_code ec;
        auto t = fs::last_write_time(WhitelistFile(), ec);
        if (LOWORD(w) != WA_INACTIVE && !ec && t != lastWl) {
            bool first = lastWl == fs::file_time_type{};
            lastWl = t;
            if (!first && g_page >= 1 && !g_busy) {
                PopulateList();
                ScanAllAsync(h);
            }
        }
        if (LOWORD(w) != WA_INACTIVE && g_page == kUninstPage && !g_uninLoading && GetTickCount64() - g_uninTick > 3000) StartUninstScan(h);  // nach einer Deinstallation wieder aktuell
        return 0;
    }
    case WM_LIST_DONE: {
        ListResult r;
        {
            std::lock_guard<std::mutex> lk(g_statMx);
            r = std::move(g_listTmp);
        }
        if (r.gen != g_listGen || r.cat != PageCat(g_page)) return 0;  // veraltet: inzwischen andere Seite oder neuer Scan
        g_listLoading = false;
        g_items = std::move(r.items);
        g_itemGroup = std::move(r.groups);
        BuildGroupCards(r.cat, false);
        g_aScroll = 0;
        FilterAutostart();
        UpdateAutoScroll();
        ShowWindow(g_alist, g_items.empty() ? SW_HIDE : SW_SHOW);  // leer: stattdessen die Leer-Ansicht (DrawEmpty)
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_UNINST_DONE: {
        {
            std::lock_guard<std::mutex> lk(g_statMx);
            g_unin = std::move(g_uninTmp);
        }
        SortUninst();
        g_uninTick = GetTickCount64();
        if (g_page == kUninstPage) {
            g_aScroll = 0;
            FilterAutostart();
            UpdateAutoScroll();
        }
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_UNINSTALL: {
        const int idx = (int)w;
        if (idx < 0 || idx >= (int)g_unin.size() || g_busy || g_updating) return 0;
        wchar_t q[768];
        swprintf_s(q, Tr(L"„%s“ deinstallieren?\n\nDas Deinstallationsprogramm des Programms wird gestartet. Dort kannst du noch abbrechen."), g_unin[idx].name.c_str());
        if (AppBox(h, q, kApp, MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
        if (!RunUninstall(g_unin[idx].uninst)) AppBox(h, Tr(L"Das Deinstallationsprogramm konnte nicht gestartet werden."), kApp, MB_OK | MB_ICONWARNING);
        return 0;
    }
    case WM_ERASEBKGND: {
        // PaintWindow zeichnet alles selbst. Fragt jemand anderes (z.B. ein Steuerelement) nach dem Hintergrund,
        // bekommt er die passende Farbe statt Schwarz.
        RECT clip;
        if (GetClipBox((HDC)w, &clip) != ERROR)
            FillRect((HDC)w, &clip, clip.left < D(kSideW) ? g_sideBrush : g_bgBrush);
        return 1;
    }
    case WM_PAINT:
        PaintWindow(h);
        return 0;
    case WM_SIZE:
        if (g_main && w != SIZE_MINIMIZED) Layout(h);
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)l)->ptMinTrackSize = {D(960), D(740)};
        return 0;
    case WM_MOUSEMOVE: {
        {
            RECT cr;
            GetClientRect(h, &cr);
            int lx = (int)((short)LOWORD(l) / g_ui), ly = (int)((short)HIWORD(l) / g_ui), lh = (int)(cr.bottom / g_ui);
            int hot = 0;   // Die Versionszeile ist kein Link mehr: Ueber-Informationen stehen in der Suite (Fusszeile)
            if (hot != g_linkHot) {
                g_linkHot = hot;
                RECT r{0, D(lh - 54), D(kSideW), cr.bottom};
                InvalidateRect(h, &r, FALSE);
            }
            POINT ip{(LONG)lx, (LONG)ly};
            const int mh = ModeSegAt(ip);
            if (mh != g_modeHot) {
                g_modeHot = mh;
                RECT mr{D(g_modeRect.left) - 2, D(g_modeRect.top) - 2, D(g_modeRect.right) + 2, D(g_modeRect.bottom) + 2};
                InvalidateRect(h, &mr, FALSE);
            }
            bool ih = g_page != 0 && g_page != kRamPage && PtInRect(&g_infoRect, ip);
            if (ih != g_infoHot) {
                g_infoHot = ih;
                RECT r{D(kSideW), 0, cr.right, D(kHeaderH + 70)};  // Symbol und Hinweis-Kasten darunter
                InvalidateRect(h, &r, FALSE);
            }
            TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
            TrackMouseEvent(&t);
        }
        if (g_page == kSettingsPage) {
            POINT p{(LONG)((short)LOWORD(l) / g_ui), (LONG)((short)HIWORD(l) / g_ui)};
            int hot = -1;
            for (int i = 0; i < kSettings; ++i) if (PtInRect(&g_setRow[i], p)) hot = i;
            if (hot != g_setHot) {
                const int old = g_setHot;
                g_setHot = hot;
                for (int i : {old, hot})
                    if (i >= 0) {
                        RECT r{D(g_setRow[i].left), D(g_setRow[i].top), D(g_setRow[i].right) + 1, D(g_setRow[i].bottom) + 1};
                        InvalidateRect(h, &r, FALSE);
                    }
            }
        }
        if (g_page == 0) {
            POINT p{(LONG)((short)LOWORD(l) / g_ui), (LONG)((short)HIWORD(l) / g_ui)};
            int hot = -1;
            for (int i = 0; i < kTiles; ++i) if (PtInRect(&g_tile[i], p)) hot = i;
            if (hot != g_tileHot) {
                int old = g_tileHot;
                g_tileHot = hot;
                for (int i : {old, hot})  // nur die betroffenen Kacheln neu zeichnen
                    if (i >= 0) {
                        RECT r{D(g_tile[i].left), D(g_tile[i].top), D(g_tile[i].right) + 1, D(g_tile[i].bottom) + 1};
                        InvalidateRect(h, &r, FALSE);
                    }
            }
        }
        return 0;
    }
    case WM_SETCURSOR:
        if ((g_linkHot || g_infoHot || g_modeHot || g_setHot >= 0) && LOWORD(l) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649))); return TRUE; }
        break;
    case WM_MOUSELEAVE:
        if (g_modeHot) {
            g_modeHot = 0;
            InvalidateRect(h, nullptr, FALSE);
        }
        if (g_infoHot) {
            g_infoHot = false;
            InvalidateRect(h, nullptr, FALSE);
        }
        if (g_linkHot) {
            g_linkHot = 0;
            RECT cr;
            GetClientRect(h, &cr);
            RECT r{0, cr.bottom - D(54), D(kSideW), cr.bottom};
            InvalidateRect(h, &r, FALSE);
        }
        if (g_setHot != -1) {
            const RECT& t = g_setRow[g_setHot];
            RECT r{D(t.left), D(t.top), D(t.right) + 1, D(t.bottom) + 1};
            g_setHot = -1;
            InvalidateRect(h, &r, FALSE);
        }
        if (g_tileHot != -1) {
            const RECT& t = g_tile[g_tileHot];
            RECT r{D(t.left), D(t.top), D(t.right) + 1, D(t.bottom) + 1};
            g_tileHot = -1;
            InvalidateRect(h, &r, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (g_modeHot && !g_busy && !g_updating && !g_listLoading) {  // Umschalter Standard | Ultra-Tiefenreinigung
            const int want = g_modeHot - 1;   // 0 Standard, 1 Ultra
            if (want != (DeepOn() ? 1 : 0)) SetMode(h, want);
            return 0;
        }
        if (g_infoHot && !g_updating) {  // Klick aufs Info-Symbol zeigt denselben Text wie der Tooltip
            AppBox(h, PageInfo(), Tr(kPageName[g_page]), MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        if (g_page == kSettingsPage && g_setHot >= 0 && !g_updating) {
            if (g_setHot == kSetAutoRow) {  // Klick auf eines der beiden Auswahlfelder statt auf den Schalter
                RECT pa, pb, pc;
                SettingPills(g_setRow[kSetAutoRow], pa, pb, pc);
                const POINT p{(LONG)((short)LOWORD(l) / g_ui), (LONG)((short)HIWORD(l) / g_ui)};
                if (PtInRect(&pa, p)) {
                    std::vector<std::wstring> labels;
                    int cur = 2;
                    for (int i = 0; i < (int)std::size(kIntervalChoices); ++i) {
                        labels.push_back(IntervalLabel(kIntervalChoices[i]));
                        if (kIntervalChoices[i] == AutoDays()) cur = i;
                    }
                    const int pick = PickFromMenu(h, pa, labels, cur);
                    if (pick >= 0) { SetInt(L"AutoDays", kIntervalChoices[pick]); InvalidateRect(h, nullptr, FALSE); }
                    return 0;
                }
                if (PtInRect(&pc, p)) {  // Temp & Systemdateien beim Auto-Clean: beim Einschalten vorher genau zeigen, was geloescht wird
                    if (GetFlag(L"AutoExtra", false)) SetFlag(L"AutoExtra", false);
                    else if (AppBox(h, Tr(L"Beim automatischen Aufräumen werden zusätzlich diese Bereiche geleert:\n\n • Temp-Dateien (AppData\\Local\\Temp)\n • Vorschaubild- und Symbol-Cache des Explorers\n • Absturzabbilder (CrashDumps)\n • Windows-Fehlerberichte\n\nDas wird ENDGÜLTIG gelöscht, nicht in den Papierkorb. Dateien, die gerade benutzt werden, und Einträge der Schutzliste bleiben erhalten.\n\nEinschalten?"),
                                         kApp, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES)
                        SetFlag(L"AutoExtra", true);
                    InvalidateRect(h, nullptr, FALSE);
                    return 0;
                }
                if (PtInRect(&pb, p)) {
                    std::vector<std::wstring> labels;
                    int cur = 0;
                    for (int i = 0; i < (int)std::size(kSizeChoices); ++i) {
                        wchar_t b[96];
                        if (kSizeChoices[i]) swprintf_s(b, Tr(L"Kleiner als %d MB bleibt erhalten"), kSizeChoices[i]);
                        else wcscpy_s(b, Tr(L"Keine Ausnahme: alles wird aufgeräumt"));
                        labels.push_back(b);
                        if (kSizeChoices[i] == KeepBelowMB()) cur = i;
                    }
                    const int pick = PickFromMenu(h, pb, labels, cur);
                    if (pick >= 0) { SetInt(L"KeepBelowMB", kSizeChoices[pick]); InvalidateRect(h, nullptr, FALSE); }
                    return 0;
                }
            }
            ToggleSetting(h, g_setHot);
            return 0;
        }
        if (g_page == 0 && g_tileHot >= 0 && !g_busy) SetPage(h, 1 + g_tileHot);
        return 0;
    case WM_TIMER:
        if (w == ID_ANIM) TickAnim(h);
        else if (w == ID_RAMTICK) { if (g_page == kRamPage) InvalidateRect(h, nullptr, FALSE); else KillTimer(h, ID_RAMTICK); }
        else if (w == ID_TRAYRETRY) { if (g_trayWanted && !g_trayShown) TrayAdd(h, false); else KillTimer(h, ID_TRAYRETRY); }
        else if (w == ID_UPDT) {  // Fortschrittsbalken des Updates nachzeichnen
            RECT cr;
            GetClientRect(h, &cr);
            RECT banner{D(kSideW), 0, cr.right, D(kHeaderH)};
            InvalidateRect(h, &banner, FALSE);
        }
        return 0;
    case WM_CLEAN_TICK:
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_CLEAN_DONE: {
        InvalidateBrowserScan();  // nach dem Loeschen muss neu gemessen werden
        const bool dlOpt = g_optDl;
        g_optDl = false;
        g_busy = false;
        { std::lock_guard<std::mutex> lk(g_jobMx); g_curName.clear(); }
        int cat = (int)l, ok = g_resOk, failed = g_resFail;
        unsigned long long freed = g_resFreed;
        if ((cat == CAT_DL && w) || dlOpt) MarkCleaned();  // Zaehler nur zuruecksetzen, wenn alles geleert wurde
        RecordFreed(freed);
        ShowWindow(GetDlgItem(h, ID_CLEAN), SW_SHOW);
        std::wstring skipList;
        { std::lock_guard<std::mutex> lk(g_jobMx);
          for (const auto& n : g_skipped) skipList += (skipList.empty() ? L"" : L", ") + n; }
        if (failed > 5) skipList += L" …";
        std::wstring msg = std::to_wstring(ok) + Tr(L" Einträge gelöscht (") + FormatSize(freed) + Tr(L" frei), ") + std::to_wstring(failed) +
                           (cat == CAT_DL ? Tr(L" fehlgeschlagen.") : Tr(L" in Benutzung/übersprungen."));
        if (failed && !skipList.empty()) msg += Tr(L"\n\nÜbersprungen: ") + skipList;
        if (const int um = g_ultraDone.exchange(0)) {  // Ergebnis der Ultra-Aktionen
            const int res = g_ultraResult;
            if (res < 0) {
                msg += Tr(L"\n\nUltra-Aktionen: Die Administrator-Abfrage wurde abgelehnt oder konnte nicht gestartet werden. Es wurde nichts davon ausgeführt.");
            } else {
                msg += Tr(L"\n\nUltra-Aktionen:");
                if (um & ULTRA_DISM) msg += std::wstring(L"\n · ") + Tr(L"Komponentenspeicher (DISM)") + L": " + ((res & ULTRA_DISM) ? Tr(L"fehlgeschlagen") : Tr(L"erledigt"));
                if (um & ULTRA_VSS) msg += std::wstring(L"\n · ") + Tr(L"Alte Schattenkopien") + L": " + ((res & ULTRA_VSS) ? Tr(L"fehlgeschlagen") : Tr(L"erledigt"));
                if (um & ULTRA_EVT) msg += std::wstring(L"\n · ") + Tr(L"Ereignisprotokolle") + L": " + ((res & ULTRA_EVT) ? Tr(L"fehlgeschlagen") : Tr(L"erledigt"));
                if (um & ULTRA_WINOLD) msg += std::wstring(L"\n · ") + Tr(L"Vorherige Windows-Installation") + L": " + ((res & ULTRA_WINOLD) ? Tr(L"fehlgeschlagen") : Tr(L"erledigt"));
            }
        }
        if (g_page == 0) {
            g_doneText = FormatSize(freed) + Tr(L" Speicherplatz erfolgreich freigegeben");
            g_doneSub = std::to_wstring(ok) + Tr(L" Einträge entfernt") +
                        (failed ? L", " + std::to_wstring(failed) + Tr(L" übersprungen (in Benutzung)") : L"") +
                        (dlOpt ? Tr(L"  ·  Downloads liegen im Papierkorb") : L"");
        } else {
            PopulateList();
        }
        ScanAllAsync(h);
        InvalidateRect(h, nullptr, FALSE);
        if (g_page != 0) AppBox(h, msg.c_str(), kApp, MB_OK | MB_ICONINFORMATION);
        if ((cat == CAT_DL || dlOpt) && ok > 0) AskEmptyRecycleBin(h);
        return 0;
    }
    case WM_UPDATE_RESULT: {
        KillTimer(h, ID_UPDT);
        if (w) { PostQuitMessage(0); return 0; }  // installiert, die neue Version laeuft schon
        g_updating = false;
        std::wstring msg;
        { std::lock_guard<std::mutex> lk(g_updMx); msg = g_updMsg; }
        ShowWindow(GetDlgItem(h, ID_UPDATE), SW_SHOW);
        InvalidateRect(h, nullptr, FALSE);
        AppBox(h, msg.c_str(), L"Duncan's Tidy", MB_OK | MB_ICONERROR);
        return 0;
    }
    case WM_ADMIN_DONE:
        if (w) { if (g_page == kDrivePage) StartDriveScan(h); }
        else AppBox(h, Tr(L"Die Werte konnten nicht gelesen werden (Abfrage abgebrochen oder von Windows nicht gemeldet)."), kApp, MB_OK | MB_ICONINFORMATION);
        return 0;
    case WM_TOP_FILES:
        if (!g_busy && !g_updating) StartTopFiles(h, (wchar_t)w);
        return 0;
    case WM_ANALYZE_PATH: {  // Start mit --analyze <Ordner> (Explorer-Kontextmenue der Suite)
        const std::wstring p = g_pendingAnalyze;
        g_pendingAnalyze.clear();
        AnalyzePath(h, p);
        return 0;
    }
    case WM_COPYDATA: {  // ein zweiter Start reicht den Ordner an die laufende Instanz weiter
        const auto* cd = reinterpret_cast<const COPYDATASTRUCT*>(l);
        if (cd && cd->dwData == kAnalyzeMagic && cd->lpData && cd->cbData >= sizeof(wchar_t) && cd->cbData <= 2 * 4096 * sizeof(wchar_t)) {
            std::wstring p(static_cast<const wchar_t*>(cd->lpData), cd->cbData / sizeof(wchar_t));
            while (!p.empty() && p.back() == L'\0') p.pop_back();
            RestoreFromTray(h);
            AnalyzePath(h, p);
        }
        return TRUE;
    }
    case WM_TOP_DONE: {
        InvalidateRect(h, nullptr, FALSE);
        ShowTopDialog(h);
        return 0;
    }
    case WM_DRIVE_DONE: {
        {
            std::lock_guard<std::mutex> lk(g_statMx);
            g_drv = std::move(g_drvTmp);
        }
        if (g_page == kDrivePage) {
            g_aScroll = 0;
            FilterAutostart();
            UpdateAutoScroll();
        }
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_BROWSER_DONE: {
        {
            std::lock_guard<std::mutex> lk(g_statMx);
            g_brow = std::move(g_browTmp);
        }
        if (g_page == kBrowserPage) {
            g_aScroll = 0;
            FilterAutostart();
            UpdateAutoScroll();
        }
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_SCAN_DONE:
        if (g_page == 0) LoadAutostart();
        if (!g_busy && !g_listLoading && (PageCat(g_page) >= 0 && PageCat(g_page) != CAT_BROWSER)) {  // Groessen sind jetzt bekannt
            BuildGroupCards(PageCat(g_page), true);
            FilterAutostart();
            UpdateAutoScroll();
        }
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_UPDATE_FOUND: {
        std::wstring t = UpdateButtonText();
        SetWindowTextW(GetDlgItem(h, ID_UPDATE), t.c_str());
        Layout(h);  // Button passend zum Text verbreitern
        ShowWindow(GetDlgItem(h, ID_UPDATE), SW_SHOW);
        return 0;
    }
    case WM_SHOW_WHATSNEW: {  // nach einem Update einmalig die Neuerungen zeigen
        std::string raw;
        if (std::ifstream in{WhatsNewFile(), std::ios::binary}) raw.assign(std::istreambuf_iterator<char>(in), {});
        if (!raw.empty()) {
            std::error_code ec;
            fs::remove(WhatsNewFile(), ec);
            const std::wstring all = Utf8ToW(raw);
            size_t nl = all.find(L'\n');
            std::wstring ver = all.substr(0, nl);
            while (!ver.empty() && ver.back() == L'\r') ver.pop_back();
            const std::wstring notes = nl == std::wstring::npos ? L"" : all.substr(nl + 1);
            ShowUpdateDialog(h, Tr(L"Update installiert"), ver, notes.empty() ? std::wstring(Tr(L"Verbesserungen und Fehlerbehebungen.")) : notes,
                             Tr(L"Viel Spaß mit der neuen Version!"), Tr(L"Los geht's"), nullptr);
        }
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        SetTextColor(dc, kText);
        SetBkColor(dc, kPanel);
        return (LRESULT)g_panelBrush;
    }
    case WM_DRAWITEM:
        DrawButton((const DRAWITEMSTRUCT*)l);
        return TRUE;
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (g_updating) return 0;  // waehrend des Updates nichts anfassen
        if (g_busy && id != ID_EXIT) return 0;  // waehrend der Bereinigung nichts anderes anfassen
        if (id == ID_EXIT) {
            DestroyWindow(h);
        } else if (id == ID_MORE) {
            g_navMore = !g_navMore;
            if (!g_navMore && NavGrouped(g_page)) SetPage(h, 0);  // aktive Seite wuerde verschwinden: zurueck zur Uebersicht
            SetWindowTextW(GetDlgItem(h, ID_MORE), g_navMore ? Tr(L"Weniger") : Tr(L"Mehr"));
            Layout(h);
            InvalidateRect(h, nullptr, FALSE);
        } else if (id >= ID_NAV0 && id < ID_NAV0 + kPages) {
            SetPage(h, id - ID_NAV0);
        } else if (id == ID_UPDATE) {
            if (ShowUpdateDialog(h, Tr(L"Update verfügbar"), std::wstring(L"v") + kVersion + L"   →   v" + g_newVersion,
                                g_notes.empty() ? std::wstring(Tr(L"Verbesserungen und Fehlerbehebungen.")) : g_notes,
                                Tr(L"Das Programm lädt das Update herunter, prüft es und startet danach automatisch neu."),
                                Tr(L"Jetzt aktualisieren"), Tr(L"Später"))) {
                ShowWindow(GetDlgItem(h, ID_UPDATE), SW_HIDE);  // an dieser Stelle der Kopfzeile zeichnet das Fenster den Balken
                SetTimer(h, ID_UPDT, 100, nullptr);
                DoUpdate(h);
                InvalidateRect(h, nullptr, FALSE);
            }
        } else if (id == ID_PROTECT) {
            EnsureWhitelistFile();
            ShowProtectDialog(h);  // Schutzliste im Programm bearbeiten (statt im Editor)
            if (!g_busy) { PopulateList(); ScanAllAsync(h); }  // geschuetzte Eintraege verschwinden bzw. erscheinen sofort in der Liste
        } else if (id == ID_RAM) {
            StartRamOptimize(h);
        } else if (id == ID_ADMIN) {
            if (g_adminBusy) return 0;
            if (AppBox(h, Tr(L"Windows fragt gleich nach Administrator-Rechten. Damit liest das Programm nur die Gesundheitswerte deiner Laufwerke (Temperatur, Abnutzung, Betriebsstunden, Fehler). Es wird nichts verändert oder gelöscht."),
                            kApp, MB_OKCANCEL | MB_ICONINFORMATION) == IDOK)
                StartAdminHealth(h);
        } else if (id == ID_SEARCH && HIWORD(w) == EN_CHANGE) {
            g_aScroll = 0;
            FilterAutostart();
            UpdateAutoScroll();
        } else if (id == ID_TOGGLE && g_page == kUninstPage) {  // Sortierung umschalten: Groesse <-> Name
            g_unSortSize = !g_unSortSize;
            SortUninst();
            SetWindowTextW(GetDlgItem(h, ID_TOGGLE), g_unSortSize ? Tr(L"Sortieren: Größe") : Tr(L"Sortieren: Name"));
            g_aScroll = 0;
            FilterAutostart();
            UpdateAutoScroll();
            InvalidateRect(h, nullptr, FALSE);
        } else if (id == ID_TOGGLE && (g_page == kAutoPage || g_page == kBrowserPage || (g_page >= 1 && g_page <= 3))) {
            bool anyOff = false;
            const bool brow = g_page == kBrowserPage;   // dort nur die Caches: Cookies und Verlauf werden nie versehentlich mitgewaehlt
            for (int i : g_autoVis) if (Cur()[i].avail && !(brow && Cur()[i].bkind) && !Cur()[i].want) anyOff = true;
            for (int i : g_autoVis) if (Cur()[i].avail && !(brow && Cur()[i].bkind)) Cur()[i].want = anyOff;
            InvalidateRect(g_alist, nullptr, FALSE);
            InvalidateRect(h, nullptr, FALSE);
        } else if (id == ID_CLEAN) {
            DoClean(h);
        }
        return 0;
    }
    case WM_DESTROY:
        TrayRemove(h);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---- Selbst-Installation: die .exe richtet sich beim ersten Start selbst ein (statt Install.bat) ----
static const wchar_t* kInstallDirName = L"DuncansTidy";
static const wchar_t* kTaskName = L"DuncansTidy";
static const wchar_t* kShortcutName = L"Duncan's Tidy.lnk";

static fs::path InstalledExe() { return KnownFolder(FOLDERID_LocalAppData) / kInstallDirName / L"DuncansTidy.exe"; }

static std::vector<int> ExeVersion(const fs::path& p) {
    DWORD ignored;
    DWORD n = GetFileVersionInfoSizeW(p.c_str(), &ignored);
    if (!n) return {};
    std::vector<char> buf(n);
    if (!GetFileVersionInfoW(p.c_str(), 0, n, buf.data())) return {};
    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(buf.data(), L"\\", (LPVOID*)&fi, &len) || !fi) return {};
    return {(int)HIWORD(fi->dwFileVersionMS), (int)LOWORD(fi->dwFileVersionMS), (int)HIWORD(fi->dwFileVersionLS), (int)LOWORD(fi->dwFileVersionLS)};
}

static std::wstring PsQuote(const std::wstring& s) {  // fuer PowerShell-Strings in einfachen Anfuehrungszeichen
    std::wstring r;
    for (wchar_t c : s) { r += c; if (c == L'\'' || (c >= 0x2018 && c <= 0x201B)) r += c; }  // PowerShell kennt auch typografische Apostrophe als Anfuehrungszeichen
    return r;
}

// Fuehrt ein PowerShell-Skript unsichtbar aus (per -EncodedCommand, damit keine Anfuehrungszeichen-Probleme entstehen)
static bool RunPowerShell(const std::wstring& script) {
    const std::wstring b64 = PsEncode(script);
    if (b64.empty()) return false;
    std::wstring cmd = L"\"" + SystemFile(L"WindowsPowerShell\\v1.0\\powershell.exe") + L"\"" + kPsArgs + b64;
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
    DWORD w = WaitForSingleObject(pi.hProcess, 30000), code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return w == WAIT_OBJECT_0 && code == 0;
}

// Rueckgabe true: Es wurde die installierte Kopie gestartet, dieser Prozess soll sich beenden.
static bool OfferInstall() {
    wchar_t selfBuf[MAX_PATH];
    GetModuleFileNameW(nullptr, selfBuf, MAX_PATH);
    const fs::path self = selfBuf, dest = InstalledExe();
    if (_wcsicmp(self.c_str(), dest.c_str()) == 0) return false;  // laeuft schon aus dem Installationsordner
    std::error_code ec;
    const fs::path flag = StateFile().parent_path() / L"portable.flag";
    if (!fs::exists(dest, ec)) {
        if (fs::exists(flag, ec)) return false;  // frueher abgelehnt: nicht noch einmal fragen
        int a = AppBox(nullptr,
            (std::wstring(Tr(L"Duncan's Tidy ist noch nicht installiert. Jetzt installieren?\n\n")) +
             Tr(L" • Räumt den Downloads-Ordner alle 30 Tage automatisch auf (mit Rückfrage)\n") +
             Tr(L" • Legt eine Verknüpfung im Startmenü an\n") +
             Tr(L" • Braucht keine Administrator-Rechte\n\n") +
             Tr(L"„Nein“ startet das Programm ohne Installation (und fragt nicht wieder).")).c_str(),
            kApp, MB_YESNO | MB_ICONQUESTION);
        if (a != IDYES) {
            std::ofstream{flag} << 1;
            return false;
        }
        // Automatisches Leeren des Downloads-Ordners ist freiwillig
        SetAutoClean(AppBox(nullptr,
            (std::wstring(Tr(L"Soll der Downloads-Ordner alle 30 Tage automatisch aufgeräumt werden?\n\n")) +
             Tr(L" • Vorher kommt immer eine Rückfrage\n") +
             Tr(L" • Alles landet erst im Papierkorb\n") +
             Tr(L" • Du kannst das jederzeit in den Einstellungen ein- oder ausschalten")).c_str(),
            kApp, MB_YESNO | MB_ICONQUESTION) == IDYES);
    } else if (ExeVersion(dest) >= ExeVersion(self)) {
        ShellExecuteW(nullptr, L"open", dest.c_str(), nullptr, nullptr, SW_SHOW);  // installierte Version ist gleich neu oder neuer
        return true;
    } else if (AppBox(nullptr, Tr(L"Eine ältere Version von Duncan's Tidy ist installiert. Auf diese Version aktualisieren?"),
                           kApp, MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return false;
    }

    fs::create_directories(dest.parent_path(), ec);
    if (!CopyFileW(self.c_str(), dest.c_str(), FALSE)) {
        AppBox(nullptr, Tr(L"Die Installation ging nicht. Ist Duncan's Tidy schon geöffnet? Bitte erst schließen und noch einmal versuchen."),
                    kApp, MB_OK | MB_ICONWARNING);
        return false;
    }
    std::wstring ps =
        L"$ErrorActionPreference='Stop'; $d='" + PsQuote(dest.wstring()) + L"';"
        L"$a=New-ScheduledTaskAction -Execute $d -Argument '--auto' -WorkingDirectory (Split-Path $d);"
        L"$t=New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME;"
        L"$s=New-ScheduledTaskSettingsSet -StartWhenAvailable -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries;"
        L"Register-ScheduledTask -TaskName '" + PsQuote(kTaskName) + L"' -Action $a -Trigger $t -Settings $s -Force | Out-Null;"
        L"Unregister-ScheduledTask -TaskName 'DownloadCleaner' -Confirm:$false -ErrorAction SilentlyContinue;"
        L"$l=Join-Path ([Environment]::GetFolderPath('Programs')) '" + PsQuote(kShortcutName) + L"';"
        L"$sc=(New-Object -ComObject WScript.Shell).CreateShortcut($l); $sc.TargetPath=$d; $sc.Save()";
    bool ok = RunPowerShell(ps);
    if (!ok)
        AppBox(nullptr, Tr(L"Das Programm wurde kopiert, aber die automatische Bereinigung beim Anmelden konnte nicht eingerichtet werden."),
                    kApp, MB_OK | MB_ICONWARNING);
    ShellExecuteW(nullptr, L"open", dest.c_str(), nullptr, nullptr, SW_SHOW);
    return true;
}

// ---- Splash-Screen beim Start: kleines Fenster mit Logo, der Besen faegt von links nach rechts, darunter "wird geladen" ----
// Danach erscheint das Hauptfenster, und das Splash-Fenster blendet sanft darueber aus. Die Analyse laeuft schon im Hintergrund.
constexpr int kSplashW = 460, kSplashH = 320;
constexpr ULONGLONG kSplashMs = 1250, kSplashFadeInMs = 220, kSplashFadeOutMs = 280;
static float g_splashP = 0.f;

static LRESULT CALLBACK SplashProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC screen = BeginPaint(h, &ps);
        RECT cr;
        GetClientRect(h, &cr);
        HDC dc = CreateCompatibleDC(screen);
        HBITMAP bmp = CreateCompatibleBitmap(screen, cr.right, cr.bottom);
        HGDIOBJ old = SelectObject(dc, bmp);
        FillRect(dc, &cr, g_bgBrush);
        SetBkMode(dc, TRANSPARENT);
        SetLogical(dc);
        RECT outer{0, 0, kSplashW, kSplashH}, inner{2, 2, kSplashW - 2, kSplashH - 2};  // feiner Rand im Farbton der Spuren
        FillRound(dc, outer, kTrack, 30);
        FillRound(dc, inner, kBg, 28);
        if (HICON ic = AppIcon(D(72))) {
            ResetLogical(dc);
            DrawIconEx(dc, D((kSplashW - 72) / 2), D(24), ic, D(72), D(72), 0, nullptr, DI_NORMAL);
            SetLogical(dc);
        }
        SetTextAlign(dc, TA_CENTER);
        DrawTxt(dc, g_fBig, kStrong, kSplashW / 2, 102, kApp);
        g_sceneP = g_splashP;
        g_sceneSwing = 0.f;
        DrawScene(dc, 0, kSplashW, 133);  // Besen und Spuren; die Grundlinie liegt bei y = 100 + 133
        DrawTxt(dc, g_fSm, kMuted, kSplashW / 2, 268, Tr(L"Duncan's Tidy wird geladen …"));
        ResetLogical(dc);
        BitBlt(screen, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old);
        DeleteObject(bmp);
        DeleteDC(dc);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// Zeigt den Splash und danach das Hauptfenster. Rueckgabe false, falls das Splash-Fenster nicht erstellt werden konnte.
static bool RunSplash(HINSTANCE inst, HWND mainWnd) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = SplashProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.lpszClassName = L"DuncansTidySplash";
    RegisterClassW(&wc);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const int w = D(kSplashW), h = D(kSplashH);
    HWND sp = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, kApp, WS_POPUP,
                              wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h, nullptr, nullptr, inst, nullptr);
    if (!sp) return false;
    SetWindowRgn(sp, CreateRoundRectRgn(0, 0, w + 1, h + 1, D(30), D(30)), TRUE);  // abgerundete Ecken
    SetLayeredWindowAttributes(sp, 0, 0, LWA_ALPHA);
    g_splashP = 0.f;
    ShowWindow(sp, SW_SHOWNOACTIVATE);
    timeBeginPeriod(1);
    auto pump = [&]() -> bool {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { PostQuitMessage((int)msg.wParam); return false; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return true;
    };
    bool alive = true;
    const ULONGLONG t0 = GetTickCount64();
    for (ULONGLONG t = 0; alive && t < kSplashMs; t = GetTickCount64() - t0) {
        alive = pump();
        const double fade = std::min(1.0, (double)t / kSplashFadeInMs);
        SetLayeredWindowAttributes(sp, 0, (BYTE)std::lround(255 * fade), LWA_ALPHA);
        g_splashP = (float)std::clamp(((double)t - 120.0) / (kSplashMs - 220.0), 0.0, 1.0);
        InvalidateRect(sp, nullptr, FALSE);
        UpdateWindow(sp);
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 8, QS_ALLINPUT);
    }
    g_sceneP = 1.f;  // das Hauptfenster startet mit ruhendem Besen (der Splash hat schon gefegt)
    g_sceneSwing = 0.f;
    ShowWindow(mainWnd, SW_SHOW);
    UpdateWindow(mainWnd);
    const ULONGLONG f0 = GetTickCount64();
    for (ULONGLONG t = 0; alive && t < kSplashFadeOutMs; t = GetTickCount64() - f0) {  // Splash blendet ueber dem sichtbaren Hauptfenster aus
        alive = pump();
        SetLayeredWindowAttributes(sp, 0, (BYTE)std::lround(255 * (1.0 - (double)t / kSplashFadeOutMs)), LWA_ALPHA);
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 8, QS_ALLINPUT);
    }
    DestroyWindow(sp);
    timeEndPeriod(1);
    SetForegroundWindow(mainWnd);
    return true;
}

static int RunWindow(HINSTANCE inst, bool trayStart) {
    // Nur eine Instanz: ein zweiter Start holt das vorhandene Fenster (auch aus dem Tray) nach vorn
    HANDLE instanceMutex = CreateMutexW(nullptr, FALSE, L"Local\\DuncansTidyWindow");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!g_pendingAnalyze.empty()) {  // --analyze: den Ordner an die laufende Instanz weitergeben und beenden
            if (HWND other = FindWindowW(L"DuncansTidyWnd", nullptr)) {
                COPYDATASTRUCT cd{};
                cd.dwData = kAnalyzeMagic;
                cd.cbData = static_cast<DWORD>((g_pendingAnalyze.size() + 1) * sizeof(wchar_t));
                cd.lpData = const_cast<wchar_t*>(g_pendingAnalyze.c_str());
                SendMessageTimeoutW(other, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cd), SMTO_ABORTIFHUNG, 5000, nullptr);
            }
            CloseHandle(instanceMutex);
            return 0;
        }
        if (!trayStart)
            if (HWND other = FindWindowW(L"DuncansTidyWnd", nullptr)) PostMessageW(other, WM_SHOW_FROM_OTHER, 0, 0);
        CloseHandle(instanceMutex);
        instanceMutex = nullptr;
        for (int i = 0; i < 15 && !instanceMutex; ++i) {  // laeuft gerade ein Update-Neustart, ist die alte Instanz gleich weg
            Sleep(200);
            instanceMutex = CreateMutexW(nullptr, FALSE, L"Local\\DuncansTidyWindow");
            if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(instanceMutex); instanceMutex = nullptr; }
        }
        if (!instanceMutex) return 0;
    }
    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    g_trayOnClose = GetFlag(L"TrayOnClose", false);
    g_animOn = !GetFlag(L"AnimOff", false);
    g_lang = LoadLanguage();  // gespeicherte Sprache, beim ersten Start die von Windows
    EnsureUi();


    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));  // Icon aus version.rc
    wc.hbrBackground = CreateSolidBrush(kBg);
    wc.lpszClassName = L"DuncansTidyWnd";
    RegisterClassW(&wc);

    // Groesse an den Bildschirm anpassen und mittig platzieren
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    RECT r{0, 0, std::min<LONG>(D(1080), wa.right - wa.left - D(80)), std::min<LONG>(D(720), wa.bottom - wa.top - D(120))};
    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    AdjustWindowRect(&r, style, FALSE);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    HWND hwnd = CreateWindowW(wc.lpszClassName, kApp, style, wa.left + (wa.right - wa.left - ww) / 2,
                              wa.top + (wa.bottom - wa.top - wh) / 2, ww, wh, nullptr, nullptr, inst, nullptr);
    if (trayStart) {  // mit Windows gestartet: unsichtbar im Tray, Fenster erst auf Klick
        TrayAdd(hwnd, false);
    } else {
        if (MotionAllowed() && RunSplash(inst, hwnd)) {
            // Splash lief und hat das Hauptfenster gezeigt; der Besen hat dort schon gefegt, deshalb kein zweites Intro
        } else {
            if (MotionAllowed()) g_sceneP = 0.f;  // der erste sichtbare Frame zeigt schon den Startzustand (Besen links, Schmutz liegt da)
            ShowWindow(hwnd, SW_SHOW);
            UpdateWindow(hwnd);
            StartAnim(hwnd, true);
        }
    }
    if (!g_pendingAnalyze.empty()) PostMessageW(hwnd, WM_ANALYZE_PATH, 0, 0);  // --analyze: Ordner nach dem Start untersuchen
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    Gdiplus::GdiplusShutdown(g_gdiToken);
    return 0;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
    // --analyze <Ordner|Laufwerk>: Fenster mit der Analyse der groessten Dateien (Explorer-Kontextmenue der Suite).
    // Wichtig: genau diese Form gesondert behandeln, denn jedes andere Argument wuerde den Kommandozeilen-Reinigungsmodus starten.
    {
        int wargc = 0;
        LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
        const bool analyze = wargv && wargc == 3 && wcscmp(wargv[1], L"--analyze") == 0;
        if (analyze) g_pendingAnalyze = wargv[2];
        if (wargv) LocalFree(wargv);
        if (analyze) {
            g_lang = LoadLanguage();
            return RunWindow(inst, false);
        }
    }
    bool dryRun = false, permanent = false, force = false, autoMode = false, trayStart = false;
    for (int i = 1; i < __argc; ++i) {
        std::string a = __argv[i];
        if (a == "--tray") trayStart = true;
        else if (a == "--dry-run") dryRun = true;
        else if (a == "--permanent") permanent = true;
        else if (a == "--force") force = true;
        else if (a == "--auto") autoMode = true;
    }

    if (__argc == 1 || (__argc == 2 && trayStart)) {  // Doppelklick (oder Start mit Windows = --tray): Fenster
        g_lang = LoadLanguage();  // schon vor dem Installationsdialog die richtige Sprache
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        DeleteFileW((std::wstring(self) + L".old").c_str());  // Rest eines frueheren Updates
        if (!trayStart && OfferInstall()) return 0;           // nicht installiert: anbieten, sich selbst einzurichten
        return RunWindow(inst, trayStart);
    }

    // --verify <datei> <signatur>: prueft eine Signatur (Exit-Code 0 = gueltig); fuer Tests
    if (__argc == 4 && std::string(__argv[1]) == "--verify") {
        auto readAll = [](const char* path) {
            std::ifstream in(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), {});
        };
        return VerifyUpdateSignature(readAll(__argv[2]), readAll(__argv[3])) ? 0 : 1;
    }

    // Kommandozeilenmodus: Ausgabe in die Konsole des Aufrufers leiten
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }

    if (autoMode && !AutoCleanOn()) return 0;  // Auto-Clean wurde ausgeschaltet
    if (!Due(force)) return 0;
    int ok, failed;
    if (!CleanDownloads(dryRun, permanent, ok, failed)) {
        std::cerr << "Downloads-Ordner nicht gefunden.\n";
        return 1;
    }
    if (!dryRun) std::cout << ok << " Eintraege geloescht, " << failed << " fehlgeschlagen.\n";
    int extraOk = 0, extraFailed = 0;  // Temp/System: benutzte Dateien bleiben normal liegen, zaehlt nicht als Fehler
    CleanExtras(dryRun, extraOk, extraFailed);
    if (!dryRun && (extraOk || extraFailed)) std::cout << "Temp/System: " << extraOk << " Eintraege geloescht, " << extraFailed << " in Benutzung.\n";
    // Nach der automatischen Bereinigung nachfragen, ob auch der Papierkorb geleert werden soll
    if (autoMode && !dryRun && !permanent && ok > 0) AskEmptyRecycleBin(nullptr);
    return failed ? 2 : 0;
}





























































































