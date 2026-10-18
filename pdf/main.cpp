// Duncan's PDF - nativer PDF-Betrachter (Win32 + GDI/GDI+, PDFium zur Laufzeit geladen).
// Aufbau: Hauptfenster (Werkzeugleiste + Seitenleisten-Kopf) mit zwei Kindfenstern:
//   Seitenleiste (Thumbnails / Werkzeuge) und Canvas (aktuelle Seite, scrollbar, zoombar).
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>
#include <vector>

using std::max;
using std::min;
#include <objbase.h>
#include <propidl.h>
#include <gdiplus.h>

#include "PdfDocument.h"

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#endif

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace {

// ---- Farben (wie Duncan's Suite) ----
const COLORREF kBackground = RGB(18, 19, 22);
const COLORREF kPanel      = RGB(22, 24, 29);
const COLORREF kCanvas     = RGB(28, 30, 36);
const COLORREF kCardRaised = RGB(35, 38, 47);
const COLORREF kCardHover  = RGB(43, 47, 59);
const COLORREF kBorder     = RGB(45, 49, 61);
const COLORREF kText       = RGB(235, 237, 242);
const COLORREF kTextDim    = RGB(150, 155, 168);
const COLORREF kAccent     = RGB(64, 156, 255);
const COLORREF kRed        = RGB(240, 84, 84);
const COLORREF kGreen      = RGB(52, 199, 120);

const wchar_t* kAppTitle = L"Duncan's PDF";

enum Cmd {
    C_NONE = 0, C_SIDE, C_OPEN, C_SAVE, C_MERGE, C_ZOOMOUT, C_ZOOMIN, C_ACTUAL, C_FITW,
    C_ROTL, C_ROTR, C_PREV, C_NEXT
};

struct Item {
    int cmd = C_NONE;          // C_NONE = Trenner
    std::wstring label;
    int minWidth = 0;          // Basisbreite (unskaliert)
    bool needsDoc = true;
    bool right = false;        // rechts ausgerichtet (Seitenwechsel)
    bool isPageLabel = false;
    RECT rc{};
};

struct App {
    HINSTANCE inst = nullptr;
    HWND main = nullptr, side = nullptr, canvas = nullptr;
    int dpi = 96;
    HFONT font = nullptr, fontBold = nullptr, fontSmall = nullptr;

    PdfDocument doc;
    int page = 0;
    double zoom = 1.0;
    bool fitWidth = true;

    // Toolbar
    std::vector<Item> items;
    int hot = -1, down = -1;
    bool trackingMain = false;

    // Seitenleiste
    bool sideVisible = true;
    int sideTab = 0;                       // 0 = Seiten, 1 = Werkzeuge
    RECT tabRc[2]{};
    std::vector<HBITMAP> thumbs;
    std::vector<double> aspect;            // Hoehe/Breite je Seite
    std::vector<int> thumbTop;
    int thumbW = 0, sideContentH = 0, sideScroll = 0, toolHot = -1;
    bool trackingSide = false;

    // Canvas
    HBITMAP pageBmp = nullptr;
    int bmpPage = -1, bmpW = 0, bmpH = 0, bmpRot = -1;
    int pageW = 0, pageH = 0, scrollX = 0, scrollY = 0;
    std::wstring toast;
    bool toastErr = false;
} g;

inline int Max(long a, long b) { return static_cast<int>(a > b ? a : b); }
inline int Min(long a, long b) { return static_cast<int>(a < b ? a : b); }
int S(int v) { return MulDiv(v, g.dpi, 96); }
int ToolbarH() { return S(56); }
int TabHeaderH() { return S(38); }
int SideW() { return g.sideVisible ? S(210) : 0; }
const int kMargin = 24;

ULONG_PTR g_gdipToken = 0;

// ---------- Zeichenhilfen ----------
Gdiplus::Color Col(COLORREF c, BYTE a = 255) { return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c)); }

void FillRound(HDC dc, RECT r, int rad, COLORREF fill, COLORREF border = CLR_INVALID) {
    using namespace Gdiplus;
    Graphics gr(dc);
    gr.SetSmoothingMode(SmoothingModeAntiAlias);
    REAL x = REAL(r.left) + 0.5f, y = REAL(r.top) + 0.5f, w = REAL(r.right - r.left) - 1, h = REAL(r.bottom - r.top) - 1;
    REAL d = REAL(rad) * 2;
    GraphicsPath p;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
    SolidBrush b(Col(fill));
    gr.FillPath(&b, &p);
    if (border != CLR_INVALID) { Pen pen(Col(border), 1.0f); gr.DrawPath(&pen, &p); }
}

void DrawTextC(HDC dc, HFONT f, const std::wstring& t, RECT r, COLORREF c, UINT flags = DT_CENTER | DT_VCENTER | DT_SINGLELINE) {
    HGDIOBJ old = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, t.c_str(), -1, &r, flags | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, old);
}

int TextW(const std::wstring& t, HFONT f) {
    HDC dc = GetDC(g.main);
    HGDIOBJ old = SelectObject(dc, f);
    SIZE sz{};
    GetTextExtentPoint32W(dc, t.c_str(), static_cast<int>(t.size()), &sz);
    SelectObject(dc, old);
    ReleaseDC(g.main, dc);
    return sz.cx;
}

