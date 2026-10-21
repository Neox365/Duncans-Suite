// main.cpp - "Duncan's Suite": zentraler Launcher fuer Duncan's Tidy und Duncan's Driver Updater.
//
// Drei grosse Karten (Tidy, Driver Updater, PDF Reader) starten die jeweilige Anwendung (zuerst aus dem eigenen Ordner, fuer Tidy zusaetzlich
// aus %LOCALAPPDATA%\DuncansTidy). Fehlt eine EXE, erscheint ein eigener Dark-Dialog statt einer MessageBox.
//
// Linker-Bibliotheken: advapi32, user32, gdi32, gdiplus, shell32, ole32, dwmapi

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <bcrypt.h>
#include <psapi.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <winver.h>
#include <wininet.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../src/Icons.h"
#include "../src/SuiteSafety.h"
#include "../common/Theme.h"
#include "../common/Gfx.h"
#include "../common/VersionUtil.h"
#include "../common/Changelog.h"
#include "../common/Crypto.h"
#include "public_key.inc"        // kSuitePublicKey: Suite-Installer und PDF Reader (erzeugt von tools\MakeSigningKey.ps1)
#include "tidy_public_key.inc"   // kTidyPublicKey: DuncansTidy.exe

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "bcrypt.lib")
#endif

#ifndef SUITE_VERSION
#define SUITE_VERSION L"1.1.34"
#endif

namespace
{
    const wchar_t* kClassName = L"DuncansSuiteWnd";
    UINT ShowRequestMessage()   // zweite Instanz bittet die laufende, sich zu zeigen
    {
        static const UINT msg = RegisterWindowMessageW(L"DuncansSuite.ShowRequest");
        return msg;
    }

    // Gemeinsame Palette und Zeichenhelfer aus common/ (gleich in allen Programmen)
    using namespace theme;
    using gfx::Blend;
    using gfx::DrawLabel;
    using gfx::FillRound;
    using gfx::TextWidth;

    void DrawWrapped(HDC dc, HFONT font, COLORREF color, RECT rc, const std::wstring& text)
    {
        HGDIOBJ old = SelectObject(dc, font);
        SetTextColor(dc, color);
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rc, DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old);
    }

    int MeasureWrapped(HDC dc, HFONT font, const std::wstring& text, int width)
    {
        RECT r{ 0, 0, std::max(1, width), 0 };
        HGDIOBJ old = SelectObject(dc, font);
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old);
        return r.bottom - r.top;
    }

    std::wstring ExeDir()
    {
        wchar_t path[MAX_PATH * 2] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH * 2);
        std::wstring p = path;
        const size_t slash = p.find_last_of(L"\\/");
        return slash == std::wstring::npos ? L"." : p.substr(0, slash);
    }

    bool FileExists(const std::wstring& path)
    {
        const DWORD attr = GetFileAttributesW(path.c_str());
        return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
    }

    std::wstring LocalAppData()
    {
        wchar_t buf[MAX_PATH * 2] = {};
        const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH * 2);
        return n > 0 && n < MAX_PATH * 2 ? std::wstring(buf) : std::wstring();
    }

    // ---- Einstellungen (%LOCALAPPDATA%\DuncansSuite\settings.ini) ----------------------------
    struct Settings
    {
        bool autoCheck = true;        // Update-Pruefung beim Start
        bool autostart = false;       // Suite mit Windows starten
        bool trayOnClose = false;     // X = in den Infobereich minimieren statt beenden
        bool trayOnLaunch = false;    // nach dem Start eines Programms in den Infobereich minimieren
        bool ctxPdf = false;          // Explorer-Kontextmenue "Mit Duncan's PDF oeffnen" (Registry ist die Wahrheit)
        bool ctxTidy = false;         // Explorer-Kontextmenue "Mit Duncan's Tidy analysieren"
        bool autoRestorePoint = true; // Privacy Shield / Driver Updater legen vor kritischen Aktionen einen Wiederherstellungspunkt an
        std::wstring downloadDir;     // leer = Standard (%LOCALAPPDATA%\DuncansSuite\update)
    };

    const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* kRunValue = L"DuncansSuite";

    std::wstring SettingsPath()
    {
        const std::wstring local = LocalAppData();
        return local.empty() ? std::wstring() : local + L"\\DuncansSuite\\settings.ini";
    }

    // Nur absolute lokale Ordner (Laufwerk oder UNC) sind als Download-Ordner zulaessig.
    bool IsUsableDir(const std::wstring& dir)
    {
        return dir.size() >= 3 && dir.size() < MAX_PATH - 20 && !PathIsRelativeW(dir.c_str());
    }

    bool IsAutostartEnabled()
    {
        wchar_t buf[MAX_PATH * 2] = {};
        DWORD size = sizeof(buf) - sizeof(wchar_t);
        return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS;
    }

    bool SetAutostart(bool on)
    {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
            return false;
        LONG rc;
        if (on)
        {
            wchar_t exe[MAX_PATH * 2] = {};
            GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
            const std::wstring value = L"\"" + std::wstring(exe) + L"\"";
            rc = RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                                static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        }
        else
        {
            rc = RegDeleteValueW(key, kRunValue);
            if (rc == ERROR_FILE_NOT_FOUND)
                rc = ERROR_SUCCESS;
        }
        RegCloseKey(key);
        return rc == ERROR_SUCCESS;
    }

    bool VerbExists(const wchar_t* verbKey);   // weiter unten (Explorer-Kontextmenue)
    extern const wchar_t* kPdfVerb;
    extern const wchar_t* kTidyVerbs[2];

    Settings LoadSettings()
    {
        Settings s;
        const std::wstring ini = SettingsPath();
        if (!ini.empty())
        {
            s.autoCheck = GetPrivateProfileIntW(L"Allgemein", L"UpdatePruefung", 1, ini.c_str()) != 0;
            s.trayOnClose = GetPrivateProfileIntW(L"Allgemein", L"TrayBeimSchliessen", 0, ini.c_str()) != 0;
            s.trayOnLaunch = GetPrivateProfileIntW(L"Allgemein", L"TrayBeimProgrammstart", 0, ini.c_str()) != 0;
            s.autoRestorePoint = GetPrivateProfileIntW(L"Allgemein", L"AutoWiederherstellungspunkt", 1, ini.c_str()) != 0;
            wchar_t dir[MAX_PATH * 2] = {};
            GetPrivateProfileStringW(L"Allgemein", L"DownloadOrdner", L"", dir, MAX_PATH * 2, ini.c_str());
            if (IsUsableDir(dir))
                s.downloadDir = dir;
        }
        s.autostart = IsAutostartEnabled();   // die Registry ist die Wahrheit, nicht die INI
        s.ctxPdf = VerbExists(kPdfVerb);
        s.ctxTidy = VerbExists(kTidyVerbs[0]) && VerbExists(kTidyVerbs[1]);
        return s;
    }

    bool SaveSettings(const Settings& s)
    {
        const std::wstring ini = SettingsPath();
        if (ini.empty())
            return false;
        CreateDirectoryW((LocalAppData() + L"\\DuncansSuite").c_str(), nullptr);
        return WritePrivateProfileStringW(L"Allgemein", L"UpdatePruefung", s.autoCheck ? L"1" : L"0", ini.c_str()) &&
               WritePrivateProfileStringW(L"Allgemein", L"TrayBeimSchliessen", s.trayOnClose ? L"1" : L"0", ini.c_str()) &&
               WritePrivateProfileStringW(L"Allgemein", L"TrayBeimProgrammstart", s.trayOnLaunch ? L"1" : L"0", ini.c_str()) &&
               WritePrivateProfileStringW(L"Allgemein", L"DownloadOrdner", s.downloadDir.c_str(), ini.c_str()) &&
               WritePrivateProfileStringW(L"Allgemein", L"AutoWiederherstellungspunkt", s.autoRestorePoint ? L"1" : L"0", ini.c_str()) &&
               WritePrivateProfileStringW(L"Allgemein", L"Autostart", s.autostart ? L"1" : L"0", ini.c_str());
    }

    // ---- Explorer-Kontextmenue (HKCU\Software\Classes, ohne Administrator-Rechte) -------------
    const wchar_t* kPdfVerb = L"Software\\Classes\\SystemFileAssociations\\.pdf\\shell\\DuncansPdf";
    const wchar_t* kTidyVerbs[2] = { L"Software\\Classes\\Directory\\shell\\DuncansTidy", L"Software\\Classes\\Drive\\shell\\DuncansTidy" };

    bool SetRegString(const std::wstring& key, const wchar_t* name, const std::wstring& value)
    {
        HKEY k = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
            return false;
        const LONG rc = RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
        return rc == ERROR_SUCCESS;
    }

    bool VerbExists(const wchar_t* verbKey)
    {
        HKEY k = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, verbKey, 0, KEY_READ, &k) != ERROR_SUCCESS)
            return false;
        RegCloseKey(k);
        return true;
    }

    bool RegisterVerb(const wchar_t* verbKey, const wchar_t* label, const std::wstring& icon, const std::wstring& command)
    {
        const std::wstring key = verbKey;
        return SetRegString(key, nullptr, label) && SetRegString(key, L"Icon", icon) && SetRegString(key + L"\\command", nullptr, command);
    }

    // Entfernt nur den eigenen Verb-Schluessel (feste Pfade oben, nichts davon kommt von aussen).
    bool UnregisterVerb(const wchar_t* verbKey)
    {
        const LONG rc = RegDeleteTreeW(HKEY_CURRENT_USER, verbKey);
        return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
    }

    std::wstring SelfPath()
    {
        wchar_t p[MAX_PATH * 2] = {};
        GetModuleFileNameW(nullptr, p, MAX_PATH * 2);
        return p;
    }

    // Programm im Ordner der Suite oder im Standard-Installationsordner suchen (wie die Karten); leer = nicht gefunden
    std::wstring FindExeFile(const wchar_t* exeName, const wchar_t* installFolder)
    {
        const std::wstring own = ExeDir() + L"\\" + exeName;
        if (FileExists(own))
            return own;
        const std::wstring local = LocalAppData();
        if (!local.empty())
        {
            const std::wstring inst = local + L"\\" + installFolder + L"\\" + exeName;
            if (FileExists(inst))
                return inst;
        }
        return std::wstring();
    }

    // Legt die Eintraege an oder entfernt sie. Gibt Probleme als Text zurueck (leer = alles gut).
    std::wstring ApplyContextMenus(bool pdf, bool tidy)
    {
        std::wstring problems;
        if (pdf)
        {
            const std::wstring exe = FindExeFile(L"DuncansPdf.exe", L"DuncansPdf");
            if (exe.empty())
                problems += L"Der PDF Reader wurde nicht gefunden – das PDF-Kontextmenü wurde nicht eingerichtet.\n";
            else if (!RegisterVerb(kPdfVerb, L"Mit Duncan's PDF öffnen", L"\"" + exe + L"\",0", L"\"" + exe + L"\" \"%1\""))
                problems += L"Das PDF-Kontextmenü konnte nicht eingerichtet werden.\n";
        }
        else if (!UnregisterVerb(kPdfVerb))
            problems += L"Das PDF-Kontextmenü konnte nicht entfernt werden.\n";

        if (tidy)
        {
            // Der Aufruf geht ueber die Suite (--tidy-analyze): sie prueft, ob ein passendes Tidy installiert ist, bevor sie es startet
            const std::wstring self = SelfPath();
            const std::wstring tidyExe = FindExeFile(L"DuncansTidy.exe", L"DuncansTidy");
            const std::wstring icon = L"\"" + (tidyExe.empty() ? self : tidyExe) + L"\",0";
            const std::wstring cmd = L"\"" + self + L"\" --tidy-analyze \"%1\"";
            for (const wchar_t* key : kTidyVerbs)
                if (!RegisterVerb(key, L"Mit Duncan's Tidy analysieren", icon, cmd))
                {
                    problems += L"Das Tidy-Kontextmenü konnte nicht eingerichtet werden.\n";
                    break;
                }
        }
        else
            for (const wchar_t* key : kTidyVerbs)
                if (!UnregisterVerb(key))
                {
                    problems += L"Das Tidy-Kontextmenü konnte nicht entfernt werden.\n";
                    break;
                }
        return problems;
    }

    // Beim Start: bestehende Eintraege auf den aktuellen Programmpfad bringen (Suite verschoben/aktualisiert); entfernt nichts
    void RefreshContextMenus()
    {
        if (VerbExists(kPdfVerb))
        {
            const std::wstring exe = FindExeFile(L"DuncansPdf.exe", L"DuncansPdf");
            if (!exe.empty())
                RegisterVerb(kPdfVerb, L"Mit Duncan's PDF öffnen", L"\"" + exe + L"\",0", L"\"" + exe + L"\" \"%1\"");
        }
        if (VerbExists(kTidyVerbs[0]) || VerbExists(kTidyVerbs[1]))
        {
            const std::wstring self = SelfPath();
            const std::wstring tidyExe = FindExeFile(L"DuncansTidy.exe", L"DuncansTidy");
            const std::wstring icon = L"\"" + (tidyExe.empty() ? self : tidyExe) + L"\",0";
            const std::wstring cmd = L"\"" + self + L"\" --tidy-analyze \"%1\"";
            for (const wchar_t* key : kTidyVerbs)
                RegisterVerb(key, L"Mit Duncan's Tidy analysieren", icon, cmd);
        }
    }

    // Ist die Datei mindestens Version major.minor.patch? (Datei-Versionsinfo)
    bool FileVersionAtLeast(const std::wstring& path, int major, int minor, int patch, std::wstring* found = nullptr)
    {
        DWORD ignored = 0;
        const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
        if (size == 0)
            return false;
        std::vector<char> data(size);
        VS_FIXEDFILEINFO* info = nullptr;
        UINT len = 0;
        if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()) || !VerQueryValueW(data.data(), L"\\", reinterpret_cast<LPVOID*>(&info), &len) || !info)
            return false;
        const int a = HIWORD(info->dwFileVersionMS), b = LOWORD(info->dwFileVersionMS), c = HIWORD(info->dwFileVersionLS);
        if (found)
            *found = std::to_wstring(a) + L"." + std::to_wstring(b) + L"." + std::to_wstring(c);
        if (a != major)
            return a > major;
        if (b != minor)
            return b > minor;
        return c >= patch;
    }

    // Aufruf aus dem Explorer-Kontextmenue: "DuncansSuite.exe --tidy-analyze "<Ordner>"". rest = Text hinter dem Schalter.
    int RunTidyAnalyze(std::wstring rest)
    {
        // Pfad von Hand aus der Kommandozeile lesen: ein Laufwerk ("C:\") ergibt in Anfuehrungszeichen sonst "C:\" und verschluckt das Ende
        while (!rest.empty() && rest.front() == L' ')
            rest.erase(rest.begin());
        if (!rest.empty() && rest.front() == L'"')
            rest.erase(rest.begin());
        while (!rest.empty() && (rest.back() == L' ' || rest.back() == L'"'))
            rest.pop_back();
        while (rest.size() > 3 && (rest.back() == L'\\' || rest.back() == L'/'))
            rest.pop_back();
        if (rest.size() == 3 && rest[1] == L':' && (rest[2] == L'\\' || rest[2] == L'/'))
            rest.pop_back();   // "C:\" -> "C:" (Laufwerk)
        const bool drive = rest.size() == 2 && rest[1] == L':';
        const DWORD attr = rest.empty() || rest.size() > 32000 ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(drive ? (rest + L"\\").c_str() : rest.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
        {
            MessageBoxW(nullptr, L"Dieser Ordner wurde nicht gefunden oder ist nicht lesbar.", L"Duncan's Suite", MB_ICONWARNING | MB_OK);
            return 1;
        }
        const std::wstring tidy = FindExeFile(L"DuncansTidy.exe", L"DuncansTidy");
        if (tidy.empty())
        {
            if (MessageBoxW(nullptr, L"Duncan's Tidy ist nicht installiert.\n\nDie Suite öffnen, um es mit einem Klick herunterzuladen?", L"Duncan's Suite",
                            MB_ICONINFORMATION | MB_YESNO) == IDYES)
                ShellExecuteW(nullptr, L"open", SelfPath().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 1;
        }
        std::wstring version;
        if (!FileVersionAtLeast(tidy, 1, 1, 4, &version))
        {
            MessageBoxW(nullptr, (L"Die installierte Version von Duncan's Tidy (" + (version.empty() ? std::wstring(L"unbekannt") : version) +
                                  L") kann noch keine Ordner analysieren.\n\nBitte Tidy auf Version 1.1.4 oder neuer aktualisieren (Tidy: Update im Programm, oder GitHub-Release).").c_str(),
                        L"Duncan's Suite", MB_ICONINFORMATION | MB_OK);
            return 1;
        }
        const std::wstring params = L"--analyze \"" + rest + L"\"";
        const std::wstring dir = tidy.substr(0, tidy.find_last_of(L"\\/"));
        return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", tidy.c_str(), params.c_str(), dir.c_str(), SW_SHOWNORMAL)) > 32 ? 0 : 1;
    }

    // Ordnerauswahl; leer = abgebrochen.
    std::wstring PickFolder(HWND owner, const std::wstring& start)
    {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        std::wstring result;
        BROWSEINFOW bi{};
        bi.hwndOwner = owner;
        bi.lpszTitle = L"Download-Ordner für Update-Installer wählen";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        bi.lpfn = [](HWND h, UINT m, LPARAM, LPARAM data) -> int
        {
            if (m == BFFM_INITIALIZED && data)
                SendMessageW(h, BFFM_SETSELECTIONW, TRUE, data);
            return 0;
        };
        bi.lParam = start.empty() ? 0 : reinterpret_cast<LPARAM>(start.c_str());
        if (PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi))
        {
            wchar_t path[MAX_PATH] = {};
            if (SHGetPathFromIDListW(pidl, path))
                result = path;
            CoTaskMemFree(pidl);
        }
        if (SUCCEEDED(co))
            CoUninitialize();
        return result;
    }
}

