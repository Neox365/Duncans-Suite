// main.cpp - "Duncan's Suite": zentraler Launcher fuer Duncan's Tidy und Duncan's Driver Updater.
//
// Zwei grosse Karten starten die jeweilige Anwendung (zuerst aus dem eigenen Ordner, fuer Tidy zusaetzlich
// aus %LOCALAPPDATA%\DuncansTidy). Fehlt eine EXE, erscheint ein eigener Dark-Dialog statt einer MessageBox.
//
// Linker-Bibliotheken: user32, gdi32, gdiplus, shell32, ole32, dwmapi

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <bcrypt.h>
#include <dwmapi.h>
#include <shellapi.h>
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
#define SUITE_VERSION L"1.0.2"
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

        // 2) Datei laden und dabei hashen
        InetHandle h;
        DWORD total = 0;
        if (!OpenHttps(session.h, base + exeName, h, total, error))
            return false;

        CreateDirectoryW(dir.c_str(), nullptr);
        const std::wstring tmp = dir + L"\\" + exeName + L".download";
        const std::wstring dest = dir + L"\\" + exeName;
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
            if (BCryptFinishHash(hash, digest, sizeof(digest), 0) != 0 || ToHex(digest, sizeof(digest)) != expected)
            {
                error = L"Die Prüfsumme der heruntergeladenen Datei stimmt nicht. Die Datei wurde verworfen.";
                ok = false;
            }
            else if (received < 100 * 1024 || (total > 0 && received != total))
            {
                error = L"Der Download ist unvollständig.";
                ok = false;
            }
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
    std::wstring InstallDir(const Tool& t) const;

    HWND m_hwnd = nullptr;
    UINT m_dpi = 96;
    HFONT m_fontTitle = nullptr, m_fontCard = nullptr, m_fontHeading = nullptr, m_fontBody = nullptr, m_fontSmall = nullptr;
    HICON m_icon = nullptr;

    Tool m_tools[2] = {
        { L"DuncansTidy.exe", L"Duncan's Tidy",
          L"System-Reinigung & Datenmüll-Beseitigung. Räumt Downloads, temporäre Dateien und Browser-Caches auf.",
          CategoryIcon::Broom, RGB(251, 191, 36), {}, {}, {},
          L"https://github.com/Neox365/Duncans-Tidy-ToolSet/releases/latest/download/" },
        { L"DuncansDriverUpdater.exe", L"Duncan's Driver Updater",
          L"Hardware- & Treiber-Manager. Liest alle Geräte aus, erkennt Treiber-Updates und installiert sie.",
          CategoryIcon::Cpu, RGB(64, 156, 255), {}, {}, {} },
    };

    RECT m_banner{}, m_folderRect{}, m_footer{};
    int  m_hover = -1;       // 0/1 Karten, 2 Ordner-Button, 3 Modal-OK
    int  m_press = -1;
    bool m_tracking = false;
    std::wstring m_note;

    std::thread m_dlThread;
    std::atomic<bool> m_dlCancel{ false };
    std::wstring m_dlMessage;   // Fehlertext des letzten Downloads (vom Worker gesetzt, vor WM_DL_DONE)

    bool m_modal = false;
    std::wstring m_modalTitle, m_modalText;
    RECT m_modalOk{};
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
                             S(860), S(600), nullptr, nullptr, instance, this);
    if (!m_hwnd)
        return false;
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
    m_icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, S(48), S(48), LR_DEFAULTCOLOR));
}

void SuiteWindow::DestroyFonts()
{
    for (HFONT* f : { &m_fontTitle, &m_fontCard, &m_fontHeading, &m_fontBody, &m_fontSmall })
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
        // Tidy wird standardmaessig nach %LOCALAPPDATA%\DuncansTidy installiert
        if (!local.empty() && std::wstring(t.exe) == L"DuncansTidy.exe")
            candidates.push_back(local + L"\\DuncansTidy\\" + t.exe);
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
    if (width >= S(640))
    {
        const int w = (width - gap) / 2;
        m_tools[0].rect = { m, top, m + w, bottom };
        m_tools[1].rect = { m + w + gap, top, m + width, bottom };
    }
    else
    {
        const int h = (bottom - top - gap) / 2;
        m_tools[0].rect = { m, top, m + width, top + h };
        m_tools[1].rect = { m, top + h + gap, m + width, bottom };
    }
}

