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

#include <dwmapi.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <windowsx.h>

#include <algorithm>
#include <string>
#include <vector>

#include "../src/Icons.h"

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "dwmapi.lib")
#endif

#ifndef SUITE_VERSION
#define SUITE_VERSION L"1.0.0"
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

    HWND m_hwnd = nullptr;
    UINT m_dpi = 96;
    HFONT m_fontTitle = nullptr, m_fontCard = nullptr, m_fontHeading = nullptr, m_fontBody = nullptr, m_fontSmall = nullptr;
    HICON m_icon = nullptr;

    Tool m_tools[2] = {
        { L"DuncansTidy.exe", L"Duncan's Tidy",
          L"System-Reinigung & Datenmüll-Beseitigung. Räumt Downloads, temporäre Dateien und Browser-Caches auf.",
          CategoryIcon::Broom, RGB(251, 191, 36), {}, {}, {} },
        { L"DuncansDriverUpdater.exe", L"Duncan's Driver Updater",
          L"Hardware- & Treiber-Manager. Liest alle Geräte aus, erkennt Treiber-Updates und installiert sie.",
          CategoryIcon::Cpu, RGB(64, 156, 255), {}, {}, {} },
    };

    RECT m_banner{}, m_folderRect{}, m_footer{};
    int  m_hover = -1;       // 0/1 Karten, 2 Ordner-Button, 3 Modal-OK
    int  m_press = -1;
    bool m_tracking = false;
    std::wstring m_note;

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
    const std::wstring status = found ? L"Installiert" : L"Nicht gefunden";
    const COLORREF statusColor = found ? kGreen : kOrange;
    const int chipW = TextWidth(dc, m_fontSmall, status) + S(34);
    const std::wstring go = L"Starten";
    const int goW = TextWidth(dc, m_fontBody, go) + S(44);

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
    COLORREF goFill = found ? (pressed ? Blend(kAccent, RGB(0, 0, 0), 25) : (hover ? Blend(kAccent, RGB(255, 255, 255), 14) : kAccent))
                            : Blend(fill, RGB(255, 255, 255), hover ? 14 : 6);
    FillRound(dc, goRc, S(12), goFill, found ? goFill : kBorder);
    DrawLabel(dc, m_fontBody, found ? RGB(8, 20, 36) : kTextDim, { goRc.left, goRc.top + S(8), goRc.right, goRc.bottom }, go, DT_CENTER);
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
    case WM_DESTROY:
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
