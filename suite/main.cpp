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
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wininet.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../src/Icons.h"
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
#define SUITE_VERSION L"1.1.2"
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace
{
    const wchar_t* kClassName = L"DuncansSuiteWnd";

    // Palette wie in den anderen Tools
    const COLORREF kBackground = RGB(18, 19, 22);
    const COLORREF kCardRaised = RGB(35, 38, 47);
    const COLORREF kCardHover  = RGB(43, 47, 59);
    const COLORREF kBorder     = RGB(45, 49, 61);
    const COLORREF kBorderHover = RGB(96, 104, 126);
    const COLORREF kText       = RGB(235, 237, 242);
    const COLORREF kTextDim    = RGB(150, 155, 168);
    const COLORREF kAccent     = RGB(64, 156, 255);
    const COLORREF kGreen      = RGB(52, 199, 120);
    const COLORREF kRed        = RGB(240, 84, 84);
    const COLORREF kOrange     = RGB(255, 150, 50);

    COLORREF Blend(COLORREF a, COLORREF b, int percentB)
    {
        auto mix = [&](int ca, int cb) { return (ca * (100 - percentB) + cb * percentB) / 100; };
        return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
    }

    void FillRound(HDC dc, const RECT& r, int radius, COLORREF fill, COLORREF border)
    {
        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, border);
        HGDIOBJ ob = SelectObject(dc, brush);
        HGDIOBJ op = SelectObject(dc, pen);
        RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(pen);
        DeleteObject(brush);
    }

    void DrawLabel(HDC dc, HFONT font, COLORREF color, RECT rc, const std::wstring& text, UINT flags = DT_LEFT)
    {
        HGDIOBJ old = SelectObject(dc, font);
        SetTextColor(dc, color);
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rc, flags | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, old);
    }

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

    int TextWidth(HDC dc, HFONT font, const std::wstring& text)
    {
        HGDIOBJ old = SelectObject(dc, font);
        SIZE size{};
        GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
        SelectObject(dc, old);
        return size.cx;
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

    Settings LoadSettings()
    {
        Settings s;
        const std::wstring ini = SettingsPath();
        if (!ini.empty())
        {
            s.autoCheck = GetPrivateProfileIntW(L"Allgemein", L"UpdatePruefung", 1, ini.c_str()) != 0;
            wchar_t dir[MAX_PATH * 2] = {};
            GetPrivateProfileStringW(L"Allgemein", L"DownloadOrdner", L"", dir, MAX_PATH * 2, ini.c_str());
            if (IsUsableDir(dir))
                s.downloadDir = dir;
        }
        s.autostart = IsAutostartEnabled();   // die Registry ist die Wahrheit, nicht die INI
        return s;
    }

    bool SaveSettings(const Settings& s)
    {
        const std::wstring ini = SettingsPath();
        if (ini.empty())
            return false;
        CreateDirectoryW((LocalAppData() + L"\\DuncansSuite").c_str(), nullptr);
        return WritePrivateProfileStringW(L"Allgemein", L"UpdatePruefung", s.autoCheck ? L"1" : L"0", ini.c_str()) &&
               WritePrivateProfileStringW(L"Allgemein", L"DownloadOrdner", s.downloadDir.c_str(), ini.c_str()) &&
               WritePrivateProfileStringW(L"Allgemein", L"Autostart", s.autostart ? L"1" : L"0", ini.c_str());
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

    std::string ToHex(const unsigned char* bytes, size_t n)
    {
        static const char* digits = "0123456789abcdef";
        std::string s;
        for (size_t i = 0; i < n; ++i)
        {
            s += digits[bytes[i] >> 4];
            s += digits[bytes[i] & 15];
        }
        return s;
    }

    // Signatur-Pruefung: <url>.sig (64 Byte, ECDSA P-256 ueber SHA-256) muss zum Schluessel passen.
    struct SigCheck
    {
        const unsigned char* key;
        size_t keyLen;
    };

    bool VerifyDigestSignature(HINTERNET session, const std::wstring& sigUrl, const SigCheck& check,
                               const unsigned char (&digest)[32], std::wstring& error)
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
        bool valid = false;
        if (sig.size() == 64)
        {
            BCRYPT_ALG_HANDLE ecc = nullptr;
            BCRYPT_KEY_HANDLE key = nullptr;
            valid = BCryptOpenAlgorithmProvider(&ecc, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) == 0 &&
                    BCryptImportKeyPair(ecc, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key, const_cast<PUCHAR>(check.key),
                                        static_cast<ULONG>(check.keyLen), 0) == 0 &&
                    BCryptVerifySignature(key, nullptr, const_cast<PUCHAR>(digest), sizeof(digest),
                                          reinterpret_cast<PUCHAR>(&sig[0]), static_cast<ULONG>(sig.size()), 0) == 0;
            if (key)
                BCryptDestroyKey(key);
            if (ecc)
                BCryptCloseAlgorithmProvider(ecc, 0);
        }
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
            else if (!VerifyDigestSignature(session, url + L".sig", sigCheck, digest, error))
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
        return ok;
    }

    // ---- Update-Pruefung (GitHub Releases) -------------------------------------------------------
    // Hinweis: /releases/latest liefert nur Releases ohne Pre-Release-Markierung. Da die Suite-Releases
    // als Pre-Release erscheinen koennen, wird die Liste /releases abgefragt und der hoechste
    // DuncansSuite_Setup_X.Y.Z.exe gewaehlt. Pruefsumme = "digest" des Assets aus der API-Antwort.

    constexpr UINT WM_UPD_CHECKED = WM_APP + 3;   // wParam = 1 neuere Version gefunden, 0 keine/Fehler

    struct Version
    {
        int major = 0, minor = 0, patch = 0;
        bool valid = false;
    };

    std::wstring Widen(const std::string& s)
    {
        return std::wstring(s.begin(), s.end());   // nur ASCII (Versionen, URLs)
    }

    Version ParseVer(const std::wstring& text)
    {
        Version v;
        const wchar_t* s = text.c_str();
        if (*s == L'v' || *s == L'V')
            ++s;
        int parts[3] = { 0, 0, 0 };
        int count = 0;
        while (*s && count < 3)
        {
            wchar_t* end = nullptr;
            const long n = wcstol(s, &end, 10);
            if (end == s || n < 0 || n > 9999)
                return Version{};
            parts[count++] = static_cast<int>(n);
            if (*end != L'.')
                break;
            s = end + 1;
        }
        if (count == 0)
            return Version{};
        v.major = parts[0];
        v.minor = parts[1];
        v.patch = parts[2];
        v.valid = true;
        return v;
    }

    bool IsNewer(const Version& a, const Version& b)
    {
        if (a.major != b.major) return a.major > b.major;
        if (a.minor != b.minor) return a.minor > b.minor;
        return a.patch > b.patch;
    }

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
        if (!DownloadFileVerified(session.h, hwnd, 2, info.url, info.sha, dir, info.fileName, cancel, error, SigCheck{ kSuitePublicKey, sizeof(kSuitePublicKey) }))
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
    void DrawModal(HDC dc, int w, int h);
    void OnMouseMove(int x, int y);
    void OnClick(int x, int y);
    void Launch(int index);
    void OpenToolsFolder();
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

    Tool m_tools[3] = {
        { L"DuncansTidy.exe", L"Duncan's Tidy",
          L"System-Reinigung & Datenmüll-Beseitigung. Räumt Downloads, temporäre Dateien und Browser-Caches auf.",
          CategoryIcon::Broom, RGB(251, 191, 36), {}, {}, {},
          L"https://github.com/Neox365/Duncans-Tidy-ToolSet/releases/latest/download/" },
        { L"DuncansDriverUpdater.exe", L"Duncan's Driver Updater",
          L"Hardware- & Treiber-Manager. Liest alle Geräte aus, erkennt Treiber-Updates und installiert sie.",
          CategoryIcon::Cpu, RGB(64, 156, 255), {}, {}, {} },
        { L"DuncansPdf.exe", L"Duncan's PDF Reader",
          L"PDF-Betrachter. Öffnet, zoomt, dreht, fügt zusammen und speichert PDF-Dateien.",
          CategoryIcon::Document, RGB(240, 84, 84), {}, {}, {},
          nullptr, L"https://github.com/Neox365/Duncans-PDF-Reader/releases/latest/download/DuncansPdf.exe" },
    };

    RECT m_banner{}, m_folderRect{}, m_footer{};
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
    RECT m_gearRect{}, m_chkAuto{}, m_chkStart{}, m_btnPick{}, m_btnReset{}, m_btnSave{}, m_btnCancel{};
    void OpenSettings();
    void CloseSettings(bool save);
    void DrawSettings(HDC dc, int w, int h);
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
    m_hwnd = CreateWindowExW(0, kClassName, L"Duncan's Suite", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             S(1100), S(600), nullptr, nullptr, instance, this);
    if (!m_hwnd)
        return false;
    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    if (animations)
    {
        m_introT = 0.0f;
        m_introStart = GetTickCount64() + 250;
        SetTimer(m_hwnd, 2, 16, nullptr);
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
        for (const auto& c : candidates)
        {
            t.searched.push_back(c);
            if (t.path.empty() && FileExists(c))
                t.path = c;
        }
    }
}