void SuiteWindow::DrawCard(HDC dc, int index)
{
    const Tool& t = m_tools[index];
    const RECT& rc = t.rect;
    const bool hover = m_hover == index && !m_modal;
    const bool pressed = m_press == index && hover;
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
    const bool canDownload = !found && t.downloadBase != nullptr;
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
    DrawBannerPanel(dc, m_banner, S(18), static_cast<int>(m_dpi));
    DrawScanScene(dc, m_banner, 0.62f, 0, static_cast<int>(m_dpi), false);
    const int iconSize = S(48);
    if (m_icon)
        DrawIconEx(dc, m_banner.left + S(24), m_banner.top + S(16), m_icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
    const int titleX = m_banner.left + S(24) + iconSize + S(16);
    DrawLabel(dc, m_fontTitle, kText, { titleX, m_banner.top + S(10), m_banner.right - S(20), m_banner.top + S(46) }, L"Duncan's Suite");
    DrawLabel(dc, m_fontSmall, kTextDim, { titleX, m_banner.top + S(46), m_banner.right - S(20), m_banner.top + S(66) },
              L"System- & Treiber-Optimierung");

    DrawCard(dc, 0);
    DrawCard(dc, 1);

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

    if (m_modal)
        DrawModal(dc, w, h);
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

void SuiteWindow::Launch(int index)
{
    FindTools();   // der Nutzer kann inzwischen installiert haben
    const Tool& t = m_tools[index];
    if (t.downloading)
    {
        SetNote(std::wstring(t.title) + L" wird gerade heruntergeladen …");
        return;
    }
    if (t.path.empty() && t.downloadBase)
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
    if (m_dlThread.joinable())
        m_dlThread.join();   // vorheriger, beendeter Download
    if (LocalAppData().empty())
    {
        ShowModal(L"Download nicht möglich", L"Der Ordner %LOCALAPPDATA% ist nicht verfügbar.");
        return;
    }
    t.downloading = true;
    t.progress = 0;
    m_dlCancel = false;
    const std::wstring base = t.downloadBase, exe = t.exe, dir = InstallDir(t);
    const HWND hwnd = m_hwnd;
    m_dlThread = std::thread([this, hwnd, index, base, exe, dir]()
    {
        std::wstring error;
        const bool ok = DownloadVerified(hwnd, index, base, exe, dir, m_dlCancel, error);
        m_dlMessage = error;   // vor PostMessage gesetzt, danach vom UI-Thread gelesen
        if (IsWindow(hwnd))
            PostMessageW(hwnd, WM_DL_DONE, ok ? 1 : 0, static_cast<LPARAM>(index));
    });
    SetNote(std::wstring(t.title) + L" wird heruntergeladen …");
    InvalidateRect(m_hwnd, nullptr, FALSE);
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
    else if (PtInRect(&m_tools[0].rect, pt))
        hover = 0;
    else if (PtInRect(&m_tools[1].rect, pt))
        hover = 1;
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
    else if (pressed == 2)
        OpenToolsFolder();
    else
        Launch(pressed);
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
        Draw(mem, rc.right, rc.bottom);
        BitBlt(screen, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
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
        else if (!m_modal && wp == '1')
            Launch(0);
        else if (!m_modal && wp == '2')
            Launch(1);
        return 0;
    case WM_TIMER:
        KillTimer(m_hwnd, 1);
        m_note.clear();
        InvalidateRect(m_hwnd, &m_footer, FALSE);
        return 0;
    case WM_DL_PROGRESS:
        if (lp >= 0 && lp < 2)
        {
            m_tools[lp].progress = static_cast<int>(wp);
            InvalidateRect(m_hwnd, &m_tools[lp].rect, FALSE);
        }
        return 0;
    case WM_DL_DONE:
    {
        if (m_dlThread.joinable())
            m_dlThread.join();
        const int index = static_cast<int>(lp);
        if (index >= 0 && index < 2)
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