// ---- Download (WinINet) --------------------------------------------------------------------
// Laedt <base><exe> und <base><exe>.sha256 per HTTPS von den GitHub-Releases, prueft die SHA-256-Summe
// waehrend des Schreibens und legt die Datei erst nach erfolgreicher Pruefung am Ziel ab.

constexpr UINT WM_DL_PROGRESS = WM_APP + 1;   // wParam = Prozent (0..100) oder 101 = unbekannt, lParam = Tool-Index
constexpr int kSuiteIdx = 100;                  // Pseudo-Index fuer den Suite-Installer (Tool-Indizes sind 0..3)
constexpr UINT WM_DL_DONE     = WM_APP + 2;   // wParam = 1 Erfolg / 0 Fehler, lParam = Tool-Index; Text in m_dlMessage

namespace
{
    struct InetHandle
    {
        HINTERNET h = nullptr;
        InetHandle() = default;
        explicit InetHandle(HINTERNET handle) : h(handle) {}
        InetHandle(const InetHandle&) = delete;
        InetHandle& operator=(const InetHandle&) = delete;
        ~InetHandle() { if (h) InternetCloseHandle(h); }
    };

    bool OpenHttps(HINTERNET session, const std::wstring& url, InetHandle& out, DWORD& total, std::wstring& error)
    {
        if (url.rfind(L"https://", 0) != 0)
        {
            error = L"Nur HTTPS-Adressen sind erlaubt.";
            return false;
        }
        out.h = InternetOpenUrlW(session, url.c_str(), nullptr, 0,
                                 INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI, 0);
        if (!out.h)
        {
            error = L"Keine Verbindung zu GitHub (Fehlercode " + std::to_wstring(GetLastError()) + L"). Bitte Internetverbindung prüfen.";
            return false;
        }
        DWORD status = 0, size = sizeof(status);
        if (!HttpQueryInfoW(out.h, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &size, nullptr) || status != 200)
        {
            error = L"GitHub antwortet mit Status " + std::to_wstring(status) + L". Gibt es bereits ein Release?";
            return false;
        }
        total = 0;
        size = sizeof(total);
        HttpQueryInfoW(out.h, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &total, &size, nullptr);
        return true;
    }

    using crypto::ToHex;


    // Signatur-Pruefung: <url>.sig (64 Byte, ECDSA P-256 ueber SHA-256) muss zum Schluessel passen.
    struct SigCheck
    {
        const unsigned char* key;
        size_t keyLen;
    };

    bool VerifyDigestSignature(HINTERNET session, const std::wstring& sigUrl, const SigCheck& check,
                               const unsigned char (&digest)[32], std::wstring& error, std::string* sigOut = nullptr)
    {
        InetHandle h;
        DWORD total = 0;
        std::wstring ignored;
        std::string sig;
        if (OpenHttps(session, sigUrl, h, total, ignored))
        {
            char buf[128];
            DWORD n = 0;
            while (sig.size() <= 64 && InternetReadFile(h.h, buf, sizeof(buf), &n) && n > 0)
                sig.append(buf, n);
        }
        const bool valid = crypto::VerifyDigest(digest, sig.data(), sig.size(), check.key, check.keyLen);
        if (valid && sigOut)
            *sigOut = sig;
        if (!valid)
            error = L"Die Signatur der Datei fehlt oder ist ungültig. Aus Sicherheitsgründen wurde sie verworfen.";
        return valid;
    }

    bool DownloadFileVerified(HINTERNET session, HWND hwnd, int index, const std::wstring& url, const std::string& expected,
                              const std::wstring& dir, const std::wstring& fileName, const std::atomic<bool>& cancel, std::wstring& error,
                              const SigCheck& sigCheck);