// ---- Layout / Zeichnen ---------------------------------------------------------------------

void SuiteWindow::Layout()
{
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    const int m = S(28);
    m_banner = { m, S(18), rc.right - m, S(18) + S(116) };
    m_footer = { m, rc.bottom - S(60), rc.right - m, rc.bottom - S(14) };
    m_folderRect = { m_footer.right - S(200), m_footer.top + S(4), m_footer.right, m_footer.top + S(4) + S(38) };

    const int top = m_banner.bottom + S(22), bottom = m_footer.top - S(16), gap = S(20);
    const int width = rc.right - 2 * m;
    if (width >= S(960))
    {
        // drei Karten nebeneinander
        const int w = (width - 2 * gap) / 3;
        m_tools[0].rect = { m, top, m + w, bottom };
        m_tools[1].rect = { m + w + gap, top, m + 2 * w + gap, bottom };
        m_tools[2].rect = { m + 2 * (w + gap), top, m + width, bottom };
    }
    else
    {
        // zwei Karten oben, die dritte volle Breite darunter
        const int h = (bottom - top - gap) / 2;
        const int w = (width - gap) / 2;
        m_tools[0].rect = { m, top, m + w, top + h };
        m_tools[1].rect = { m + w + gap, top, m + width, top + h };
        m_tools[2].rect = { m, top + h + gap, m + width, bottom };
    }
}

