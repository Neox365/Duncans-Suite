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

#ifndef APP_VERSION
#define APP_VERSION L"1.1.15"   // normalerweise aus CMake (PROJECT_VERSION)
#endif

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
    C_ROTL, C_ROTR, C_PREV, C_NEXT, C_FIND, C_DELETE, C_HILITE, C_NOTE, C_COMPRESS, C_CROP, C_UNCROP
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
    RECT aboutRc{};                        // Fusszeile "© 2026 Duncan · vX.Y.Z" (Klick oeffnet "Ueber")
    bool aboutHot = false;

    // Seitenleiste
    bool sideVisible = true;
    int sideTab = 0;                       // 0 = Seiten, 1 = Werkzeuge
    RECT tabRc[2]{};
    std::vector<HBITMAP> thumbs;
    std::vector<double> aspect;            // Hoehe/Breite je Seite
    std::vector<int> thumbTop;
    int thumbW = 0, sideContentH = 0, sideScroll = 0, toolHot = -1;
    bool trackingSide = false;
    std::vector<char> sel;                 // ausgewaehlte Seiten (Strg/Umschalt-Klick); die aktuelle Seite gehoert immer dazu
    int selAnchor = 0;
    bool selKeep = false;                  // GoToPage soll die Auswahl nicht auf die Zielseite zuruecksetzen

    // Seiten per Drag & Drop umordnen
    bool dragPending = false, dragging = false, dragDeferClick = false;
    int dragHit = -1, dragStartY = 0, dragInsert = -1;
    std::vector<int> dragPages;

    // Anmerkungen: 0 = kein Werkzeug, 1 = Text markieren, 2 = Notiz setzen, 3 = Zuschnitt aufziehen
    int tool = 0;
    bool selDrag = false;                  // Textauswahl beim Markieren laeuft
    int selPage = -1, selA = -1, selB = -1;
    bool cropDrag = false;                 // Zuschnitt-Rahmen wird aufgezogen
    POINT cropA{}, cropB{};                // Eckpunkte in Canvas-Koordinaten

    // Formulare
    bool formFocus = false;                // ein Text-/Auswahlfeld hat den Eingabefokus (Tasten gehen an das Feld)
    bool formMouse = false;                // Maustaste im Formular gedrueckt
    bool formHintShown = false;

    // Textsuche (Strg+F)
    bool searchOpen = false;
    HWND searchEdit = nullptr;
    std::wstring lastQuery;                // Suchbegriff des letzten Suchlaufs
    std::vector<PdfDocument::TextHit> hits;
    int curHit = -1;
    unsigned hitsVersion = 0;
    std::vector<std::pair<int, RECT>> hlRects;   // Markierungen der aktuellen Seite (Trefferindex, Pixelrechteck)
    int hlPage = -1, hlW = 0, hlH = 0, hlRot = -1;
    unsigned hlVersion = ~0u;

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
int FooterH() { return S(26); }   // schmale Fusszeile unter Seitenleiste und Canvas
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
void UpdateTitle();
void OpenSearch();
void CloseSearch();
void SearchStep(int dir);
void ClearSearch();

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
    add(C_FIND, L"Suchen", 0, true);
    add(C_HILITE, L"Marker", 0, true);
    add(C_NOTE, L"Notiz", 0, true);
    add(C_CROP, L"Zuschnitt", 0, true);
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
        const bool active = (it.cmd == C_SIDE && g.sideVisible) || (it.cmd == C_HILITE && g.tool == 1) || (it.cmd == C_NOTE && g.tool == 2) || (it.cmd == C_CROP && g.tool == 3);
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
    { C_DELETE, L"Seite(n) löschen  (Entf)" }, { C_FIND, L"Im Dokument suchen  (Strg+F)" },
    { C_HILITE, L"Text markieren  (H)" }, { C_NOTE, L"Notiz hinzufügen  (N)" }, { C_COMPRESS, L"Datei verkleinern …" },
    { C_MERGE, L"PDF anhängen (Merge)" }, { C_SAVE, L"Speichern unter …" },
    { C_FITW, L"Auf Seitenbreite einpassen" }, { C_ACTUAL, L"Originalgröße (100 %)" },
};
int ToolRowH() { return S(42); }

void ClearThumbs() {
    for (HBITMAP b : g.thumbs) if (b) DeleteObject(b);
    g.thumbs.clear();
}

void SelectOnly(int i) {
    g.sel.assign(g.doc.PageCount(), 0);
    if (i >= 0 && i < static_cast<int>(g.sel.size())) g.sel[i] = 1;
    g.selAnchor = i;
}

// Seiten, auf die Drehen/Loeschen wirkt: die Auswahl, sonst die aktuelle Seite
std::vector<int> TargetPages() {
    std::vector<int> v;
    for (int i = 0; i < static_cast<int>(g.sel.size()); ++i) if (g.sel[i]) v.push_back(i);
    if (v.empty() && g.doc.IsOpen()) v.push_back(g.page);
    return v;
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
    if (static_cast<int>(g.sel.size()) != n) SelectOnly(n > 0 ? std::min(g.page, n - 1) : -1);
    g.thumbTop.assign(n + 1, 0);
    int y = pad;
    for (int i = 0; i < n; ++i) {
        double w, h;
        if (g.doc.PageSize(i, w, h)) g.aspect[i] = std::clamp(h / w, 0.05, 20.0);   // absurde Seitenformate (manipulierte PDF) begrenzen
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
        const bool selected = i < static_cast<int>(g.sel.size()) && g.sel[i];
        if (i == g.page) FillRound(dc, frame, S(6), kCardRaised, kAccent);
        else if (selected) FillRound(dc, frame, S(6), kCardRaised, RGB(56, 104, 170));   // ausgewaehlt, aber nicht die angezeigte Seite
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
    if (g.dragging && g.dragInsert >= 0 && g.dragInsert < static_cast<int>(g.thumbTop.size())) {   // Einfuegemarke beim Verschieben
        const int ly = g.thumbTop[g.dragInsert] - g.sideScroll - S(9);
        RECT line{ pad - S(4), ly - S(1), pad + g.thumbW + S(4), ly + S(2) };
        FillRound(dc, line, S(2), kAccent);
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
    g.formFocus = false;
    if (!g.selKeep) SelectOnly(p);
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
    g.pageBmp = g.doc.HasForms() ? g.doc.RenderPageForm(g.page, g.pageW, g.pageH) : g.doc.RenderPage(g.page, g.pageW, g.pageH);
    g.bmpPage = g.page; g.bmpW = g.pageW; g.bmpH = g.pageH; g.bmpRot = g.doc.Rotation(g.page);
}

void ShowToast(const std::wstring& text, bool err) {
    g.toast = text; g.toastErr = err;
    SetTimer(g.canvas, 1, err ? 5000 : 3000, nullptr);
    InvalidateRect(g.canvas, nullptr, FALSE);
}

// ---------- Textsuche ----------
// Suchfeld oben rechts im Canvas: [Eingabe] [Treffer x / y] [‹] [›] [✕]
struct SearchLayout { RECT panel, edit, count, prev, next, close; };

SearchLayout GetSearchLayout(RECT cr) {
    SearchLayout l{};
    const int w = std::min(S(430), static_cast<int>(cr.right) - S(24)), h = S(46), btn = S(30);
    l.panel = RECT{ cr.right - S(14) - w, S(12), cr.right - S(14), S(12) + h };
    l.close = RECT{ l.panel.right - S(8) - btn, l.panel.top + (h - btn) / 2, l.panel.right - S(8), l.panel.top + (h - btn) / 2 + btn };
    l.next = RECT{ l.close.left - S(4) - btn, l.close.top, l.close.left - S(4), l.close.bottom };
    l.prev = RECT{ l.next.left - S(4) - btn, l.close.top, l.next.left - S(4), l.close.bottom };
    l.count = RECT{ l.prev.left - S(8) - S(92), l.panel.top, l.prev.left - S(8), l.panel.bottom };
    l.edit = RECT{ l.panel.left + S(14), l.panel.top + (h - S(22)) / 2, l.count.left - S(6), l.panel.top + (h - S(22)) / 2 + S(22) };
    return l;
}

WNDPROC g_oldEditProc = nullptr;

// Eingabefeld der Suche: Enter = naechster Treffer, Umschalt+Enter = vorheriger, Esc = schliessen;
// Seitenwechsel und die wichtigsten Strg-Kuerzel funktionieren auch waehrend des Tippens.
LRESULT CALLBACK SearchEditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
        if (wp == VK_ESCAPE) { CloseSearch(); return 0; }
        if (wp == VK_RETURN) { SearchStep(shift ? -1 : 1); return 0; }
        if (wp == VK_F3) { SearchStep(shift ? -1 : 1); return 0; }
        if (wp == VK_PRIOR || wp == VK_NEXT) { HandleKey(static_cast<UINT>(wp)); return 0; }
        if (ctrl && (wp == 'O' || wp == 'S' || wp == 'M' || wp == 'B')) { HandleKey(static_cast<UINT>(wp)); return 0; }
        if (ctrl && wp == 'F') { SendMessageW(h, EM_SETSEL, 0, -1); return 0; }
    }
    if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;   // kein Signalton
    return CallWindowProcW(g_oldEditProc, h, msg, wp, lp);
}