    // Laedt exeName samt Pruefsumme nach dir\exeName. Rueckgabe false + error bei jedem Problem.
    bool DownloadVerified(HWND hwnd, int index, const std::wstring& base, const std::wstring& exeName,
                          const std::wstring& dir, const std::atomic<bool>& cancel, std::wstring& error)
    {
        InetHandle session(InternetOpenW(L"DuncansSuite/" SUITE_VERSION, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0));
        if (!session.h)
        {
            error = L"Internet-Zugriff konnte nicht initialisiert werden.";
            return false;
        }
        DWORD timeout = 20000;
        InternetSetOptionW(session.h, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionW(session.h, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

        // 1) erwartete SHA-256 vom selben Release lesen (64 Hex-Zeichen)
        std::string expected;
        {
            InetHandle h;
            DWORD total = 0;
            if (!OpenHttps(session.h, base + exeName + L".sha256", h, total, error))
                return false;
            char buf[256] = {};
            DWORD n = 0;
            InternetReadFile(h.h, buf, sizeof(buf) - 1, &n);
            std::string text(buf, n);
            const size_t start = text.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
            if (start != std::string::npos)
                expected = text.substr(start, 64);
            const bool valid = expected.size() == 64 &&
                               std::all_of(expected.begin(), expected.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
            if (!valid)
            {
                error = L"Die Prüfsumme des Herstellers ist nicht lesbar. Download abgebrochen.";
                return false;
            }
            std::transform(expected.begin(), expected.end(), expected.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
        }

        return DownloadFileVerified(session.h, hwnd, index, base + exeName, expected, dir, exeName, cancel, error, SigCheck{ kTidyPublicKey, sizeof(kTidyPublicKey) });
    }

    // Laedt eine EXE direkt von einer HTTPS-Adresse (das Release hat keine .sha256-Datei).
    bool DownloadDirect(HWND hwnd, int index, const std::wstring& url, const std::wstring& exeName,
                        const std::wstring& dir, const std::atomic<bool>& cancel, std::wstring& error)
    {
        InetHandle session(InternetOpenW(L"DuncansSuite/" SUITE_VERSION, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0));
        if (!session.h)
        {
            error = L"Internet-Zugriff konnte nicht initialisiert werden.";
            return false;
        }
        DWORD timeout = 20000;
        InternetSetOptionW(session.h, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionW(session.h, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
        return DownloadFileVerified(session.h, hwnd, index, url, std::string(), dir, exeName, cancel, error, SigCheck{ kSuitePublicKey, sizeof(kSuitePublicKey) });
    }

    // Laedt url nach dir\fileName und prueft dabei die SHA-256 (expected: 64 Hex, klein).
    bool DownloadFileVerified(HINTERNET session, HWND hwnd, int index, const std::wstring& url, const std::string& expected,
                              const std::wstring& dir, const std::wstring& fileName, const std::atomic<bool>& cancel, std::wstring& error,
                              const SigCheck& sigCheck)
    {
        InetHandle h;
        DWORD total = 0;
        if (!OpenHttps(session, url, h, total, error))
            return false;

        CreateDirectoryW(dir.c_str(), nullptr);
        std::string sigBytes;   // geprueft gueltige Signatur: wird neben der EXE abgelegt und vor dem Start erneut geprueft
        const std::wstring tmp = dir + L"\\" + fileName + L".download";
        const std::wstring dest = dir + L"\\" + fileName;
        HANDLE file = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            error = L"Zielordner nicht beschreibbar:\n" + dir;
            return false;
        }

        BCRYPT_ALG_HANDLE alg = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
                  BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0;
        if (!ok)
            error = L"Prüfsummen-Funktion nicht verfügbar.";

        DWORD received = 0;
        int lastPercent = -1;
        std::vector<char> buffer(64 * 1024);
        while (ok)
        {
            if (cancel.load())
            {
                error = L"Download abgebrochen.";
                ok = false;
                break;
            }
            DWORD n = 0;
            if (!InternetReadFile(h.h, buffer.data(), static_cast<DWORD>(buffer.size()), &n))
            {
                error = L"Die Verbindung wurde unterbrochen (Fehlercode " + std::to_wstring(GetLastError()) + L").";
                ok = false;
                break;
            }
            if (n == 0)
                break;
            DWORD written = 0;
            if (!WriteFile(file, buffer.data(), n, &written, nullptr) || written != n)
            {
                error = L"Schreiben der Datei fehlgeschlagen (Datenträger voll?).";
                ok = false;
                break;
            }
            BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), n, 0);
            received += n;
            const int percent = total > 0 ? static_cast<int>(std::min<unsigned long long>(100ull, static_cast<unsigned long long>(received) * 100 / total)) : 101;
            if (percent != lastPercent)
            {
                lastPercent = percent;
                if (IsWindow(hwnd))
                    PostMessageW(hwnd, WM_DL_PROGRESS, static_cast<WPARAM>(percent), static_cast<LPARAM>(index));
            }
        }
        CloseHandle(file);

        if (ok)
        {
            unsigned char digest[32] = {};
            // expected leer = Direkt-Download ohne Pruefsummen-Datei (dann entscheidet allein die Signatur)
            if (BCryptFinishHash(hash, digest, sizeof(digest), 0) != 0 || (!expected.empty() && ToHex(digest, sizeof(digest)) != expected))
            {
                error = L"Die Prüfsumme der heruntergeladenen Datei stimmt nicht. Die Datei wurde verworfen.";
                ok = false;
            }
            else if (received < 100 * 1024 || (total > 0 && received != total))
            {
                error = L"Der Download ist unvollständig.";
                ok = false;
            }
            else if (!VerifyDigestSignature(session, url + L".sig", sigCheck, digest, error, &sigBytes))
                ok = false;   // Echtheit: nur von Duncan signierte Dateien werden abgelegt
        }
        if (hash)
            BCryptDestroyHash(hash);
        if (alg)
            BCryptCloseAlgorithmProvider(alg, 0);

        if (ok && !MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            error = L"Die Datei konnte nicht abgelegt werden (läuft sie gerade?):\n" + dest;
            ok = false;
        }
        if (!ok)
            DeleteFileW(tmp.c_str());
        else if (!sigBytes.empty())
        {
            const std::wstring sigPath = dest + L".sig";
            HANDLE sf = CreateFileW(sigPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (sf != INVALID_HANDLE_VALUE)
            {
                DWORD w = 0;
                WriteFile(sf, sigBytes.data(), static_cast<DWORD>(sigBytes.size()), &w, nullptr);
                CloseHandle(sf);
            }
        }
        return ok;
    }

    // Prueft vor dem Start, ob eine EXE im Benutzerordner noch die Datei ist, die wir signiert ausgeliefert haben (<Datei>.sig daneben).
    // Wichtig fuer Programme mit Adminrechten: ein Benutzerordner ist fuer jedes Programm des Nutzers beschreibbar.
    bool VerifyFileSignature(const std::wstring& path, const unsigned char* key, size_t keyLen)
    {
        auto readAll = [](const std::wstring& p, std::string& out, size_t maxBytes) {
            HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f == INVALID_HANDLE_VALUE)
                return false;
            LARGE_INTEGER sz{};
            bool ok = GetFileSizeEx(f, &sz) && sz.QuadPart > 0 && static_cast<unsigned long long>(sz.QuadPart) <= maxBytes;
            if (ok)
            {
                out.resize(static_cast<size_t>(sz.QuadPart));
                DWORD got = 0;
                ok = ReadFile(f, &out[0], static_cast<DWORD>(out.size()), &got, nullptr) && got == out.size();
            }
            CloseHandle(f);
            return ok;
        };
        std::string exe, sig;
        unsigned char digest[32] = {};
        return readAll(path, exe, 64u << 20) && readAll(path + L".sig", sig, 64) && crypto::Sha256(exe.data(), exe.size(), digest) &&
               crypto::VerifyDigest(digest, sig.data(), sig.size(), key, keyLen);
    }

    // ---- Update-Pruefung (GitHub Releases) -------------------------------------------------------
    // Hinweis: /releases/latest liefert nur Releases ohne Pre-Release-Markierung. Da die Suite-Releases
    // als Pre-Release erscheinen koennen, wird die Liste /releases abgefragt und der hoechste
    // DuncansSuite_Setup_X.Y.Z.exe gewaehlt. Pruefsumme = "digest" des Assets aus der API-Antwort.

    constexpr UINT WM_UPD_CHECKED = WM_APP + 3;   // wParam = 1 neuere Version gefunden, 0 keine/Fehler
    constexpr UINT WM_CHANGES = WM_APP + 12;      // Aenderungsprotokoll aus dem Netz geladen (Ergebnis in m_changeFetch)

    using versionutil::IsNewer;
    using versionutil::Version;

    std::wstring Widen(const std::string& s)
    {
        return std::wstring(s.begin(), s.end());   // nur ASCII (Versionen, URLs)
    }

    Version ParseVer(const std::wstring& text) { return versionutil::Parse(text); }

    struct UpdateInfo
    {
        std::wstring version;    // z. B. "1.0.3"
        std::wstring url;        // Download-Adresse des Installers
        std::wstring fileName;   // DuncansSuite_Setup_1.0.3.exe
        std::string sha;         // 64 Hex, klein ("" = unbekannt)
    };

    // Sucht im JSON der Release-Liste den neuesten Suite-Installer (nur Assets aus dem eigenen Repository).
    bool FindNewestInstaller(const std::string& json, UpdateInfo& best)
    {
        const std::string key = "\"browser_download_url\":\"";
        const std::string prefix = "https://github.com/Neox365/Duncans-Suite/releases/download/";
        const std::string head = "DuncansSuite_Setup_", tail = ".exe";
        const std::string digestKey = "\"digest\":\"sha256:";

        Version bestVer;
        bool found = false;
        size_t pos = 0, prevEnd = 0;
        while ((pos = json.find(key, pos)) != std::string::npos)
        {
            const size_t urlStart = pos + key.size();
            const size_t urlEnd = json.find('"', urlStart);
            if (urlEnd == std::string::npos)
                break;
            const std::string url = json.substr(urlStart, urlEnd - urlStart);
            const size_t blockLow = prevEnd;   // alles davor gehoert zum vorherigen Asset
            prevEnd = urlEnd;
            pos = urlEnd;

            if (url.rfind(prefix, 0) != 0)
                continue;
            const std::string file = url.substr(url.find_last_of('/') + 1);
            if (file.size() <= head.size() + tail.size() || file.rfind(head, 0) != 0 ||
                file.compare(file.size() - tail.size(), tail.size(), tail) != 0)
                continue;
            if (file.find_first_of("\\:*?\"<>|") != std::string::npos)
                continue;   // kein Pfad-Trick im Dateinamen
            const std::string ver = file.substr(head.size(), file.size() - head.size() - tail.size());
            const Version v = ParseVer(Widen(ver));
            if (!v.valid)
                continue;

            // "digest" steht im Asset-Objekt vor browser_download_url
            std::string sha;
            const size_t d = json.rfind(digestKey, urlStart);
            if (d != std::string::npos && d >= blockLow)
            {
                sha = json.substr(d + digestKey.size(), 64);
                const bool hex = sha.size() == 64 &&
                                 std::all_of(sha.begin(), sha.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
                if (!hex)
                    sha.clear();
                std::transform(sha.begin(), sha.end(), sha.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
            }

            if (!found || IsNewer(v, bestVer))
            {
                found = true;
                bestVer = v;
                best.version = Widen(ver);
                best.url = Widen(url);
                best.fileName = Widen(file);
                best.sha = sha;
            }
        }
        return found;
    }

    // Fragt GitHub ab. true = Antwort erhalten (best ggf. gefuellt), Fehler werden still ignoriert.
    bool FetchLatestSuiteInstaller(UpdateInfo& best)
    {
        InetHandle session(InternetOpenW(L"DuncansSuite/" SUITE_VERSION, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0));
        if (!session.h)
            return false;
        DWORD timeout = 10000;
        InternetSetOptionW(session.h, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionW(session.h, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

        InetHandle h;
        DWORD total = 0;
        std::wstring error;
        if (!OpenHttps(session.h, L"https://api.github.com/repos/Neox365/Duncans-Suite/releases?per_page=10", h, total, error))
            return false;

        std::string body;
        char buf[8192];
        DWORD n = 0;
        while (body.size() < 1024 * 1024 && InternetReadFile(h.h, buf, sizeof(buf), &n) && n > 0)
            body.append(buf, n);
        return FindNewestInstaller(body, best);
    }

    // Liest die Versionsdatei eines Moduls (zentrale Update-Pruefung der Suite). Inhalt: nur eine Versionsnummer ("1.1.5")
    // oder Zeilen "DuncansPrivacy.exe=0.6.0" (dann zaehlt die Zeile der gesuchten EXE). true = Version gelesen.
    bool FetchModuleVersion(const std::wstring& url, const std::wstring& exeName, std::wstring& version)
    {
        InetHandle session(InternetOpenW(L"DuncansSuite/" SUITE_VERSION, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0));
        if (!session.h)
            return false;
        DWORD timeout = 10000;
        InternetSetOptionW(session.h, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionW(session.h, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
        InetHandle h;
        DWORD total = 0;
        std::wstring error;
        if (!OpenHttps(session.h, url, h, total, error))
            return false;
        char buf[1024] = {};
        DWORD n = 0;
        InternetReadFile(h.h, buf, sizeof(buf) - 1, &n);
        std::string text(buf, n);
        const std::string key = std::string(exeName.begin(), exeName.end()) + "=";   // Dateinamen sind ASCII
        const size_t at = text.find(key);
        size_t from = 0;
        if (at != std::string::npos)
            from = at + key.size();
        else if (text.find('=') != std::string::npos)
            return false;   // Datei kennt nur andere Programme
        std::string v;
        for (size_t i = from; i < text.size(); ++i)
        {
            const char c = text[i];
            if (std::isdigit(static_cast<unsigned char>(c)) || c == '.')
                v.push_back(c);
            else if (!v.empty())
                break;   // Ende der Versionsnummer
        }
        if (!ParseVer(Widen(v)).valid)
            return false;
        version = Widen(v);
        return true;
    }

    // ---- Aenderungsprotokoll (Info-Dialog) ----------------------------------------------------
    // Die Suite bringt changelog.txt als Ressource mit (Anzeige auch offline) und holt beim Oeffnen des Dialogs die
    // aktuelle Fassung aus dem Release "modules". Der Text wird nur angezeigt, nie ausgefuehrt; changelog::Parse begrenzt ihn.
    constexpr const wchar_t* kChangelogUrl = L"https://github.com/Neox365/Duncans-Suite/releases/download/modules/changelog.txt";
    constexpr int kChangelogResource = 101;
    constexpr int kToolCount = 7;   // Karten: Tidy, Driver Updater, PDF Reader, Privacy Shield, Photo Commander, Oggys Docs-Files, Duncan's Vault
    // Kennung einer Karte fuer Hover/Klick (0 und 1 historisch direkt, 5/17/22 aus alten Versionen, ab Karte 5 einheitlich 40 + Index;
    // die Ids 24..31 sind die Info-Reiter, 32 der Schliessen-Knopf des Info-Dialogs)
    inline int CardHid(int index) { return index < 2 ? index : (index == 2 ? 5 : (index == 3 ? 17 : (index == 4 ? 22 : 40 + index))); }
    inline int CardFromHid(int hid)
    {
        for (int i = 0; i < kToolCount; ++i)
            if (CardHid(i) == hid)
                return i;
        return -1;
    }
    constexpr int kInfoTabs = kToolCount + 1;   // ein Reiter je Programm plus die Suite selbst

    std::string EmbeddedChangelog()
    {
        HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(kChangelogResource), RT_RCDATA);
        if (!res)
            return {};
        HGLOBAL mem = LoadResource(nullptr, res);
        const void* data = mem ? LockResource(mem) : nullptr;
        const DWORD size = SizeofResource(nullptr, res);
        return data ? std::string(static_cast<const char*>(data), size) : std::string();
    }

    bool FetchChangelogText(std::string& out)
    {
        InetHandle session(InternetOpenW(L"DuncansSuite/" SUITE_VERSION, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0));
        if (!session.h)
            return false;
        DWORD timeout = 10000;
        InternetSetOptionW(session.h, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionW(session.h, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
        InetHandle h;
        DWORD total = 0;
        std::wstring error;
        if (!OpenHttps(session.h, kChangelogUrl, h, total, error))
            return false;
        char buf[8192];
        DWORD n = 0;
        out.clear();
        while (out.size() < changelog::kMaxText && InternetReadFile(h.h, buf, sizeof(buf), &n) && n > 0)
            out.append(buf, n);
        return !out.empty();
    }

    struct ChangeFetch
    {
        std::mutex mutex;
        std::string text;
        bool ok = false;
    };

    // Laedt den Installer in den Temp-Ordner und prueft die SHA-256 aus der API-Antwort.
    bool DownloadInstaller(HWND hwnd, const UpdateInfo& info, const std::wstring& customDir, std::wstring& installerPath,
                           const std::atomic<bool>& cancel, std::wstring& error)
    {
        if (info.sha.size() != 64)
        {
            error = L"Für dieses Release liegt keine Prüfsumme vor. Bitte den Installer manuell von GitHub laden.";
            return false;
        }
        InetHandle session(InternetOpenW(L"DuncansSuite/" SUITE_VERSION, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0));
        if (!session.h)
        {
            error = L"Internet-Zugriff konnte nicht initialisiert werden.";
            return false;
        }
        DWORD timeout = 20000;
        InternetSetOptionW(session.h, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionW(session.h, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

        // Privater Ordner des Nutzers statt des gemeinsamen %TEMP%: dort koennte ein anderer Prozess die Datei
        // zwischen Pruefung und Start austauschen.
        const std::wstring local = LocalAppData();
        if (local.empty())
        {
            error = L"Der Ordner %LOCALAPPDATA% ist nicht verfügbar.";
            return false;
        }
        CreateDirectoryW((local + L"\\DuncansSuite").c_str(), nullptr);
        std::wstring dir = local + L"\\DuncansSuite\\update";
        if (!customDir.empty())
        {
            dir = customDir;
            while (dir.size() > 3 && (dir.back() == L'\\' || dir.back() == L'/'))
                dir.pop_back();
            const int rc = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
            if (rc != ERROR_SUCCESS && rc != ERROR_ALREADY_EXISTS && rc != ERROR_FILE_EXISTS)
            {
                error = L"Der gewählte Download-Ordner ist nicht beschreibbar:\n" + dir;
                return false;
            }
        }
        if (!DownloadFileVerified(session.h, hwnd, kSuiteIdx, info.url, info.sha, dir, info.fileName, cancel, error, SigCheck{ kSuitePublicKey, sizeof(kSuitePublicKey) }))
            return false;
        installerPath = dir + L"\\" + info.fileName;
        return true;
    }

    // SHA-256 einer Datei als Hex (leer bei Fehler). Dient der erneuten Pruefung direkt vor dem Start des Installers.
    std::string HashFileSha256(const std::wstring& path)
    {
        HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE)
            return std::string();
        std::string result;
        BCRYPT_ALG_HANDLE alg = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
            BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0)
        {
            std::vector<char> buf(64 * 1024);
            DWORD n = 0;
            bool ok = true;
            while (ReadFile(f, buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr) && n > 0)
                if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(buf.data()), n, 0) != 0)
                {
                    ok = false;
                    break;
                }
            unsigned char digest[32] = {};
            if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0)
                result = ToHex(digest, sizeof(digest));
        }
        if (hash)
            BCryptDestroyHash(hash);
        if (alg)
            BCryptCloseAlgorithmProvider(alg, 0);
        CloseHandle(f);
        return result;
    }
}

struct Tool

{
    const wchar_t* exe;
    const wchar_t* title;
    const wchar_t* description;
    CategoryIcon icon;
    COLORREF color;
    std::wstring path;      // gefundener Pfad (leer = nicht gefunden)
    std::vector<std::wstring> searched;
    RECT rect{};
    const wchar_t* downloadBase = nullptr;   // GitHub-Release-Adresse; nullptr = kein Download moeglich
    const wchar_t* directUrl = nullptr;      // direkte EXE-Adresse (ohne Pruefsummen-Datei), alternativ zu downloadBase
    const wchar_t* versionUrl = nullptr;     // Versionsdatei im neuesten Release (zentrale Update-Pruefung); nullptr = keine
    std::wstring newVersion;                 // neuere Version laut Pruefung (leer = aktuell)
    const wchar_t* extraUrl = nullptr;       // zusaetzliche Datei, die neben der EXE liegen muss (PDF Reader: pdfium.dll), signiert wie die EXE
    const wchar_t* extraName = nullptr;
    bool downloading = false;
    int progress = 0;                        // 0..100, 101 = unbekannt
};

class SuiteWindow
{
public:
    bool Create(HINSTANCE instance, int showCommand);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    int  S(int px) const { return MulDiv(px, static_cast<int>(m_dpi), 96); }
    void CreateFonts();
    void DestroyFonts();
    void FindTools();
    void Layout();
    void Draw(HDC dc, int w, int h);
    void DrawCard(HDC dc, int index);
    void DrawOfficeCard(HDC dc);
    void DrawModal(HDC dc, int w, int h);
    void OnMouseMove(int x, int y);
    void OnClick(int x, int y);
    void Launch(int index);
    void ShowModal(const std::wstring& title, const std::wstring& text);
    void SetNote(const std::wstring& text);
    void StartDownload(int index);
    void StartUpdateCheck();
    void StartUpdateDownload();
    void RunInstaller();
    std::wstring InstallDir(const Tool& t) const;

    HWND m_hwnd = nullptr;
    UINT m_dpi = 96;
    HFONT m_fontTitle = nullptr, m_fontCard = nullptr, m_fontHeading = nullptr, m_fontBody = nullptr, m_fontSmall = nullptr;
    HICON m_icon = nullptr;

    Tool m_tools[kToolCount] = {
        { L"DuncansTidy.exe", L"Duncan's Tidy",
          L"System-Reinigung & Datenmüll-Beseitigung. Räumt Downloads, temporäre Dateien und Browser-Caches auf.",
          CategoryIcon::Broom, RGB(251, 191, 36), {}, {}, {},
          L"https://github.com/Neox365/Duncans-Tidy-ToolSet/releases/latest/download/", nullptr,
          L"https://github.com/Neox365/Duncans-Tidy-ToolSet/releases/latest/download/version.txt" },
        { L"DuncansDriverUpdater.exe", L"Duncan's Driver Updater",
          L"Hardware- & Treiber-Manager. Liest alle Geräte aus, erkennt Treiber-Updates und installiert sie.",
          CategoryIcon::Cpu, RGB(64, 156, 255), {}, {}, {},
          nullptr, L"https://github.com/Neox365/Duncans-Suite/releases/download/modules/DuncansDriverUpdater.exe",
          L"https://github.com/Neox365/Duncans-Suite/releases/download/modules/modules.txt" },
        { L"DuncansPdf.exe", L"Duncan's PDF Reader",
          L"PDF-Betrachter. Öffnet, zoomt, dreht, fügt zusammen und speichert PDF-Dateien.",
          CategoryIcon::Document, RGB(240, 84, 84), {}, {}, {},
          nullptr, L"https://github.com/Neox365/Duncans-PDF-Reader/releases/latest/download/DuncansPdf.exe",
          L"https://github.com/Neox365/Duncans-PDF-Reader/releases/latest/download/version.txt", {},
          L"https://github.com/Neox365/Duncans-PDF-Reader/releases/latest/download/pdfium.dll", L"pdfium.dll" },
        { L"DuncansPrivacy.exe", L"Duncan's Privacy Shield",
          L"Datenschutz-Manager. Schaltet Tracking, Telemetrie und unnötige Windows-Dienste per Klick ab.",
          CategoryIcon::Shield, RGB(167, 139, 250), {}, {}, {},
          nullptr, L"https://github.com/Neox365/Duncans-Suite/releases/download/modules/DuncansPrivacy.exe",
          L"https://github.com/Neox365/Duncans-Suite/releases/download/modules/modules.txt" },
        { L"DuncansPhotoCommander.exe", L"Duncan's Photo Commander",
          L"Foto-Verwaltung. Vorschaubilder, EXIF-Daten, Bildkorrektur, Diashow und Entfernen von Standortdaten.",
          CategoryIcon::Image, RGB(52, 199, 120), {}, {}, {},
          nullptr, L"https://github.com/Neox365/Duncans-Suite/releases/download/modules/DuncansPhotoCommander.exe",
          L"https://github.com/Neox365/Duncans-Suite/releases/download/modules/modules.txt" },
        { L"OggysDocsFiles.exe", L"Oggys Docs-Files",
          L"Rechnungs-Manager. Sortiert Rechnungen nach Jahr und Gläubiger und erstellt eigene Rechnungen als PDF.",
          CategoryIcon::Invoice, RGB(255, 150, 50), {}, {}, {},
          nullptr, L"https://github.com/Neox365/Duncans-Suite/releases/download/modules/OggysDocsFiles.exe",
          L"https://github.com/Neox365/Duncans-Suite/releases/download/modules/modules.txt" },
        { L"DuncansVault.exe", L"Duncan's Vault",
          L"Lokaler Tresor für Passwörter, Software-Lizenzen, WLAN-Schlüssel und Notizen. AES-256-verschlüsselt.",
          CategoryIcon::Vault, RGB(34, 211, 238), {}, {}, {},
          nullptr, L"https://github.com/Neox365/Duncans-Suite/releases/download/modules/DuncansVault.exe",
          L"https://github.com/Neox365/Duncans-Suite/releases/download/modules/modules.txt" },
    };

    RECT m_banner{}, m_footer{};
    RECT m_officeRect{};     // Platzhalter-Karte "Duncan's Office" (Coming Soon), kein echtes Programm
    static constexpr int kOfficeHid = 60;
    int  m_hover = -1;       // 0/1 Karten, 2 Ordner-Button, 3 Modal-OK
    int  m_press = -1;
    bool m_tracking = false;
    std::wstring m_note;

    std::thread m_dlThread;
    std::atomic<bool> m_dlCancel{ false };
    bool m_dlBusy = false;      // ein Download (Tidy oder Suite-Update) laeuft
    std::shared_ptr<UpdateInfo> m_checkResult;
    UpdateInfo m_upd;
    bool m_updAvailable = false;
    std::shared_ptr<std::vector<std::wstring>> m_modRemote;   // Ergebnis der Modul-Pruefung (Hintergrundthread), je Tool-Index
    bool m_updDownloading = false;
    // Start-Animation des Banners (Besen faegt, Chip folgt) und vorgerenderter Banner-Hintergrund
    float m_introT = 1.0f;
    ULONGLONG m_introStart = 0;
    HDC m_bnDc = nullptr;
    HBITMAP m_bnBmp = nullptr;
    HGDIOBJ m_bnOld = nullptr;
    int m_bnW = 0, m_bnH = 0;
    UINT m_bnDpi = 0;
    void ReleaseBannerCache();

    int  m_updProgress = 0;
    std::wstring m_updInstallerPath;   // fertig geladener Installer (leer = noch nicht geladen)
    RECT m_updateRect{};
    std::wstring m_dlMessage;   // Fehlertext des letzten Downloads (vom Worker gesetzt, vor WM_DL_DONE)

    bool m_modal = false;
    std::wstring m_modalTitle, m_modalText;
    RECT m_modalOk{};

    // Einstellungsdialog (Zahnrad). Hover/Press-IDs: 6 Zahnrad, 7 Update-Haken, 8 Autostart-Haken,
    // 9 Ordner waehlen, 10 Ordner Standard, 11 Speichern, 12 Abbrechen
    Settings m_settings;          // gespeicherter Stand
    Settings m_draft;             // Entwurf im offenen Dialog
    bool m_settingsOpen = false;
    HFONT m_fontSym = nullptr;
    RECT m_gearRect{}, m_chkAuto{}, m_chkStart{}, m_chkTrayClose{}, m_chkTrayLaunch{}, m_chkCtxPdf{}, m_chkCtxTidy{}, m_chkRestore{}, m_btnPick{}, m_btnReset{}, m_btnSave{}, m_btnCancel{};
    RECT m_btnBackup{}, m_btnLog{}, m_btnCheck{};   // Ids: 18 Wiederherstellungspunkt-Haken, 19 Backup-Ordner, 20 Logdatei, 21 Update-Suche
    bool m_manualCheck = false;                      // Update-Suche per Knopf: Ergebnis als Hinweis melden

    // Info-Dialog (Button "i" neben dem Zahnrad): Neuigkeiten je Programm. Ids: 23 Info-Button, 24..30 Reiter, 32 Schliessen (31 = Karte Oggys Docs-Files)
    bool m_infoOpen = false, m_changesFetched = false, m_changesOnline = false;
    int m_infoTab = 0, m_infoScroll = 0, m_infoMaxScroll = 0;
    RECT m_infoRect{}, m_infoTabs[kInfoTabs]{}, m_infoClose{}, m_infoView{};
    std::vector<changelog::Section> m_changes;       // eingebaute Fassung, nach dem Laden aus dem Netz ersetzt
    std::shared_ptr<ChangeFetch> m_changeFetch;

    // Infobereich (Tray): Symbol neben der Uhr, Kontextmenue, Quick Clean, Benachrichtigung
    static constexpr UINT kTrayMsg = WM_APP + 10;     // Rueckmeldungen des Tray-Symbols
    static constexpr UINT kQuickDone = WM_APP + 11;   // Quick Clean fertig (wParam = freigegebene Bytes, lParam = Anzahl Prozesse)
    NOTIFYICONDATAW m_nid{};
    bool m_trayShown = false;
    bool m_exiting = false;                           // wirklich beenden (nicht in den Infobereich minimieren)
    bool m_hiddenToTray = false;
    UINT m_taskbarMsg = 0;                            // "TaskbarCreated": Explorer wurde neu gestartet
    std::atomic<bool> m_quickBusy{ false };
    HICON m_trayIcon = nullptr;
    void AddTrayIcon();
    void RemoveTrayIcon();
    void HideToTray();
    void RestoreFromTray();
    void ShowTrayMenu();
    void TrayBalloon(const std::wstring& title, const std::wstring& text);
    void StartQuickClean();
    void OpenSettings();
    void CloseSettings(bool save);
    void DrawSettings(HDC dc, int w, int h);
    void OpenInfo();
    void CloseInfo();
    void DrawInfo(HDC dc, int w, int h);
    void StartChangelogFetch();
    void ScrollInfo(int delta);
    const wchar_t* InfoKey(int tab) const;        // Abschnittsname in changelog.txt (EXE-Name)
    std::wstring InfoInstalled(int tab) const;    // installierte Version, leer = nicht gefunden
    std::wstring InfoNewVersion(int tab) const;   // neuere Version laut Update-Pruefung, leer = keine
    void DrawCheck(HDC dc, const RECT& r, bool on, bool hover, const std::wstring& label, const std::wstring& hint);
    void DrawButton(HDC dc, const RECT& r, bool primary, bool hover, bool pressed, const std::wstring& text);
};

// ---- Aufbau --------------------------------------------------------------------------------

bool SuiteWindow::Create(HINSTANCE instance, int showCommand)
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &SuiteWindow::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc))
        return false;

    IconsInit();
    m_dpi = GetDpiForSystem();
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int startW = std::min<int>(S(1100), work.right - work.left - S(20)), startH = std::min<int>(S(960), work.bottom - work.top - S(20));
    m_hwnd = CreateWindowExW(0, kClassName, L"Duncan's Suite", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             startW, startH, nullptr, nullptr, instance, this);
    if (!m_hwnd)
        return false;
    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    if (animations)
    {
        m_introT = 0.0f;
        m_introStart = GetTickCount64() + 250;
        SetTimer(m_hwnd, 2, 33, nullptr);
    }
    ShowWindow(m_hwnd, showCommand);
    UpdateWindow(m_hwnd);
    return true;
}

LRESULT CALLBACK SuiteWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    SuiteWindow* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        self = static_cast<SuiteWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
        self = reinterpret_cast<SuiteWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->HandleMessage(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

void SuiteWindow::CreateFonts()
{
    DestroyFonts();
    auto make = [this](int pt, int weight)
    {
        return CreateFontW(-MulDiv(pt, static_cast<int>(m_dpi), 72), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    };
    m_fontTitle = make(20, FW_SEMIBOLD);
    m_fontCard = make(15, FW_SEMIBOLD);
    m_fontHeading = make(11, FW_SEMIBOLD);
    m_fontBody = make(10, FW_NORMAL);
    m_fontSmall = make(9, FW_NORMAL);
    m_fontSym = CreateFontW(-MulDiv(16, static_cast<int>(m_dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");
    m_icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, S(48), S(48), LR_DEFAULTCOLOR));
}

void SuiteWindow::DestroyFonts()
{
    for (HFONT* f : { &m_fontTitle, &m_fontCard, &m_fontHeading, &m_fontBody, &m_fontSmall, &m_fontSym })
        if (*f)
        {
            DeleteObject(*f);
            *f = nullptr;
        }
    if (m_icon)
    {
        DestroyIcon(m_icon);
        m_icon = nullptr;
    }
}

void SuiteWindow::FindTools()
{
    const std::wstring own = ExeDir();
    const std::wstring local = LocalAppData();
    for (Tool& t : m_tools)
    {
        t.path.clear();
        t.searched.clear();
        std::vector<std::wstring> candidates{ own + L"\\" + t.exe };
        // ladbare Programme (Tidy, PDF Reader) landen standardmaessig in %LOCALAPPDATA%\<Name>
        if (!local.empty() && (t.downloadBase || t.directUrl))
            candidates.push_back(InstallDir(t) + L"\\" + t.exe);
        // Gibt es mehrere Kopien (z. B. Installer-Ordner und per Update geladene Kopie in %LOCALAPPDATA%), gewinnt die neuere Version
        Version best{};
        for (const auto& c : candidates)
        {
            t.searched.push_back(c);
            if (!FileExists(c))
                continue;
            std::wstring ver;
            FileVersionAtLeast(c, 0, 0, 0, &ver);
            const Version v = ParseVer(ver);
            if (t.path.empty() || (v.valid && (!best.valid || IsNewer(v, best))))
            {
                t.path = c;
                best = v;
            }
        }
    }
}

// ---- Layout / Zeichnen ---------------------------------------------------------------------

void SuiteWindow::Layout()
{
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    const int m = S(28);
    m_banner = { m, S(18), rc.right - m, S(18) + S(134) };
    m_footer = { m, rc.bottom - S(60), rc.right - m, rc.bottom - S(14) };

    const int top = m_banner.bottom + S(22), bottom = m_footer.top - S(16), gap = S(20);
    const int width = rc.right - 2 * m;
    // 3 x 3 Raster: Tidy | Driver Updater | PDF Reader / Privacy Shield | Photo Commander | Oggys Docs-Files / Duncan's Vault
    const int h = (bottom - top - 2 * gap) / 3;
    const int w = (width - 2 * gap) / 3;
    for (int i = 0; i < kToolCount; ++i)
    {
        const int col = i % 3, row = i / 3;
        m_tools[i].rect = { m + col * (w + gap), top + row * (h + gap), m + col * (w + gap) + w, top + row * (h + gap) + h };
    }
    {
        const int col = kToolCount % 3, row = kToolCount / 3;   // naechster freier Platz im Raster
        m_officeRect = { m + col * (w + gap), top + row * (h + gap), m + col * (w + gap) + w, top + row * (h + gap) + h };
    }
}

void SuiteWindow::DrawOfficeCard(HDC dc)
{
    const RECT& rc = m_officeRect;
    const bool hover = m_hover == kOfficeHid && !m_modal;
    const bool pressed = m_press == kOfficeHid && hover;
    const COLORREF color = RGB(129, 140, 248);
    const COLORREF fill = pressed ? Blend(kCardRaised, RGB(0, 0, 0), 18) : (hover ? kCardHover : kCardRaised);

    DrawShadow(dc, rc, S(18), static_cast<int>(m_dpi));
    DrawPanelAA(dc, rc, S(18), fill, hover ? kBorderHover : kBorder);

    const bool compact = (rc.bottom - rc.top) < S(300);
    const int pad = compact ? S(20) : S(24);
    const int tileSize = compact ? S(48) : S(84);
    const int tileTop = compact ? S(14) : pad;
    RECT tile{ rc.left + pad, rc.top + tileTop, rc.left + pad + tileSize, rc.top + tileTop + tileSize };
    DrawCategoryTile(dc, tile, CategoryIcon::Document, color, fill);

    int textLeft, textTop, descLeft, descTop;
    const int textRight = rc.right - pad;
    if (compact)
    {
        textLeft = tile.right + S(14);
        textTop = tile.top + (tileSize - S(30)) / 2;
        descLeft = rc.left + pad;
        descTop = tile.bottom + S(6);
    }
    else
    {
        textLeft = descLeft = rc.left + pad;
        textTop = tile.bottom + S(18);
        descTop = textTop + S(32);
    }
    DrawLabel(dc, m_fontCard, kText, { textLeft, textTop, textRight, textTop + S(30) }, L"Duncan's Office");
    const int btnH = S(38);
    const int bottomRow = rc.bottom - pad - btnH;
    DrawWrapped(dc, m_fontBody, kTextDim, { descLeft, descTop, textRight, compact ? bottomRow - S(4) : bottomRow - S(10) },
                L"Office-Paket mit Writer (Texte) und Sheets (Tabellen). Kommt bald.");

    // Coming-Soon-Abzeichen links, deaktivierte Pille rechts
    const std::wstring badge = L"Coming Soon";
    const std::wstring go = L"In Entwicklung";
    const int goW = TextWidth(dc, m_fontBody, go) + S(44);
    const int chipW = TextWidth(dc, m_fontSmall, badge) + S(34);
    const bool dotOnly = chipW + S(10) + goW > rc.right - rc.left - 2 * pad;
    RECT chip{ rc.left + pad, bottomRow + (btnH - S(26)) / 2, rc.left + pad + chipW, bottomRow + (btnH - S(26)) / 2 + S(26) };
    if (compact)
    {
        chip.top = rc.bottom - pad - S(26);
        chip.bottom = chip.top + S(26);
    }
    if (dotOnly)
        chip.right = chip.left + S(26);
    FillRound(dc, chip, S(26), Blend(fill, color, 18), Blend(fill, color, 50));
    {
        HBRUSH dot = CreateSolidBrush(color);
        HPEN none = CreatePen(PS_NULL, 0, 0);
        HGDIOBJ ob = SelectObject(dc, dot);
        HGDIOBJ op = SelectObject(dc, none);
        const int cy = (chip.top + chip.bottom) / 2;
        const int dx = dotOnly ? S(10) : S(11);
        Ellipse(dc, chip.left + dx, cy - S(3), chip.left + dx + S(6) + 1, cy + S(3) + 1);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(dot);
        DeleteObject(none);
    }
    if (!dotOnly)
        DrawLabel(dc, m_fontSmall, color, { chip.left + S(20), chip.top + S(4), chip.right, chip.bottom }, badge, DT_CENTER);

    const int btnTop = compact ? rc.bottom - pad - btnH : bottomRow;
    RECT goRc{ rc.right - pad - goW, btnTop, rc.right - pad, btnTop + btnH };
    FillRound(dc, goRc, S(12), Blend(fill, RGB(255, 255, 255), hover ? 14 : 6), kBorder);
    DrawLabel(dc, m_fontBody, kTextDim, { goRc.left, goRc.top + S(8), goRc.right, goRc.bottom }, go, DT_CENTER);
}

void SuiteWindow::DrawCard(HDC dc, int index)
{
    const Tool& t = m_tools[index];
    const RECT& rc = t.rect;
    const int hid = CardHid(index);
    const bool hover = m_hover == hid && !m_modal;
    const bool pressed = m_press == hid && hover;
    const bool found = !t.path.empty();
    const COLORREF fill = pressed ? Blend(kCardRaised, RGB(0, 0, 0), 18) : (hover ? kCardHover : kCardRaised);

    DrawShadow(dc, rc, S(18), static_cast<int>(m_dpi));
    DrawPanelAA(dc, rc, S(18), fill, hover ? kBorderHover : kBorder);

    const bool compact = (rc.bottom - rc.top) < S(300);   // das grosse Layout (Kachel oben) braucht Platz fuer Titel, Beschreibung und Knopfzeile
    const int pad = compact ? S(20) : S(24);
    const int tileSize = compact ? S(48) : S(84);
    const int tileTop = compact ? S(14) : pad;
    RECT tile{ rc.left + pad, rc.top + tileTop, rc.left + pad + tileSize, rc.top + tileTop + tileSize };
    DrawCategoryTile(dc, tile, t.icon, t.color, fill);

    // Status-Chip + Starten-Pille
    const bool downloading = t.downloading;
    const bool canDownload = !found && (t.downloadBase || t.directUrl);
    const std::wstring status = found ? L"Installiert" : (downloading ? L"Wird geladen" : L"Nicht installiert");
    const COLORREF statusColor = found ? kGreen : (downloading ? kAccent : kOrange);
    const int chipW = TextWidth(dc, m_fontSmall, status) + S(34);
    const bool tidyUpdate = found && !t.newVersion.empty() && (t.downloadBase || t.directUrl);   // neuere Version verfuegbar
    std::wstring go = tidyUpdate ? L"Update auf v" + t.newVersion : (found ? L"Starten" : (canDownload ? L"Herunterladen" : L"Starten"));
    if (downloading)
        go = t.progress <= 100 ? L"Lade herunter … " + std::to_wstring(t.progress) + L" %" : L"Lade herunter …";
    // Breite fuer den laengsten Text reservieren, damit die Pille beim Laden nicht springt
    const int goW = std::max(TextWidth(dc, m_fontBody, go), TextWidth(dc, m_fontBody, canDownload || downloading ? L"Lade herunter … 100 %" : go)) + S(44);

    // kompakt: Titel neben der Kachel, Beschreibung darunter ueber die volle Kartenbreite; sonst Kachel oben, Titel und Text darunter
    int textLeft, textTop, textRight = rc.right - pad, descLeft, descTop;
    if (compact)
    {
        textLeft = tile.right + S(14);
        textTop = tile.top + (tileSize - S(30)) / 2;
        descLeft = rc.left + pad;
        descTop = tile.bottom + S(6);
    }
    else
    {
        textLeft = descLeft = rc.left + pad;
        textTop = tile.bottom + S(18);
        descTop = textTop + S(32);
    }

    DrawLabel(dc, m_fontCard, kText, { textLeft, textTop, textRight, textTop + S(30) }, t.title);
    const int btnH = S(38);
    const int bottomRow = rc.bottom - pad - btnH;
    RECT descRc{ descLeft, descTop, textRight, compact ? bottomRow - S(4) : bottomRow - S(10) };   // Text endet immer ueber Chip und Knopf
    DrawWrapped(dc, m_fontBody, kTextDim, descRc, t.description);

    // untere Zeile: Chip links, Starten rechts
    RECT chip{ rc.left + pad, bottomRow + (btnH - S(26)) / 2, rc.left + pad + chipW, bottomRow + (btnH - S(26)) / 2 + S(26) };
    if (compact)
    {
        chip.top = rc.bottom - pad - S(26);
        chip.bottom = chip.top + S(26);
    }
    // schmale Karte: ist neben dem Knopf kein Platz fuer den Text, bleibt nur der farbige Punkt
    const bool dotOnly = chipW + S(10) + goW > rc.right - rc.left - 2 * pad;
    if (dotOnly)
        chip.right = chip.left + S(26);
    FillRound(dc, chip, S(26), Blend(fill, statusColor, 18), Blend(fill, statusColor, 50));
    {
        HBRUSH dot = CreateSolidBrush(statusColor);
        HPEN none = CreatePen(PS_NULL, 0, 0);
        HGDIOBJ ob = SelectObject(dc, dot);
        HGDIOBJ op = SelectObject(dc, none);
        const int cy = (chip.top + chip.bottom) / 2;
        const int dx = dotOnly ? S(10) : S(11);
        Ellipse(dc, chip.left + dx, cy - S(3), chip.left + dx + S(6) + 1, cy + S(3) + 1);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(dot);
        DeleteObject(none);
    }
    if (!dotOnly)
        DrawLabel(dc, m_fontSmall, statusColor, { chip.left + S(20), chip.top + S(4), chip.right, chip.bottom }, status, DT_CENTER);

    RECT goRc{ rc.right - pad - goW, compact ? rc.bottom - pad - btnH : bottomRow, rc.right - pad, (compact ? rc.bottom - pad - btnH : bottomRow) + btnH };
    const bool enabled = (found || canDownload) && !downloading;
    COLORREF goFill = enabled ? (pressed ? Blend(kAccent, RGB(0, 0, 0), 25) : (hover ? Blend(kAccent, RGB(255, 255, 255), 14) : kAccent))
                              : Blend(fill, RGB(255, 255, 255), hover ? 14 : 6);
    FillRound(dc, goRc, S(12), goFill, enabled ? goFill : kBorder);
    if (downloading && t.progress <= 100)
    {
        // Fortschritt: Pille von links mit Akzentfarbe fuellen
        RECT part = goRc;
        part.right = goRc.left + (goRc.right - goRc.left) * t.progress / 100;
        SaveDC(dc);
        IntersectClipRect(dc, part.left, part.top, part.right, part.bottom);
        FillRound(dc, goRc, S(12), Blend(fill, kAccent, 55), Blend(fill, kAccent, 55));
        RestoreDC(dc, -1);
    }
    DrawLabel(dc, m_fontBody, enabled ? RGB(8, 20, 36) : (downloading ? kText : kTextDim),
              { goRc.left, goRc.top + S(8), goRc.right, goRc.bottom }, go, DT_CENTER);
}

void SuiteWindow::Draw(HDC dc, int w, int h)
{
    RECT all{ 0, 0, w, h };
    HBRUSH bg = CreateSolidBrush(kBackground);
    FillRect(dc, &all, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);

    // Banner
    if (RectVisible(dc, &m_banner))
    {
        // Panel samt Schatten nur bei Groessen-/DPI-Aenderung neu rendern, sonst kopieren
        const int mg = S(16);
        const int bw = m_banner.right - m_banner.left, bh = m_banner.bottom - m_banner.top;
        if (!m_bnDc || m_bnW != bw || m_bnH != bh || m_bnDpi != m_dpi)
        {
            ReleaseBannerCache();
            m_bnDc = CreateCompatibleDC(dc);
            m_bnBmp = CreateCompatibleBitmap(dc, bw + 2 * mg, bh + 2 * mg);
            m_bnOld = SelectObject(m_bnDc, m_bnBmp);
            RECT whole{ 0, 0, bw + 2 * mg, bh + 2 * mg };
            HBRUSH back = CreateSolidBrush(kBackground);
            FillRect(m_bnDc, &whole, back);
            DeleteObject(back);
            DrawBannerPanel(m_bnDc, RECT{ mg, mg, mg + bw, mg + bh }, S(18), static_cast<int>(m_dpi));
            m_bnW = bw;
            m_bnH = bh;
            m_bnDpi = m_dpi;
        }
        BitBlt(dc, m_banner.left - mg, m_banner.top - mg, bw + 2 * mg, bh + 2 * mg, m_bnDc, 0, 0, SRCCOPY);
        // ein gemeinsamer Banner fuer beide Programme: Besen (Tidy) + Pruef-Chip (Driver Updater)
        DrawSuiteScene(dc, m_banner, m_introT, static_cast<int>(m_dpi));
    }
    // Icon + Titel mittig unter der Welle
    const int iconSize = S(60);
    int textW = 0;
    {
        HGDIOBJ oldFont = SelectObject(dc, m_fontTitle);
        SIZE sz{};
        GetTextExtentPoint32W(dc, L"Duncan's Suite", 14, &sz);
        textW = sz.cx;
        SelectObject(dc, m_fontSmall);
        GetTextExtentPoint32W(dc, L"System- & Treiber-Optimierung", 29, &sz);
        textW = std::max<int>(textW, sz.cx);
        SelectObject(dc, oldFont);
    }
    const int groupW = iconSize + S(16) + textW;
    const int groupX = (m_banner.left + m_banner.right - groupW) / 2;
    const int rowTop = m_banner.top + S(64);
    DrawSuiteLogo(dc, RECT{ groupX, rowTop, groupX + iconSize, rowTop + iconSize }, static_cast<int>(m_dpi));
    const int titleX = groupX + iconSize + S(16);
    DrawLabel(dc, m_fontTitle, kText, { titleX, rowTop + S(0), titleX + textW + S(8), rowTop + S(34) }, L"Duncan's Suite");
    DrawLabel(dc, m_fontSmall, kTextDim, { titleX, rowTop + S(34), titleX + textW + S(8), rowTop + S(54) },
              L"System- & Treiber-Optimierung");

    // Zahnrad oben rechts im Banner
    {
        const int gs = S(38);
        m_gearRect = { m_banner.right - S(22) - gs, m_banner.top + S(18), m_banner.right - S(22), m_banner.top + S(18) + gs };
        const bool hover = m_hover == 6 && !m_modal && !m_settingsOpen;
        const bool pressed = m_press == 6 && hover;
        FillRound(dc, m_gearRect, S(12), pressed ? RGB(24, 26, 32) : (hover ? kCardHover : kCardRaised), hover ? kBorderHover : kBorder);
        HGDIOBJ old = SelectObject(dc, m_fontSym);
        SetTextColor(dc, hover ? kText : kTextDim);
        RECT g = m_gearRect;
        DrawTextW(dc, L"⚙", 1, &g, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, old);
    }

    // Info-Button "i" links neben dem Zahnrad (Neuigkeiten aller Programme); gruener Punkt, wenn irgendwo ein Update bereitsteht
    {
        const int gs = S(38);
        m_infoRect = { m_gearRect.left - S(10) - gs, m_gearRect.top, m_gearRect.left - S(10), m_gearRect.bottom };
        const bool hover = m_hover == 23 && !m_modal && !m_settingsOpen && !m_infoOpen;
        const bool pressed = m_press == 23 && hover;
        FillRound(dc, m_infoRect, S(12), pressed ? RGB(24, 26, 32) : (hover ? kCardHover : kCardRaised), hover ? kBorderHover : kBorder);
        DrawLabel(dc, m_fontHeading, hover ? kText : kTextDim, { m_infoRect.left, m_infoRect.top, m_infoRect.right, m_infoRect.bottom }, L"i", DT_CENTER | DT_VCENTER);
        bool anyUpdate = m_updAvailable;
        for (int i = 0; i < kToolCount && !anyUpdate; ++i)
            anyUpdate = !m_tools[i].newVersion.empty();
        if (anyUpdate)
        {
            const int d = S(10);
            FillRound(dc, RECT{ m_infoRect.right - d - S(3), m_infoRect.top + S(3), m_infoRect.right - S(3), m_infoRect.top + S(3) + d }, d, kGreen, kCardRaised);
        }
    }

    // Update-Badge oben rechts im Banner (links neben dem Zahnrad)
    m_updateRect = RECT{};
    if (m_updAvailable)
    {
        const bool ready = !m_updInstallerPath.empty() && !m_updDownloading;
        const std::wstring label = m_updDownloading
            ? (m_updProgress <= 100 ? L"Lade Update … " + std::to_wstring(m_updProgress) + L" %" : std::wstring(L"Lade Update …"))
            : (ready ? L"Installer starten (v" + m_upd.version + L")" : L"Update verfügbar (v" + m_upd.version + L")");
        const int width = std::max(TextWidth(dc, m_fontBody, label), TextWidth(dc, m_fontBody, L"Lade Update … 100 %")) + S(44);
        const int right = m_infoRect.left - S(12);
        m_updateRect = { right - width, m_banner.top + S(18), right, m_banner.top + S(18) + S(38) };
        const bool hover = m_hover == 4 && !m_modal && !m_settingsOpen;
        const bool pressed = m_press == 4 && hover;
        const COLORREF fill = m_updDownloading ? Blend(RGB(27, 40, 36), kGreen, 25)
                                               : (pressed ? Blend(kGreen, RGB(0, 0, 0), 25) : (hover ? Blend(kGreen, RGB(255, 255, 255), 14) : kGreen));
        FillRound(dc, m_updateRect, S(12), fill, m_updDownloading ? kBorder : fill);
        if (m_updDownloading && m_updProgress <= 100)
        {
            RECT part = m_updateRect;
            part.right = m_updateRect.left + (m_updateRect.right - m_updateRect.left) * m_updProgress / 100;
            SaveDC(dc);
            IntersectClipRect(dc, part.left, part.top, part.right, part.bottom);
            FillRound(dc, m_updateRect, S(12), Blend(RGB(27, 40, 36), kGreen, 60), Blend(RGB(27, 40, 36), kGreen, 60));
            RestoreDC(dc, -1);
        }
        DrawLabel(dc, m_fontBody, m_updDownloading ? kText : RGB(6, 30, 18),
                  { m_updateRect.left, m_updateRect.top + S(9), m_updateRect.right, m_updateRect.bottom }, label, DT_CENTER);
    }

    if (RectVisible(dc, &m_tools[0].rect))
        DrawCard(dc, 0);
    if (RectVisible(dc, &m_tools[1].rect))
        DrawCard(dc, 1);
    if (RectVisible(dc, &m_tools[2].rect))
        DrawCard(dc, 2);
    if (RectVisible(dc, &m_tools[3].rect))
        DrawCard(dc, 3);
    if (RectVisible(dc, &m_tools[4].rect))
        DrawCard(dc, 4);
    if (RectVisible(dc, &m_tools[5].rect))
        DrawCard(dc, 5);
    if (RectVisible(dc, &m_tools[6].rect))
        DrawCard(dc, 6);
    if (RectVisible(dc, &m_officeRect))
        DrawOfficeCard(dc);

    // Footer: Version links, Hinweis in der Mitte
    DrawLabel(dc, m_fontSmall, kTextDim, { m_footer.left + S(4), m_footer.top + S(13), m_footer.left + S(176), m_footer.bottom },
              std::wstring(L"© 2026 Duncan · v") + SUITE_VERSION);
    if (!m_note.empty())
        DrawLabel(dc, m_fontSmall, kText, { m_footer.left + S(176), m_footer.top + S(13), m_footer.right - S(16), m_footer.bottom },
                  m_note, DT_CENTER);
    else if (!suite_safety::IsElevated())     // dezenter Hinweis: die Suite selbst braucht keine Adminrechte, zwei Programme schon
        DrawLabel(dc, m_fontSmall, Blend(kBackground, kTextDim, 70), { m_footer.left + S(176), m_footer.top + S(13), m_footer.right - S(16), m_footer.bottom },
                  L"Privacy Shield und Driver Updater fragen beim Start nach Administratorrechten", DT_CENTER | DT_END_ELLIPSIS);

    if (m_settingsOpen)
        DrawSettings(dc, w, h);
    if (m_infoOpen)
        DrawInfo(dc, w, h);
    if (m_modal)
        DrawModal(dc, w, h);
}

void SuiteWindow::DrawButton(HDC dc, const RECT& r, bool primary, bool hover, bool pressed, const std::wstring& text)
{
    COLORREF fill, border;
    if (primary)
    {
        fill = pressed ? Blend(kAccent, RGB(0, 0, 0), 25) : (hover ? Blend(kAccent, RGB(255, 255, 255), 14) : kAccent);
        border = fill;
    }
    else
    {
        fill = pressed ? RGB(24, 26, 32) : (hover ? kCardHover : kCardRaised);
        border = hover ? kBorderHover : kBorder;
    }
    FillRound(dc, r, S(12), fill, border);
    DrawLabel(dc, m_fontBody, primary ? RGB(8, 20, 36) : (hover ? kText : kTextDim),
              { r.left, r.top + (r.bottom - r.top - S(20)) / 2 - S(1), r.right, r.bottom }, text, DT_CENTER);
}

void SuiteWindow::DrawCheck(HDC dc, const RECT& r, bool on, bool hover, const std::wstring& label, const std::wstring& hint)
{
    const int box = S(22);
    RECT b{ r.left, r.top + S(2), r.left + box, r.top + S(2) + box };
    FillRound(dc, b, S(7), on ? kAccent : kCardRaised, on ? kAccent : (hover ? kBorderHover : kBorder));
    if (on)
    {
        HPEN pen = CreatePen(PS_SOLID, std::max(2, S(2)), RGB(8, 20, 36));
        HGDIOBJ old = SelectObject(dc, pen);
        MoveToEx(dc, b.left + S(5), b.top + S(11), nullptr);
        LineTo(dc, b.left + S(9), b.top + S(15));
        LineTo(dc, b.left + S(17), b.top + S(7));
        SelectObject(dc, old);
        DeleteObject(pen);
    }
    DrawLabel(dc, m_fontBody, kText, { r.left + box + S(14), r.top, r.right, r.top + S(24) }, label);
    DrawLabel(dc, m_fontSmall, kTextDim, { r.left + box + S(14), r.top + S(24), r.right, r.bottom }, hint);
}

void SuiteWindow::DrawSettings(HDC dc, int w, int h)
{
    FillAlphaRect(dc, RECT{ 0, 0, w, h }, RGB(0, 0, 0), 150);

    // zwei Spalten: links "Allgemein" (Schalter), rechts "Pfade & Backups" sowie "Updates & Info"
    const int pad = S(22), colGap = S(26);
    const int boxW = std::min(S(920), w - S(40));
    const int colW = (boxW - 2 * pad - colGap) / 2;
    const int titleH = S(30), btnH = S(38), rowH = S(40), gap = S(6), sectionH = S(28);
    const int leftH = 7 * rowH + 6 * gap;
    const int boxH = pad + titleH + S(8) + sectionH + leftH + S(18) + btnH + pad;
    RECT box{ (w - boxW) / 2, (h - boxH) / 2, (w - boxW) / 2 + boxW, (h - boxH) / 2 + boxH };

    DrawShadow(dc, box, S(18), static_cast<int>(m_dpi));
    DrawPanelAA(dc, box, S(18), RGB(27, 29, 36), kBorder);
    DrawLabel(dc, m_fontHeading, kText, { box.left + pad, box.top + pad + S(3), box.right - pad, box.top + pad + titleH }, L"Einstellungen");

    const int x0 = box.left + pad, x1 = x0 + colW + colGap;
    const int top = box.top + pad + titleH + S(8);

    // ---- links: Allgemein ----
    DrawLabel(dc, m_fontHeading, kTextDim, { x0, top, x0 + colW, top + sectionH }, L"Allgemein");
    int y = top + sectionH;
    auto row = [&](RECT& rc, bool on, int id, const wchar_t* label, const wchar_t* hint)
    {
        rc = { x0, y, x0 + colW, y + rowH };
        DrawCheck(dc, rc, on, m_hover == id, label, hint);
        y += rowH + gap;
    };
    row(m_chkStart, m_draft.autostart, 8, L"Duncan's Suite mit Windows starten", L"Startet die Suite bei der Anmeldung (nur für dein Konto).");
    row(m_chkTrayClose, m_draft.trayOnClose, 13, L"Beim Schließen in den Infobereich minimieren", L"Das X versteckt die Suite neben der Uhr, statt sie zu beenden.");
    row(m_chkRestore, m_draft.autoRestorePoint, 18, L"Vor kritischen Aktionen Wiederherstellungspunkt anlegen", L"Privacy Shield und Driver Updater sichern das System vorab.");
    row(m_chkAuto, m_draft.autoCheck, 7, L"Beim Start nach Updates suchen", L"Prüft beim Öffnen, ob eine neue Suite-Version vorliegt.");
    row(m_chkTrayLaunch, m_draft.trayOnLaunch, 14, L"Nach dem Start eines Programms minimieren", L"Die Suite zieht sich nach dem Starten in den Infobereich zurück.");
    row(m_chkCtxPdf, m_draft.ctxPdf, 15, L"Explorer-Kontextmenü für PDFs", L"Rechtsklick auf eine PDF: „Mit Duncan's PDF öffnen“.");
    row(m_chkCtxTidy, m_draft.ctxTidy, 16, L"Explorer-Kontextmenü für Tidy-Analyse", L"Rechtsklick auf Ordner/Laufwerk: „Mit Duncan's Tidy analysieren“.");

    // ---- rechts: Pfade & Backups ----
    DrawLabel(dc, m_fontHeading, kTextDim, { x1, top, x1 + colW, top + sectionH }, L"Pfade & Backups");
    int ry = top + sectionH;
    const int pathH = S(24) + S(18);
    auto pathRow = [&](const wchar_t* label, const std::wstring& path, RECT& b1, int id1, const wchar_t* t1, RECT* b2, int id2, const wchar_t* t2)
    {
        const int bw1 = (b2 ? S(96) : S(150));
        b1 = { x1 + colW - bw1, ry, x1 + colW, ry + pathH };
        int textRight = b1.left - S(10);
        if (b2)
        {
            *b2 = b1;
            b1 = { b2->left - S(8) - bw1, ry, b2->left - S(8), ry + pathH };
            textRight = b1.left - S(10);
        }
        DrawLabel(dc, m_fontBody, kText, { x1, ry, textRight, ry + S(24) }, label);
        DrawLabel(dc, m_fontSmall, kTextDim, { x1, ry + S(24), textRight, ry + pathH }, path, DT_LEFT | DT_PATH_ELLIPSIS);
        DrawButton(dc, b1, false, m_hover == id1, m_press == id1 && m_hover == id1, t1);
        if (b2)
            DrawButton(dc, *b2, false, m_hover == id2, m_press == id2 && m_hover == id2, t2);
        ry += pathH + S(12);
    };
    pathRow(L"Backup-Ordner", suite_safety::BackupsDirPath() + L"\\", m_btnBackup, 19, L"Ordner öffnen", nullptr, 0, nullptr);
    pathRow(L"Logdatei", suite_safety::LogFilePath(), m_btnLog, 20, L"Log-Datei anzeigen", nullptr, 0, nullptr);
    pathRow(L"Download-Ordner",
            m_draft.downloadDir.empty() ? L"Standard (%LOCALAPPDATA%\\DuncansSuite\\update)" : m_draft.downloadDir,
            m_btnPick, 9, L"Ändern …", &m_btnReset, 10, L"Standard");

    // ---- rechts: Updates & Info ----
    ry += S(6);
    DrawLabel(dc, m_fontHeading, kTextDim, { x1, ry, x1 + colW, ry + sectionH }, L"Updates & Info");
    ry += sectionH;
    m_btnCheck = { x1, ry, x1 + S(210), ry + btnH };
    DrawButton(dc, m_btnCheck, false, m_hover == 21, m_press == 21 && m_hover == 21, L"Nach Updates suchen");
    DrawLabel(dc, m_fontBody, kText, { m_btnCheck.right + S(16), ry, x1 + colW, ry + S(20) }, std::wstring(L"Duncan's Suite v") + SUITE_VERSION);
    DrawLabel(dc, m_fontSmall, kTextDim, { m_btnCheck.right + S(16), ry + S(20), x1 + colW, ry + S(38) }, L"© 2026 Duncan");

    const int bw = S(120);
    m_btnSave = { box.right - pad - bw, box.bottom - pad - btnH, box.right - pad, box.bottom - pad };
    m_btnCancel = { m_btnSave.left - S(10) - bw, m_btnSave.top, m_btnSave.left - S(10), m_btnSave.bottom };
    DrawButton(dc, m_btnCancel, false, m_hover == 12, m_press == 12 && m_hover == 12, L"Abbrechen");
    DrawButton(dc, m_btnSave, true, m_hover == 11, m_press == 11 && m_hover == 11, L"Speichern");
}

void SuiteWindow::OpenSettings()
{
    m_draft = m_settings;
    m_draft.autostart = IsAutostartEnabled();
    m_settingsOpen = true;
    m_hover = m_press = -1;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SuiteWindow::CloseSettings(bool save)
{
    m_settingsOpen = false;
    m_hover = m_press = -1;
    if (save)
    {
        std::wstring problems;
        if (!SetAutostart(m_draft.autostart))
        {
            problems += L"Der Autostart konnte nicht geändert werden.\n";
            m_draft.autostart = IsAutostartEnabled();
        }
        {
            // Explorer-Kontextmenue (HKCU, kein Admin): nur anfassen, wenn sich etwas geaendert hat
            if (m_draft.ctxPdf != m_settings.ctxPdf || m_draft.ctxTidy != m_settings.ctxTidy)
            {
                problems += ApplyContextMenus(m_draft.ctxPdf, m_draft.ctxTidy);
                m_draft.ctxPdf = VerbExists(kPdfVerb);                                          // tatsaechlichen Stand uebernehmen
                m_draft.ctxTidy = VerbExists(kTidyVerbs[0]) && VerbExists(kTidyVerbs[1]);
            }
        }
        if (!SaveSettings(m_draft))
            problems += L"Die Einstellungen konnten nicht gespeichert werden.\n";
        const bool enabledNow = m_draft.autoCheck && !m_settings.autoCheck;
        m_settings = m_draft;
        if (!problems.empty())
            ShowModal(L"Einstellungen", problems);
        else
            SetNote(L"Einstellungen gespeichert");
        if (enabledNow && !m_updAvailable)
            StartUpdateCheck();
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ---- Infobereich (Tray) --------------------------------------------------------------------

// ---- Info-Dialog: Neuigkeiten je Programm ----------------------------------------------------

const wchar_t* SuiteWindow::InfoKey(int tab) const
{
    return tab >= 0 && tab < kToolCount ? m_tools[tab].exe : L"DuncansSuite.exe";
}

std::wstring SuiteWindow::InfoInstalled(int tab) const
{
    if (tab == kToolCount)
        return SUITE_VERSION;
    std::wstring v;
    if (tab >= 0 && tab < kToolCount && !m_tools[tab].path.empty() && FileVersionAtLeast(m_tools[tab].path, 0, 0, 0, &v))
        return v;
    return {};
}

std::wstring SuiteWindow::InfoNewVersion(int tab) const
{
    if (tab == kToolCount)
        return m_updAvailable ? m_upd.version : std::wstring();
    return tab >= 0 && tab < kToolCount ? m_tools[tab].newVersion : std::wstring();
}

void SuiteWindow::OpenInfo()
{
    if (m_changes.empty())
        m_changes = changelog::Parse(EmbeddedChangelog());
    m_infoOpen = true;
    m_infoScroll = 0;
    m_infoTab = 0;
    for (int i = 0; i < kInfoTabs; ++i)   // zuerst das Programm zeigen, fuer das ein Update bereitsteht
        if (!InfoNewVersion(i).empty())
        {
            m_infoTab = i;
            break;
        }
    m_hover = m_press = -1;
    if (!m_changesFetched)
        StartChangelogFetch();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SuiteWindow::CloseInfo()
{
    m_infoOpen = false;
    m_hover = m_press = -1;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SuiteWindow::ScrollInfo(int delta)
{
    m_infoScroll = std::clamp(m_infoScroll + delta, 0, m_infoMaxScroll);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SuiteWindow::StartChangelogFetch()
{
    // Im Hintergrund laden; bei Fehlern (offline) bleibt die eingebaute Fassung stehen
    m_changesFetched = true;
    m_changeFetch = std::make_shared<ChangeFetch>();
    const HWND hwnd = m_hwnd;
    const std::shared_ptr<ChangeFetch> st = m_changeFetch;
    std::thread([hwnd, st]()
    {
        std::string text;
        const bool ok = FetchChangelogText(text);
        {
            std::lock_guard<std::mutex> lock(st->mutex);
            st->ok = ok;
            if (ok)
                st->text = std::move(text);
        }
        if (IsWindow(hwnd))
            PostMessageW(hwnd, WM_CHANGES, 0, 0);
    }).detach();
}

void SuiteWindow::DrawInfo(HDC dc, int w, int h)
{
    FillAlphaRect(dc, RECT{ 0, 0, w, h }, RGB(0, 0, 0), 150);

    static const wchar_t* const kNames[kInfoTabs] = { L"Tidy", L"Driver Updater", L"PDF Reader", L"Privacy Shield", L"Photo Commander", L"Docs-Files", L"Vault", L"Suite" };
    const int pad = S(22), btnH = S(38);
    const int boxW = std::min(S(940), w - S(40)), boxH = std::min(S(700), h - S(50));
    RECT box{ (w - boxW) / 2, (h - boxH) / 2, (w - boxW) / 2 + boxW, (h - boxH) / 2 + boxH };
    DrawShadow(dc, box, S(18), static_cast<int>(m_dpi));
    DrawPanelAA(dc, box, S(18), RGB(27, 29, 36), kBorder);
    DrawLabel(dc, m_fontHeading, kText, { box.left + pad, box.top + pad + S(3), box.right - pad, box.top + pad + S(30) }, L"Neuigkeiten & Updates");

    // Reiter: einer pro Programm (Farbe wie die Karte), gruener Punkt = Update verfuegbar
    const int tabH = S(34), tabGap = S(8), tabTop = box.top + pad + S(42);
    int want[kInfoTabs], sum = 0;
    for (int i = 0; i < kInfoTabs; ++i)
    {
        want[i] = TextWidth(dc, m_fontBody, kNames[i]) + S(44);
        sum += want[i];
    }
    const int avail = boxW - 2 * pad - (kInfoTabs - 1) * tabGap;
    int x = box.left + pad;
    for (int i = 0; i < kInfoTabs; ++i)
    {
        const int tw = sum > avail ? want[i] * avail / sum : want[i];
        m_infoTabs[i] = { x, tabTop, x + tw, tabTop + tabH };
        x += tw + tabGap;
        const COLORREF color = i < kToolCount ? m_tools[i].color : kAccent;
        const bool active = i == m_infoTab, hover = m_hover == 24 + i;
        FillRound(dc, m_infoTabs[i], S(10), active ? Blend(kCardRaised, color, 22) : (hover ? kCardHover : kCardRaised),
                  active ? color : (hover ? kBorderHover : kBorder));
        const int dot = S(8);
        FillRound(dc, RECT{ m_infoTabs[i].left + S(14), m_infoTabs[i].top + (tabH - dot) / 2, m_infoTabs[i].left + S(14) + dot, m_infoTabs[i].top + (tabH - dot) / 2 + dot }, dot, color, color);
        DrawLabel(dc, m_fontBody, active || hover ? kText : kTextDim,
                  { m_infoTabs[i].left + S(28), m_infoTabs[i].top + S(7), m_infoTabs[i].right - S(8), m_infoTabs[i].bottom }, kNames[i]);
        if (!InfoNewVersion(i).empty())
        {
            const int d = S(9);
            FillRound(dc, RECT{ m_infoTabs[i].right - d - S(4), m_infoTabs[i].top + S(4), m_infoTabs[i].right - S(4), m_infoTabs[i].top + S(4) + d }, d, kGreen, kGreen);
        }
    }

    // Statuszeile des gewaehlten Programms
    const std::wstring installed = InfoInstalled(m_infoTab), newer = InfoNewVersion(m_infoTab);
    std::wstring status;
    COLORREF statusColor = kTextDim;
    if (!newer.empty())
    {
        status = (installed.empty() ? std::wstring() : L"Installiert: v" + installed + L"   ·   ") + L"Update verfügbar: v" + newer;
        statusColor = kGreen;
    }
    else if (!installed.empty())
        status = L"Installiert: v" + installed + L"   ·   aktuell";
    else
        status = L"Nicht installiert (die Einträge zeigen, was die aktuelle Version kann)";
    const int statusTop = tabTop + tabH + S(14);
    DrawLabel(dc, m_fontBody, statusColor, { box.left + pad, statusTop, box.right - pad, statusTop + S(24) }, status);

    // Eintraege (scrollbar)
    const RECT view{ box.left + pad, statusTop + S(34), box.right - pad, box.bottom - pad - btnH - S(14) };
    m_infoView = view;
    const int viewH = std::max(1, static_cast<int>(view.bottom - view.top));
    const int textX = S(18), rightGap = S(18);
    const int textW = std::max(S(100), static_cast<int>(view.right - view.left) - textX - rightGap);
    const versionutil::Version have = versionutil::Parse(installed);
    const changelog::Section* sec = changelog::Find(m_changes, InfoKey(m_infoTab));

    // Ein Durchlauf misst nur (Gesamthoehe fuer Scroll-Grenze), der zweite zeichnet.
    auto pass = [&](bool draw) -> int
    {
        int y = draw ? view.top - m_infoScroll : 0;
        const int y0 = y;
        if (!sec || sec->entries.empty())
        {
            if (draw)
                DrawLabel(dc, m_fontBody, kTextDim, { view.left, y, view.right, y + S(24) }, L"Noch keine Einträge vorhanden.");
            return S(24);
        }
        for (const changelog::Entry& e : sec->entries)
        {
            if (draw)
            {
                DrawLabel(dc, m_fontHeading, kText, { view.left, y, view.right - S(120), y + S(28) }, e.title);
                const versionutil::Version ev = versionutil::Parse(e.title);
                if (ev.valid && have.valid && versionutil::IsNewer(ev, have))   // juenger als die installierte Version
                {
                    const int tx = view.left + TextWidth(dc, m_fontHeading, e.title) + S(12);
                    RECT chip{ tx, y + S(4), tx + S(44), y + S(24) };
                    FillRound(dc, chip, S(8), kGreen, kGreen);
                    DrawLabel(dc, m_fontSmall, RGB(6, 30, 18), { chip.left, chip.top + S(1), chip.right, chip.bottom }, L"NEU", DT_CENTER);
                }
                DrawLabel(dc, m_fontSmall, kTextDim, { view.right - S(160) - rightGap, y + S(4), view.right - rightGap, y + S(28) }, e.date, DT_RIGHT);
            }
            y += S(30);
            for (const std::wstring& item : e.items)
            {
                const int ih = MeasureWrapped(dc, m_fontBody, item, textW);
                if (draw)
                {
                    DrawLabel(dc, m_fontBody, kTextDim, { view.left + S(2), y, view.left + textX, y + S(22) }, L"•");
                    DrawWrapped(dc, m_fontBody, kText, { view.left + textX, y, view.left + textX + textW, y + ih }, item);
                }
                y += ih + S(6);
            }
            if (draw)
                FillAlphaRect(dc, RECT{ view.left, y + S(7), view.right - rightGap, y + S(8) }, kBorder, 255);
            y += S(18);
        }
        return y - y0;
    };
    const int contentH = pass(false);
    m_infoMaxScroll = std::max(0, contentH - viewH);
    m_infoScroll = std::clamp(m_infoScroll, 0, m_infoMaxScroll);
    SaveDC(dc);
    IntersectClipRect(dc, view.left, view.top, view.right, view.bottom);
    pass(true);
    RestoreDC(dc, -1);
    if (m_infoMaxScroll > 0)   // schmaler Scrollbalken rechts
    {
        const int thumbH = std::max(S(30), viewH * viewH / contentH);
        const int thumbY = view.top + (viewH - thumbH) * m_infoScroll / m_infoMaxScroll;
        FillRound(dc, RECT{ view.right - S(6), thumbY, view.right, thumbY + thumbH }, S(6), kBorderHover, kBorderHover);
    }

    // Fusszeile: Quelle links, Schliessen rechts
    DrawLabel(dc, m_fontSmall, kTextDim, { box.left + pad, box.bottom - pad - btnH + S(10), box.right - pad - S(160), box.bottom - pad },
              m_changesOnline ? L"Quelle: GitHub (aktuell)" : L"Quelle: in der Suite eingebaut");
    const int bw = S(120);
    m_infoClose = { box.right - pad - bw, box.bottom - pad - btnH, box.right - pad, box.bottom - pad };
    DrawButton(dc, m_infoClose, true, m_hover == 32, m_press == 32 && m_hover == 32, L"Schließen");
}

void SuiteWindow::AddTrayIcon()
{
    if (!m_trayIcon)
        m_trayIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
                                                   GetSystemMetricsForDpi(SM_CXSMICON, m_dpi), GetSystemMetricsForDpi(SM_CYSMICON, m_dpi), LR_DEFAULTCOLOR));
    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize = sizeof(m_nid);
    m_nid.hWnd = m_hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    m_nid.uCallbackMessage = kTrayMsg;
    m_nid.hIcon = m_trayIcon;
    wcscpy_s(m_nid.szTip, L"Duncan's Suite");
    m_trayShown = Shell_NotifyIconW(NIM_ADD, &m_nid) != FALSE;
}

void SuiteWindow::RemoveTrayIcon()
{
    if (m_trayShown)
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
    m_trayShown = false;
    if (m_trayIcon)
    {
        DestroyIcon(m_trayIcon);
        m_trayIcon = nullptr;
    }
}

void SuiteWindow::HideToTray()
{
    if (!m_trayShown)
        AddTrayIcon();
    if (!m_trayShown)
    {
        ShowWindow(m_hwnd, SW_MINIMIZE);   // ohne Symbol nicht verstecken, sonst waere die Suite nicht mehr erreichbar
        return;
    }
    ShowWindow(m_hwnd, SW_HIDE);
    m_hiddenToTray = true;
}

void SuiteWindow::RestoreFromTray()
{
    ShowWindow(m_hwnd, IsIconic(m_hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(m_hwnd);
    m_hiddenToTray = false;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SuiteWindow::TrayBalloon(const std::wstring& title, const std::wstring& text)
{
    if (!m_trayShown)
        AddTrayIcon();
    if (!m_trayShown)
        return;
    NOTIFYICONDATAW n = m_nid;
    n.uFlags = NIF_INFO;
    wcsncpy_s(n.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(n.szInfo, text.c_str(), _TRUNCATE);
    n.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    Shell_NotifyIconW(NIM_MODIFY, &n);   // erscheint als Windows-Benachrichtigung
}

void SuiteWindow::ShowTrayMenu()
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Duncan's Suite öffnen");
    AppendMenuW(menu, MF_STRING, 2, L"RAM schnell leeren (Quick Clean)");
    AppendMenuW(menu, MF_STRING, 3, L"Tidy starten");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 4, L"Beenden");
    SetMenuDefaultItem(menu, 1, FALSE);
    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(m_hwnd);   // sonst schliesst sich das Menue nicht beim Klick daneben
    const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);
    PostMessageW(m_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
    switch (cmd)
    {
    case 1:
        RestoreFromTray();
        break;
    case 2:
        StartQuickClean();
        break;
    case 3:
        FindTools();
        if (m_tools[0].path.empty())
            RestoreFromTray();   // nicht installiert: Download/Hinweis braucht das sichtbare Fenster
        Launch(0);
        break;
    case 4:
        m_exiting = true;
        DestroyWindow(m_hwnd);
        break;
    default:
        break;
    }
}

namespace
{
    unsigned long long AvailablePhysical()
    {
        MEMORYSTATUSEX ms{};
        ms.dwLength = sizeof(ms);
        GlobalMemoryStatusEx(&ms);
        return ms.ullAvailPhys;
    }

    // Standby-Liste leeren (braucht Administrator-Rechte; ohne sie scheitert das still)
    bool PurgeStandbyList()
    {
        HANDLE tok = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok))
            return false;
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        bool ok = LookupPrivilegeValueW(nullptr, L"SeProfileSingleProcessPrivilege", &tp.Privileges[0].Luid) &&
                  AdjustTokenPrivileges(tok, FALSE, &tp, 0, nullptr, nullptr) && GetLastError() == ERROR_SUCCESS;
        CloseHandle(tok);
        if (!ok)
            return false;
        using NtSetFn = LONG(NTAPI*)(ULONG, PVOID, ULONG);
        HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        auto fn = nt ? reinterpret_cast<NtSetFn>(reinterpret_cast<void*>(GetProcAddress(nt, "NtSetSystemInformation"))) : nullptr;
        ULONG command = 4;   // MemoryPurgeStandbyList
        return fn && fn(80 /* SystemMemoryListInformation */, &command, sizeof(command)) >= 0;
    }

    std::wstring SizeText(unsigned long long b)
    {
        wchar_t buf[40];
        if (b >= (1ull << 30)) swprintf(buf, 40, L"%.1f GB", b / 1073741824.0);
        else swprintf(buf, 40, L"%.0f MB", b / 1048576.0);
        return buf;
    }
}

// Quick Clean: Arbeitssaetze aller Programme verkleinern (mit Administrator-Rechten zusaetzlich die Standby-Liste leeren)
void SuiteWindow::StartQuickClean()
{
    if (m_quickBusy.exchange(true))
        return;
    SetNote(L"RAM wird geleert …");
    const HWND hwnd = m_hwnd;
    std::thread([hwnd]()
    {
        const unsigned long long before = AvailablePhysical();
        int trimmed = 0;
        std::vector<DWORD> ids(8192);
        DWORD bytes = 0;
        if (EnumProcesses(ids.data(), static_cast<DWORD>(ids.size() * sizeof(DWORD)), &bytes))
        {
            const DWORD self = GetCurrentProcessId();
            for (DWORD i = 0; i < bytes / sizeof(DWORD); ++i)
            {
                if (ids[i] == 0 || ids[i] == self)
                    continue;
                HANDLE p = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_INFORMATION, FALSE, ids[i]);   // geschuetzte Prozesse scheitern still
                if (!p)
                    continue;
                if (EmptyWorkingSet(p))
                    ++trimmed;
                CloseHandle(p);
            }
        }
        PurgeStandbyList();
        Sleep(600);   // Windows braucht einen Moment, bis die Zaehler stimmen
        const unsigned long long after = AvailablePhysical();
        if (IsWindow(hwnd))
            PostMessageW(hwnd, kQuickDone, static_cast<WPARAM>(after > before ? after - before : 0), static_cast<LPARAM>(trimmed));
    }).detach();
}

void SuiteWindow::DrawModal(HDC dc, int w, int h)
{
    FillAlphaRect(dc, RECT{ 0, 0, w, h }, RGB(0, 0, 0), 150);

    const int pad = S(26);
    const int boxW = std::min(S(540), w - S(40));
    const int inner = boxW - 2 * pad;
    const int titleH = S(30), btnH = S(38);
    const int textH = MeasureWrapped(dc, m_fontBody, m_modalText, inner);
    const int boxH = pad + titleH + S(14) + textH + S(24) + btnH + pad;
    RECT box{ (w - boxW) / 2, (h - boxH) / 2, (w - boxW) / 2 + boxW, (h - boxH) / 2 + boxH };

    DrawShadow(dc, box, S(18), static_cast<int>(m_dpi));
    DrawPanelAA(dc, box, S(18), RGB(27, 29, 36), kBorder);

    const COLORREF accent = kOrange;
    const int icon = S(30);
    RECT iconRc{ box.left + pad, box.top + pad, box.left + pad + icon, box.top + pad + icon };
    FillRound(dc, iconRc, icon, Blend(RGB(27, 29, 36), accent, 22), Blend(RGB(27, 29, 36), accent, 55));
    DrawLabel(dc, m_fontHeading, accent, { iconRc.left, iconRc.top + S(2), iconRc.right, iconRc.bottom }, L"!", DT_CENTER);
    DrawLabel(dc, m_fontHeading, kText, { iconRc.right + S(12), box.top + pad + S(3), box.right - pad, box.top + pad + titleH }, m_modalTitle);
    DrawWrapped(dc, m_fontBody, kText, RECT{ box.left + pad, box.top + pad + titleH + S(14), box.right - pad, box.top + pad + titleH + S(14) + textH }, m_modalText);

    const int okW = S(110);
    m_modalOk = { box.right - pad - okW, box.bottom - pad - btnH, box.right - pad, box.bottom - pad };
    const bool hover = m_hover == 3;
    const bool pressed = m_press == 3 && hover;
    const COLORREF fill = pressed ? Blend(kAccent, RGB(0, 0, 0), 25) : (hover ? Blend(kAccent, RGB(255, 255, 255), 14) : kAccent);
    FillRound(dc, m_modalOk, S(12), fill, fill);
    DrawLabel(dc, m_fontBody, RGB(8, 20, 36), { m_modalOk.left, m_modalOk.top + S(8), m_modalOk.right, m_modalOk.bottom }, L"OK", DT_CENTER);
}

// ---- Aktionen ------------------------------------------------------------------------------

void SuiteWindow::ShowModal(const std::wstring& title, const std::wstring& text)
{
    m_modal = true;
    m_modalTitle = title;
    m_modalText = text;
    m_hover = -1;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SuiteWindow::SetNote(const std::wstring& text)
{
    m_note = text;
    SetTimer(m_hwnd, 1, 3200, nullptr);
    InvalidateRect(m_hwnd, &m_footer, FALSE);
}

void SuiteWindow::ReleaseBannerCache()
{
    if (m_bnDc)
    {
        SelectObject(m_bnDc, m_bnOld);
        DeleteObject(m_bnBmp);
        DeleteDC(m_bnDc);
    }
    m_bnDc = nullptr;
    m_bnBmp = nullptr;
    m_bnOld = nullptr;
    m_bnW = m_bnH = 0;
}

void SuiteWindow::Launch(int index)
{
    FindTools();   // der Nutzer kann inzwischen installiert haben
    const Tool& t = m_tools[index];
    if (t.downloading)
    {
        SetNote(std::wstring(t.title) + L" wird gerade heruntergeladen …");
        return;
    }
    if ((t.path.empty() && (t.downloadBase || t.directUrl)) || (!t.newVersion.empty() && !t.path.empty() && (t.downloadBase || t.directUrl)))
    {
        StartDownload(index);   // nicht installiert: Ein-Klick-Installation von GitHub
        return;
    }
    if (t.path.empty())
    {
        std::wstring text = std::wstring(t.title) + L" wurde nicht gefunden. Gesucht wurde in:\n";
        for (const auto& s : t.searched)
        {
            // lange Pfade kuerzen (…\Ordner\Datei.exe), damit der Dialog sauber umbricht
            wchar_t compact[96] = {};
            PathCompactPathExW(compact, s.c_str(), 64, 0);
            text += L"\n" + std::wstring(compact);
        }
        text += L"\n\nLege " + std::wstring(t.exe) + L" in denselben Ordner wie die Suite oder installiere das Programm.";
        ShowModal(L"Programm nicht gefunden", text);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // Programme mit Adminrechten (Driver Updater, Privacy Shield) aus dem Benutzerordner nur starten, wenn die EXE noch zu ihrer
    // Signatur passt. Fehlt die Signatur (aeltere Installation) oder wurde die Datei veraendert, laedt die Suite sie neu und prueft sie.
    {
        const std::wstring local = LocalAppData();
        const bool adminTool = _wcsicmp(t.exe, L"DuncansDriverUpdater.exe") == 0 || _wcsicmp(t.exe, L"DuncansPrivacy.exe") == 0;
        const bool inUserDir = !local.empty() && t.path.size() > local.size() && _wcsnicmp(t.path.c_str(), local.c_str(), local.size()) == 0;
        if (adminTool && inUserDir && !VerifyFileSignature(t.path, kSuitePublicKey, sizeof(kSuitePublicKey)) && (t.downloadBase || t.directUrl))
        {
            SetNote(std::wstring(t.title) + L": Signatur fehlt oder passt nicht – die Datei wird neu geladen und geprüft …");
            StartDownload(index);
            return;
        }
    }

    const std::wstring dir = t.path.substr(0, t.path.find_last_of(L"\\/"));
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC;
    sei.hwnd = m_hwnd;
    sei.lpVerb = L"open";   // loest bei EXEs mit requireAdministrator die UAC-Abfrage aus
    sei.lpFile = t.path.c_str();
    sei.lpDirectory = dir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei))
    {
        SetNote(std::wstring(t.title) + L" wird gestartet …");
        if (m_settings.trayOnLaunch)
            HideToTray();   // Suite zieht sich in den Infobereich zurueck
    }
    else if (GetLastError() == ERROR_CANCELLED)
        SetNote(L"Start abgebrochen");
    else
        ShowModal(L"Start fehlgeschlagen", std::wstring(t.title) + L" konnte nicht gestartet werden (Fehlercode " + std::to_wstring(GetLastError()) + L").\n\n" + t.path);
}

std::wstring SuiteWindow::InstallDir(const Tool& t) const
{
    // Standard-Installationsordner von Tidy: %LOCALAPPDATA%\DuncansTidy (kein Admin noetig, wird beim Suchen mit erfasst)
    std::wstring stem = t.exe;
    const size_t dot = stem.find_last_of(L'.');
    if (dot != std::wstring::npos)
        stem.erase(dot);
    return LocalAppData() + L"\\" + stem;
}

void SuiteWindow::StartDownload(int index)
{
    Tool& t = m_tools[index];
    if (m_dlBusy)
    {
        SetNote(L"Es läuft bereits ein Download …");
        return;
    }
    if (m_dlThread.joinable())
        m_dlThread.join();   // vorheriger, beendeter Download
    if (LocalAppData().empty())
    {
        ShowModal(L"Download nicht möglich", L"Der Ordner %LOCALAPPDATA% ist nicht verfügbar.");
        return;
    }
    t.downloading = true;
    t.progress = 0;
    m_dlBusy = true;
    m_dlCancel = false;
    const std::wstring extraUrl = t.extraUrl ? t.extraUrl : L"", extraName = t.extraName ? t.extraName : L"";
    const std::wstring base = t.downloadBase ? t.downloadBase : L"", url = t.directUrl ? t.directUrl : L"", exe = t.exe,
                       dir = InstallDir(t);   // Updates landen immer in %LOCALAPPDATA%<Name>; FindTools nimmt die neuere Kopie
    const HWND hwnd = m_hwnd;
    m_dlThread = std::thread([this, hwnd, index, base, url, exe, dir, extraUrl, extraName]()
    {
        std::wstring error;
        bool ok = url.empty() ? DownloadVerified(hwnd, index, base, exe, dir, m_dlCancel, error)
                                    : DownloadDirect(hwnd, index, url, exe, dir, m_dlCancel, error);
        if (ok && !extraUrl.empty() && !FileExists(dir + L"\\" + extraName))   // Begleitdatei (pdfium.dll) nur laden, wenn sie fehlt
            ok = DownloadDirect(hwnd, index, extraUrl, extraName, dir, m_dlCancel, error);
        m_dlMessage = error;   // vor PostMessage gesetzt, danach vom UI-Thread gelesen
        if (IsWindow(hwnd))
            PostMessageW(hwnd, WM_DL_DONE, ok ? 1 : 0, static_cast<LPARAM>(index));
    });
    SetNote(std::wstring(t.title) + L" wird heruntergeladen …");
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SuiteWindow::StartUpdateCheck()
{
    // Pruefung im Hintergrund; Fehler (offline, Rate-Limit) bleiben still. Ergebnis ueber shared_ptr,
    // damit der Thread auch nach dem Schliessen des Fensters gefahrlos auslaufen kann.
    FindTools();
    m_checkResult = std::make_shared<UpdateInfo>();
    const HWND hwnd = m_hwnd;
    const std::shared_ptr<UpdateInfo> result = m_checkResult;
    // Alle Module zentral mitpruefen: installierte Version aus der EXE lesen, Release-Version im Hintergrund vergleichen
    struct Job { int index; std::wstring url, exe, local; };
    std::vector<Job> jobs;
    for (int i = 0; i < kToolCount; ++i)
    {
        const Tool& t = m_tools[i];
        std::wstring local;
        if (t.versionUrl && !t.path.empty() && FileVersionAtLeast(t.path, 0, 0, 0, &local))
            jobs.push_back({ i, t.versionUrl, t.exe, local });
    }
    m_modRemote = std::make_shared<std::vector<std::wstring>>(kToolCount);
    const std::shared_ptr<std::vector<std::wstring>> modRemote = m_modRemote;
    std::thread([hwnd, result, jobs, modRemote]()
    {
        bool newer = false, failed = false;
        int mask = 0;
        for (const Job& j : jobs)
        {
            std::wstring remote;
            if (FetchModuleVersion(j.url, j.exe, remote) && IsNewer(ParseVer(remote), ParseVer(j.local)))
            {
                (*modRemote)[j.index] = remote;
                mask |= 1 << j.index;
            }
        }
        UpdateInfo best;
        if (!FetchLatestSuiteInstaller(best))
            failed = true;
        else
        {
            const Version remote = ParseVer(best.version);
            const Version local = ParseVer(SUITE_VERSION);
            if (remote.valid && local.valid && IsNewer(remote, local))
            {
                *result = best;
                newer = true;
            }
        }
        if (IsWindow(hwnd))
            PostMessageW(hwnd, WM_UPD_CHECKED, newer ? 1 : (failed ? 2 : 0), mask);
    }).detach();
}

void SuiteWindow::StartUpdateDownload()
{
    if (!m_updInstallerPath.empty() && FileExists(m_updInstallerPath))
    {
        RunInstaller();   // schon geladen (z. B. UAC-Abfrage vorher abgebrochen)
        return;
    }
    if (m_dlBusy)
    {
        SetNote(L"Es läuft bereits ein Download …");
        return;
    }
    if (m_dlThread.joinable())
        m_dlThread.join();
    m_dlBusy = true;
    m_updDownloading = true;
    m_updProgress = 0;
    m_dlCancel = false;
    const UpdateInfo info = m_upd;
    const HWND hwnd = m_hwnd;
    const std::wstring customDir = m_settings.downloadDir;
    m_dlThread = std::thread([this, hwnd, info, customDir]()
    {
        std::wstring error, path;
        const bool ok = DownloadInstaller(hwnd, info, customDir, path, m_dlCancel, error);
        m_updInstallerPath = ok ? path : std::wstring();
        m_dlMessage = error;
        if (IsWindow(hwnd))
            PostMessageW(hwnd, WM_DL_DONE, ok ? 1 : 0, static_cast<LPARAM>(kSuiteIdx));
    });
    InvalidateRect(m_hwnd, &m_banner, FALSE);
}

void SuiteWindow::RunInstaller()
{
    if (m_updInstallerPath.empty() || !FileExists(m_updInstallerPath))
    {
        m_updInstallerPath.clear();
        ShowModal(L"Update", L"Der heruntergeladene Installer wurde nicht gefunden. Bitte erneut auf das Update klicken.");
        return;
    }
    // Vor dem Start erneut pruefen: die Datei liegt im Nutzerordner und koennte nach dem Download veraendert worden sein.
    if (HashFileSha256(m_updInstallerPath) != m_upd.sha)
    {
        DeleteFileW(m_updInstallerPath.c_str());
        m_updInstallerPath.clear();
        ShowModal(L"Update", L"Der heruntergeladene Installer wurde verändert und wird nicht gestartet. Bitte erneut auf das Update klicken.");
        return;
    }
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC;
    sei.hwnd = m_hwnd;
    sei.lpVerb = L"open";   // der Installer fordert Administratorrechte selbst an (UAC)
    sei.lpFile = m_updInstallerPath.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei))
    {
        SetNote(L"Installer wird gestartet …");
        m_exiting = true;                       // wirklich beenden, nicht in den Infobereich
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);   // Suite beenden, damit das Update die Dateien ersetzen kann
    }
    else if (GetLastError() == ERROR_CANCELLED)
        SetNote(L"Update abgebrochen");
    else
        ShowModal(L"Start fehlgeschlagen", L"Der Installer konnte nicht gestartet werden (Fehlercode " + std::to_wstring(GetLastError()) + L").\n\n" + m_updInstallerPath);
}

// ---- Eingaben ------------------------------------------------------------------------------

void SuiteWindow::OnMouseMove(int x, int y)
{
    if (!m_tracking)
    {
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hwnd, 0 };
        TrackMouseEvent(&tme);
        m_tracking = true;
    }
    POINT pt{ x, y };
    int hover = -1;
    if (m_modal)
        hover = PtInRect(&m_modalOk, pt) ? 3 : -1;
    else if (m_settingsOpen)
    {
        const struct { const RECT* r; int id; } hits[] = { { &m_chkAuto, 7 }, { &m_chkStart, 8 }, { &m_chkTrayClose, 13 }, { &m_chkTrayLaunch, 14 }, { &m_chkCtxPdf, 15 }, { &m_chkCtxTidy, 16 }, { &m_chkRestore, 18 }, { &m_btnBackup, 19 }, { &m_btnLog, 20 }, { &m_btnCheck, 21 }, { &m_btnPick, 9 },
                                                          { &m_btnReset, 10 }, { &m_btnSave, 11 }, { &m_btnCancel, 12 } };
        for (const auto& hit : hits)
            if (PtInRect(hit.r, pt))
                hover = hit.id;
    }
    else if (m_infoOpen)
    {
        for (int i = 0; i < kInfoTabs; ++i)
            if (PtInRect(&m_infoTabs[i], pt))
                hover = 24 + i;
        if (PtInRect(&m_infoClose, pt))
            hover = 32;
    }
    else if (PtInRect(&m_gearRect, pt))
        hover = 6;
    else if (PtInRect(&m_infoRect, pt))
        hover = 23;
    else if (m_updAvailable && PtInRect(&m_updateRect, pt))
        hover = 4;
    else if (PtInRect(&m_tools[0].rect, pt))
        hover = 0;
    else if (PtInRect(&m_tools[1].rect, pt))
        hover = 1;
    else if (PtInRect(&m_tools[2].rect, pt))
        hover = 5;
    else if (PtInRect(&m_tools[3].rect, pt))
        hover = 17;
    else
    {
        for (int i = 4; i < kToolCount; ++i)
            if (PtInRect(&m_tools[i].rect, pt))
                hover = CardHid(i);
        if (PtInRect(&m_officeRect, pt))
            hover = kOfficeHid;
    }
    if (hover != m_hover)
    {
        m_hover = hover;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void SuiteWindow::OnClick(int, int)
{
    const int pressed = m_press;
    m_press = -1;
    if (pressed < 0 || pressed != m_hover)
    {
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (pressed == 3)
    {
        m_modal = false;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (pressed == 6)
        OpenSettings();
    else if (pressed == 23)
        OpenInfo();
    else if (pressed >= 24 && pressed < 24 + kInfoTabs)
    {
        m_infoTab = pressed - 24;
        m_infoScroll = 0;
    }
    else if (pressed == 32)
        CloseInfo();
    else if (pressed == 7)
        m_draft.autoCheck = !m_draft.autoCheck;
    else if (pressed == 8)
        m_draft.autostart = !m_draft.autostart;
    else if (pressed == 13)
        m_draft.trayOnClose = !m_draft.trayOnClose;
    else if (pressed == 14)
        m_draft.trayOnLaunch = !m_draft.trayOnLaunch;
    else if (pressed == 15)
        m_draft.ctxPdf = !m_draft.ctxPdf;
    else if (pressed == 16)
        m_draft.ctxTidy = !m_draft.ctxTidy;
    else if (pressed == 18)
        m_draft.autoRestorePoint = !m_draft.autoRestorePoint;
    else if (pressed == 19)          // Backup-Ordner im Explorer oeffnen (wird bei Bedarf angelegt)
    {
        const std::wstring dir = suite_safety::BackupsDir();
        if (dir.empty() || reinterpret_cast<INT_PTR>(ShellExecuteW(m_hwnd, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
            ShowModal(L"Backup-Ordner", L"Es gibt noch keinen Backup-Ordner (er entsteht, sobald Privacy Shield etwas ändert) oder er ist nicht vertrauenswürdig:\n" + suite_safety::BackupsDirPath());
    }
    else if (pressed == 20)          // Logdatei anzeigen (existiert sie noch nicht, wird ihr Ordner geoeffnet)
    {
        const std::wstring log = suite_safety::LogFilePath();
        if (!log.empty() && FileExists(log))
            ShellExecuteW(m_hwnd, L"open", log.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        else
        {
            const std::wstring dir = suite_safety::DataDir();
            if (dir.empty() || reinterpret_cast<INT_PTR>(ShellExecuteW(m_hwnd, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
                ShowModal(L"Logdatei", L"Es gibt noch keine Logdatei. Sie entsteht, sobald Privacy Shield oder Driver Updater etwas ändern.");
        }
    }
    else if (pressed == 21)          // sofort nach Updates suchen, Ergebnis als Hinweis in der Fusszeile
    {
        m_manualCheck = true;
        SetNote(L"Suche nach Updates …");
        StartUpdateCheck();
    }
    else if (pressed == 9)
    {
        const std::wstring dir = PickFolder(m_hwnd, m_draft.downloadDir);
        if (IsUsableDir(dir))
            m_draft.downloadDir = dir;
        else if (!dir.empty())
            ShowModal(L"Ordner nicht zulässig", L"Bitte einen vollständigen Ordnerpfad wählen.");
    }
    else if (pressed == 10)
        m_draft.downloadDir.clear();
    else if (pressed == 11)
        CloseSettings(true);
    else if (pressed == 12)
        CloseSettings(false);
    else if (pressed == 4)
    {
        if (!m_updDownloading)
            StartUpdateDownload();
    }
    else if (pressed == kOfficeHid)
        ShowModal(L"Duncan's Office", L"Duncan's Office (Writer & Sheets) – in Entwicklung.\nDas Programm ist noch nicht verfügbar und erscheint in einem späteren Update.");
    else
        Launch(CardFromHid(pressed) >= 0 ? CardFromHid(pressed) : pressed);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

LRESULT SuiteWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == ShowRequestMessage())                  // zweiter Start: Fenster aus dem Infobereich/Hintergrund holen
    {
        RestoreFromTray();
        return 0;
    }
    if (m_taskbarMsg != 0 && msg == m_taskbarMsg)   // Explorer wurde neu gestartet: Symbol neu anlegen
    {
        m_trayShown = false;
        AddTrayIcon();
        return 0;
    }
    switch (msg)
    {
    case kTrayMsg:
        switch (LOWORD(lp))
        {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowTrayMenu();
            break;
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case NIN_BALLOONUSERCLICK:
            RestoreFromTray();
            break;
        default:
            break;
        }
        return 0;
    case kQuickDone:
    {
        m_quickBusy = false;
        const std::wstring text = L"Freigegeben: " + SizeText(static_cast<unsigned long long>(wp)) + L"  ·  " + std::to_wstring(static_cast<int>(lp)) + L" Programme verkleinert";
        SetNote(text);
        TrayBalloon(L"Quick Clean fertig", text);
        return 0;
    }
    case WM_CLOSE:
        if (m_settings.trayOnClose && !m_exiting)
        {
            HideToTray();   // statt Beenden in den Infobereich
            return 0;
        }
        break;
    case WM_CREATE:
    {
        m_dpi = GetDpiForWindow(m_hwnd);
        gfx::EnableDarkTitleBar(m_hwnd);
        CreateFonts();
        FindTools();
        m_settings = LoadSettings();
        if (m_settings.autoCheck)
            StartUpdateCheck();
        {
            // dunkle Kontextmenues (undokumentierte uxtheme-Ordinale 135 = SetPreferredAppMode, 136 = FlushMenuThemes)
            if (HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
            {
                using SetModeFn = int(WINAPI*)(int);
                using FlushFn = void(WINAPI*)();
                auto setMode = reinterpret_cast<SetModeFn>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(135))));
                auto flush = reinterpret_cast<FlushFn>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(136))));
                if (setMode) setMode(2);   // ForceDark
                if (flush) flush();
            }
        }
        m_taskbarMsg = RegisterWindowMessageW(L"TaskbarCreated");
        AddTrayIcon();
        RefreshContextMenus();   // vorhandene Kontextmenue-Eintraege auf den aktuellen Pfad bringen
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        Layout();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    case WM_GETMINMAXINFO:
    {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = S(920);
        mmi->ptMinTrackSize.y = S(860);
        return 0;
    }
    case WM_DPICHANGED:
    {
        m_dpi = HIWORD(wp);
        CreateFonts();
        const RECT* r = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(m_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC screen = BeginPaint(m_hwnd, &ps);
        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        HDC mem = CreateCompatibleDC(screen);
        HBITMAP bmp = CreateCompatibleBitmap(screen, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        const RECT& pr = ps.rcPaint;
        IntersectClipRect(mem, pr.left, pr.top, pr.right, pr.bottom);
        Draw(mem, rc.right, rc.bottom);
        BitBlt(screen, pr.left, pr.top, pr.right - pr.left, pr.bottom - pr.top, mem, pr.left, pr.top, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(m_hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSELEAVE:
        m_tracking = false;
        if (m_hover != -1)
        {
            m_hover = -1;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && m_hover != -1)
        {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_MOUSEWHEEL:
        if (m_infoOpen && !m_modal)
            ScrollInfo(-GET_WHEEL_DELTA_WPARAM(wp) * S(60) / WHEEL_DELTA);
        return 0;
    case WM_CHANGES:
    {
        bool ok = false;
        std::string text;
        if (m_changeFetch)
        {
            std::lock_guard<std::mutex> lock(m_changeFetch->mutex);
            ok = m_changeFetch->ok;
            text = m_changeFetch->text;
        }
        std::vector<changelog::Section> parsed;
        if (ok)
            parsed = changelog::Parse(text);
        if (!parsed.empty())
        {
            m_changes = std::move(parsed);
            m_changesOnline = true;
        }
        else
            m_changesFetched = false;   // beim naechsten Oeffnen noch einmal versuchen
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN:
        OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        m_press = m_hover;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP:
        OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        OnClick(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_KEYDOWN:
        if (m_modal && (wp == VK_ESCAPE || wp == VK_RETURN || wp == VK_SPACE))
        {
            m_modal = false;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        else if (m_infoOpen && !m_modal)
        {
            const int page = std::max(S(80), static_cast<int>(m_infoView.bottom - m_infoView.top) - S(40));
            switch (wp)
            {
            case VK_ESCAPE: case VK_RETURN: CloseInfo(); break;
            case VK_LEFT:  m_infoTab = (m_infoTab + kInfoTabs - 1) % kInfoTabs; m_infoScroll = 0; InvalidateRect(m_hwnd, nullptr, FALSE); break;
            case VK_RIGHT: m_infoTab = (m_infoTab + 1) % kInfoTabs; m_infoScroll = 0; InvalidateRect(m_hwnd, nullptr, FALSE); break;
            case VK_UP:    ScrollInfo(-S(48)); break;
            case VK_DOWN:  ScrollInfo(S(48)); break;
            case VK_PRIOR: ScrollInfo(-page); break;
            case VK_NEXT:  ScrollInfo(page); break;
            case VK_HOME:  ScrollInfo(-m_infoMaxScroll); break;
            case VK_END:   ScrollInfo(m_infoMaxScroll); break;
            }
        }
        else if (m_settingsOpen && !m_modal && (wp == VK_ESCAPE || wp == VK_RETURN))
            CloseSettings(wp == VK_RETURN);
        else if (!m_modal && !m_settingsOpen && wp == '1')
            Launch(0);
        else if (!m_modal && !m_settingsOpen && wp == '2')
            Launch(1);
        return 0;
    case WM_TIMER:
        if (wp == 2)
        {
            const ULONGLONG now = GetTickCount64();
            const double elapsed = now > m_introStart ? static_cast<double>(now - m_introStart) : 0.0;   // 250 ms Anlaufpause
            m_introT = static_cast<float>(std::clamp(elapsed / 1800.0, 0.0, 1.0));
            if (m_introT >= 1.0f)
                KillTimer(m_hwnd, 2);
            InvalidateRect(m_hwnd, &m_banner, FALSE);
            return 0;
        }
        KillTimer(m_hwnd, 1);
        m_note.clear();
        InvalidateRect(m_hwnd, &m_footer, FALSE);
        return 0;
    case WM_UPD_CHECKED:
    {
        m_updAvailable = wp == 1;
        std::wstring modNote;   // z. B. "Tidy v1.1.5, PDF Reader v1.1.27"
        for (int i = 0; i < kToolCount; ++i)
        {
            m_tools[i].newVersion = ((lp >> i) & 1) && m_modRemote && i < static_cast<int>(m_modRemote->size()) ? (*m_modRemote)[i] : std::wstring();
            if (!m_tools[i].newVersion.empty())
            {
                std::wstring name = m_tools[i].title;
                if (name.rfind(L"Duncan's ", 0) == 0)
                    name.erase(0, 9);
                modNote += (modNote.empty() ? L"" : L", ") + name + L" v" + m_tools[i].newVersion;
            }
        }
        if (m_manualCheck)
        {
            m_manualCheck = false;
            std::wstring note = wp == 1 ? L"Update verfügbar: Duncan's Suite v" + (m_checkResult ? m_checkResult->version : std::wstring()) : (wp == 2 ? L"Updatesuche nicht möglich (offline?)" : L"Duncan's Suite ist aktuell (v" SUITE_VERSION L")");
            if (!modNote.empty())
                note += L" · Programm-Updates: " + modNote + L" (auf den Karten)";
            SetNote(note);
        }
        else if (!modNote.empty() && !m_updAvailable)
            SetNote(L"Programm-Updates verfügbar: " + modNote + L" (auf den Karten installieren)");
        InvalidateRect(m_hwnd, nullptr, FALSE);
        if (m_updAvailable && m_checkResult)
            m_upd = *m_checkResult;
        if (m_updAvailable && m_hiddenToTray)
            TrayBalloon(L"Update verfügbar", L"Duncan's Suite v" + m_upd.version + L" kann installiert werden.");
        else if (!modNote.empty() && m_hiddenToTray)
            TrayBalloon(L"Programm-Updates verfügbar", modNote);
        return 0;
    }
    case WM_DL_PROGRESS:
        if (lp >= 0 && lp < kToolCount)
        {
            m_tools[lp].progress = static_cast<int>(wp);
            InvalidateRect(m_hwnd, &m_tools[lp].rect, FALSE);
        }
        else if (lp == kSuiteIdx)
        {
            m_updProgress = static_cast<int>(wp);
            InvalidateRect(m_hwnd, &m_banner, FALSE);
        }
        return 0;
    case WM_DL_DONE:
    {
        if (m_dlThread.joinable())
            m_dlThread.join();
        const int index = static_cast<int>(lp);
        m_dlBusy = false;
        if (index == kSuiteIdx)
        {
            m_updDownloading = false;
            if (wp)
                RunInstaller();   // Installer starten, danach beendet sich die Suite
            else
                ShowModal(L"Update-Download fehlgeschlagen", m_dlMessage.empty() ? std::wstring(L"Unbekannter Fehler.") : m_dlMessage);
        }
        else if (index >= 0 && index < kToolCount)
        {
            m_tools[index].downloading = false;
            FindTools();   // Button wechselt automatisch von "Herunterladen" auf "Starten"
            if (wp)
            {
                const bool wasUpdate = !m_tools[index].newVersion.empty();
                m_tools[index].newVersion.clear();
                SetNote(std::wstring(m_tools[index].title) + (wasUpdate ? L" wurde aktualisiert" : L" wurde installiert"));
            }
            else
                ShowModal(L"Download fehlgeschlagen", m_dlMessage.empty() ? std::wstring(L"Unbekannter Fehler.") : m_dlMessage);
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_DESTROY:
        RemoveTrayIcon();
        ReleaseBannerCache();
        m_dlCancel = true;
        if (m_dlThread.joinable())
            m_dlThread.join();
        DestroyFonts();
        IconsShutdown();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    {
        // Explorer-Kontextmenue "Mit Duncan's Tidy analysieren": kein Fenster, nur pruefen und Tidy starten
        const std::wstring cmd = GetCommandLineW();
        const wchar_t* flag = L"--tidy-analyze";
        const size_t pos = cmd.find(flag);
        if (pos != std::wstring::npos)
            return RunTidyAnalyze(cmd.substr(pos + wcslen(flag)));
    }
    // Nur eine Suite pro Anmeldung: ein weiterer Start holt das vorhandene Fenster (auch aus dem Infobereich) nach vorn
    HANDLE singleton = CreateMutexW(nullptr, FALSE, L"Local\\DuncansSuite.SingleInstance");
    if (singleton && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (HWND other = FindWindowW(kClassName, nullptr))
            PostMessageW(other, ShowRequestMessage(), 0, 0);
        CloseHandle(singleton);
        return 0;
    }
    SuiteWindow window;
    if (!window.Create(instance, showCommand))
    {
        MessageBoxW(nullptr, L"Das Hauptfenster konnte nicht erstellt werden.", L"Duncan's Suite", MB_ICONERROR | MB_OK);
        return 1;
    }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