void MakeFonts() {
    for (HFONT* f : { &g.font, &g.fontBold, &g.fontSmall }) if (*f) DeleteObject(*f);
    g.font      = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g.fontBold  = CreateFontW(-S(14), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g.fontSmall = CreateFontW(-S(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
}

std::wstring FileName(const std::wstring& p) {
    size_t i = p.find_last_of(L"\\/");
    return i == std::wstring::npos ? p : p.substr(i + 1);
}

// ---------- Vorwaertsdeklarationen ----------
void RunCommand(int cmd);
void LayoutChildren();
void LayoutToolbar();
void LayoutSidebar();
void UpdateCanvas(bool resetBitmap);
void GoToPage(int p, bool bottom = false);
void ShowToast(const std::wstring& text, bool err = false);
void HandleKey(UINT vk);

void InvalidateToolbar() {
    RECT r{ 0, 0, 100000, ToolbarH() };
    InvalidateRect(g.main, &r, FALSE);
}

bool ItemEnabled(const Item& it) { return !it.needsDoc || g.doc.IsOpen(); }

// ---------- Werkzeugleiste ----------
void BuildItems() {
    auto add = [](int cmd, const wchar_t* label, int minW, bool needsDoc, bool right = false, bool pageLabel = false) {
        Item it; it.cmd = cmd; it.label = label; it.minWidth = minW; it.needsDoc = needsDoc; it.right = right; it.isPageLabel = pageLabel;
        g.items.push_back(it);
    };
    add(C_SIDE, L"☰", 40, false);
    add(C_NONE, L"", 0, false);
    add(C_OPEN, L"Öffnen", 0, false);
    add(C_SAVE, L"Speichern", 0, true);
    add(C_MERGE, L"Merge", 0, true);
    add(C_NONE, L"", 0, false);
    add(C_ZOOMOUT, L"−", 40, true);
    add(C_ACTUAL, L"100 %", 64, true);
    add(C_ZOOMIN, L"+", 40, true);
    add(C_FITW, L"Breite", 0, true);
    add(C_NONE, L"", 0, false);
    add(C_ROTL, L"↺", 40, true);
    add(C_ROTR, L"↻", 40, true);
    add(C_PREV, L"‹", 40, true, true);
    add(C_NONE, L"1 / 1", 84, true, true, true);   // Seitenanzeige (nicht klickbar)
    add(C_NEXT, L"›", 40, true, true);
}

void UpdateLabels() {
    for (Item& it : g.items) {
        if (it.cmd == C_ACTUAL) { wchar_t b[32]; swprintf(b, 32, L"%d %%", static_cast<int>(std::lround(g.zoom * 100))); it.label = b; }
        if (it.isPageLabel) {
            wchar_t b[48];
            if (g.doc.IsOpen()) swprintf(b, 48, L"%d / %d", g.page + 1, g.doc.PageCount()); else wcscpy(b, L"– / –");
            it.label = b;
        }
    }
}

void LayoutToolbar() {
    RECT cr; GetClientRect(g.main, &cr);
    const int h = ToolbarH(), bh = S(36), top = (h - bh) / 2, gap = S(6), pad = S(12);
    int x = pad;
    auto width = [](const Item& it) {
        if (it.cmd == C_NONE && it.label.empty()) return S(9);       // Trenner
        int w = it.label.empty() ? 0 : TextW(it.label, g.font) + S(28);
        return Max(w, S(it.minWidth));
    };
    int rx = cr.right - pad;
    for (Item& it : g.items) {
        if (it.right) continue;
        int w = width(it);
        it.rc = RECT{ x, top, x + w, top + bh };
        x += w + (it.cmd == C_NONE ? 0 : gap);
    }
    for (auto i = g.items.rbegin(); i != g.items.rend(); ++i) {
        if (!i->right) continue;
        int w = width(*i);
        i->rc = RECT{ rx - w, top, rx, top + bh };
        rx -= w + gap;
    }
}

void DrawToolbar(HDC dc, int width) {
    HBRUSH bg = CreateSolidBrush(kBackground);
    RECT r{ 0, 0, width, ToolbarH() };
    FillRect(dc, &r, bg);
    DeleteObject(bg);
    HPEN pen = CreatePen(PS_SOLID, 1, kBorder);
    HGDIOBJ op = SelectObject(dc, pen);
    MoveToEx(dc, 0, ToolbarH() - 1, nullptr); LineTo(dc, width, ToolbarH() - 1);
    SelectObject(dc, op); DeleteObject(pen);

    for (size_t i = 0; i < g.items.size(); ++i) {
        const Item& it = g.items[i];
        if (it.cmd == C_NONE && it.label.empty()) {                      // Trenner
            HPEN sp = CreatePen(PS_SOLID, 1, kBorder);
            HGDIOBJ o = SelectObject(dc, sp);
            int cx = (it.rc.left + it.rc.right) / 2;
            MoveToEx(dc, cx, it.rc.top + S(6), nullptr); LineTo(dc, cx, it.rc.bottom - S(6));
            SelectObject(dc, o); DeleteObject(sp);
            continue;
        }
        if (it.isPageLabel) { DrawTextC(dc, g.font, it.label, it.rc, g.doc.IsOpen() ? kText : kTextDim); continue; }
        const bool enabled = ItemEnabled(it);
        const bool hot = enabled && static_cast<int>(i) == g.hot;
        const bool pressed = hot && static_cast<int>(i) == g.down;
        const bool active = it.cmd == C_SIDE && g.sideVisible;
        COLORREF fill = pressed ? RGB(28, 31, 39) : hot ? kCardHover : (it.cmd == C_OPEN ? kAccent : kCardRaised);
        COLORREF txt = !enabled ? RGB(95, 100, 112) : (it.cmd == C_OPEN && !hot) ? RGB(8, 20, 36) : kText;
        if (it.cmd == C_OPEN && hot) txt = kText;
        FillRound(dc, it.rc, S(10), fill, active ? kAccent : (hot ? RGB(96, 104, 126) : kBorder));
        DrawTextC(dc, it.cmd == C_OPEN ? g.fontBold : g.font, it.label, it.rc, txt);
    }
}

int HitToolbar(POINT p) {
    for (size_t i = 0; i < g.items.size(); ++i) {
        const Item& it = g.items[i];
        if ((it.cmd != C_NONE) && PtInRect(&it.rc, p)) return static_cast<int>(i);
    }
    return -1;
}

// ---------- Seitenleiste: Layout & Zeichnen ----------
const struct { int cmd; const wchar_t* label; } kTools[] = {
    { C_ROTL, L"Seite nach links drehen" }, { C_ROTR, L"Seite nach rechts drehen" },
    { C_MERGE, L"PDF anhängen (Merge)" }, { C_SAVE, L"Speichern unter …" },
    { C_FITW, L"Auf Seitenbreite einpassen" }, { C_ACTUAL, L"Originalgröße (100 %)" },
};
int ToolRowH() { return S(42); }

void ClearThumbs() {
    for (HBITMAP b : g.thumbs) if (b) DeleteObject(b);
    g.thumbs.clear();
}

void SideScrollbar() {
    RECT cr; GetClientRect(g.side, &cr);
    SCROLLINFO si{ sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS };
    si.nMin = 0; si.nMax = g.sideTab == 0 ? Max(0, g.sideContentH - 1) : 0; si.nPage = cr.bottom; si.nPos = g.sideScroll;
    SetScrollInfo(g.side, SB_VERT, &si, TRUE);
}

void LayoutSidebar() {
    if (!g.side) return;
    RECT wr; GetWindowRect(g.side, &wr);
    const int pad = S(14);
    g.thumbW = Max(S(60), (wr.right - wr.left) - GetSystemMetrics(SM_CXVSCROLL) - 2 * pad);
    const int n = g.doc.PageCount();
    g.aspect.resize(n, 1.4142);
    g.thumbs.resize(n, nullptr);
    g.thumbTop.assign(n + 1, 0);
    int y = pad;
    for (int i = 0; i < n; ++i) {
        double w, h;
        if (g.doc.PageSize(i, w, h)) g.aspect[i] = h / w;
        g.thumbTop[i] = y;
        y += static_cast<int>(g.thumbW * g.aspect[i]) + S(30) + pad / 2;
    }
    g.thumbTop[n] = y;
    g.sideContentH = y;
    RECT cr; GetClientRect(g.side, &cr);
    g.sideScroll = Max(0, Min(g.sideScroll, g.sideContentH - cr.bottom));
    SideScrollbar();
    InvalidateRect(g.side, nullptr, FALSE);
}

void EnsureThumbVisible() {
    if (g.sideTab != 0 || g.page >= static_cast<int>(g.thumbTop.size()) - 1) return;
    RECT cr; GetClientRect(g.side, &cr);
    const int top = g.thumbTop[g.page], bot = g.thumbTop[g.page + 1];
    if (top < g.sideScroll) g.sideScroll = Max(0, top - S(8));
    else if (bot > g.sideScroll + cr.bottom) g.sideScroll = Min(bot - cr.bottom + S(8), Max(0, g.sideContentH - cr.bottom));
    SideScrollbar();
    InvalidateRect(g.side, nullptr, FALSE);
}

void PaintSide(HDC dc, RECT cr) {
    HBRUSH bg = CreateSolidBrush(kPanel);
    FillRect(dc, &cr, bg);
    DeleteObject(bg);
    if (g.sideTab == 1) {
        for (int i = 0; i < static_cast<int>(sizeof(kTools) / sizeof(kTools[0])); ++i) {
            RECT r{ S(12), S(12) + i * ToolRowH(), cr.right - S(12), S(12) + i * ToolRowH() + ToolRowH() - S(6) };
            bool enabled = kTools[i].cmd != C_NONE && g.doc.IsOpen();
            FillRound(dc, r, S(10), i == g.toolHot && enabled ? kCardHover : kCardRaised, i == g.toolHot && enabled ? RGB(96, 104, 126) : kBorder);
            RECT t = r; t.left += S(14);
            DrawTextC(dc, g.font, kTools[i].label, t, enabled ? kText : RGB(95, 100, 112), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        return;
    }
    if (!g.doc.IsOpen()) {
        RECT t = cr; t.top = S(20); t.bottom = S(80);
        DrawTextC(dc, g.fontSmall, L"Keine PDF geöffnet", t, kTextDim);
        return;
    }
    const int n = g.doc.PageCount(), pad = S(14);
    for (int i = 0; i < n; ++i) {
        const int top = g.thumbTop[i] - g.sideScroll;
        const int h = static_cast<int>(g.thumbW * g.aspect[i]);
        if (top + h + S(30) < 0) continue;
        if (top > cr.bottom) break;
        if (!g.thumbs[i]) g.thumbs[i] = g.doc.RenderPage(i, g.thumbW, Max(1, h));
        RECT frame{ pad - S(4), top - S(4), pad + g.thumbW + S(4), top + h + S(4) };
        if (i == g.page) FillRound(dc, frame, S(6), kCardRaised, kAccent);
        if (g.thumbs[i]) {
            HDC mem = CreateCompatibleDC(dc);
            HGDIOBJ old = SelectObject(mem, g.thumbs[i]);
            BitBlt(dc, pad, top, g.thumbW, h, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old); DeleteDC(mem);
        }
        wchar_t b[16]; swprintf(b, 16, L"%d", i + 1);
        RECT lr{ pad, top + h + S(4), pad + g.thumbW, top + h + S(26) };
        DrawTextC(dc, g.fontSmall, b, lr, i == g.page ? kAccent : kTextDim);
    }
}

// ---------- Canvas ----------
void ComputePageSize() {
    g.pageW = g.pageH = 0;
    double w, h;
    if (!g.doc.IsOpen() || !g.doc.PageSize(g.page, w, h)) return;
    const double base = g.dpi / 72.0;
    if (g.fitWidth) {
        RECT wr; GetWindowRect(g.canvas, &wr);
        double avail = (wr.right - wr.left) - GetSystemMetrics(SM_CXVSCROLL) - 2 * S(kMargin);
        g.zoom = std::clamp(avail / (w * base), 0.1, 8.0);
    }
    g.zoom = std::clamp(g.zoom, 0.1, 8.0);
    double pw = w * base * g.zoom, ph = h * base * g.zoom;
    const double maxPixels = 64e6;                          // Speichergrenze fuer die gerenderte Seite
    if (pw * ph > maxPixels) { double k = std::sqrt(maxPixels / (pw * ph)); pw *= k; ph *= k; g.zoom *= k; }
    g.pageW = Max(1, static_cast<int>(pw));
    g.pageH = Max(1, static_cast<int>(ph));
}

void CanvasScrollbars() {
    RECT cr; GetClientRect(g.canvas, &cr);
    for (int pass = 0; pass < 2; ++pass) {
        GetClientRect(g.canvas, &cr);
        SCROLLINFO v{ sizeof(v), SIF_RANGE | SIF_PAGE | SIF_POS };
        v.nMax = Max(0, g.pageH + 2 * S(kMargin) - 1); v.nPage = cr.bottom; v.nPos = g.scrollY;
        SetScrollInfo(g.canvas, SB_VERT, &v, TRUE);
        SCROLLINFO hz{ sizeof(hz), SIF_RANGE | SIF_PAGE | SIF_POS };
        hz.nMax = Max(0, g.pageW + 2 * S(kMargin) - 1); hz.nPage = cr.right; hz.nPos = g.scrollX;
        SetScrollInfo(g.canvas, SB_HORZ, &hz, TRUE);
    }
    GetClientRect(g.canvas, &cr);
    g.scrollY = std::clamp(g.scrollY, 0, Max(0, g.pageH + 2 * S(kMargin) - cr.bottom));
    g.scrollX = std::clamp(g.scrollX, 0, Max(0, g.pageW + 2 * S(kMargin) - cr.right));
}

void UpdateCanvas(bool resetBitmap) {
    if (resetBitmap && g.pageBmp) { DeleteObject(g.pageBmp); g.pageBmp = nullptr; g.bmpPage = -1; }
    ComputePageSize();
    CanvasScrollbars();
    UpdateLabels();
    LayoutToolbar();
    InvalidateToolbar();
    InvalidateRect(g.canvas, nullptr, FALSE);
}

void GoToPage(int p, bool bottom) {
    const int n = g.doc.PageCount();
    if (n == 0) return;
    p = std::clamp(p, 0, n - 1);
    g.page = p;
    g.scrollX = 0;
    g.scrollY = bottom ? 1 << 28 : 0;
    UpdateCanvas(true);
    EnsureThumbVisible();
    InvalidateRect(g.side, nullptr, FALSE);
}

void SetZoom(double z, bool fit) {
    if (!g.doc.IsOpen()) return;
    RECT cr; GetClientRect(g.canvas, &cr);
    const int oldW = Max(1, g.pageW), oldH = Max(1, g.pageH);
    g.fitWidth = fit;
    g.zoom = std::clamp(z, 0.1, 8.0);
    ComputePageSize();
    // Bildschirmmitte beibehalten
    g.scrollX = static_cast<int>((g.scrollX + cr.right / 2.0) * g.pageW / oldW - cr.right / 2.0);
    g.scrollY = static_cast<int>((g.scrollY + cr.bottom / 2.0) * g.pageH / oldH - cr.bottom / 2.0);
    UpdateCanvas(true);
}

void EnsureBitmap() {
    if (!g.doc.IsOpen() || g.pageW <= 0) return;
    if (g.pageBmp && g.bmpPage == g.page && g.bmpW == g.pageW && g.bmpH == g.pageH && g.bmpRot == g.doc.Rotation(g.page)) return;
    if (g.pageBmp) DeleteObject(g.pageBmp);
    g.pageBmp = g.doc.RenderPage(g.page, g.pageW, g.pageH);
    g.bmpPage = g.page; g.bmpW = g.pageW; g.bmpH = g.pageH; g.bmpRot = g.doc.Rotation(g.page);
}

void ShowToast(const std::wstring& text, bool err) {
    g.toast = text; g.toastErr = err;
    SetTimer(g.canvas, 1, err ? 5000 : 3000, nullptr);
    InvalidateRect(g.canvas, nullptr, FALSE);
}

void PaintCanvas(HDC dc, RECT cr) {
    HBRUSH bg = CreateSolidBrush(kCanvas);
    FillRect(dc, &cr, bg);
    DeleteObject(bg);

    if (!g.doc.IsOpen()) {
        RECT t = cr; t.bottom = cr.bottom / 2 + S(10);
        DrawTextC(dc, g.fontBold, L"Duncan's PDF", t, kText, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
        t = cr; t.top = cr.bottom / 2 + S(16);
        DrawTextC(dc, g.font, L"Datei öffnen (Strg+O) oder eine PDF hierher ziehen", t, kTextDim, DT_CENTER | DT_TOP | DT_SINGLELINE);
    } else {
        EnsureBitmap();
        const int m = S(kMargin);
        int x = (g.pageW + 2 * m <= cr.right) ? (cr.right - g.pageW) / 2 : m - g.scrollX;
        int y = (g.pageH + 2 * m <= cr.bottom) ? (cr.bottom - g.pageH) / 2 : m - g.scrollY;
        RECT shadow{ x - 1, y - 1, x + g.pageW + 1, y + g.pageH + 1 };
        HBRUSH sb = CreateSolidBrush(kBorder);
        FrameRect(dc, &shadow, sb);
        DeleteObject(sb);
        if (g.pageBmp) {
            HDC mem = CreateCompatibleDC(dc);
            HGDIOBJ old = SelectObject(mem, g.pageBmp);
            BitBlt(dc, x, y, g.pageW, g.pageH, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old); DeleteDC(mem);
        } else {
            RECT t{ x, y, x + g.pageW, y + g.pageH };
            HBRUSH w = CreateSolidBrush(RGB(255, 255, 255));
            FillRect(dc, &t, w); DeleteObject(w);
            DrawTextC(dc, g.font, L"Seite konnte nicht gerendert werden", t, RGB(90, 90, 90));
        }
    }

    if (!g.toast.empty()) {
        const int w = Min(cr.right - S(40), TextW(g.toast, g.font) + S(48)), h = S(40);
        RECT r{ (cr.right - w) / 2, cr.bottom - h - S(24), (cr.right + w) / 2, cr.bottom - S(24) };
        FillRound(dc, r, S(12), RGB(27, 29, 36), g.toastErr ? kRed : kGreen);
        DrawTextC(dc, g.font, g.toast, r, kText);
    }
}

// ---------- Dateien ----------
bool PickFile(HWND owner, bool save, std::wstring& path, const wchar_t* title, const std::wstring& suggestion = L"") {
    wchar_t buf[MAX_PATH * 4]{};
    wcsncpy(buf, suggestion.c_str(), MAX_PATH - 1);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"PDF-Dokumente (*.pdf)\0*.pdf\0Alle Dateien (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH * 4;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = L"pdf";
    ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST));
    if (!(save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn))) return false;
    path = buf;
    return true;
}

void UpdateTitle() {
    std::wstring t = g.doc.IsOpen() ? FileName(g.doc.Path()) + (g.doc.Modified() ? L" *" : L"") + L" – " + kAppTitle : kAppTitle;
    SetWindowTextW(g.main, t.c_str());
}

void OpenPath(const std::wstring& path) {
    if (!g.doc.Open(path)) { ShowToast(PdfDocument::LastError(), true); return; }
    g.page = 0; g.zoom = 1.0; g.fitWidth = true; g.scrollX = g.scrollY = g.sideScroll = 0;
    ClearThumbs();
    LayoutSidebar();
    UpdateCanvas(true);
    UpdateTitle();
    g.toast.clear();
}

void RunCommand(int cmd) {
    switch (cmd) {
    case C_SIDE: g.sideVisible = !g.sideVisible; LayoutChildren(); InvalidateRect(g.main, nullptr, FALSE); break;
    case C_OPEN: {
        std::wstring p;
        if (PickFile(g.main, false, p, L"PDF öffnen")) OpenPath(p);
        break;
    }
    case C_SAVE: {
        if (!g.doc.IsOpen()) break;
        std::wstring p;
        if (!PickFile(g.main, true, p, L"PDF speichern unter", FileName(g.doc.Path()))) break;
        if (g.doc.SaveCopy(p)) { UpdateTitle(); ShowToast(L"Gespeichert: " + FileName(p)); }
        else ShowToast(PdfDocument::LastError(), true);
        break;
    }
    case C_MERGE: {
        if (!g.doc.IsOpen()) break;
        std::wstring p;
        if (!PickFile(g.main, false, p, L"PDF zum Anhängen auswählen")) break;
        const int before = g.doc.PageCount();
        if (!g.doc.Append(p)) { ShowToast(PdfDocument::LastError(), true); break; }
        LayoutSidebar();
        UpdateCanvas(false);
        UpdateTitle();
        ShowToast(std::to_wstring(g.doc.PageCount() - before) + L" Seiten angehängt");
        break;
    }
    case C_ZOOMOUT: SetZoom(g.zoom / 1.2, false); break;
    case C_ZOOMIN:  SetZoom(g.zoom * 1.2, false); break;
    case C_ACTUAL:  SetZoom(1.0, false); break;
    case C_FITW:    SetZoom(g.zoom, true); break;
    case C_ROTL: case C_ROTR: {
        if (!g.doc.IsOpen()) break;
        g.doc.SetRotation(g.page, g.doc.Rotation(g.page) + (cmd == C_ROTR ? 1 : 3));
        if (g.page < static_cast<int>(g.thumbs.size()) && g.thumbs[g.page]) { DeleteObject(g.thumbs[g.page]); g.thumbs[g.page] = nullptr; }
        LayoutSidebar();
        g.scrollX = g.scrollY = 0;
        UpdateCanvas(true);
        UpdateTitle();
        break;
    }
    case C_PREV: GoToPage(g.page - 1); break;
    case C_NEXT: GoToPage(g.page + 1); break;
    }
}

void HandleKey(UINT vk) {
    const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    if (ctrl) {
        switch (vk) {
        case 'O': RunCommand(C_OPEN); return;
        case 'S': RunCommand(C_SAVE); return;
        case 'B': RunCommand(C_SIDE); return;
        case 'M': RunCommand(C_MERGE); return;
        case VK_OEM_PLUS: case VK_ADD: RunCommand(C_ZOOMIN); return;
        case VK_OEM_MINUS: case VK_SUBTRACT: RunCommand(C_ZOOMOUT); return;
        case '0': RunCommand(C_FITW); return;
        case '1': RunCommand(C_ACTUAL); return;
        }
        return;
    }
    if (!g.doc.IsOpen()) return;
    switch (vk) {
    case 'R': RunCommand(shift ? C_ROTL : C_ROTR); break;
    case VK_PRIOR: RunCommand(C_PREV); break;
    case VK_NEXT: RunCommand(C_NEXT); break;
    case VK_HOME: GoToPage(0); break;
    case VK_END: GoToPage(g.doc.PageCount() - 1); break;
    case VK_UP: g.scrollY -= S(60); CanvasScrollbars(); InvalidateRect(g.canvas, nullptr, FALSE); break;
    case VK_DOWN: g.scrollY += S(60); CanvasScrollbars(); InvalidateRect(g.canvas, nullptr, FALSE); break;
    case VK_LEFT: g.scrollX -= S(60); CanvasScrollbars(); InvalidateRect(g.canvas, nullptr, FALSE); break;
    case VK_RIGHT: g.scrollX += S(60); CanvasScrollbars(); InvalidateRect(g.canvas, nullptr, FALSE); break;
    }
}

// ---------- Fensterprozeduren ----------
void PaintBuffered(HWND hwnd, void (*fn)(HDC, RECT)) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT cr; GetClientRect(hwnd, &cr);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    fn(mem, cr);
    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

void ScrollMessage(HWND hwnd, int bar, WPARAM wp, int& pos, int line, int pageStep) {
    SCROLLINFO si{ sizeof(si), SIF_ALL };
    GetScrollInfo(hwnd, bar, &si);
    int p = si.nPos;
    switch (LOWORD(wp)) {
    case SB_LINEUP: p -= line; break;
    case SB_LINEDOWN: p += line; break;
    case SB_PAGEUP: p -= pageStep; break;
    case SB_PAGEDOWN: p += pageStep; break;
    case SB_THUMBTRACK: case SB_THUMBPOSITION: p = si.nTrackPos; break;
    case SB_TOP: p = 0; break;
    case SB_BOTTOM: p = si.nMax; break;
    }
    pos = p;
}

LRESULT CALLBACK CanvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: PaintBuffered(hwnd, PaintCanvas); return 0;
    case WM_SIZE: {
        static bool busy = false;               // SetScrollInfo kann selbst wieder WM_SIZE ausloesen
        if (g.doc.IsOpen() && !busy) { busy = true; UpdateCanvas(g.fitWidth); busy = false; }
        return 0;
    }
    case WM_TIMER:
        if (wp == 1) { KillTimer(hwnd, 1); g.toast.clear(); InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    case WM_VSCROLL: {
        RECT cr; GetClientRect(hwnd, &cr);
        ScrollMessage(hwnd, SB_VERT, wp, g.scrollY, S(40), cr.bottom);
        CanvasScrollbars(); InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_HSCROLL: {
        RECT cr; GetClientRect(hwnd, &cr);
        ScrollMessage(hwnd, SB_HORZ, wp, g.scrollX, S(40), cr.right);
        CanvasScrollbars(); InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (!g.doc.IsOpen()) return 0;
        const int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) {
            SetZoom(g.zoom * std::pow(1.1, delta / 120.0), false);
            return 0;
        }
        RECT cr; GetClientRect(hwnd, &cr);
        const int maxY = Max(0, g.pageH + 2 * S(kMargin) - cr.bottom);
        if (delta < 0 && g.scrollY >= maxY && g.page + 1 < g.doc.PageCount()) { GoToPage(g.page + 1); return 0; }
        if (delta > 0 && g.scrollY <= 0 && g.page > 0) { GoToPage(g.page - 1, true); return 0; }
        g.scrollY -= static_cast<int>(delta / 120.0 * S(70));
        CanvasScrollbars(); InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN: SetFocus(hwnd); return 0;
    case WM_KEYDOWN: HandleKey(static_cast<UINT>(wp)); return 0;
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK SideProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: PaintBuffered(hwnd, PaintSide); return 0;
    case WM_SIZE: LayoutSidebar(); return 0;
    case WM_VSCROLL: {
        RECT cr; GetClientRect(hwnd, &cr);
        ScrollMessage(hwnd, SB_VERT, wp, g.sideScroll, S(40), cr.bottom);
        g.sideScroll = std::clamp(g.sideScroll, 0, Max(0, g.sideContentH - cr.bottom));
        SideScrollbar(); InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (g.sideTab != 0) return 0;
        RECT cr; GetClientRect(hwnd, &cr);
        g.sideScroll = std::clamp(g.sideScroll - static_cast<int>(GET_WHEEL_DELTA_WPARAM(wp) / 120.0 * S(70)), 0, Max(0, g.sideContentH - cr.bottom));
        SideScrollbar(); InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!g.trackingSide) { TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, hwnd, 0 }; TrackMouseEvent(&t); g.trackingSide = true; }
        if (g.sideTab == 1) {
            int row = (GET_Y_LPARAM(lp) - S(12)) / ToolRowH();
            if (row < 0 || row >= static_cast<int>(sizeof(kTools) / sizeof(kTools[0]))) row = -1;
            if (row != g.toolHot) { g.toolHot = row; InvalidateRect(hwnd, nullptr, FALSE); }
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g.trackingSide = false;
        if (g.toolHot != -1) { g.toolHot = -1; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(g.canvas);
        const int y = GET_Y_LPARAM(lp);
        if (g.sideTab == 1) {
            int row = (y - S(12)) / ToolRowH();
            if (row >= 0 && row < static_cast<int>(sizeof(kTools) / sizeof(kTools[0])) && g.doc.IsOpen()) RunCommand(kTools[row].cmd);
        } else if (g.doc.IsOpen()) {
            for (int i = 0; i < g.doc.PageCount(); ++i)
                if (y + g.sideScroll >= g.thumbTop[i] - S(6) && y + g.sideScroll < g.thumbTop[i + 1] - S(6)) { GoToPage(i); break; }
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void LayoutChildren() {
    RECT cr; GetClientRect(g.main, &cr);
    const int top = ToolbarH(), sw = SideW();
    if (g.side) {
        ShowWindow(g.side, g.sideVisible ? SW_SHOW : SW_HIDE);
        MoveWindow(g.side, 0, top + TabHeaderH(), sw, Max(0, cr.bottom - top - TabHeaderH()), TRUE);
    }
    if (g.canvas) MoveWindow(g.canvas, sw, top, Max(0, cr.right - sw), Max(0, cr.bottom - top), TRUE);
    const int tw = (sw - S(24)) / 2;
    g.tabRc[0] = RECT{ S(12), top + S(6), S(12) + tw, top + TabHeaderH() - S(4) };
    g.tabRc[1] = RECT{ S(12) + tw, top + S(6), S(12) + 2 * tw, top + TabHeaderH() - S(4) };
    LayoutToolbar();
}

void PaintMain(HDC dc, RECT cr) {
    HBRUSH bg = CreateSolidBrush(kBackground);
    FillRect(dc, &cr, bg);
    DeleteObject(bg);
    DrawToolbar(dc, cr.right);
    if (g.sideVisible) {
        RECT col{ 0, ToolbarH(), SideW(), cr.bottom };
        HBRUSH pb = CreateSolidBrush(kPanel);
        FillRect(dc, &col, pb);
        DeleteObject(pb);
        const wchar_t* names[2] = { L"Seiten", L"Werkzeuge" };
        for (int i = 0; i < 2; ++i) {
            const bool sel = g.sideTab == i;
            if (sel) FillRound(dc, g.tabRc[i], S(8), kCardRaised, kBorder);
            DrawTextC(dc, sel ? g.fontBold : g.font, names[i], g.tabRc[i], sel ? kText : kTextDim);
        }
        HPEN pen = CreatePen(PS_SOLID, 1, kBorder);
        HGDIOBJ o = SelectObject(dc, pen);
        MoveToEx(dc, SideW() - 1, ToolbarH(), nullptr); LineTo(dc, SideW() - 1, cr.bottom);
        SelectObject(dc, o); DeleteObject(pen);
    }
}

void SetDarkScrollbars(HWND h) { SetWindowTheme(h, L"DarkMode_Explorer", nullptr); }

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g.main = hwnd;
        g.dpi = static_cast<int>(GetDpiForWindow(hwnd));
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        MakeFonts();
        BuildItems();
        g.side = CreateWindowExW(0, L"DuncansPdfSide", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL, 0, 0, 10, 10, hwnd, nullptr, g.inst, nullptr);
        g.canvas = CreateWindowExW(0, L"DuncansPdfCanvas", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL, 0, 0, 10, 10, hwnd, nullptr, g.inst, nullptr);
        SetDarkScrollbars(g.side);
        SetDarkScrollbars(g.canvas);
        DragAcceptFiles(hwnd, TRUE);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: PaintBuffered(hwnd, PaintMain); return 0;
    case WM_SIZE:
        LayoutChildren();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
        mm->ptMinTrackSize = { S(760), S(480) };
        return 0;
    }
    case WM_DPICHANGED: {
        g.dpi = HIWORD(wp);
        MakeFonts();
        const RECT* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        ClearThumbs();
        LayoutChildren();
        LayoutSidebar();
        UpdateCanvas(true);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!g.trackingMain) { TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, hwnd, 0 }; TrackMouseEvent(&t); g.trackingMain = true; }
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int h = HitToolbar(p);
        if (h != g.hot) { g.hot = h; InvalidateToolbar(); }
        return 0;
    }
    case WM_MOUSELEAVE:
        g.trackingMain = false;
        if (g.hot != -1) { g.hot = -1; InvalidateToolbar(); }
        return 0;
    case WM_LBUTTONDOWN: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (g.sideVisible) {
            for (int i = 0; i < 2; ++i) if (PtInRect(&g.tabRc[i], p) && g.sideTab != i) {
                g.sideTab = i;
                g.sideScroll = 0;
                SideScrollbar();
                InvalidateRect(hwnd, nullptr, FALSE);
                InvalidateRect(g.side, nullptr, FALSE);
                return 0;
            }
        }
        int h = HitToolbar(p);
        if (h >= 0 && ItemEnabled(g.items[h])) { g.down = h; SetCapture(hwnd); InvalidateToolbar(); }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (g.down >= 0) {
            ReleaseCapture();
            POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            const int cmd = HitToolbar(p) == g.down ? g.items[g.down].cmd : C_NONE;
            g.down = -1;
            InvalidateToolbar();
            if (cmd != C_NONE) RunCommand(cmd);
        }
        return 0;
    }
    case WM_KEYDOWN: HandleKey(static_cast<UINT>(wp)); return 0;
    case WM_SETFOCUS: if (g.canvas) SetFocus(g.canvas); return 0;
    case WM_DROPFILES: {
        wchar_t path[MAX_PATH * 2]{};
        if (DragQueryFileW(reinterpret_cast<HDROP>(wp), 0, path, MAX_PATH * 2)) OpenPath(path);
        DragFinish(reinterpret_cast<HDROP>(wp));
        return 0;
    }
    case WM_CLOSE:
        if (g.doc.IsOpen() && g.doc.Modified() &&
            MessageBoxW(hwnd, L"Es gibt ungespeicherte Änderungen (gedrehte oder angehängte Seiten).\nTrotzdem schließen?",
                        kAppTitle, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
            return 0;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    g.inst = inst;
    Gdiplus::GdiplusStartupInput gsi;
    Gdiplus::GdiplusStartup(&g_gdipToken, &gsi, nullptr);

    auto reg = [&](const wchar_t* name, WNDPROC proc, HBRUSH bg, HICON icon) {
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = proc; wc.hInstance = inst; wc.lpszClassName = name;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = bg; wc.hIcon = icon; wc.hIconSm = icon;
        RegisterClassExW(&wc);
    };
    HICON icon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    reg(L"DuncansPdfMain", MainProc, nullptr, icon);
    reg(L"DuncansPdfSide", SideProc, nullptr, nullptr);
    reg(L"DuncansPdfCanvas", CanvasProc, nullptr, nullptr);

    HWND hwnd = CreateWindowExW(0, L"DuncansPdfMain", kAppTitle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1280, 820, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    // Startgroesse DPI-abhaengig
    SetWindowPos(hwnd, nullptr, 0, 0, S(1180), S(780), SWP_NOMOVE | SWP_NOZORDER);
    LayoutChildren();
    UpdateLabels();
    LayoutToolbar();
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    SetFocus(g.canvas);

    // PDF per Kommandozeile / "Oeffnen mit"
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) OpenPath(argv[1]);
    if (argv) LocalFree(argv);
    if (!g.doc.IsOpen() && !PdfDocument::InitLibrary()) ShowToast(PdfDocument::LastError(), true);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    ClearThumbs();
    if (g.pageBmp) DeleteObject(g.pageBmp);
    g.doc.Close();
    Gdiplus::GdiplusShutdown(g_gdipToken);
    return 0;
}