void PositionSearchEdit() {
    if (!g.searchEdit) return;
    RECT cr; GetClientRect(g.canvas, &cr);
    const RECT e = GetSearchLayout(cr).edit;
    MoveWindow(g.searchEdit, e.left, e.top, std::max(0L, e.right - e.left), e.bottom - e.top, TRUE);
}

void ClearSearch() {
    g.hits.clear();
    g.curHit = -1;
    g.lastQuery.clear();
    ++g.hitsVersion;
}

// Markierungen der angezeigten Seite neu berechnen, wenn sich Seite, Groesse, Drehung oder Treffer geaendert haben
void UpdateHighlights() {
    if (g.hlVersion == g.hitsVersion && g.hlPage == g.page && g.hlW == g.pageW && g.hlH == g.pageH && g.hlRot == g.doc.Rotation(g.page)) return;
    g.hlRects.clear();
    for (size_t k = 0; k < g.hits.size(); ++k) {
        if (g.hits[k].page != g.page) continue;
        std::vector<RECT> rs;
        g.doc.HitRects(g.hits[k], g.pageW, g.pageH, rs);
        for (const RECT& r : rs) g.hlRects.emplace_back(static_cast<int>(k), r);
    }
    g.hlVersion = g.hitsVersion; g.hlPage = g.page; g.hlW = g.pageW; g.hlH = g.pageH; g.hlRot = g.doc.Rotation(g.page);
}

void ScrollToCurrentHit() {
    if (g.curHit < 0 || g.curHit >= static_cast<int>(g.hits.size())) return;
    UpdateHighlights();
    for (const auto& h : g.hlRects) {
        if (h.first != g.curHit) continue;
        RECT cr; GetClientRect(g.canvas, &cr);
        g.scrollY = h.second.top + S(kMargin) - cr.bottom / 3;
        g.scrollX = h.second.left + S(kMargin) - cr.right / 2;
        CanvasScrollbars();
        break;
    }
    InvalidateRect(g.canvas, nullptr, FALSE);
}

void GoToHit(int i) {
    const int n = static_cast<int>(g.hits.size());
    if (n == 0) { InvalidateRect(g.canvas, nullptr, FALSE); return; }
    g.curHit = ((i % n) + n) % n;
    if (g.hits[g.curHit].page != g.page) GoToPage(g.hits[g.curHit].page);
    ScrollToCurrentHit();
}

void RunSearch() {
    if (!g.searchEdit || !g.doc.IsOpen()) return;
    wchar_t buf[256]{};
    GetWindowTextW(g.searchEdit, buf, 256);
    ClearSearch();
    g.lastQuery = buf;
    if (g.lastQuery.empty()) { InvalidateRect(g.canvas, nullptr, FALSE); return; }
    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    g.hits = g.doc.Search(g.lastQuery);
    SetCursor(old);
    ++g.hitsVersion;
    int first = 0;                                           // erster Treffer ab der aktuellen Seite
    for (size_t k = 0; k < g.hits.size(); ++k) if (g.hits[k].page >= g.page) { first = static_cast<int>(k); break; }
    GoToHit(first);
}

void SearchStep(int dir) {
    if (!g.searchOpen || !g.searchEdit) return;
    wchar_t buf[256]{};
    GetWindowTextW(g.searchEdit, buf, 256);
    if (g.lastQuery != buf) { RunSearch(); return; }        // Begriff geaendert: erst suchen
    GoToHit(g.curHit < 0 ? 0 : g.curHit + dir);
}

void OpenSearch() {
    if (!g.doc.IsOpen()) return;
    if (!g.searchEdit) {
        g.searchEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, g.canvas, reinterpret_cast<HMENU>(100), g.inst, nullptr);
        SendMessageW(g.searchEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
        SendMessageW(g.searchEdit, EM_SETLIMITTEXT, 200, 0);
        g_oldEditProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g.searchEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&SearchEditProc)));
    }
    g.searchOpen = true;
    PositionSearchEdit();
    ShowWindow(g.searchEdit, SW_SHOW);
    SetFocus(g.searchEdit);
    SendMessageW(g.searchEdit, EM_SETSEL, 0, -1);
    InvalidateRect(g.canvas, nullptr, FALSE);
}

void CloseSearch() {
    if (!g.searchOpen) return;
    g.searchOpen = false;
    if (g.searchEdit) ShowWindow(g.searchEdit, SW_HIDE);
    ClearSearch();
    SetFocus(g.canvas);
    InvalidateRect(g.canvas, nullptr, FALSE);
}