void SuiteWindow::DrawCard(HDC dc, int index)
{
    const Tool& t = m_tools[index];
    const RECT& rc = t.rect;
    const bool hover = m_hover == (index < 2 ? index : 5) && !m_modal;
    const bool pressed = m_press == (index < 2 ? index : 5) && hover;
    const bool found = !t.path.empty();
    const COLORREF fill = pressed ? Blend(kCardRaised, RGB(0, 0, 0), 18) : (hover ? kCardHover : kCardRaised);

    DrawShadow(dc, rc, S(18), static_cast<int>(m_dpi));
    DrawPanelAA(dc, rc, S(18), fill, hover ? kBorderHover : kBorder);

    const bool compact = (rc.bottom - rc.top) < S(210);
    const int pad = S(24);
    const int tileSize = compact ? S(64) : S(84);
    RECT tile{ rc.left + pad, rc.top + pad, rc.left + pad + tileSize, rc.top + pad + tileSize };
    DrawCategoryTile(dc, tile, t.icon, t.color, fill);

    // Status-Chip + Starten-Pille
    const bool downloading = t.downloading;
    const bool canDownload = !found && (t.downloadBase || t.directUrl);
    const std::wstring status = found ? L"Installiert" : (downloading ? L"Wird geladen" : L"Nicht installiert");
    const COLORREF statusColor = found ? kGreen : (downloading ? kAccent : kOrange);
    const int chipW = TextWidth(dc, m_fontSmall, status) + S(34);
    std::wstring go = found ? L"Starten" : (canDownload ? L"Herunterladen" : L"Starten");
    if (downloading)
        go = t.progress <= 100 ? L"Lade herunter … " + std::to_wstring(t.progress) + L" %" : L"Lade herunter …";
    // Breite fuer den laengsten Text reservieren, damit die Pille beim Laden nicht springt
    const int goW = std::max(TextWidth(dc, m_fontBody, go), TextWidth(dc, m_fontBody, canDownload || downloading ? L"Lade herunter … 100 %" : go)) + S(44);

    int textLeft, textTop, textRight = rc.right - pad;
    if (compact)
    {
        textLeft = tile.right + S(20);
        textTop = rc.top + pad - S(2);
    }
    else
    {
        textLeft = rc.left + pad;
        textTop = tile.bottom + S(18);
    }

    DrawLabel(dc, m_fontCard, kText, { textLeft, textTop, textRight, textTop + S(30) }, t.title);
    const int descTop = textTop + S(32);
    const int btnH = S(38);
    const int bottomRow = rc.bottom - pad - btnH;
    RECT descRc{ textLeft, descTop, textRight, compact ? rc.bottom - pad : bottomRow - S(10) };
    DrawWrapped(dc, m_fontBody, kTextDim, descRc, t.description);

    // untere Zeile: Chip links, Starten rechts
    RECT chip{ rc.left + pad, bottomRow + (btnH - S(26)) / 2, rc.left + pad + chipW, bottomRow + (btnH - S(26)) / 2 + S(26) };
    if (compact)
    {
        chip.left = textLeft;
        chip.right = textLeft + chipW;
        chip.top = rc.bottom - pad - S(26);
        chip.bottom = chip.top + S(26);
    }
    FillRound(dc, chip, S(26), Blend(fill, statusColor, 18), Blend(fill, statusColor, 50));
    {
        HBRUSH dot = CreateSolidBrush(statusColor);
        HPEN none = CreatePen(PS_NULL, 0, 0);
        HGDIOBJ ob = SelectObject(dc, dot);
        HGDIOBJ op = SelectObject(dc, none);
        const int cy = (chip.top + chip.bottom) / 2;
        Ellipse(dc, chip.left + S(11), cy - S(3), chip.left + S(11) + S(6) + 1, cy + S(3) + 1);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(dot);
        DeleteObject(none);
    }
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
    const int iconSize = S(48);
    if (m_icon)
        DrawIconEx(dc, m_banner.left + S(24), m_banner.top + S(16), m_icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
    const int titleX = m_banner.left + S(24) + iconSize + S(16);
    DrawLabel(dc, m_fontTitle, kText, { titleX, m_banner.top + S(10), m_banner.right - S(20), m_banner.top + S(46) }, L"Duncan's Suite");
    DrawLabel(dc, m_fontSmall, kTextDim, { titleX, m_banner.top + S(46), m_banner.right - S(20), m_banner.top + S(66) },
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

    // Update-Badge oben rechts im Banner (links neben dem Zahnrad)
    m_updateRect = RECT{};
    if (m_updAvailable)
    {
        const bool ready = !m_updInstallerPath.empty() && !m_updDownloading;
        const std::wstring label = m_updDownloading
            ? (m_updProgress <= 100 ? L"Lade Update … " + std::to_wstring(m_updProgress) + L" %" : std::wstring(L"Lade Update …"))
            : (ready ? L"Installer starten (v" + m_upd.version + L")" : L"Update verfügbar (v" + m_upd.version + L")");
        const int width = std::max(TextWidth(dc, m_fontBody, label), TextWidth(dc, m_fontBody, L"Lade Update … 100 %")) + S(44);
        const int right = m_gearRect.left - S(12);
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

    // Footer: Version links, Hinweis in der Mitte, Ordner-Button rechts
    DrawLabel(dc, m_fontSmall, kTextDim, { m_footer.left + S(4), m_footer.top + S(13), m_footer.left + S(120), m_footer.bottom },
              std::wstring(L"v") + SUITE_VERSION);
    if (!m_note.empty())
        DrawLabel(dc, m_fontSmall, kText, { m_footer.left + S(120), m_footer.top + S(13), m_folderRect.left - S(16), m_footer.bottom },
                  m_note, DT_CENTER);
    {
        const bool hover = m_hover == 2 && !m_modal;
        const bool pressed = m_press == 2 && hover;
        FillRound(dc, m_folderRect, S(14), pressed ? RGB(24, 26, 32) : (hover ? kCardHover : kCardRaised), hover ? kBorderHover : kBorder);
        DrawLabel(dc, m_fontBody, hover ? kText : kTextDim, { m_folderRect.left, m_folderRect.top + S(9), m_folderRect.right, m_folderRect.bottom },
                  L"Tools-Ordner öffnen", DT_CENTER);
    }

    if (m_settingsOpen)
        DrawSettings(dc, w, h);
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

    const int pad = S(26);
    const int boxW = std::min(S(560), w - S(40));
    const int inner = boxW - 2 * pad;
    const int titleH = S(30), btnH = S(38), rowH = S(50);
    const int folderH = S(24) + S(18) + S(12) + btnH;
    const int boxH = pad + titleH + S(18) + rowH + S(10) + rowH + S(16) + folderH + S(26) + btnH + pad;
    RECT box{ (w - boxW) / 2, (h - boxH) / 2, (w - boxW) / 2 + boxW, (h - boxH) / 2 + boxH };

    DrawShadow(dc, box, S(18), static_cast<int>(m_dpi));
    DrawPanelAA(dc, box, S(18), RGB(27, 29, 36), kBorder);
    DrawLabel(dc, m_fontHeading, kText, { box.left + pad, box.top + pad + S(3), box.right - pad, box.top + pad + titleH }, L"Einstellungen");

    int y = box.top + pad + titleH + S(18);
    m_chkAuto = { box.left + pad, y, box.right - pad, y + rowH };
    DrawCheck(dc, m_chkAuto, m_draft.autoCheck, m_hover == 7, L"Beim Start nach Updates suchen", L"Prüft beim Öffnen automatisch, ob eine neue Suite-Version vorliegt.");
    y += rowH + S(10);
    m_chkStart = { box.left + pad, y, box.right - pad, y + rowH };
    DrawCheck(dc, m_chkStart, m_draft.autostart, m_hover == 8, L"Mit Windows starten", L"Startet die Suite bei der Anmeldung automatisch (nur für dein Konto).");
    y += rowH + S(16);

    DrawLabel(dc, m_fontBody, kText, { box.left + pad, y, box.right - pad, y + S(24) }, L"Download-Ordner für Update-Installer");
    const std::wstring dirText = m_draft.downloadDir.empty() ? L"Standard (%LOCALAPPDATA%\\DuncansSuite\\update)" : m_draft.downloadDir;
    DrawLabel(dc, m_fontSmall, kTextDim, { box.left + pad, y + S(24), box.right - pad, y + S(24) + S(18) }, dirText, DT_LEFT | DT_PATH_ELLIPSIS);
    y += S(24) + S(18) + S(12);
    m_btnPick = { box.left + pad, y, box.left + pad + S(120), y + btnH };
    m_btnReset = { m_btnPick.right + S(10), y, m_btnPick.right + S(10) + S(120), y + btnH };
    DrawButton(dc, m_btnPick, false, m_hover == 9, m_press == 9 && m_hover == 9, L"Ändern …");
    DrawButton(dc, m_btnReset, false, m_hover == 10, m_press == 10 && m_hover == 10, L"Standard");

    const int bw = S(120);
    m_btnSave = { box.right - pad - bw, box.bottom - pad - btnH, box.right - pad, box.bottom - pad };
    m_btnCancel = { m_btnSave.left - S(10) - bw, m_btnSave.top, m_btnSave.left - S(10), m_btnSave.bottom };
    DrawButton(dc, m_btnCancel, false, m_hover == 12, m_press == 12 && m_hover == 12, L"Abbrechen");
    DrawButton(dc, m_btnSave, true, m_hover == 11, m_press == 11 && m_hover == 11, L"Speichern");
    (void)inner;
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
    if (t.path.empty() && (t.downloadBase || t.directUrl))
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
        SetNote(std::wstring(t.title) + L" wird gestartet …");
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
    const std::wstring base = t.downloadBase ? t.downloadBase : L"", url = t.directUrl ? t.directUrl : L"", exe = t.exe, dir = InstallDir(t);
    const HWND hwnd = m_hwnd;
    m_dlThread = std::thread([this, hwnd, index, base, url, exe, dir]()
    {
        std::wstring error;
        const bool ok = url.empty() ? DownloadVerified(hwnd, index, base, exe, dir, m_dlCancel, error)
                                    : DownloadDirect(hwnd, index, url, exe, dir, m_dlCancel, error);
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
    m_checkResult = std::make_shared<UpdateInfo>();
    const HWND hwnd = m_hwnd;
    const std::shared_ptr<UpdateInfo> result = m_checkResult;
    std::thread([hwnd, result]()
    {
        bool newer = false;
        UpdateInfo best;
        if (FetchLatestSuiteInstaller(best))
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
            PostMessageW(hwnd, WM_UPD_CHECKED, newer ? 1 : 0, 0);
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
            PostMessageW(hwnd, WM_DL_DONE, ok ? 1 : 0, static_cast<LPARAM>(2));
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
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);   // Suite beenden, damit das Update die Dateien ersetzen kann
    }
    else if (GetLastError() == ERROR_CANCELLED)
        SetNote(L"Update abgebrochen");
    else
        ShowModal(L"Start fehlgeschlagen", L"Der Installer konnte nicht gestartet werden (Fehlercode " + std::to_wstring(GetLastError()) + L").\n\n" + m_updInstallerPath);
}

void SuiteWindow::OpenToolsFolder()
{
    ShellExecuteW(m_hwnd, L"open", ExeDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
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
        const struct { const RECT* r; int id; } hits[] = { { &m_chkAuto, 7 }, { &m_chkStart, 8 }, { &m_btnPick, 9 },
                                                          { &m_btnReset, 10 }, { &m_btnSave, 11 }, { &m_btnCancel, 12 } };
        for (const auto& hit : hits)
            if (PtInRect(hit.r, pt))
                hover = hit.id;
    }
    else if (PtInRect(&m_gearRect, pt))
        hover = 6;
    else if (m_updAvailable && PtInRect(&m_updateRect, pt))
        hover = 4;
    else if (PtInRect(&m_tools[0].rect, pt))
        hover = 0;
    else if (PtInRect(&m_tools[1].rect, pt))
        hover = 1;
    else if (PtInRect(&m_tools[2].rect, pt))
        hover = 5;
    else if (PtInRect(&m_folderRect, pt))
        hover = 2;
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
    else if (pressed == 7)
        m_draft.autoCheck = !m_draft.autoCheck;
    else if (pressed == 8)
        m_draft.autostart = !m_draft.autostart;
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
    else if (pressed == 2)
        OpenToolsFolder();
    else
        Launch(pressed == 5 ? 2 : pressed);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

LRESULT SuiteWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        m_dpi = GetDpiForWindow(m_hwnd);
        BOOL dark = TRUE;
        DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        CreateFonts();
        FindTools();
        m_settings = LoadSettings();
        if (m_settings.autoCheck)
            StartUpdateCheck();
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
        mmi->ptMinTrackSize.x = S(520);
        mmi->ptMinTrackSize.y = S(560);
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
            m_introT = static_cast<float>(std::clamp(elapsed / 1200.0, 0.0, 1.0));
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
        m_updAvailable = wp != 0;
        if (m_updAvailable && m_checkResult)
            m_upd = *m_checkResult;
        InvalidateRect(m_hwnd, &m_banner, FALSE);
        return 0;
    case WM_DL_PROGRESS:
        if (lp >= 0 && lp < 2)
        {
            m_tools[lp].progress = static_cast<int>(wp);
            InvalidateRect(m_hwnd, &m_tools[lp].rect, FALSE);
        }
        else if (lp == 2)
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
        if (index == 2)
        {
            m_updDownloading = false;
            if (wp)
                RunInstaller();   // Installer starten, danach beendet sich die Suite
            else
                ShowModal(L"Update-Download fehlgeschlagen", m_dlMessage.empty() ? std::wstring(L"Unbekannter Fehler.") : m_dlMessage);
        }
        else if (index >= 0 && index < 2)
        {
            m_tools[index].downloading = false;
            FindTools();   // Button wechselt automatisch von "Herunterladen" auf "Starten"
            if (wp)
                SetNote(std::wstring(m_tools[index].title) + L" wurde installiert");
            else
                ShowModal(L"Download fehlgeschlagen", m_dlMessage.empty() ? std::wstring(L"Unbekannter Fehler.") : m_dlMessage);
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_DESTROY:
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