void DrawSearchPanel(HDC dc, RECT cr) {
    const SearchLayout l = GetSearchLayout(cr);
    FillRound(dc, l.panel, S(12), kCardRaised, kBorder);
    std::wstring c;
    if (g.lastQuery.empty()) c = L"";
    else if (g.hits.empty()) c = L"Keine Treffer";
    else c = std::to_wstring(g.curHit + 1) + L" / " + std::to_wstring(g.hits.size());
    DrawTextC(dc, g.fontSmall, c, l.count, g.hits.empty() && !g.lastQuery.empty() ? kRed : kTextDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    const struct { RECT r; const wchar_t* t; } btns[] = { { l.prev, L"‹" }, { l.next, L"›" }, { l.close, L"✕" } };
    for (const auto& b : btns) DrawTextC(dc, g.font, b.t, b.r, kText);
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
        if (g.cropDrag) {                            // Zuschnitt-Rahmen: Aussenbereich abdunkeln, Rahmen zeichnen
            using namespace Gdiplus;
            Graphics gr(dc);
            gr.SetClip(Rect(x, y, g.pageW, g.pageH));
            const int l = std::min<int>(g.cropA.x, g.cropB.x), r = std::max<int>(g.cropA.x, g.cropB.x);
            const int t = std::min<int>(g.cropA.y, g.cropB.y), b = std::max<int>(g.cropA.y, g.cropB.y);
            SolidBrush dim(Color(140, 0, 0, 0));
            gr.FillRectangle(&dim, Rect(x, y, g.pageW, t - y));
            gr.FillRectangle(&dim, Rect(x, b, g.pageW, y + g.pageH - b));
            gr.FillRectangle(&dim, Rect(x, t, l - x, b - t));
            gr.FillRectangle(&dim, Rect(r, t, x + g.pageW - r, b - t));
            Pen frame(Color(255, 64, 156, 255), 2.0f);
            gr.DrawRectangle(&frame, Rect(l, t, r - l, b - t));
        }
        if (g.selDrag && g.selPage == g.page) {      // laufende Textauswahl beim Markieren
            std::vector<RECT> rs;
            PdfDocument::TextHit th;
            th.page = g.page; th.start = std::min(g.selA, g.selB); th.count = std::abs(g.selB - g.selA) + 1;
            g.doc.HitRects(th, g.pageW, g.pageH, rs);
            using namespace Gdiplus;
            Graphics gr(dc);
            gr.SetClip(Rect(x, y, g.pageW, g.pageH));
            SolidBrush sel(Color(90, 64, 156, 255));
            for (const RECT& r : rs) gr.FillRectangle(&sel, Rect(x + r.left, y + r.top, r.right - r.left, r.bottom - r.top));
        }
        if (!g.hits.empty()) {                       // Treffer der Suche markieren (aktueller Treffer kraeftiger)
            UpdateHighlights();
            using namespace Gdiplus;
            Graphics gr(dc);
            gr.SetClip(Rect(x, y, g.pageW, g.pageH));
            SolidBrush others(Color(110, 255, 214, 0)), current(Color(170, 255, 140, 0));
            Pen outline(Color(255, 230, 110, 0), 1.5f);
            for (const auto& h : g.hlRects) {
                const RECT& r = h.second;
                const bool cur = h.first == g.curHit;
                Rect gr2(x + r.left - 1, y + r.top - 1, (r.right - r.left) + 2, (r.bottom - r.top) + 2);
                gr.FillRectangle(cur ? &current : &others, gr2);
                if (cur) gr.DrawRectangle(&outline, gr2);
            }
        }
    }
    if (g.searchOpen) DrawSearchPanel(dc, cr);

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

// Mehrfachauswahl: eine oder mehrere PDF-Dateien (Reihenfolge wie im Dialog)
bool PickFiles(HWND owner, std::vector<std::wstring>& out, const wchar_t* title) {
    std::vector<wchar_t> buf(32768, 0);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"PDF-Dokumente (*.pdf)\0*.pdf\0Alle Dateien (*.*)\0*.*\0";
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = static_cast<DWORD>(buf.size());
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT;
    if (!GetOpenFileNameW(&ofn)) return false;
    const wchar_t* p = buf.data();
    const std::wstring first = p;
    p += first.size() + 1;
    if (*p == 0) { out.push_back(first); return true; }      // genau eine Datei: voller Pfad
    const std::wstring dir = (!first.empty() && first.back() == L'\\') ? first : first + L"\\";
    while (*p) {                                              // sonst: Ordner, danach die Dateinamen
        const std::wstring name = p;
        out.push_back(dir + name);
        p += name.size() + 1;
    }
    return !out.empty();
}

// ---------- Kleiner Eingabedialog (Notiztext) im Dark-Stil ----------
struct PromptState {
    std::wstring title, text;
    bool about = false;             // "Ueber"-Fenster: Text statt Eingabefeld, Knoepfe "GitHub oeffnen" / "Schliessen"
    std::wstring body;
    bool done = false, ok = false;
    HWND edit = nullptr;
    RECT btnOk{}, btnCancel{};
    int hot = 0;                    // 1 = Hinzufuegen, 2 = Abbrechen
};
PromptState* g_prompt = nullptr;

void PromptCommit(HWND hwnd) {
    const int len = GetWindowTextLengthW(g_prompt->edit);
    std::wstring t(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(g_prompt->edit, &t[0], len + 1);
    t.resize(static_cast<size_t>(len));
    g_prompt->text = t;
    g_prompt->ok = !t.empty();
    g_prompt->done = true;
    (void)hwnd;
}

void PromptLayout(HWND hwnd) {
    RECT cr; GetClientRect(hwnd, &cr);
    const int pad = S(20), bh = S(38), bw = S(130);
    g_prompt->btnOk = RECT{ cr.right - pad - bw, cr.bottom - pad - bh, cr.right - pad, cr.bottom - pad };
    g_prompt->btnCancel = RECT{ g_prompt->btnOk.left - S(10) - bw, g_prompt->btnOk.top, g_prompt->btnOk.left - S(10), g_prompt->btnOk.bottom };
    if (g_prompt->edit)
        MoveWindow(g_prompt->edit, pad + S(10), S(62), cr.right - 2 * pad - S(20), cr.bottom - S(62) - pad - bh - S(24), TRUE);
}

LRESULT CALLBACK PromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (!g_prompt) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_CREATE: {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        if (g_prompt->about) return 0;
        g_prompt->edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                                         0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(1), g.inst, nullptr);
        SendMessageW(g_prompt->edit, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
        SendMessageW(g_prompt->edit, EM_SETLIMITTEXT, 2000, 0);
        SetWindowTheme(g_prompt->edit, L"DarkMode_Explorer", nullptr);
        return 0;
    }
    case WM_SIZE: PromptLayout(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_CTLCOLOREDIT: {
        static HBRUSH br = CreateSolidBrush(kCardRaised);
        SetTextColor(reinterpret_cast<HDC>(wp), kText);
        SetBkColor(reinterpret_cast<HDC>(wp), kCardRaised);
        return reinterpret_cast<LRESULT>(br);
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT cr; GetClientRect(hwnd, &cr);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        HBRUSH bg = CreateSolidBrush(kBackground);
        FillRect(mem, &cr, bg);
        DeleteObject(bg);
        RECT t{ S(20), S(18), cr.right - S(20), S(52) };
        DrawTextC(mem, g.fontBold, g_prompt->title, t, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT card{ S(20), S(58), cr.right - S(20), cr.bottom - S(20) - S(38) - S(20) };
        FillRound(mem, card, S(10), kCardRaised, kBorder);
        if (g_prompt->about) {
            RECT bt{ card.left + S(16), card.top + S(12), card.right - S(16), card.bottom - S(10) };
            DrawTextC(mem, g.font, g_prompt->body, bt, kText, DT_LEFT | DT_TOP | DT_WORDBREAK);
        }
        auto button = [&](const RECT& r, bool primary, bool hot, const wchar_t* label) {
            COLORREF fill = primary ? (hot ? RGB(96, 176, 255) : kAccent) : (hot ? kCardHover : kCardRaised);
            FillRound(mem, r, S(10), fill, primary ? fill : (hot ? RGB(96, 104, 126) : kBorder));
            DrawTextC(mem, g.font, label, r, primary ? RGB(8, 20, 36) : kText);
        };
        button(g_prompt->btnCancel, false, g_prompt->hot == 2, g_prompt->about ? L"GitHub öffnen" : L"Abbrechen");
        button(g_prompt->btnOk, true, g_prompt->hot == 1, g_prompt->about ? L"Schließen" : L"Hinzufügen");
        BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        const int hot = PtInRect(&g_prompt->btnOk, p) ? 1 : PtInRect(&g_prompt->btnCancel, p) ? 2 : 0;
        if (hot != g_prompt->hot) { g_prompt->hot = hot; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (g_prompt->about) {
            if (PtInRect(&g_prompt->btnOk, p)) g_prompt->done = true;
            else if (PtInRect(&g_prompt->btnCancel, p)) ShellExecuteW(hwnd, L"open", L"https://github.com/Neox365/Duncans-Suite", nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        if (PtInRect(&g_prompt->btnOk, p)) PromptCommit(hwnd);
        else if (PtInRect(&g_prompt->btnCancel, p)) g_prompt->done = true;
        return 0;
    }
    case WM_CLOSE: g_prompt->done = true; return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Zeigt den Dialog modal (verschachtelte Nachrichtenschleife). true = Text eingegeben und bestaetigt.
bool PromptText(HWND owner, const wchar_t* title, std::wstring& text, const wchar_t* aboutBody = nullptr) {
    PromptState st;
    st.title = title;
    if (aboutBody) { st.about = true; st.body = aboutBody; }
    g_prompt = &st;
    RECT cr{ 0, 0, S(460), S(300) };
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU, ex = WS_EX_DLGMODALFRAME;
    AdjustWindowRectEx(&cr, style, FALSE, ex);
    RECT orc; GetWindowRect(owner, &orc);
    const int w = cr.right - cr.left, h = cr.bottom - cr.top;
    HWND hwnd = CreateWindowExW(ex, L"DuncansPdfPrompt", title, style, orc.left + ((orc.right - orc.left) - w) / 2,
                                orc.top + ((orc.bottom - orc.top) - h) / 3, w, h, owner, nullptr, g.inst, nullptr);
    if (!hwnd) { g_prompt = nullptr; return false; }
    PromptLayout(hwnd);
    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    if (st.edit) SetFocus(st.edit);
    MSG m;
    while (!st.done) {
        const BOOL r = GetMessageW(&m, nullptr, 0, 0);
        if (r == 0) { PostQuitMessage(static_cast<int>(m.wParam)); break; }   // WM_QUIT weiterreichen
        if (r < 0) break;
        if (m.message == WM_KEYDOWN) {
            if (m.wParam == VK_ESCAPE || (st.about && m.wParam == VK_RETURN)) { st.done = true; continue; }
            if (!st.about && m.wParam == VK_RETURN && GetKeyState(VK_CONTROL) < 0) { PromptCommit(hwnd); continue; }
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    EnableWindow(owner, TRUE);
    DestroyWindow(hwnd);
    SetForegroundWindow(owner);
    g_prompt = nullptr;
    if (st.ok) text = st.text;
    return st.ok;
}

// "Ueber"-Fenster im selben Dark-Stil
void ShowAbout() {
    const std::wstring body = std::wstring(L"Duncan's PDF Reader\n") +
        L"Version v" + APP_VERSION + L"\n\n" +
        L"Entwickler: Duncan  ·  © 2026\n\n" +
        L"Teil von Duncan's Suite (Driver Updater, PDF Reader, Tidy).\n" +
        L"Quellcode: github.com/Neox365/Duncans-Suite";
    std::wstring unused;
    PromptText(g.main, L"Über Duncan's PDF Reader", unused, body.c_str());
}

// Obere linke Ecke der angezeigten Seite im Canvas (Client-Koordinaten)
void PageOrigin(int& ox, int& oy) {
    RECT cr; GetClientRect(g.canvas, &cr);
    const int m = S(kMargin);
    ox = (g.pageW + 2 * m <= cr.right) ? (cr.right - g.pageW) / 2 : m - g.scrollX;
    oy = (g.pageH + 2 * m <= cr.bottom) ? (cr.bottom - g.pageH) / 2 : m - g.scrollY;
}

// Nach einer Aenderung am Dokument (Anmerkung): Seite und Vorschau neu zeichnen
void RefreshAfterEdit(int page) {
    if (page >= 0 && page < static_cast<int>(g.thumbs.size()) && g.thumbs[page]) { DeleteObject(g.thumbs[page]); g.thumbs[page] = nullptr; }
    UpdateCanvas(true);
    InvalidateRect(g.side, nullptr, FALSE);
    UpdateTitle();
}

std::wstring FormatBytes(unsigned long long b) {
    wchar_t buf[48];
    if (b >= (1ull << 20)) swprintf(buf, 48, L"%.1f MB", b / 1048576.0);
    else swprintf(buf, 48, L"%.0f KB", b / 1024.0);
    return buf;
}

void UpdateTitle() {
    std::wstring t = g.doc.IsOpen() ? FileName(g.doc.Path()) + (g.doc.Modified() ? L" *" : L"") + L" – " + kAppTitle : kAppTitle;
    SetWindowTextW(g.main, t.c_str());
}

void OpenPath(const std::wstring& path) {
    if (!g.doc.Open(path)) { ShowToast(PdfDocument::LastError(), true); return; }
    g.page = 0; g.zoom = 1.0; g.fitWidth = true; g.scrollX = g.scrollY = g.sideScroll = 0;
    ClearThumbs();
    g.sel.clear();
    ClearSearch();
    if (g.searchEdit) SetWindowTextW(g.searchEdit, L"");
    LayoutSidebar();
    UpdateCanvas(true);
    UpdateTitle();
    g.toast.clear();
    g.formFocus = g.formMouse = false;
    if (g.doc.HasForms()) ShowToast(L"Formular erkannt – Felder zum Ausfüllen einfach anklicken");
}

void ResetFormFocus() {
    if (g.formFocus) { g.doc.FormKillFocus(); g.formFocus = false; }
}

// Nach einer Aenderung an Seitengroessen (Zuschnitt): Vorschauen, Seitenleiste und Canvas neu aufbauen
void RefreshAllPages() {
    ClearThumbs();
    LayoutSidebar();
    g.scrollX = g.scrollY = 0;
    UpdateCanvas(true);
    EnsureThumbVisible();
    UpdateTitle();
}

void RunCommand(int cmd) {
    if (cmd != C_NONE) ResetFormFocus();       // Feldinhalt uebernehmen, bevor etwas anderes passiert
    switch (cmd) {
    case C_CROP: {
        if (!g.doc.IsOpen()) break;
        g.tool = g.tool == 3 ? 0 : 3;
        g.selDrag = g.cropDrag = false;
        InvalidateToolbar();
        InvalidateRect(g.canvas, nullptr, FALSE);
        if (g.tool == 3) ShowToast(L"Zuschnitt: Rahmen mit der Maus aufziehen (Esc beendet, U hebt den Zuschnitt auf)");
        SetFocus(g.canvas);
        break;
    }
    case C_UNCROP: {
        if (!g.doc.IsOpen()) break;
        const std::vector<int> t = TargetPages();
        if (!g.doc.ResetCrop(t)) { ShowToast(PdfDocument::LastError(), true); break; }
        RefreshAllPages();
        ShowToast(L"Zuschnitt aufgehoben (" + std::to_wstring(t.size()) + L" Seite(n)) – noch nicht gespeichert");
        break;
    }
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
        std::vector<std::wstring> files;
        if (!PickFiles(g.main, files, L"PDF(s) zum Anhängen auswählen (mehrere mit Strg/Umschalt)")) break;
        const int before = g.doc.PageCount();
        int failed = 0;
        std::wstring lastErr;
        for (const std::wstring& f : files)
            if (!g.doc.Append(f)) { ++failed; lastErr = FileName(f) + L": " + PdfDocument::LastError(); }
        ClearSearch();
        LayoutSidebar();
        UpdateCanvas(false);
        UpdateTitle();
        const int added = g.doc.PageCount() - before;
        if (failed) ShowToast(std::to_wstring(added) + L" Seiten angehängt, " + std::to_wstring(failed) + L" Datei(en) fehlgeschlagen – " + lastErr, true);
        else ShowToast(std::to_wstring(added) + L" Seiten aus " + std::to_wstring(files.size()) + L" Datei(en) angehängt");
        break;
    }
    case C_FIND: OpenSearch(); break;
    case C_HILITE: case C_NOTE: {
        if (!g.doc.IsOpen()) break;
        const int want = cmd == C_HILITE ? 1 : 2;
        g.tool = g.tool == want ? 0 : want;
        g.selDrag = false;
        InvalidateToolbar();
        InvalidateRect(g.canvas, nullptr, FALSE);
        if (g.tool == 1) ShowToast(L"Marker: Text mit der Maus überstreichen (Esc beendet)");
        else if (g.tool == 2) ShowToast(L"Notiz: auf die Stelle klicken (Esc beendet)");
        SetFocus(g.canvas);
        break;
    }
    case C_COMPRESS: {
        if (!g.doc.IsOpen()) break;
        if (MessageBoxW(g.main,
                L"Die Datei wird verkleinert: Bilder werden auf höchstens 150 dpi heruntergerechnet und mit JPEG-Qualität 70 neu gespeichert, ungenutzte Objekte entfallen.\n\n"
                L"Lesezeichen und Formularfelder gehen dabei verloren. Das geöffnete Dokument bleibt unverändert; die verkleinerte Kopie wird unter neuem Namen gespeichert.\n\nFortfahren?",
                kAppTitle, MB_YESNO | MB_ICONQUESTION) != IDYES) break;
        std::wstring suggestion = FileName(g.doc.Path());
        if (suggestion.size() > 4 && _wcsicmp(suggestion.c_str() + suggestion.size() - 4, L".pdf") == 0) suggestion.resize(suggestion.size() - 4);
        suggestion += L"_klein.pdf";
        std::wstring p;
        if (!PickFile(g.main, true, p, L"Verkleinerte PDF speichern unter", suggestion)) break;
        HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        PdfDocument::CompressStats st;
        const bool ok = g.doc.SaveCompressed(p, 150, 70, st);
        SetCursor(old);
        if (!ok) { ShowToast(PdfDocument::LastError(), true); break; }
        wchar_t pct[32];
        swprintf(pct, 32, L"%d %%", st.before ? static_cast<int>(100.0 * (static_cast<double>(st.after) - static_cast<double>(st.before)) / static_cast<double>(st.before)) : 0);
        ShowToast(L"Verkleinert: " + FormatBytes(st.before) + L" → " + FormatBytes(st.after) + L" (" + pct + L"), "
                  + std::to_wstring(st.recompressed) + L" von " + std::to_wstring(st.images) + L" Bildern neu kodiert");
        break;
    }
    case C_DELETE: {
        if (!g.doc.IsOpen()) break;
        const std::vector<int> t = TargetPages();
        if (!g.doc.DeletePages(t)) { ShowToast(PdfDocument::LastError(), true); break; }
        ClearThumbs();
        ClearSearch();
        g.page = std::clamp(t.front(), 0, g.doc.PageCount() - 1);
        SelectOnly(g.page);
        g.scrollX = g.scrollY = 0;
        LayoutSidebar();
        UpdateCanvas(true);
        EnsureThumbVisible();
        UpdateTitle();
        ShowToast(std::to_wstring(t.size()) + L" Seite(n) gelöscht – noch nicht gespeichert");
        break;
    }
    case C_ZOOMOUT: SetZoom(g.zoom / 1.2, false); break;
    case C_ZOOMIN:  SetZoom(g.zoom * 1.2, false); break;
    case C_ACTUAL:  SetZoom(1.0, false); break;
    case C_FITW:    SetZoom(g.zoom, true); break;
    case C_ROTL: case C_ROTR: {
        if (!g.doc.IsOpen()) break;
        for (int i : TargetPages()) {                 // alle ausgewaehlten Seiten (sonst die aktuelle)
            g.doc.SetRotation(i, g.doc.Rotation(i) + (cmd == C_ROTR ? 1 : 3));
            if (i < static_cast<int>(g.thumbs.size()) && g.thumbs[i]) { DeleteObject(g.thumbs[i]); g.thumbs[i] = nullptr; }
        }
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
        case 'F': RunCommand(C_FIND); return;
        case 'A':
            if (g.doc.IsOpen()) { g.sel.assign(g.doc.PageCount(), 1); InvalidateRect(g.side, nullptr, FALSE); }
            return;
        case VK_OEM_PLUS: case VK_ADD: RunCommand(C_ZOOMIN); return;
        case VK_OEM_MINUS: case VK_SUBTRACT: RunCommand(C_ZOOMOUT); return;
        case '0': RunCommand(C_FITW); return;
        case '1': RunCommand(C_ACTUAL); return;
        }
        return;
    }
    if (!g.doc.IsOpen()) return;
    switch (vk) {
    case VK_ESCAPE:
        if (g.tool) { g.tool = 0; g.selDrag = g.cropDrag = false; InvalidateToolbar(); InvalidateRect(g.canvas, nullptr, FALSE); }
        else CloseSearch();
        break;
    case 'C': RunCommand(C_CROP); break;
    case 'U': RunCommand(C_UNCROP); break;
    case 'H': RunCommand(C_HILITE); break;
    case 'N': RunCommand(C_NOTE); break;
    case VK_F3: SearchStep(shift ? -1 : 1); break;
    case VK_DELETE: RunCommand(C_DELETE); break;
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

// ---------- Formulare: Neuzeichnen, Zwischenablage ----------
void FormRedrawNow() {
    if (!g.doc.IsOpen() || !g.doc.TakeFormDirty()) return;
    if (g.pageBmp) { DeleteObject(g.pageBmp); g.pageBmp = nullptr; g.bmpPage = -1; }
    InvalidateRect(g.canvas, nullptr, FALSE);
}

void RefreshAfterFormEdit() {       // Vorschau der Seite und Titel (Stern) aktualisieren
    if (g.page >= 0 && g.page < static_cast<int>(g.thumbs.size()) && g.thumbs[g.page]) { DeleteObject(g.thumbs[g.page]); g.thumbs[g.page] = nullptr; }
    InvalidateRect(g.side, nullptr, FALSE);
    UpdateTitle();
}

void FormRedrawHook() { if (g.canvas) PostMessageW(g.canvas, WM_APP + 1, 0, 0); }

void ClipboardSet(const std::wstring& text) {
    if (!OpenClipboard(g.main)) return;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (mem) {
        memcpy(GlobalLock(mem), text.c_str(), (text.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(mem);
        if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
    }
    CloseClipboard();
}

std::wstring ClipboardGet() {
    std::wstring out;
    if (!OpenClipboard(g.main)) return out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) { out = p; GlobalUnlock(h); }
    }
    CloseClipboard();
    return out;
}

// Taste an das fokussierte Formularfeld weiterreichen (true = behandelt)
bool FormKeyDown(UINT vk) {
    if (!g.formFocus || !g.doc.IsOpen()) return false;
    const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0, alt = GetKeyState(VK_MENU) < 0;
    if (vk == VK_ESCAPE) { ResetFormFocus(); FormRedrawNow(); RefreshAfterFormEdit(); return true; }
    if (ctrl) {
        if (vk == 'A') { g.doc.FormSelectAll(g.page); FormRedrawNow(); return true; }
        if (vk == 'C' || vk == 'X') {
            const std::wstring t = g.doc.FormSelectedText(g.page);
            if (!t.empty()) { ClipboardSet(t); if (vk == 'X') g.doc.FormReplaceSelection(g.page, L""); }
            FormRedrawNow();
            return true;
        }
        if (vk == 'V') { const std::wstring t = ClipboardGet(); if (!t.empty()) g.doc.FormReplaceSelection(g.page, t); FormRedrawNow(); return true; }
        return false;                                         // andere Strg-Kuerzel (Speichern, Oeffnen ...) normal behandeln
    }
    g.doc.FormKey(g.page, static_cast<int>(vk), true, (shift ? 1 : 0) | (alt ? 4 : 0));
    FormRedrawNow();
    return true;
}

LRESULT CALLBACK CanvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: PaintBuffered(hwnd, PaintCanvas); return 0;
    case WM_SIZE: {
        static bool busy = false;               // SetScrollInfo kann selbst wieder WM_SIZE ausloesen
        if (g.doc.IsOpen() && !busy) { busy = true; UpdateCanvas(g.fitWidth); busy = false; }
        PositionSearchEdit();
        return 0;
    }
    case WM_TIMER:
        if (wp == 1) { KillTimer(hwnd, 1); g.toast.clear(); InvalidateRect(hwnd, nullptr, FALSE); }
        else if (wp == 2) { KillTimer(hwnd, 2); RunSearch(); }       // kurz nach der letzten Eingabe suchen
        else if (wp == 3) { KillTimer(hwnd, 3); FormRedrawNow(); }   // Formular neu zeichnen
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == 100 && HIWORD(wp) == EN_CHANGE) SetTimer(hwnd, 2, 400, nullptr);
        return 0;
    case WM_CTLCOLOREDIT: {                                          // dunkles Suchfeld passend zum Panel
        static HBRUSH br = CreateSolidBrush(kCardRaised);
        SetTextColor(reinterpret_cast<HDC>(wp), kText);
        SetBkColor(reinterpret_cast<HDC>(wp), kCardRaised);
        return reinterpret_cast<LRESULT>(br);
    }
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
    case WM_LBUTTONDOWN: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (g.searchOpen) {
            RECT cr; GetClientRect(hwnd, &cr);
            const SearchLayout l = GetSearchLayout(cr);
            if (PtInRect(&l.prev, p)) { SearchStep(-1); return 0; }
            if (PtInRect(&l.next, p)) { SearchStep(1); return 0; }
            if (PtInRect(&l.close, p)) { CloseSearch(); return 0; }
            if (PtInRect(&l.panel, p)) { SetFocus(g.searchEdit); return 0; }
        }
        SetFocus(hwnd);
        if (!g.tool && g.doc.IsOpen() && g.doc.HasForms()) {            // Formularfelder ausfuellen
            int ox, oy; PageOrigin(ox, oy);
            const int dx = p.x - ox, dy = p.y - oy;
            if (dx >= 0 && dy >= 0 && dx < g.pageW && dy < g.pageH) {
                g.doc.FormMouse(PdfDocument::FormDown, g.page, g.pageW, g.pageH, dx, dy);
                const int type = g.doc.FormFieldAt(g.page, g.pageW, g.pageH, dx, dy);
                g.formFocus = type == 4 || type == 5 || type == 6;       // Kombinationsfeld, Liste, Textfeld
                g.formMouse = true;
                SetCapture(hwnd);
                FormRedrawNow();
            } else ResetFormFocus();
            return 0;
        }
        if (g.tool && g.doc.IsOpen()) {                                  // Marker-, Notiz- bzw. Zuschnitt-Werkzeug
            int ox, oy; PageOrigin(ox, oy);
            const int dx = p.x - ox, dy = p.y - oy;
            if (dx < 0 || dy < 0 || dx >= g.pageW || dy >= g.pageH) return 0;
            if (g.tool == 3) {
                g.cropDrag = true;
                g.cropA = g.cropB = POINT{ p.x, p.y };
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (g.tool == 1) {
                const int c = g.doc.CharAt(g.page, g.pageW, g.pageH, dx, dy);
                if (c >= 0) { g.selDrag = true; g.selPage = g.page; g.selA = g.selB = c; SetCapture(hwnd); InvalidateRect(hwnd, nullptr, FALSE); }
            } else {
                std::wstring text;
                if (PromptText(g.main, L"Notiz hinzufügen", text)) {
                    if (g.doc.AddNote(g.page, g.pageW, g.pageH, dx, dy, text)) { RefreshAfterEdit(g.page); ShowToast(L"Notiz hinzugefügt – noch nicht gespeichert"); }
                    else ShowToast(PdfDocument::LastError(), true);
                }
            }
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g.cropDrag) {
            int ox, oy; PageOrigin(ox, oy);
            g.cropB.x = std::clamp<int>(GET_X_LPARAM(lp), ox, ox + g.pageW);
            g.cropB.y = std::clamp<int>(GET_Y_LPARAM(lp), oy, oy + g.pageH);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (!g.tool && g.doc.IsOpen() && g.doc.HasForms()) {
            int ox, oy; PageOrigin(ox, oy);
            const int dx = GET_X_LPARAM(lp) - ox, dy = GET_Y_LPARAM(lp) - oy;
            if ((dx >= 0 && dy >= 0 && dx < g.pageW && dy < g.pageH) || g.formMouse)
                g.doc.FormMouse(PdfDocument::FormMove, g.page, g.pageW, g.pageH, dx, dy);
            FormRedrawNow();
            return 0;
        }
        if (g.selDrag) {
            int ox, oy; PageOrigin(ox, oy);
            const int c = g.doc.CharAt(g.selPage, g.pageW, g.pageH, GET_X_LPARAM(lp) - ox, GET_Y_LPARAM(lp) - oy);
            if (c >= 0 && c != g.selB) { g.selB = c; InvalidateRect(hwnd, nullptr, FALSE); }
        }
        return 0;
    case WM_LBUTTONUP:
        if (g.formMouse) {
            g.formMouse = false;
            if (GetCapture() == hwnd) ReleaseCapture();
            int ox, oy; PageOrigin(ox, oy);
            g.doc.FormMouse(PdfDocument::FormUp, g.page, g.pageW, g.pageH, GET_X_LPARAM(lp) - ox, GET_Y_LPARAM(lp) - oy);
            FormRedrawNow();
            RefreshAfterFormEdit();
            return 0;
        }
        if (g.cropDrag) {
            g.cropDrag = false;
            if (GetCapture() == hwnd) ReleaseCapture();
            int ox, oy; PageOrigin(ox, oy);
            const double fx0 = double(std::min(g.cropA.x, g.cropB.x) - ox) / g.pageW, fx1 = double(std::max(g.cropA.x, g.cropB.x) - ox) / g.pageW;
            const double fy0 = double(std::min(g.cropA.y, g.cropB.y) - oy) / g.pageH, fy1 = double(std::max(g.cropA.y, g.cropB.y) - oy) / g.pageH;
            if (fx1 - fx0 < 0.02 || fy1 - fy0 < 0.02) { InvalidateRect(hwnd, nullptr, FALSE); return 0; }   // nur ein Klick
            const std::vector<int> t = TargetPages();
            const std::wstring q = L"Auf diesen Ausschnitt zuschneiden?\n\nGilt für " + std::to_wstring(t.size()) + (t.size() == 1 ? L" Seite" : L" Seiten") +
                L" (Auswahl in der Seitenleiste; Strg+A wählt alle). Der Seiteninhalt bleibt in der Datei erhalten, nur der sichtbare Bereich ändert sich – "
                L"mit der Taste U lässt sich der Zuschnitt wieder aufheben.";
            if (MessageBoxW(g.main, q.c_str(), kAppTitle, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                if (g.doc.CropPages(t, fx0, fy0, fx1, fy1)) {
                    g.tool = 0;
                    InvalidateToolbar();
                    RefreshAllPages();
                    ShowToast(L"Zugeschnitten (" + std::to_wstring(t.size()) + L" Seite(n)) – noch nicht gespeichert");
                } else ShowToast(PdfDocument::LastError(), true);
            } else InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (g.selDrag) {
            g.selDrag = false;
            if (GetCapture() == hwnd) ReleaseCapture();
            const int start = std::min(g.selA, g.selB), count = std::abs(g.selB - g.selA) + 1;
            if (g.doc.AddHighlight(g.selPage, start, count)) { RefreshAfterEdit(g.selPage); ShowToast(L"Markierung hinzugefügt – noch nicht gespeichert"); }
            else { ShowToast(PdfDocument::LastError(), true); InvalidateRect(hwnd, nullptr, FALSE); }
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (g.selDrag && reinterpret_cast<HWND>(lp) != hwnd) { g.selDrag = false; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    case WM_SETCURSOR:
        if (g.tool && LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, g.tool == 1 ? IDC_IBEAM : IDC_CROSS)); return TRUE; }
        if (!g.tool && LOWORD(lp) == HTCLIENT && g.doc.IsOpen() && g.doc.HasForms()) {     // Mauszeiger ueber Formularfeldern
            POINT cp; GetCursorPos(&cp); ScreenToClient(hwnd, &cp);
            int ox, oy; PageOrigin(ox, oy);
            const int dx = cp.x - ox, dy = cp.y - oy;
            if (dx >= 0 && dy >= 0 && dx < g.pageW && dy < g.pageH) {
                const int type = g.doc.FormFieldAt(g.page, g.pageW, g.pageH, dx, dy);
                if (type >= 0) { SetCursor(LoadCursorW(nullptr, (type == 6 || type == 4) ? IDC_IBEAM : IDC_HAND)); return TRUE; }
            }
        }
        break;
    case WM_APP + 1:                                     // PDFium meldet: Formular neu zeichnen (gebuendelt)
        SetTimer(hwnd, 3, 16, nullptr);
        return 0;
    case WM_KEYDOWN:
        if (FormKeyDown(static_cast<UINT>(wp))) return 0;
        HandleKey(static_cast<UINT>(wp));
        return 0;
    case WM_KEYUP:
        if (g.formFocus && g.doc.IsOpen() && GetKeyState(VK_CONTROL) >= 0) g.doc.FormKey(g.page, static_cast<int>(wp), false, 0);
        return 0;
    case WM_CHAR:
        if (g.formFocus && g.doc.IsOpen() && GetKeyState(VK_CONTROL) >= 0 && (wp >= 32 || wp == 8 || wp == 13)) {
            g.doc.FormChar(g.page, static_cast<int>(wp), 0);
            FormRedrawNow();
        }
        return 0;
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void RefreshAfterEdit(int page);

// Einfuegeposition (0..n) fuer das Verschieben: vor der Vorschau, deren obere Haelfte getroffen wird
int InsertSlot(int y) {
    const int n = g.doc.PageCount();
    const int yy = y + g.sideScroll;
    for (int i = 0; i < n && i + 1 < static_cast<int>(g.thumbTop.size()); ++i)
        if (yy < (g.thumbTop[i] + g.thumbTop[i + 1]) / 2) return i;
    return n;
}

void FinishDrag(bool apply) {
    const bool wasDragging = g.dragging;
    g.dragging = g.dragPending = false;
    KillTimer(g.side, 1);
    if (GetCapture() == g.side) ReleaseCapture();
    if (!apply || !wasDragging) { InvalidateRect(g.side, nullptr, FALSE); return; }
    std::vector<int> no;                                        // alt -> neu
    if (!g.doc.MovePages(g.dragPages, g.dragInsert, &no)) { ShowToast(PdfDocument::LastError(), true); InvalidateRect(g.side, nullptr, FALSE); return; }
    if (no.empty()) { InvalidateRect(g.side, nullptr, FALSE); return; }   // Reihenfolge unveraendert
    const int n = g.doc.PageCount();
    g.sel.assign(n, 0);
    for (int i : g.dragPages) if (i < static_cast<int>(no.size())) g.sel[no[i]] = 1;
    g.selAnchor = g.dragHit >= 0 && g.dragHit < static_cast<int>(no.size()) ? no[g.dragHit] : 0;
    g.page = no[std::clamp(g.page, 0, static_cast<int>(no.size()) - 1)];
    ClearThumbs();
    ClearSearch();
    LayoutSidebar();
    UpdateCanvas(true);
    EnsureThumbVisible();
    UpdateTitle();
    ShowToast(std::to_wstring(g.dragPages.size()) + L" Seite(n) verschoben – noch nicht gespeichert");
}

// Index der Seitenvorschau an der Client-Position y der Seitenleiste (-1 = keine)
int ThumbAt(int y) {
    for (int i = 0; i < g.doc.PageCount() && i + 1 < static_cast<int>(g.thumbTop.size()); ++i)
        if (y + g.sideScroll >= g.thumbTop[i] - S(6) && y + g.sideScroll < g.thumbTop[i + 1] - S(6)) return i;
    return -1;
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
        if (g.dragPending) {                                            // Verschieben starten, sobald die Maus sich merklich bewegt hat
            const int y = GET_Y_LPARAM(lp);
            if (!g.dragging && std::abs(y - g.dragStartY) > S(6)) {
                g.dragging = true;
                g.dragPages = TargetPages();
                if (std::find(g.dragPages.begin(), g.dragPages.end(), g.dragHit) == g.dragPages.end()) g.dragPages = { g.dragHit };
                SetTimer(hwnd, 1, 40, nullptr);                         // Autoscroll am Rand
            }
            if (g.dragging) { g.dragInsert = InsertSlot(y); InvalidateRect(hwnd, nullptr, FALSE); }
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == 1 && g.dragging) {
            POINT cur{}; GetCursorPos(&cur); ScreenToClient(hwnd, &cur);
            RECT cr; GetClientRect(hwnd, &cr);
            const int edge = S(36), step = S(22);
            int d = 0;
            if (cur.y < edge) d = -step; else if (cur.y > cr.bottom - edge) d = step;
            if (d) {
                g.sideScroll = std::clamp(g.sideScroll + d, 0, Max(0, g.sideContentH - cr.bottom));
                SideScrollbar();
            }
            g.dragInsert = InsertSlot(cur.y);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g.dragPending) {
            const bool wasDragging = g.dragging;
            const bool defer = g.dragDeferClick;
            const int hit = g.dragHit;
            FinishDrag(wasDragging);
            if (!wasDragging && defer && hit >= 0) {                    // einfacher Klick auf eine schon ausgewaehlte Seite
                SelectOnly(hit);
                g.selKeep = true; GoToPage(hit); g.selKeep = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (g.dragPending && reinterpret_cast<HWND>(lp) != hwnd) { g.dragging = g.dragPending = false; KillTimer(hwnd, 1); InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
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
            const int hit = ThumbAt(y);
            if (hit >= 0) {
                const int n = g.doc.PageCount();
                if (GetKeyState(VK_SHIFT) < 0) {                       // Bereich vom Ankerpunkt bis hierher
                    const int a = std::min(g.selAnchor, hit), b = std::max(g.selAnchor, hit);
                    g.sel.assign(n, 0);
                    for (int i = std::max(0, a); i <= b && i < n; ++i) g.sel[i] = 1;
                } else if (GetKeyState(VK_CONTROL) < 0) {              // einzelne Seite an-/abwaehlen
                    g.sel[hit] = !g.sel[hit];
                    g.selAnchor = hit;
                    if (std::find(g.sel.begin(), g.sel.end(), 1) == g.sel.end()) g.sel[hit] = 1;
                } else {
                    // Einfacher Klick: auf einer schon ausgewaehlten Seite bleibt die Auswahl bestehen (so laesst sich die ganze Auswahl ziehen)
                    // und wird erst beim Loslassen auf diese Seite beschraenkt.
                    g.dragDeferClick = g.sel[hit] != 0;
                    g.dragHit = hit; g.dragStartY = y; g.dragPending = true; g.dragging = false;
                    SetCapture(hwnd);
                    if (g.dragDeferClick) { InvalidateRect(hwnd, nullptr, FALSE); return 0; }
                    SelectOnly(hit);
                }
                g.selKeep = true;
                GoToPage(hit);
                g.selKeep = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    }
    case WM_RBUTTONUP: {                                               // Kontextmenue auf einer Vorschau
        if (g.sideTab != 0 || !g.doc.IsOpen()) return 0;
        const int hit = ThumbAt(GET_Y_LPARAM(lp));
        if (hit < 0) return 0;
        if (!g.sel[hit]) { SelectOnly(hit); g.selKeep = false; GoToPage(hit); }
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1, L"Nach links drehen");
        AppendMenuW(menu, MF_STRING, 2, L"Nach rechts drehen");
        AppendMenuW(menu, MF_STRING, 3, L"Seite(n) löschen");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, 4, L"Alle auswählen");
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ClientToScreen(hwnd, &pt);
        const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(menu);
        if (cmd == 1) RunCommand(C_ROTL);
        else if (cmd == 2) RunCommand(C_ROTR);
        else if (cmd == 3) RunCommand(C_DELETE);
        else if (cmd == 4) { g.sel.assign(g.doc.PageCount(), 1); InvalidateRect(hwnd, nullptr, FALSE); }
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
        MoveWindow(g.side, 0, top + TabHeaderH(), sw, Max(0, cr.bottom - top - TabHeaderH() - FooterH()), TRUE);
    }
    if (g.canvas) MoveWindow(g.canvas, sw, top, Max(0, cr.right - sw), Max(0, cr.bottom - top - FooterH()), TRUE);
    g.aboutRc = RECT{ S(12), cr.bottom - FooterH() + S(4), S(12) + S(210), cr.bottom - S(4) };
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
    {   // Fusszeile: Urheber und Version, Klick oeffnet "Ueber"
        RECT strip{ 0, cr.bottom - FooterH(), cr.right, cr.bottom };
        HBRUSH fb = CreateSolidBrush(kBackground);
        FillRect(dc, &strip, fb);
        DeleteObject(fb);
        HPEN fp = CreatePen(PS_SOLID, 1, kBorder);
        HGDIOBJ fo = SelectObject(dc, fp);
        MoveToEx(dc, 0, strip.top, nullptr); LineTo(dc, cr.right, strip.top);
        SelectObject(dc, fo); DeleteObject(fp);
        DrawTextC(dc, g.fontSmall, std::wstring(L"© 2026 Duncan · v") + APP_VERSION, g.aboutRc, g.aboutHot ? kText : kTextDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    if (g.sideVisible) {
        RECT col{ 0, ToolbarH(), SideW(), cr.bottom - FooterH() };
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
        MoveToEx(dc, SideW() - 1, ToolbarH(), nullptr); LineTo(dc, SideW() - 1, cr.bottom - FooterH());
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
        if (HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {   // dunkle Kontextmenues (undokumentierte Ordinale 135/136)
            using SetModeFn = int(WINAPI*)(int);
            using FlushFn = void(WINAPI*)();
            auto setMode = reinterpret_cast<SetModeFn>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(135))));
            auto flush = reinterpret_cast<FlushFn>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(136))));
            if (setMode) setMode(2);
            if (flush) flush();
        }
        MakeFonts();
        BuildItems();
        g.side = CreateWindowExW(0, L"DuncansPdfSide", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL, 0, 0, 10, 10, hwnd, nullptr, g.inst, nullptr);
        g.canvas = CreateWindowExW(0, L"DuncansPdfCanvas", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_CLIPCHILDREN, 0, 0, 10, 10, hwnd, nullptr, g.inst, nullptr);
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
        mm->ptMinTrackSize = { S(1000), S(480) };   // so breit, dass die Werkzeugleiste ohne Ueberlappung passt
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
        const bool about = PtInRect(&g.aboutRc, p) != FALSE;
        if (about != g.aboutHot) { g.aboutHot = about; InvalidateRect(hwnd, &g.aboutRc, FALSE); }
        return 0;
    }
    case WM_SETCURSOR:
        if (g.aboutHot && LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
        break;
    case WM_MOUSELEAVE:
        g.trackingMain = false;
        if (g.hot != -1) { g.hot = -1; InvalidateToolbar(); }
        if (g.aboutHot) { g.aboutHot = false; InvalidateRect(hwnd, &g.aboutRc, FALSE); }
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
        if (PtInRect(&g.aboutRc, p)) { ShowAbout(); return 0; }
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
            MessageBoxW(hwnd, L"Es gibt ungespeicherte Änderungen (gedrehte, gelöschte oder angehängte Seiten).\nTrotzdem schließen?",
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
    PdfDocument::SetFormRedrawHook(&FormRedrawHook);
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
    reg(L"DuncansPdfPrompt", PromptProc, nullptr, nullptr);

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
