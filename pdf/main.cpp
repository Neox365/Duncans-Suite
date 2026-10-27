// Duncan's PDF - nativer PDF-Betrachter (Win32 + GDI/GDI+, PDFium zur Laufzeit geladen).
// Aufbau: Hauptfenster (Werkzeugleiste + Seitenleisten-Kopf) mit zwei Kindfenstern:
//   Seitenleiste (Thumbnails / Werkzeuge) und Canvas (aktuelle Seite, scrollbar, zoombar).
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
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
#include "../common/Theme.h"
#include "../common/Gfx.h"

#ifndef APP_VERSION
#define APP_VERSION L"1.2.0"   // normalerweise aus CMake (PROJECT_VERSION)
#endif

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#endif


namespace {

// ---- Farben (wie Duncan's Suite) ----
using namespace theme;   // gemeinsame Palette aus common/ (wie in allen Duncan's-Programmen)
const COLORREF kPanel      = RGB(22, 24, 29);
const COLORREF kCanvas     = RGB(28, 30, 36);

const wchar_t* kAppTitle = L"Duncan's PDF";

enum Cmd {
    C_NONE = 0, C_SIDE, C_OPEN, C_SAVE, C_MERGE, C_ZOOMOUT, C_ZOOMIN, C_ACTUAL, C_FITW,
    C_ROTL, C_ROTR, C_PREV, C_NEXT, C_FIND, C_DELETE, C_HILITE, C_NOTE, C_COMPRESS, C_CROP, C_UNCROP,
    C_COL0, C_COL1, C_COL2, C_COL3, C_COL4, C_COL5, C_COLMORE, C_SETTINGS, C_ERASE, C_STAMP, C_REDACT, C_SPLIT, C_EXTRACT, C_MORE   // Markerfarben: 5 Voreinstellungen, eigene Farbe, "Weitere Farben ..."
};

struct Item {
    int cmd = C_NONE;          // C_NONE = Trenner
    std::wstring label;
    int minWidth = 0;          // Basisbreite (unskaliert)
    bool needsDoc = true;
    bool right = false;        // rechts ausgerichtet (Seitenwechsel)
    bool isPageLabel = false;
    int prio = 0;              // > 0: bei schmalem Fenster in das Menue "..." verlagern (hoechster Wert zuerst)
    bool hidden = false;       // derzeit im Menue "..." statt in der Leiste
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

    // Anmerkungen: 0 = kein Werkzeug, 1 = Text markieren, 2 = Notiz setzen, 3 = Zuschnitt aufziehen, 4 = Radierer (Markierung entfernen), 5 = Stempel/Unterschrift setzen, 6 = Schwärzen
    int tool = 0;
    bool selDrag = false;                  // Textauswahl beim Markieren laeuft
    bool eraseDrag = false;                // Radierer: Maustaste gedrueckt, Markierungen werden entfernt
    int selPage = -1, selA = -1, selB = -1;
    bool cropDrag = false;                 // Zuschnitt-Rahmen wird aufgezogen
    POINT cropA{}, cropB{};                // Eckpunkte in Canvas-Koordinaten (auch fuer den Schwaerzen-Rahmen)
    bool redDrag = false;                  // Schwaerzen-Rahmen wird aufgezogen

    // Stempel / Unterschrift: das Bild folgt der Maus, ein Klick setzt es auf die Seite
    PdfDocument::Image stampImg;
    Gdiplus::Bitmap* stampGdi = nullptr;   // Sicht auf stampImg.bgra fuer die Vorschau
    double stampFrac = 0.30;               // Breite des Stempels als Anteil der Seitenbreite
    POINT stampPos{};
    bool stampHover = false, stampTrack = false;

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

    // Markerfarbe (Palette in der Werkzeugleiste)
    COLORREF hlColor = RGB(255, 255, 0);   // aktuelle Farbe
    int hlSel = 0;                         // 0-4 Voreinstellung, 5 = eigene Farbe
    COLORREF customColor = RGB(180, 180, 180);
    bool customSet = false;
    COLORREF custColors[16] = {};          // Palette des Windows-Farbdialogs
} g;

inline int Max(long a, long b) { return static_cast<int>(a > b ? a : b); }
inline int Min(long a, long b) { return static_cast<int>(a < b ? a : b); }
int S(int v) { return MulDiv(v, g.dpi, 96); }
int ToolbarH() { return S(56); }
int TabHeaderH() { return S(38); }
int SideW() { return g.sideVisible ? S(250) : 0; }
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
void ShowSettings();

void InvalidateToolbar() {
    RECT r{ 0, 0, 100000, ToolbarH() };
    InvalidateRect(g.main, &r, FALSE);
}

bool ItemEnabled(const Item& it) { return !it.needsDoc || g.doc.IsOpen(); }

// ---------- Werkzeugleiste ----------
void BuildItems() {
    auto add = [](int cmd, const wchar_t* label, int minW, bool needsDoc, bool right = false, bool pageLabel = false, int prio = 0) {
        Item it; it.cmd = cmd; it.label = label; it.minWidth = minW; it.needsDoc = needsDoc; it.right = right; it.isPageLabel = pageLabel; it.prio = prio;
        g.items.push_back(it);
    };
    add(C_SIDE, L"☰", 40, false);
    add(C_NONE, L"", 0, false);
    add(C_OPEN, L"Öffnen", 0, false);
    add(C_SAVE, L"Speichern", 0, true);
    add(C_MERGE, L"Merge", 0, true, false, false, 3);
    add(C_SPLIT, L"Split", 0, true);
    add(C_FIND, L"Suchen", 0, true, false, false, 1);
    add(C_HILITE, L"Marker", 0, true, false, false, 2);
    add(C_ERASE, L"⌫", 40, true, false, false, 5);   // Radierer: entfernt Markierungen
    add(C_NOTE, L"Notiz", 0, true, false, false, 6);
    add(C_CROP, L"Zuschnitt", 0, true, false, false, 4);
    add(C_STAMP, L"Stempel", 0, true);
    add(C_REDACT, L"Schwärzen", 0, true);
    add(C_NONE, L"", 0, false);
    add(C_ZOOMOUT, L"−", 40, true);
    add(C_ACTUAL, L"100 %", 64, true);
    add(C_ZOOMIN, L"+", 40, true);
    add(C_FITW, L"Breite", 0, true, false, false, 7);
    add(C_NONE, L"", 0, false);
    add(C_ROTL, L"↺", 40, true, false, false, 9);
    add(C_ROTR, L"↻", 40, true, false, false, 8);
    add(C_MORE, L"⋯", 40, false, true);   // nimmt bei schmalem Fenster die ausgeblendeten Knoepfe auf
    add(C_PREV, L"‹", 40, true, true);
    add(C_NONE, L"1 / 1", 84, true, true, true);   // Seitenanzeige (nicht klickbar)
    add(C_NEXT, L"›", 40, true, true);
    add(C_SETTINGS, L"⚙", 44, false, true);   // Zahnrad ganz rechts: Einstellungen
}

const COLORREF kPresetColors[5] = { RGB(255, 255, 0), RGB(0, 255, 0), RGB(0, 255, 255), RGB(255, 0, 255), RGB(255, 165, 0) };
COLORREF SwatchColor(int idx) { return idx < 5 ? kPresetColors[idx] : g.customColor; }

std::wstring SettingsPath() {
    wchar_t base[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH)) return L"";
    const std::wstring dir = std::wstring(base) + L"\\DuncansPdf";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.ini";
}

void LoadMarkerColor() {
    const std::wstring ini = SettingsPath();
    if (ini.empty()) return;
    g.customSet = GetPrivateProfileIntW(L"Marker", L"EigeneFarbeGesetzt", 0, ini.c_str()) != 0;
    g.customColor = static_cast<COLORREF>(GetPrivateProfileIntW(L"Marker", L"EigeneFarbe", static_cast<INT>(g.customColor), ini.c_str())) & 0xFFFFFF;
    const int sel = GetPrivateProfileIntW(L"Marker", L"Auswahl", 0, ini.c_str());
    g.hlSel = (sel >= 0 && sel <= 5 && (sel < 5 || g.customSet)) ? sel : 0;
    g.hlColor = SwatchColor(g.hlSel);
}

void SaveMarkerColor() {
    const std::wstring ini = SettingsPath();
    if (ini.empty()) return;
    WritePrivateProfileStringW(L"Marker", L"Auswahl", std::to_wstring(g.hlSel).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Marker", L"EigeneFarbe", std::to_wstring(static_cast<unsigned long>(g.customColor)).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Marker", L"EigeneFarbeGesetzt", g.customSet ? L"1" : L"0", ini.c_str());
}

// Allgemeine Einstellungen (Dialog hinter dem Zahnrad), gespeichert in settings.ini [Allgemein]
constexpr int kNHot = 11;   // Anzahl der frei belegbaren Tastenkuerzel
struct Prefs {
    bool singleKeys = true;     // Einzelbuchstaben-Hotkeys (H, N, C, U, R)
    int zoomMode = 0;           // 0 Seitenbreite, 1 Originalgroesse, 2 letzter Zoom
    bool bakeRot = true;        // Drehung beim Speichern in die Datei schreiben
    bool lastFit = true;
    double lastZoom = 1.0;
    // Belegung der aenderbaren Tastenkuerzel (Reihenfolge = Anzeige im Einstellungsdialog)
    struct Bind { UINT vk; bool ctrl; };
    Bind bind[kNHot]{};
} cfg;

struct HotkeyDef { int cmd; const wchar_t* label; UINT vk; bool ctrl; };
const HotkeyDef kHot[kNHot] = {
    { C_HILITE, L"Text markieren", 'H', false }, { C_NOTE, L"Notiz hinzufügen", 'N', false },
    { C_CROP, L"Zuschnitt aufziehen", 'C', false }, { C_UNCROP, L"Zuschnitt zurücksetzen", 'U', false },
    { C_ROTR, L"Seite drehen (+Umschalt: links)", 'R', false },
    { C_ERASE, L"Markierung entfernen", 'E', false },
    { C_OPEN, L"Öffnen", 'O', true }, { C_SAVE, L"Speichern", 'S', true },
    { C_FIND, L"Suchen", 'F', true }, { C_DELETE, L"Seite löschen", VK_DELETE, false }, { C_SIDE, L"Seitenleiste", 'B', true },
};

void ResetBindings() { for (int i = 0; i < kNHot; ++i) cfg.bind[i] = { kHot[i].vk, kHot[i].ctrl }; }

// Einzelbuchstaben-/Ziffern-Kuerzel ohne Strg: nur aktiv, wenn "Einzelbuchstaben-Hotkeys" an ist
bool IsSingleKey(const Prefs::Bind& b) { return !b.ctrl && ((b.vk >= 'A' && b.vk <= 'Z') || (b.vk >= '0' && b.vk <= '9')); }

// Index der Aktion zu dieser Taste (-1 = keine)
int FindBinding(UINT vk, bool ctrl) {
    for (int i = 0; i < kNHot; ++i) if (cfg.bind[i].vk == vk && cfg.bind[i].ctrl == ctrl) return i;
    return -1;
}

std::wstring KeyName(const Prefs::Bind& b) {
    std::wstring n;
    switch (b.vk) {
    case 0: return L"–";
    case VK_DELETE: n = L"Entf"; break;
    case VK_BACK: n = L"Rück"; break;
    case VK_INSERT: n = L"Einfg"; break;
    case VK_HOME: n = L"Pos1"; break;
    case VK_END: n = L"Ende"; break;
    case VK_PRIOR: n = L"Bild ↑"; break;
    case VK_NEXT: n = L"Bild ↓"; break;
    case VK_SPACE: n = L"Leertaste"; break;
    case VK_TAB: n = L"Tab"; break;
    default:
        if ((b.vk >= 'A' && b.vk <= 'Z') || (b.vk >= '0' && b.vk <= '9')) n = std::wstring(1, static_cast<wchar_t>(b.vk));
        else if (b.vk >= VK_F1 && b.vk <= VK_F12) n = L"F" + std::to_wstring(b.vk - VK_F1 + 1);
        else {
            wchar_t buf[32] = {};
            if (GetKeyNameTextW(static_cast<LONG>(MapVirtualKeyW(b.vk, MAPVK_VK_TO_VSC) << 16), buf, 32) > 0) n = buf; else n = L"?";
        }
    }
    return (b.ctrl ? L"Strg+" : L"") + n;
}

void LoadPrefs() {
    ResetBindings();
    const std::wstring ini = SettingsPath();
    if (!ini.empty()) {
        cfg.singleKeys = GetPrivateProfileIntW(L"Allgemein", L"EinzelbuchstabenHotkeys", 1, ini.c_str()) != 0;
        cfg.zoomMode = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Allgemein", L"StandardZoom", 0, ini.c_str())), 0, 2);
        cfg.bakeRot = GetPrivateProfileIntW(L"Allgemein", L"DrehungSpeichern", 1, ini.c_str()) != 0;
        cfg.lastFit = GetPrivateProfileIntW(L"Allgemein", L"LetzterZoomBreite", 1, ini.c_str()) != 0;
        cfg.lastZoom = std::clamp(GetPrivateProfileIntW(L"Allgemein", L"LetzterZoom", 1000, ini.c_str()) / 1000.0, 0.1, 8.0);
        for (int i = 0; i < kNHot; ++i) {                                          // eigene Tastenbelegung: "Aktion<i>=<vk>,<strg>"
            wchar_t key[24], val[32] = {};
            swprintf(key, 24, L"Aktion%d", i);
            GetPrivateProfileStringW(L"Hotkeys", key, L"", val, 32, ini.c_str());
            unsigned vk = 0, c = 0;
            if (swscanf(val, L"%u,%u", &vk, &c) == 2 && vk > 0 && vk < 256) cfg.bind[i] = { vk, c != 0 };
        }
        for (int i = 0; i < kNHot; ++i)                                            // doppelte Belegung (z. B. von Hand bearbeitet): Standard
            for (int j = i + 1; j < kNHot; ++j)
                if (cfg.bind[i].vk == cfg.bind[j].vk && cfg.bind[i].ctrl == cfg.bind[j].ctrl) { ResetBindings(); i = j = kNHot; }
    }
    PdfDocument::SetBakeRotations(cfg.bakeRot);
}

void SavePrefs() {
    const std::wstring ini = SettingsPath();
    if (ini.empty()) return;
    auto put = [&](const wchar_t* key, long v) { WritePrivateProfileStringW(L"Allgemein", key, std::to_wstring(v).c_str(), ini.c_str()); };
    put(L"EinzelbuchstabenHotkeys", cfg.singleKeys ? 1 : 0);
    put(L"StandardZoom", cfg.zoomMode);
    put(L"DrehungSpeichern", cfg.bakeRot ? 1 : 0);
    put(L"LetzterZoomBreite", cfg.lastFit ? 1 : 0);
    put(L"LetzterZoom", std::lround(cfg.lastZoom * 1000));
    for (int i = 0; i < kNHot; ++i) {
        wchar_t key[24];
        swprintf(key, 24, L"Aktion%d", i);
        const std::wstring v = std::to_wstring(cfg.bind[i].vk) + L"," + (cfg.bind[i].ctrl ? L"1" : L"0");
        WritePrivateProfileStringW(L"Hotkeys", key, v.c_str(), ini.c_str());
    }
}

// Windows-Standard-Farbdialog; die gewaehlte Farbe wird zur eigenen Markerfarbe (Palettenplatz 6) und bleibt aktiv
bool PickCustomColor() {
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = g.main;
    cc.rgbResult = g.customColor;
    cc.lpCustColors = g.custColors;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;
    if (!ChooseColorW(&cc)) return false;
    g.customColor = cc.rgbResult;
    g.customSet = true;
    g.hlSel = 5;
    g.hlColor = g.customColor;
    SaveMarkerColor();
    return true;
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
    // Reicht die Breite nicht, wandern die unwichtigsten Knoepfe (hoechste prio zuerst) in das Menue "⋯"
    int moreIdx = -1;
    for (size_t i = 0; i < g.items.size(); ++i) { g.items[i].hidden = false; if (g.items[i].cmd == C_MORE) moreIdx = static_cast<int>(i); }
    bool anyHidden = false;
    auto need = [&]() {
        int total = 2 * pad;
        for (size_t i = 0; i < g.items.size(); ++i) {
            const Item& it = g.items[i];
            if (it.hidden || (static_cast<int>(i) == moreIdx && !anyHidden)) continue;
            total += width(it) + (it.cmd == C_NONE ? 0 : gap);
        }
        return total;
    };
    while (need() > cr.right) {
        int best = -1;
        for (size_t i = 0; i < g.items.size(); ++i)
            if (!g.items[i].hidden && g.items[i].prio > 0 && (best < 0 || g.items[i].prio > g.items[best].prio)) best = static_cast<int>(i);
        if (best < 0) break;
        g.items[best].hidden = true;
        anyHidden = true;
    }
    if (moreIdx >= 0 && !anyHidden) g.items[moreIdx].hidden = true;
    for (size_t i = 0; i < g.items.size(); ++i) {                       // Trenner ohne Nachbarn (am Rand oder doppelt) ausblenden
        Item& it = g.items[i];
        if (!(it.cmd == C_NONE && it.label.empty()) || it.hidden || it.right) continue;
        int prev = -1, next = -1;
        for (int j = static_cast<int>(i) - 1; j >= 0 && prev < 0; --j) if (!g.items[j].hidden && !g.items[j].right) prev = j;
        for (size_t j = i + 1; j < g.items.size() && next < 0; ++j) if (!g.items[j].hidden && !g.items[j].right) next = static_cast<int>(j);
        const bool prevSep = prev >= 0 && g.items[prev].cmd == C_NONE && g.items[prev].label.empty();
        if (prev < 0 || next < 0 || prevSep) it.hidden = true;
    }
    int rx = cr.right - pad;
    for (Item& it : g.items) {
        if (it.hidden) continue;
        if (it.right) continue;
        int w = width(it);
        it.rc = RECT{ x, top, x + w, top + bh };
        x += w + (it.cmd == C_NONE ? 0 : gap);
    }
    for (auto i = g.items.rbegin(); i != g.items.rend(); ++i) {
        if (!i->right || i->hidden) continue;
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
        if (it.hidden) continue;
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
        const bool active = (it.cmd == C_SIDE && g.sideVisible) || (it.cmd == C_HILITE && g.tool == 1) || (it.cmd == C_NOTE && g.tool == 2) || (it.cmd == C_CROP && g.tool == 3) || (it.cmd == C_ERASE && g.tool == 4) || (it.cmd == C_STAMP && g.tool == 5) || (it.cmd == C_REDACT && g.tool == 6);
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
        if (!it.hidden && (it.cmd != C_NONE) && PtInRect(&it.rc, p)) return static_cast<int>(i);
    }
    return -1;
}

// ---------- Seitenleiste: Layout & Zeichnen ----------
const struct { int cmd; const wchar_t* label; } kTools[] = {
    { C_ROTL, L"Seite nach links drehen" }, { C_ROTR, L"Seite nach rechts drehen" },
    { C_DELETE, L"Seite(n) löschen" }, { C_FIND, L"Im Dokument suchen" },
    { C_HILITE, L"Text markieren" }, { C_ERASE, L"Markierung entfernen" }, { C_NOTE, L"Notiz hinzufügen" }, { C_COMPRESS, L"Datei verkleinern …" },
    { C_MERGE, L"PDF anhängen (Merge)" }, { C_SPLIT, L"PDF teilen (Split) …" }, { C_EXTRACT, L"Gewählte Seiten als neue PDF …" },
    { C_STAMP, L"Stempel & Unterschrift …" }, { C_REDACT, L"Schwärzen (dauerhaft)" }, { C_SAVE, L"Speichern unter …" },
    { C_FITW, L"Auf Seitenbreite einpassen" }, { C_ACTUAL, L"Originalgröße (100 %)" },
};
int ToolRowH() { return S(38); }

// Zeile "Text markieren": rechts daneben sitzt der Farbknopf (oeffnet die Farbpalette)
int HiliteRow() {
    for (int i = 0; i < static_cast<int>(sizeof(kTools) / sizeof(kTools[0])); ++i) if (kTools[i].cmd == C_HILITE) return i;
    return -1;
}
RECT ToolRowRect(int i, const RECT& cr) {
    const int y0 = S(12) + i * ToolRowH() - g.sideScroll;
    RECT r{ S(12), y0, cr.right - S(12), y0 + ToolRowH() - S(6) };
    if (i == HiliteRow()) r.right -= S(46);
    return r;
}
RECT ColorBtnRect(const RECT& cr) {
    const int i = HiliteRow();
    const int top = S(12) + i * ToolRowH() - g.sideScroll;
    return RECT{ cr.right - S(12) - S(38), top, cr.right - S(12), top + ToolRowH() - S(6) };
}
constexpr int kColorBtnHot = 1000;   // toolHot-Wert fuer den Farbknopf

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

// Hoehe des Inhalts der Seitenleiste: Vorschauen bzw. die Liste der Werkzeuge (scrollt bei niedrigem Fenster)
int SideContentH() { return g.sideTab == 0 ? g.sideContentH : S(12) + static_cast<int>(sizeof(kTools) / sizeof(kTools[0])) * ToolRowH() + S(6); }

void SideScrollbar() {
    RECT cr; GetClientRect(g.side, &cr);
    SCROLLINFO si{ sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS };
    si.nMin = 0; si.nMax = Max(0, SideContentH() - 1); si.nPage = cr.bottom; si.nPos = g.sideScroll;
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
    g.sideScroll = Max(0, Min(g.sideScroll, SideContentH() - cr.bottom));
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
            RECT r = ToolRowRect(i, cr);
            bool enabled = kTools[i].cmd != C_NONE && g.doc.IsOpen();
            FillRound(dc, r, S(10), i == g.toolHot && enabled ? kCardHover : kCardRaised, i == g.toolHot && enabled ? RGB(96, 104, 126) : kBorder);
            RECT t = r; t.left += S(14); t.right -= S(6);
            DrawTextC(dc, g.font, kTools[i].label, t, enabled ? kText : RGB(95, 100, 112), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        {   // Farbknopf neben "Text markieren": zeigt die aktuelle Markerfarbe, Klick oeffnet die Palette
            const RECT b = ColorBtnRect(cr);
            const bool en = g.doc.IsOpen(), hot = en && g.toolHot == kColorBtnHot;
            FillRound(dc, b, S(10), hot ? kCardHover : kCardRaised, hot ? RGB(96, 104, 126) : kBorder);
            const int cx = (b.left + b.right) / 2, cy = (b.top + b.bottom) / 2 - S(3), d = S(18);
            HBRUSH br = CreateSolidBrush(en ? g.hlColor : RGB(70, 74, 84));
            HPEN pn = CreatePen(PS_SOLID, 1, kText);
            HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pn);
            Ellipse(dc, cx - d / 2, cy - d / 2, cx + d / 2 + 1, cy + d / 2 + 1);
            SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(pn); DeleteObject(br);
            RECT car{ b.left, cy + d / 2, b.right, b.bottom };                      // kleiner Pfeil als Hinweis auf das Aufklappen
            DrawTextC(dc, g.fontSmall, L"▾", car, en ? kTextDim : RGB(95, 100, 112));
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
        if (ctrl && FindBinding(static_cast<UINT>(wp), true) >= 0 && kHot[FindBinding(static_cast<UINT>(wp), true)].cmd == C_FIND) { SendMessageW(h, EM_SETSEL, 0, -1); return 0; }
        if (ctrl && (wp == 'M' || FindBinding(static_cast<UINT>(wp), true) >= 0)) { HandleKey(static_cast<UINT>(wp)); return 0; }
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

// Zielrechteck des Stempels um die Mausposition p (Canvas-Koordinaten), innerhalb der Seite mit Ursprung (ox, oy)
RECT StampRectAt(POINT p, int ox, int oy) {
    const int w = std::max(8, static_cast<int>(std::lround(g.pageW * g.stampFrac)));
    const int h = std::max(8, static_cast<int>(std::lround(double(w) * g.stampImg.h / std::max(1, g.stampImg.w))));
    const int l = std::clamp<int>(p.x - w / 2, ox, std::max(ox, ox + g.pageW - w));
    const int t = std::clamp<int>(p.y - h / 2, oy, std::max(oy, oy + g.pageH - h));
    return RECT{ l, t, l + w, t + h };
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
        if (g.redDrag) {                             // Schwaerzen-Rahmen: dunkle Vorschau des spaeteren Balkens mit rotem Rand
            using namespace Gdiplus;
            Graphics gr(dc);
            gr.SetClip(Rect(x, y, g.pageW, g.pageH));
            const int l = std::min<int>(g.cropA.x, g.cropB.x), r = std::max<int>(g.cropA.x, g.cropB.x);
            const int t = std::min<int>(g.cropA.y, g.cropB.y), b = std::max<int>(g.cropA.y, g.cropB.y);
            SolidBrush fill(Color(170, 0, 0, 0));
            gr.FillRectangle(&fill, Rect(l, t, r - l, b - t));
            Pen frame(Color(255, 230, 60, 60), 2.0f);
            gr.DrawRectangle(&frame, Rect(l, t, r - l, b - t));
        }
        if (g.tool == 5 && g.stampHover && g.stampGdi) {   // Stempel/Unterschrift als Vorschau unter dem Mauszeiger
            using namespace Gdiplus;
            Graphics gr(dc);
            gr.SetClip(Rect(x, y, g.pageW, g.pageH));
            gr.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            const RECT sr = StampRectAt(g.stampPos, x, y);
            ColorMatrix cm = { 1,0,0,0,0,  0,1,0,0,0,  0,0,1,0,0,  0,0,0,0.8f,0,  0,0,0,0,1 };
            ImageAttributes ia;
            ia.SetColorMatrix(&cm);
            gr.DrawImage(g.stampGdi, Rect(sr.left, sr.top, sr.right - sr.left, sr.bottom - sr.top), 0, 0, g.stampImg.w, g.stampImg.h, UnitPixel, &ia);
            Pen outline(Color(160, 64, 156, 255), 1.0f);
            outline.SetDashStyle(DashStyleDash);
            gr.DrawRectangle(&outline, Rect(sr.left, sr.top, sr.right - sr.left - 1, sr.bottom - sr.top - 1));
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
    std::wstring title, text, hint, okLabel;   // hint: Erklaerung ueber dem Eingabefeld; okLabel ersetzt "Hinzufuegen"
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
    {
        const int et = S(62) + (g_prompt->hint.empty() ? 0 : S(126));
        MoveWindow(g_prompt->edit, pad + S(10), et, cr.right - 2 * pad - S(20), std::max(S(30), static_cast<int>(cr.bottom) - et - pad - bh - S(24)), TRUE);
    }
}

LRESULT CALLBACK PromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (!g_prompt) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_CREATE: {
        gfx::EnableDarkTitleBar(hwnd);
        if (g_prompt->about) return 0;
        g_prompt->edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                                         0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(1), g.inst, nullptr);
        SendMessageW(g_prompt->edit, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
        SendMessageW(g_prompt->edit, EM_SETLIMITTEXT, 2000, 0);
        SetWindowTheme(g_prompt->edit, L"DarkMode_Explorer", nullptr);
        if (!g_prompt->text.empty()) {
            SetWindowTextW(g_prompt->edit, g_prompt->text.c_str());
            SendMessageW(g_prompt->edit, EM_SETSEL, 0, -1);
        }
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
        if (!g_prompt->about && !g_prompt->hint.empty()) {
            RECT ht{ card.left + S(16), card.top + S(10), card.right - S(16), card.top + S(122) };
            DrawTextC(mem, g.font, g_prompt->hint, ht, kTextDim, DT_LEFT | DT_TOP | DT_WORDBREAK);
        }
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
        button(g_prompt->btnOk, true, g_prompt->hot == 1, g_prompt->about ? L"Schließen" : (g_prompt->okLabel.empty() ? L"Hinzufügen" : g_prompt->okLabel.c_str()));
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
bool PromptText(HWND owner, const wchar_t* title, std::wstring& text, const wchar_t* aboutBody = nullptr,
                const wchar_t* hint = nullptr, const wchar_t* okLabel = nullptr) {
    PromptState st;
    st.title = title;
    st.text = text;
    if (hint) st.hint = hint;
    if (okLabel) st.okLabel = okLabel;
    if (aboutBody) { st.about = true; st.body = aboutBody; }
    g_prompt = &st;
    RECT cr{ 0, 0, S(460), S(st.hint.empty() ? 300 : 366) };
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


// ---------- Einstellungen (Zahnrad oben rechts) ----------
struct SetState {
    bool done = false, ok = false;
    bool single = true, bake = true;
    int zoom = 0, color = 0;
    int hot = -1;                                   // 1 Hotkeys, 2 Drehung, 10-12 Zoom, 20-25 Farbe, 30 OK, 31 Abbrechen
    RECT card1{}, rcSingle{}, rcZoomRow{}, rcColorRow{}, rcBake{}, rcZoom[3]{}, rcColor[6]{}, rcOk{}, rcCancel{}, rcKey[kNHot]{}, rcReset{};
    Prefs::Bind bind[kNHot]{};                         // Entwurf der Tastenbelegung
    int capture = -1;                               // Zeile, die gerade auf einen Tastendruck wartet
    std::wstring msg;                               // Hinweis zur Tastenbelegung (z. B. bereits belegt)
    int secY[3]{};                                  // Ueberschriften der drei Bereiche
    int height = 0;
} *g_set = nullptr;

constexpr int kSetW = 560;   // Clientbreite (unskaliert)

void SetDoLayout(SetState& s) {
    const int W = S(kSetW), pad = S(20), lab = S(26);
    int y = S(58);
    s.secY[0] = y; y += lab;
    s.card1 = RECT{ pad, y, W - pad, y + S(6 * 24 + 22) }; y = s.card1.bottom + S(8);
    s.rcSingle = RECT{ pad, y, W - pad, y + S(48) }; y = s.rcSingle.bottom + S(18);
    s.secY[1] = y; y += lab;
    s.rcZoomRow = RECT{ pad, y, W - pad, y + S(52) }; y = s.rcZoomRow.bottom + S(8);
    s.rcColorRow = RECT{ pad, y, W - pad, y + S(52) }; y = s.rcColorRow.bottom + S(18);
    s.secY[2] = y; y += lab;
    s.rcBake = RECT{ pad, y, W - pad, y + S(66) }; y = s.rcBake.bottom + S(20);
    const int bh = S(38), bw = S(130);
    s.rcOk = RECT{ W - pad - bw, y, W - pad, y + bh };
    s.rcCancel = RECT{ s.rcOk.left - S(10) - bw, y, s.rcOk.left - S(10), y + bh };
    s.height = y + bh + pad;
    s.rcReset = RECT{ pad, y, pad + S(190), y + bh };
    const int hcol = (s.card1.right - s.card1.left) / 2;      // Tastenfelder der Hotkey-Liste (links 5, rechts 5)
    for (int i = 0; i < kNHot; ++i) {
        const int kx = s.card1.left + (i < 6 ? 0 : hcol) + S(10), ky = s.card1.top + S(11) + (i % 6) * S(24);
        s.rcKey[i] = RECT{ kx, ky + S(1), kx + S(96), ky + S(23) };
    }
    const int cw = S(100), cg = S(6);                // Zoom-Auswahl (3 Felder rechts)
    for (int i = 0; i < 3; ++i) {
        const int right = s.rcZoomRow.right - S(12) - (2 - i) * (cw + cg);
        s.rcZoom[i] = RECT{ right - cw, s.rcZoomRow.top + S(10), right, s.rcZoomRow.bottom - S(10) };
    }
    const int d = S(32), dg = S(10);                 // Farbpunkte
    for (int i = 0; i < 6; ++i) {
        const int right = s.rcColorRow.right - S(16) - (5 - i) * (d + dg);
        const int cy = (s.rcColorRow.top + s.rcColorRow.bottom) / 2;
        s.rcColor[i] = RECT{ right - d, cy - d / 2, right, cy + d / 2 };
    }
}

int SetHit(const SetState& s, POINT p) {
    for (int i = 0; i < kNHot; ++i) if (PtInRect(&s.rcKey[i], p)) return 50 + i;
    if (PtInRect(&s.rcReset, p)) return 40;
    if (PtInRect(&s.rcOk, p)) return 30;
    if (PtInRect(&s.rcCancel, p)) return 31;
    if (PtInRect(&s.rcSingle, p)) return 1;
    if (PtInRect(&s.rcBake, p)) return 2;
    for (int i = 0; i < 3; ++i) if (PtInRect(&s.rcZoom[i], p)) return 10 + i;
    for (int i = 0; i < 6; ++i) if (PtInRect(&s.rcColor[i], p)) return 20 + i;
    return 0;
}

void DrawSwitch(HDC dc, const RECT& row, bool on, bool hot) {
    const int w = S(44), h = S(24);
    RECT sw{ row.right - S(16) - w, (row.top + row.bottom) / 2 - h / 2, row.right - S(16), (row.top + row.bottom) / 2 + h / 2 };
    const COLORREF fill = on ? kAccent : RGB(60, 64, 76);
    FillRound(dc, sw, h / 2, fill, hot && !on ? RGB(110, 118, 140) : fill);
    const int kd = h - S(8), kx = on ? sw.right - S(4) - kd : sw.left + S(4);
    HBRUSH kb = CreateSolidBrush(on ? RGB(8, 20, 36) : RGB(200, 205, 215));
    HPEN np = CreatePen(PS_NULL, 0, 0);
    HGDIOBJ ob = SelectObject(dc, kb), op = SelectObject(dc, np);
    Ellipse(dc, kx, sw.top + S(4), kx + kd + 1, sw.top + S(4) + kd + 1);
    SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(np); DeleteObject(kb);
}

void SetPaint(HWND hwnd) {
    SetState& s = *g_set;
    PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps);
    RECT cr; GetClientRect(hwnd, &cr);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    HBRUSH bg = CreateSolidBrush(kBackground); FillRect(mem, &cr, bg); DeleteObject(bg);
    const int pad = S(20);
    DrawTextC(mem, g.fontBold, L"Einstellungen", RECT{ pad, S(16), cr.right - pad, S(52) }, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    const wchar_t* secs[3] = { L"Tastenkürzel & Hotkeys", L"Standard-Verhalten & Ansicht", L"Performance & Rendering" };
    for (int i = 0; i < 3; ++i) DrawTextC(mem, g.fontSmall, secs[i], RECT{ pad + S(4), s.secY[i], cr.right - pad, s.secY[i] + S(22) }, kTextDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Hotkey-Liste
    FillRound(mem, s.card1, S(10), kCardRaised, kBorder);
    const int colW = (s.card1.right - s.card1.left) / 2;
    for (int i = 0; i < kNHot; ++i) {                  // Taste anklicken, dann die neue Taste druecken
        const RECT& k = s.rcKey[i];
        const bool cap = s.capture == i, hot = s.hot == 50 + i;
        const bool off = IsSingleKey(s.bind[i]) && !s.single;                       // Einzelbuchstabe, aber Einzelbuchstaben-Hotkeys aus
        FillRound(mem, k, S(7), cap ? kAccent : (hot ? kCardHover : RGB(36, 40, 50)), cap ? kAccent : (hot ? RGB(110, 118, 140) : kBorder));
        DrawTextC(mem, g.font, cap ? L"Taste drücken …" : KeyName(s.bind[i]), k, cap ? RGB(8, 20, 36) : (off ? RGB(120, 126, 140) : kAccent));
        const int dl = k.right + S(8), dr = (i < 6 ? s.card1.left + colW : s.card1.right) - S(6);
        DrawTextC(mem, g.fontSmall, kHot[i].label, RECT{ dl, k.top, dr, k.bottom }, off ? RGB(120, 126, 140) : kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    if (s.capture >= 0 || !s.msg.empty())
        DrawTextC(mem, g.fontSmall, !s.msg.empty() ? s.msg : std::wstring(L"Taste (mit oder ohne Strg) drücken – Esc bricht ab"),
                  RECT{ pad + S(200), s.secY[0], cr.right - pad, s.secY[0] + S(22) }, !s.msg.empty() ? RGB(255, 170, 60) : kAccent, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    auto row = [&](const RECT& r, bool hot) { FillRound(mem, r, S(10), hot ? kCardHover : kCardRaised, hot ? RGB(96, 104, 126) : kBorder); };
    row(s.rcSingle, s.hot == 1);
    DrawTextC(mem, g.font, L"Einzelbuchstaben-Hotkeys aktivieren (z. B. H, N)", RECT{ s.rcSingle.left + S(16), s.rcSingle.top, s.rcSingle.right - S(70), s.rcSingle.bottom }, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawSwitch(mem, s.rcSingle, s.single, s.hot == 1);

    // Zoom + Markerfarbe
    FillRound(mem, s.rcZoomRow, S(10), kCardRaised, kBorder);
    DrawTextC(mem, g.font, L"Zoom beim Öffnen", RECT{ s.rcZoomRow.left + S(16), s.rcZoomRow.top, s.rcZoom[0].left - S(8), s.rcZoomRow.bottom }, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    static const wchar_t* zl[3] = { L"Seitenbreite", L"100 %", L"Zuletzt" };
    for (int i = 0; i < 3; ++i) {
        const bool sel = s.zoom == i, hot = s.hot == 10 + i;
        FillRound(mem, s.rcZoom[i], S(8), sel ? kAccent : (hot ? kCardHover : RGB(36, 40, 50)), sel ? kAccent : (hot ? RGB(110, 118, 140) : kBorder));
        DrawTextC(mem, g.font, zl[i], s.rcZoom[i], sel ? RGB(8, 20, 36) : kText);
    }
    FillRound(mem, s.rcColorRow, S(10), kCardRaised, kBorder);
    DrawTextC(mem, g.font, L"Standard-Markerfarbe", RECT{ s.rcColorRow.left + S(16), s.rcColorRow.top, s.rcColor[0].left - S(8), s.rcColorRow.bottom }, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    for (int i = 0; i < 6; ++i) {
        const bool sel = s.color == i, hot = s.hot == 20 + i;
        const RECT& r = s.rcColor[i];
        const int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, d = S(sel ? 30 : (hot ? 28 : 24));
        HBRUSH br = CreateSolidBrush(SwatchColor(i));
        HPEN rp = CreatePen(PS_SOLID, sel ? S(3) : 1, sel ? kText : (hot ? RGB(150, 158, 178) : kBorder));
        HGDIOBJ ob = SelectObject(mem, br), op = SelectObject(mem, rp);
        Ellipse(mem, cx - d / 2, cy - d / 2, cx + d / 2 + 1, cy + d / 2 + 1);
        SelectObject(mem, op); SelectObject(mem, ob); DeleteObject(rp); DeleteObject(br);
        if (i == 5 && !g.customSet) DrawTextC(mem, g.font, L"+", r, RGB(40, 40, 40));
    }

    // Performance
    row(s.rcBake, s.hot == 2);
    DrawTextC(mem, g.font, L"Gedrehte Seiten beim Speichern dauerhaft übernehmen", RECT{ s.rcBake.left + S(16), s.rcBake.top + S(8), s.rcBake.right - S(70), s.rcBake.top + S(34) }, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawTextC(mem, g.fontSmall, L"Aus: Die Drehung gilt nur in der Ansicht, die gespeicherte Datei bleibt unverändert.", RECT{ s.rcBake.left + S(16), s.rcBake.top + S(34), s.rcBake.right - S(70), s.rcBake.bottom - S(6) }, kTextDim, DT_LEFT | DT_TOP | DT_WORDBREAK);
    DrawSwitch(mem, s.rcBake, s.bake, s.hot == 2);

    auto button = [&](const RECT& r, bool primary, bool hot, const wchar_t* label) {
        const COLORREF fill = primary ? (hot ? RGB(96, 176, 255) : kAccent) : (hot ? kCardHover : kCardRaised);
        FillRound(mem, r, S(10), fill, primary ? fill : (hot ? RGB(96, 104, 126) : kBorder));
        DrawTextC(mem, g.font, label, r, primary ? RGB(8, 20, 36) : kText);
    };
    button(s.rcReset, false, s.hot == 40, L"Tasten zurücksetzen");
    button(s.rcCancel, false, s.hot == 31, L"Abbrechen");
    button(s.rcOk, true, s.hot == 30, L"Speichern");
    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (!g_set) return DefWindowProcW(hwnd, msg, wp, lp);
    SetState& s = *g_set;
    switch (msg) {
    case WM_CREATE: { gfx::EnableDarkTitleBar(hwnd); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: SetPaint(hwnd); return 0;
    case WM_MOUSEMOVE: {
        const int h = SetHit(s, POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        if (h != s.hot) { s.hot = h; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    }
    case WM_LBUTTONUP: {
        const int h = SetHit(s, POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        if (h >= 50 && h < 60) { s.capture = (s.capture == h - 50) ? -1 : h - 50; s.msg.clear(); }
        else if (h == 40) { for (int i = 0; i < kNHot; ++i) s.bind[i] = { kHot[i].vk, kHot[i].ctrl }; s.capture = -1; s.msg.clear(); }
        else if (h == 1) s.single = !s.single;
        else if (h == 2) s.bake = !s.bake;
        else if (h >= 10 && h <= 12) s.zoom = h - 10;
        else if (h >= 20 && h <= 25) {
            const int i = h - 20;
            if (i == 5 && (!g.customSet || s.color == 5)) {      // eigene Farbe waehlen (erneuter Klick aendert sie)
                CHOOSECOLORW cc{};
                cc.lStructSize = sizeof(cc); cc.hwndOwner = hwnd; cc.rgbResult = g.customColor;
                cc.lpCustColors = g.custColors; cc.Flags = CC_RGBINIT | CC_FULLOPEN;
                if (ChooseColorW(&cc)) { g.customColor = cc.rgbResult; g.customSet = true; s.color = 5; }
            } else s.color = i;
        }
        else if (h == 30) { s.ok = true; s.done = true; }
        else if (h == 31) s.done = true;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_CLOSE: s.done = true; return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ShowSettings() {
    SetState st;
    st.single = cfg.singleKeys; st.bake = cfg.bakeRot; st.zoom = cfg.zoomMode; st.color = g.hlSel;
    for (int i = 0; i < kNHot; ++i) st.bind[i] = cfg.bind[i];
    g_set = &st;
    SetDoLayout(st);
    RECT cr{ 0, 0, S(kSetW), st.height };
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU, ex = WS_EX_DLGMODALFRAME;
    AdjustWindowRectEx(&cr, style, FALSE, ex);
    RECT orc; GetWindowRect(g.main, &orc);
    const int w = cr.right - cr.left, h = cr.bottom - cr.top;
    HWND hwnd = CreateWindowExW(ex, L"DuncansPdfSettings", L"Einstellungen", style, orc.left + ((orc.right - orc.left) - w) / 2,
                                orc.top + ((orc.bottom - orc.top) - h) / 3, w, h, g.main, nullptr, g.inst, nullptr);
    if (!hwnd) { g_set = nullptr; return; }
    EnableWindow(g.main, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    MSG m;
    while (!st.done) {
        const BOOL r = GetMessageW(&m, nullptr, 0, 0);
        if (r == 0) { PostQuitMessage(static_cast<int>(m.wParam)); break; }
        if (r < 0) break;
        if (m.message == WM_KEYDOWN || m.message == WM_SYSKEYDOWN) {
            if (st.capture >= 0) {                                   // neue Taste fuer die gewaehlte Aktion
                const UINT vk = static_cast<UINT>(m.wParam);
                if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN) continue;   // nur Modifikator
                if (vk == VK_ESCAPE) { st.capture = -1; InvalidateRect(hwnd, nullptr, FALSE); continue; }
                const bool ctrl = GetKeyState(VK_CONTROL) < 0;
                // fest vergebene Tasten der Anwendung
                const bool reserved = vk == VK_RETURN || vk == VK_F3 || vk == VK_PRIOR || vk == VK_NEXT || vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT
                    || (ctrl && (vk == 'A' || vk == 'M' || vk == '0' || vk == '1' || vk == VK_OEM_PLUS || vk == VK_OEM_MINUS || vk == VK_ADD || vk == VK_SUBTRACT));
                int clash = -1;
                for (int i = 0; i < kNHot; ++i) if (i != st.capture && st.bind[i].vk == vk && st.bind[i].ctrl == ctrl) clash = i;
                if (reserved) st.msg = L"Diese Taste ist fest vergeben.";
                else if (clash >= 0) st.msg = std::wstring(L"Bereits belegt: ") + kHot[clash].label;
                else { st.bind[st.capture] = { vk, ctrl }; st.msg.clear(); st.capture = -1; }
                InvalidateRect(hwnd, nullptr, FALSE);
                continue;
            }
            if (m.wParam == VK_ESCAPE) { st.done = true; continue; }
            if (m.wParam == VK_RETURN) { st.ok = true; st.done = true; continue; }
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    EnableWindow(g.main, TRUE);
    DestroyWindow(hwnd);
    SetForegroundWindow(g.main);
    g_set = nullptr;
    if (!st.ok) return;
    cfg.singleKeys = st.single; cfg.bakeRot = st.bake; cfg.zoomMode = st.zoom;
    for (int i = 0; i < kNHot; ++i) cfg.bind[i] = st.bind[i];
    g.hlSel = st.color; g.hlColor = SwatchColor(st.color);
    PdfDocument::SetBakeRotations(cfg.bakeRot);
    SavePrefs();
    SaveMarkerColor();
    InvalidateToolbar();
    InvalidateRect(g.side, nullptr, FALSE);
    ShowToast(L"Einstellungen gespeichert");
}

// ---------- Stempel, Unterschrift, Auszug und Split ----------
bool GetEncoderClsid(const wchar_t* mime, CLSID* out) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (!size) return false;
    std::vector<char> buf(size);
    auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    Gdiplus::GetImageEncoders(num, size, info);
    for (UINT i = 0; i < num; ++i)
        if (wcscmp(info[i].MimeType, mime) == 0) { *out = info[i].Clsid; return true; }
    return false;
}

std::wstring SignaturePath() {
    const std::wstring p = SettingsPath();
    const size_t k = p.find_last_of(L'\\');
    return k == std::wstring::npos ? L"" : p.substr(0, k + 1) + L"signature.png";
}

bool ImageFromBitmap(Gdiplus::Bitmap& bmp, PdfDocument::Image& out) {
    const int w = static_cast<int>(bmp.GetWidth()), h = static_cast<int>(bmp.GetHeight());
    if (w <= 0 || h <= 0) return false;
    Gdiplus::BitmapData bd;
    Gdiplus::Rect r(0, 0, w, h);
    if (bmp.LockBits(&r, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) != Gdiplus::Ok) return false;
    out.w = w; out.h = h;
    out.bgra.resize(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) memcpy(&out.bgra[static_cast<size_t>(y) * w * 4], static_cast<const unsigned char*>(bd.Scan0) + static_cast<ptrdiff_t>(y) * bd.Stride, static_cast<size_t>(w) * 4);
    bmp.UnlockBits(&bd);
    return true;
}

// Schneidet transparente Raender ab (3 Pixel Luft bleiben)
void TrimImage(PdfDocument::Image& im) {
    int x0 = im.w, y0 = im.h, x1 = -1, y1 = -1;
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x)
            if (im.bgra[(static_cast<size_t>(y) * im.w + x) * 4 + 3] > 8) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
    if (x1 < x0 || y1 < y0) { im = PdfDocument::Image{}; return; }
    x0 = std::max(0, x0 - 3); y0 = std::max(0, y0 - 3); x1 = std::min(im.w - 1, x1 + 3); y1 = std::min(im.h - 1, y1 + 3);
    PdfDocument::Image o;
    o.w = x1 - x0 + 1; o.h = y1 - y0 + 1;
    o.bgra.resize(static_cast<size_t>(o.w) * o.h * 4);
    for (int y = 0; y < o.h; ++y) memcpy(&o.bgra[static_cast<size_t>(y) * o.w * 4], &im.bgra[(static_cast<size_t>(y + y0) * im.w + x0) * 4], static_cast<size_t>(o.w) * 4);
    im = std::move(o);
}

std::wstring TodayText() {
    SYSTEMTIME t{};
    GetLocalTime(&t);
    wchar_t b[24];
    swprintf(b, 24, L"%02d.%02d.%04d", t.wDay, t.wMonth, t.wYear);
    return b;
}

// Gummistempel: Doppelrahmen, grosses Wort und Datum, leicht gekippt, auf transparentem Grund
PdfDocument::Image RenderStamp(const std::wstring& word, COLORREF col) {
    using namespace Gdiplus;
    const int W = 800, H = 320;
    Bitmap bmp(W, H, PixelFormat32bppARGB);
    {
        Graphics gr(&bmp);
        gr.SetSmoothingMode(SmoothingModeAntiAlias);
        gr.SetTextRenderingHint(TextRenderingHintAntiAlias);
        gr.Clear(Color(0, 0, 0, 0));
        gr.TranslateTransform(W / 2.0f, H / 2.0f);
        gr.RotateTransform(-6.0f);
        const Color c(235, GetRValue(col), GetGValue(col), GetBValue(col));
        Pen outer(c, 12.0f), inner(c, 4.0f);
        outer.SetLineJoin(LineJoinRound);
        gr.DrawRectangle(&outer, -340.0f, -118.0f, 680.0f, 236.0f);
        gr.DrawRectangle(&inner, -318.0f, -96.0f, 636.0f, 192.0f);
        FontFamily ff(L"Segoe UI");
        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);
        float size = 112.0f;
        RectF box;
        for (; size > 30.0f; size -= 4.0f) {                          // lange Woerter verkleinern, bis sie in den Rahmen passen
            Font f(&ff, size, FontStyleBold, UnitPixel);
            gr.MeasureString(word.c_str(), -1, &f, PointF(0, 0), StringFormat::GenericTypographic(), &box);
            if (box.Width <= 570.0f) break;
        }
        SolidBrush brush(c);
        Font fw(&ff, size, FontStyleBold, UnitPixel), fd(&ff, 36.0f, FontStyleBold, UnitPixel);
        gr.DrawString(word.c_str(), -1, &fw, RectF(-300.0f, -92.0f, 600.0f, 130.0f), &sf, &brush);
        const std::wstring d = TodayText();
        gr.DrawString(d.c_str(), -1, &fd, RectF(-300.0f, 40.0f, 600.0f, 50.0f), &sf, &brush);
    }
    PdfDocument::Image img;
    if (ImageFromBitmap(bmp, img)) TrimImage(img);
    return img;
}

bool SaveSignature(const PdfDocument::Image& im) {
    const std::wstring path = SignaturePath();
    CLSID png;
    if (path.empty() || im.w <= 0 || !GetEncoderClsid(L"image/png", &png)) return false;
    Gdiplus::Bitmap bmp(im.w, im.h, im.w * 4, PixelFormat32bppARGB, const_cast<BYTE*>(im.bgra.data()));
    return bmp.Save(path.c_str(), &png, nullptr) == Gdiplus::Ok;
}

// Bilddatei (PNG, JPG, BMP) als Unterschrift: auf hoechstens 1200 Pixel Breite verkleinern; hat das Bild keine Transparenz, wird der helle Grund durchsichtig
bool LoadSignatureFile(const std::wstring& path, PdfDocument::Image& out) {
    Gdiplus::Bitmap src(path.c_str());
    if (src.GetLastStatus() != Gdiplus::Ok) return false;
    const int w = static_cast<int>(src.GetWidth()), h = static_cast<int>(src.GetHeight());
    if (w <= 0 || h <= 0 || w > 30000 || h > 30000) return false;
    const double sc = w > 1200 ? 1200.0 / w : 1.0;
    const int tw = std::max(1, static_cast<int>(w * sc)), th = std::max(1, static_cast<int>(h * sc));
    Gdiplus::Bitmap dst(tw, th, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics gr(&dst);
        gr.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        gr.Clear(Gdiplus::Color(0, 0, 0, 0));
        gr.DrawImage(&src, 0, 0, tw, th);
    }
    PdfDocument::Image im;
    if (!ImageFromBitmap(dst, im)) return false;
    bool hasAlpha = false;
    for (size_t i = 3; i < im.bgra.size() && !hasAlpha; i += 4) hasAlpha = im.bgra[i] < 250;
    if (!hasAlpha) {
        for (size_t i = 0; i + 3 < im.bgra.size(); i += 4) {
            const int lum = (im.bgra[i + 2] * 30 + im.bgra[i + 1] * 59 + im.bgra[i] * 11) / 100;
            const int a = std::clamp((255 - lum) * 14 / 10, 0, 255);
            im.bgra[i + 3] = static_cast<unsigned char>(a);
            if (!a) im.bgra[i] = im.bgra[i + 1] = im.bgra[i + 2] = 0;
        }
    }
    TrimImage(im);
    if (im.w < 4 || im.h < 4) return false;
    out = std::move(im);
    return true;
}

// ---- Dialog "Unterschrift zeichnen" ----
struct SignState {
    bool done = false, ok = false, drawing = false;
    std::vector<std::vector<POINT>> strokes;
    RECT area{}, btnOk{}, btnCancel{}, btnClear{};
    int hot = 0;                              // 1 = Uebernehmen, 2 = Abbrechen, 3 = Loeschen
    PdfDocument::Image result;
};
SignState* g_sign = nullptr;

void SignLayout(HWND hwnd) {
    RECT cr; GetClientRect(hwnd, &cr);
    const int pad = S(20), bh = S(38), bw = S(130);
    g_sign->btnOk = RECT{ cr.right - pad - bw, cr.bottom - pad - bh, cr.right - pad, cr.bottom - pad };
    g_sign->btnCancel = RECT{ g_sign->btnOk.left - S(10) - bw, g_sign->btnOk.top, g_sign->btnOk.left - S(10), g_sign->btnOk.bottom };
    g_sign->btnClear = RECT{ pad, g_sign->btnOk.top, pad + bw, g_sign->btnOk.bottom };
    g_sign->area = RECT{ pad, S(58), cr.right - pad, cr.bottom - pad - bh - S(16) };
}

// Zeichnet die Striche in eine transparente Grafik (doppelte Aufloesung) und beschneidet sie auf den Inhalt
bool SignRender(PdfDocument::Image& out) {
    LONG minx = LONG_MAX, miny = LONG_MAX, maxx = LONG_MIN, maxy = LONG_MIN;
    for (const auto& st : g_sign->strokes)
        for (const POINT& p : st) { minx = std::min(minx, p.x); maxx = std::max(maxx, p.x); miny = std::min(miny, p.y); maxy = std::max(maxy, p.y); }
    if (minx > maxx) return false;
    const int pad = S(6);
    float k = 2.0f;
    if ((maxx - minx + 2 * pad) * k > 2400.0f) k = 2400.0f / static_cast<float>(maxx - minx + 2 * pad);
    const int w = std::max(16, static_cast<int>((maxx - minx + 2 * pad) * k)), h = std::max(16, static_cast<int>((maxy - miny + 2 * pad) * k));
    Gdiplus::Bitmap bmp(w, h, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics gr(&bmp);
        gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        gr.Clear(Gdiplus::Color(0, 0, 0, 0));
        const float pw = std::max(static_cast<float>(S(3)) * k, w / 100.0f);       // Strichstaerke wächst mit der Unterschrift
        Gdiplus::Pen pen(Gdiplus::Color(255, 18, 28, 105), pw);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        Gdiplus::SolidBrush dot(Gdiplus::Color(255, 18, 28, 105));
        for (const auto& st : g_sign->strokes) {
            std::vector<Gdiplus::PointF> pts;
            for (const POINT& p : st) pts.emplace_back((p.x - minx + pad) * k, (p.y - miny + pad) * k);
            if (pts.size() == 1) gr.FillEllipse(&dot, pts[0].X - pw / 2, pts[0].Y - pw / 2, pw, pw);
            else if (pts.size() > 1) gr.DrawLines(&pen, pts.data(), static_cast<INT>(pts.size()));
        }
    }
    if (!ImageFromBitmap(bmp, out)) return false;
    TrimImage(out);
    return out.w > 4 && out.h > 4;
}

void SignCommit() {
    if (SignRender(g_sign->result)) { g_sign->ok = true; g_sign->done = true; }
}

LRESULT CALLBACK SignProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (!g_sign) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_CREATE: gfx::EnableDarkTitleBar(hwnd); return 0;
    case WM_SIZE: SignLayout(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
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
        RECT t{ S(20), S(14), cr.right - S(20), S(50) };
        DrawTextC(mem, g.fontBold, L"Unterschrift zeichnen", t, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        FillRound(mem, g_sign->area, S(10), RGB(250, 250, 252), kBorder);
        const int baseY = g_sign->area.top + (g_sign->area.bottom - g_sign->area.top) * 7 / 10;
        HPEN lp2 = CreatePen(PS_SOLID, 1, RGB(200, 204, 214));
        HGDIOBJ op = SelectObject(mem, lp2);
        MoveToEx(mem, g_sign->area.left + S(24), baseY, nullptr); LineTo(mem, g_sign->area.right - S(24), baseY);
        SelectObject(mem, op); DeleteObject(lp2);
        if (g_sign->strokes.empty()) {
            RECT hint{ g_sign->area.left, g_sign->area.top, g_sign->area.right, baseY };
            DrawTextC(mem, g.font, L"Hier mit der Maus (oder dem Stift) unterschreiben", hint, RGB(150, 154, 166));
        }
        {
            Gdiplus::Graphics gr(mem);
            gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            gr.SetClip(Gdiplus::Rect(g_sign->area.left, g_sign->area.top, g_sign->area.right - g_sign->area.left, g_sign->area.bottom - g_sign->area.top));
            Gdiplus::Pen pen(Gdiplus::Color(255, 18, 28, 105), static_cast<float>(S(3)));
            pen.SetLineJoin(Gdiplus::LineJoinRound);
            pen.SetStartCap(Gdiplus::LineCapRound);
            pen.SetEndCap(Gdiplus::LineCapRound);
            Gdiplus::SolidBrush dot(Gdiplus::Color(255, 18, 28, 105));
            for (const auto& st : g_sign->strokes) {
                std::vector<Gdiplus::Point> pts;
                for (const POINT& p : st) pts.emplace_back(p.x, p.y);
                if (pts.size() == 1) gr.FillEllipse(&dot, pts[0].X - S(2), pts[0].Y - S(2), S(4), S(4));
                else if (pts.size() > 1) gr.DrawLines(&pen, pts.data(), static_cast<INT>(pts.size()));
            }
        }
        auto button = [&](const RECT& r, bool primary, bool hot, const wchar_t* label) {
            COLORREF fill = primary ? (hot ? RGB(96, 176, 255) : kAccent) : (hot ? kCardHover : kCardRaised);
            FillRound(mem, r, S(10), fill, primary ? fill : (hot ? RGB(96, 104, 126) : kBorder));
            DrawTextC(mem, g.font, label, r, primary ? RGB(8, 20, 36) : kText);
        };
        button(g_sign->btnClear, false, g_sign->hot == 3, L"Löschen");
        button(g_sign->btnCancel, false, g_sign->hot == 2, L"Abbrechen");
        button(g_sign->btnOk, true, g_sign->hot == 1, L"Übernehmen");
        BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (PtInRect(&g_sign->area, p)) {
            g_sign->drawing = true;
            g_sign->strokes.push_back({ p });
            SetCapture(hwnd);
            InvalidateRect(hwnd, &g_sign->area, FALSE);
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (g_sign->drawing) {
            p.x = std::clamp<LONG>(p.x, g_sign->area.left, g_sign->area.right);
            p.y = std::clamp<LONG>(p.y, g_sign->area.top, g_sign->area.bottom);
            auto& st = g_sign->strokes.back();
            if (st.empty() || st.back().x != p.x || st.back().y != p.y) { st.push_back(p); InvalidateRect(hwnd, &g_sign->area, FALSE); }
            return 0;
        }
        const int hot = PtInRect(&g_sign->btnOk, p) ? 1 : PtInRect(&g_sign->btnCancel, p) ? 2 : PtInRect(&g_sign->btnClear, p) ? 3 : 0;
        if (hot != g_sign->hot) { g_sign->hot = hot; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (g_sign->drawing) { g_sign->drawing = false; if (GetCapture() == hwnd) ReleaseCapture(); return 0; }
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (PtInRect(&g_sign->btnOk, p)) SignCommit();
        else if (PtInRect(&g_sign->btnCancel, p)) g_sign->done = true;
        else if (PtInRect(&g_sign->btnClear, p)) { g_sign->strokes.clear(); InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    }
    case WM_CLOSE: g_sign->done = true; return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool DrawSignature(PdfDocument::Image& out) {
    SignState st;
    g_sign = &st;
    RECT cr{ 0, 0, S(640), S(380) };
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU, ex = WS_EX_DLGMODALFRAME;
    AdjustWindowRectEx(&cr, style, FALSE, ex);
    RECT orc; GetWindowRect(g.main, &orc);
    const int w = cr.right - cr.left, h = cr.bottom - cr.top;
    HWND hwnd = CreateWindowExW(ex, L"DuncansPdfSign", L"Unterschrift zeichnen", style, orc.left + ((orc.right - orc.left) - w) / 2,
                                orc.top + ((orc.bottom - orc.top) - h) / 3, w, h, g.main, nullptr, g.inst, nullptr);
    if (!hwnd) { g_sign = nullptr; return false; }
    SignLayout(hwnd);
    EnableWindow(g.main, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    MSG m;
    while (!st.done) {
        const BOOL r = GetMessageW(&m, nullptr, 0, 0);
        if (r == 0) { PostQuitMessage(static_cast<int>(m.wParam)); break; }   // WM_QUIT weiterreichen
        if (r < 0) break;
        if (m.message == WM_KEYDOWN) {
            if (m.wParam == VK_ESCAPE) { st.done = true; continue; }
            if (m.wParam == VK_RETURN) { SignCommit(); continue; }
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    EnableWindow(g.main, TRUE);
    DestroyWindow(hwnd);
    SetForegroundWindow(g.main);
    g_sign = nullptr;
    if (st.ok) out = std::move(st.result);
    return st.ok;
}

// Das Bild folgt jetzt der Maus; ein Klick auf die Seite setzt es dort ein
void ActivateStamp(PdfDocument::Image&& img, double frac, const std::wstring& what) {
    g.stampImg = std::move(img);
    delete g.stampGdi;
    g.stampGdi = new Gdiplus::Bitmap(g.stampImg.w, g.stampImg.h, g.stampImg.w * 4, PixelFormat32bppARGB, g.stampImg.bgra.data());
    g.stampFrac = frac;
    g.stampHover = false;
    g.tool = 5;
    g.selDrag = g.cropDrag = g.redDrag = false;
    InvalidateToolbar();
    InvalidateRect(g.canvas, nullptr, FALSE);
    ShowToast(what + L": auf die Seite klicken (Umschalt + Mausrad ändert die Größe, Esc beendet)");
    SetFocus(g.canvas);
}

void ShowStampMenu() {
    if (!g.doc.IsOpen()) return;
    if (!PdfDocument::EditSupported()) { ShowToast(L"Diese pdfium.dll kann keine Stempel einfügen – bitte pdf\\third_party\\get_pdfium.ps1 ausführen.", true); return; }
    const struct { const wchar_t* word; COLORREF col; } kStamps[4] = {
        { L"BEZAHLT", RGB(28, 140, 62) }, { L"GENEHMIGT", RGB(30, 96, 200) }, { L"ENTWURF", RGB(205, 112, 20) }, { L"VERTRAULICH", RGB(200, 40, 40) } };
    const std::wstring sigPath = SignaturePath();
    const bool haveSig = !sigPath.empty() && GetFileAttributesW(sigPath.c_str()) != INVALID_FILE_ATTRIBUTES;
    HMENU m = CreatePopupMenu();
    for (int i = 0; i < 4; ++i) AppendMenuW(m, MF_STRING, 1 + i, (std::wstring(L"Stempel „") + kStamps[i].word + L"“").c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 10, L"Unterschrift zeichnen …");
    AppendMenuW(m, MF_STRING, 11, L"Unterschrift aus Bilddatei (PNG) …");
    AppendMenuW(m, MF_STRING | (haveSig ? 0 : MF_GRAYED), 12, L"Gespeicherte Unterschrift einfügen");
    POINT pt{};
    GetCursorPos(&pt);
    for (const Item& it : g.items)
        if (it.cmd == C_STAMP) { pt = POINT{ it.rc.left, it.rc.bottom + S(4) }; ClientToScreen(g.main, &pt); }
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, g.main, nullptr);
    DestroyMenu(m);
    if (cmd >= 1 && cmd <= 4) {
        PdfDocument::Image im = RenderStamp(kStamps[cmd - 1].word, kStamps[cmd - 1].col);
        if (im.w > 0) ActivateStamp(std::move(im), 0.34, std::wstring(L"Stempel „") + kStamps[cmd - 1].word + L"“");
        else ShowToast(L"Der Stempel konnte nicht erzeugt werden.", true);
    } else if (cmd == 10) {
        PdfDocument::Image im;
        if (DrawSignature(im)) {
            SaveSignature(im);                                       // fuer das naechste Mal merken
            ActivateStamp(std::move(im), 0.26, L"Unterschrift");
        }
    } else if (cmd == 11 || cmd == 12) {
        std::wstring path = sigPath;
        if (cmd == 11) {
            wchar_t buf[MAX_PATH * 2]{};
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = g.main;
            ofn.lpstrFilter = L"Bilder (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0Alle Dateien (*.*)\0*.*\0";
            ofn.lpstrFile = buf;
            ofn.nMaxFile = MAX_PATH * 2;
            ofn.lpstrTitle = L"Unterschrift als Bild wählen (PNG mit transparentem Hintergrund ist ideal)";
            ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (!GetOpenFileNameW(&ofn)) return;
            path = buf;
        }
        PdfDocument::Image im;
        if (!LoadSignatureFile(path, im)) { ShowToast(L"Das Bild konnte nicht gelesen werden.", true); return; }
        if (cmd == 11) SaveSignature(im);
        ActivateStamp(std::move(im), 0.26, L"Unterschrift");
    }
}

// ---- Seiten-Auszug und Split ----
std::wstring BaseName(const std::wstring& path) {
    std::wstring n = FileName(path);
    if (n.size() > 4 && _wcsicmp(n.c_str() + n.size() - 4, L".pdf") == 0) n.resize(n.size() - 4);
    return n;
}

std::wstring DirName(const std::wstring& path) {
    const size_t k = path.find_last_of(L"\\/");
    return k == std::wstring::npos ? L"" : path.substr(0, k);
}

// Kurzform fuer Dateinamen: "S3", "S2-4" bzw. "S2+5+9"
std::wstring PagesLabel(const std::vector<int>& p) {
    if (p.empty()) return L"";
    bool contiguous = true;
    for (size_t i = 1; i < p.size(); ++i) contiguous = contiguous && p[i] == p[i - 1] + 1;
    if (p.size() == 1) return L"S" + std::to_wstring(p[0] + 1);
    if (contiguous) return L"S" + std::to_wstring(p.front() + 1) + L"-" + std::to_wstring(p.back() + 1);
    if (p.size() > 4) return L"Auswahl";
    std::wstring s = L"S" + std::to_wstring(p[0] + 1);
    for (size_t i = 1; i < p.size(); ++i) s += L"+" + std::to_wstring(p[i] + 1);
    return s;
}

bool SameFile(const std::wstring& a, const std::wstring& b) {
    wchar_t fa[MAX_PATH * 2]{}, fb[MAX_PATH * 2]{};
    if (!GetFullPathNameW(a.c_str(), MAX_PATH * 2, fa, nullptr) || !GetFullPathNameW(b.c_str(), MAX_PATH * 2, fb, nullptr)) return false;
    return _wcsicmp(fa, fb) == 0;
}

void DoExtract() {
    if (!g.doc.IsOpen()) return;
    const std::vector<int> t = TargetPages();
    std::wstring p;
    if (!PickFile(g.main, true, p, L"Gewählte Seiten als neue PDF speichern", BaseName(g.doc.Path()) + L"_" + PagesLabel(t) + L".pdf")) return;
    if (SameFile(p, g.doc.Path())) { ShowToast(L"Bitte einen anderen Dateinamen wählen – das Original bleibt unverändert.", true); return; }
    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    const bool ok = g.doc.SavePages(p, t);
    SetCursor(old);
    if (ok) ShowToast(std::to_wstring(t.size()) + L" Seite(n) gespeichert: " + FileName(p));
    else ShowToast(PdfDocument::LastError(), true);
}

// "1-3, 4-7, 9" (je ein Bereich = eine Datei), "je 5" (alle 5 Seiten) oder "einzeln" (jede Seite)
bool ParseSplit(const std::wstring& in, int n, std::vector<std::pair<int, int>>& parts) {
    std::wstring s;
    for (wchar_t c : in) s += static_cast<wchar_t>(towlower(c));
    size_t i = 0;
    while (i < s.size() && iswspace(s[i])) ++i;
    auto each = [&](int k) { for (int p = 1; p <= n; p += k) parts.push_back({ p, std::min(n, p + k - 1) }); };
    auto number = [&](size_t from, int& v, size_t& end) {
        while (from < s.size() && iswspace(s[from])) ++from;
        if (from >= s.size() || !iswdigit(s[from])) return false;
        wchar_t* e = nullptr;
        const long x = wcstol(s.c_str() + from, &e, 10);
        if (x < 1 || x > 100000) return false;
        v = static_cast<int>(x);
        end = static_cast<size_t>(e - s.c_str());
        return true;
    };
    if (s.compare(i, 7, L"einzeln") == 0 || s.compare(i, 4, L"jede") == 0) { each(1); return true; }
    if (s.compare(i, 2, L"je") == 0 || s.compare(i, 4, L"alle") == 0) {
        int k = 0;
        size_t e = 0;
        if (number(i + (s[i] == L'j' ? 2 : 4), k, e)) { each(k); return true; }
        if (s[i] == L'a') { each(1); return true; }
        return false;
    }
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t e = s.find_first_of(L",;\r\n", pos);
        if (e == std::wstring::npos) e = s.size();
        std::wstring tok = s.substr(pos, e - pos);
        pos = e + 1;
        size_t b = 0;
        while (b < tok.size() && iswspace(tok[b])) ++b;
        tok = tok.substr(b);
        while (!tok.empty() && iswspace(tok.back())) tok.pop_back();
        if (tok.empty()) continue;
        const std::wstring t2 = tok;
        wchar_t* e1 = nullptr;
        const long a = wcstol(t2.c_str(), &e1, 10);
        if (e1 == t2.c_str()) return false;
        long z = a;
        const wchar_t* rest = e1;
        while (iswspace(*rest)) ++rest;
        if (*rest == L'-' || *rest == 0x2013) {
            wchar_t* e2 = nullptr;
            z = wcstol(rest + 1, &e2, 10);
            if (e2 == rest + 1) return false;
            rest = e2;
            while (iswspace(*rest)) ++rest;
        }
        if (*rest != 0 || a < 1 || z < a || z > n) return false;
        parts.push_back({ static_cast<int>(a), static_cast<int>(z) });
    }
    return !parts.empty();
}

bool PickFolder(HWND owner, std::wstring& path, const std::wstring& startDir) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);      // mehrfach aufrufbar; S_FALSE/Fehler sind hier unkritisch
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return false;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(L"Zielordner für die Teile wählen");
    if (!startDir.empty()) {
        IShellItem* si = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(startDir.c_str(), nullptr, IID_PPV_ARGS(&si)))) { dlg->SetFolder(si); si->Release(); }
    }
    bool ok = false;
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* r = nullptr;
        if (SUCCEEDED(dlg->GetResult(&r))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(r->GetDisplayName(SIGDN_FILESYSPATH, &p))) { path = p; CoTaskMemFree(p); ok = true; }
            r->Release();
        }
    }
    dlg->Release();
    return ok;
}

std::wstring UniquePath(const std::wstring& dir, const std::wstring& base) {
    std::wstring p = dir + L"\\" + base + L".pdf";
    for (int k = 2; GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES && k < 1000; ++k) p = dir + L"\\" + base + L" (" + std::to_wstring(k) + L").pdf";
    return p;
}

void DoSplit() {
    if (!g.doc.IsOpen()) return;
    const int n = g.doc.PageCount();
    if (n < 2) { ShowToast(L"Die PDF hat nur eine Seite – es gibt nichts zu teilen.", true); return; }
    std::wstring spec = L"1-" + std::to_wstring(n / 2) + L", " + std::to_wstring(n / 2 + 1) + L"-" + std::to_wstring(n);
    const std::wstring hint = L"Diese PDF hat " + std::to_wstring(n) + L" Seiten. Eingabe:\n"
        L"•  Bereiche mit Komma, z. B. 1-3, 4-7, 9  (jeder Bereich wird eine Datei)\n"
        L"•  je 5  –  alle 5 Seiten eine neue Datei\n"
        L"•  einzeln  –  jede Seite als eigene Datei";
    std::vector<std::pair<int, int>> parts;
    for (;;) {
        if (!PromptText(g.main, L"PDF teilen (Split)", spec, nullptr, hint.c_str(), L"Weiter …")) return;
        parts.clear();
        if (ParseSplit(spec, n, parts)) break;
        MessageBoxW(g.main, (L"Die Eingabe passt nicht zu dieser PDF (Seiten 1 bis " + std::to_wstring(n) + L").\n\nBeispiele:  1-3, 4-7, 9   ·   je 5   ·   einzeln").c_str(), kAppTitle, MB_OK | MB_ICONWARNING);
    }
    if (parts.size() > 20 &&
        MessageBoxW(g.main, (L"Es werden " + std::to_wstring(parts.size()) + L" Dateien erstellt. Fortfahren?").c_str(), kAppTitle, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    std::wstring dir;
    if (!PickFolder(g.main, dir, DirName(g.doc.Path()))) return;
    while (dir.size() > 3 && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
    const std::wstring base = BaseName(g.doc.Path());
    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    int ok = 0;
    std::wstring err;
    for (size_t i = 0; i < parts.size(); ++i) {
        std::vector<int> pages;
        for (int p = parts[i].first; p <= parts[i].second; ++p) pages.push_back(p - 1);
        const std::wstring name = base + L"_Teil" + std::to_wstring(i + 1) + L"_" + PagesLabel(pages);
        if (g.doc.SavePages(UniquePath(dir, name), pages)) ++ok; else err = PdfDocument::LastError();
    }
    SetCursor(old);
    if (ok == static_cast<int>(parts.size())) ShowToast(std::to_wstring(ok) + L" Datei(en) erstellt in " + dir);
    else ShowToast(std::to_wstring(ok) + L" von " + std::to_wstring(parts.size()) + L" Dateien erstellt – " + err, true);
}

// Radierer: entfernt Markierungen an der Mausposition (Canvas-Koordinaten)
void EraseAtPoint(int x, int y);

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

void EraseAtPoint(int x, int y) {
    int ox, oy; PageOrigin(ox, oy);
    const int dx = x - ox, dy = y - oy;
    if (!g.doc.IsOpen() || dx < 0 || dy < 0 || dx >= g.pageW || dy >= g.pageH) return;
    if (g.doc.RemoveHighlightAt(g.page, g.pageW, g.pageH, dx, dy)) {
        RefreshAfterEdit(g.page);
        ShowToast(L"Markierung entfernt – noch nicht gespeichert");
    }
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
    g.page = 0;
    if (cfg.zoomMode == 1) { g.zoom = 1.0; g.fitWidth = false; }                       // Standard-Zoom aus den Einstellungen
    else if (cfg.zoomMode == 2) { g.zoom = cfg.lastZoom; g.fitWidth = cfg.lastFit; }
    else { g.zoom = 1.0; g.fitWidth = true; }
    g.scrollX = g.scrollY = g.sideScroll = 0;
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
    case C_MORE: {
        HMENU m = CreatePopupMenu();
        for (const Item& it : g.items) {
            if (!it.hidden || it.cmd == C_NONE || it.cmd == C_MORE) continue;
            const wchar_t* name = it.label.c_str();
            switch (it.cmd) {
            case C_ROTL: name = L"Seite nach links drehen"; break;
            case C_ROTR: name = L"Seite nach rechts drehen"; break;
            case C_FITW: name = L"Auf Seitenbreite einpassen"; break;
            case C_NOTE: name = L"Notiz hinzufügen"; break;
            case C_ERASE: name = L"Markierung entfernen (Radierer)"; break;
            case C_CROP: name = L"Zuschnitt"; break;
            case C_MERGE: name = L"PDF anhängen (Merge)"; break;
            case C_HILITE: name = L"Text markieren (Marker)"; break;
            case C_FIND: name = L"Im Dokument suchen"; break;
            }
            const bool on = (it.cmd == C_HILITE && g.tool == 1) || (it.cmd == C_NOTE && g.tool == 2) || (it.cmd == C_CROP && g.tool == 3) || (it.cmd == C_ERASE && g.tool == 4);
            AppendMenuW(m, MF_STRING | (ItemEnabled(it) ? 0 : MF_GRAYED) | (on ? MF_CHECKED : 0), 1000 + it.cmd, name);
        }
        POINT pt{};
        GetCursorPos(&pt);
        for (const Item& it : g.items)
            if (it.cmd == C_MORE) { pt = POINT{ it.rc.right, it.rc.bottom + S(4) }; ClientToScreen(g.main, &pt); }
        const int sel = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, g.main, nullptr);
        DestroyMenu(m);
        if (sel >= 1000) RunCommand(sel - 1000);
        break;
    }
    case C_STAMP: ShowStampMenu(); break;
    case C_SPLIT: DoSplit(); break;
    case C_EXTRACT: DoExtract(); break;
    case C_REDACT: {
        if (!g.doc.IsOpen()) break;
        if (!PdfDocument::EditSupported()) { ShowToast(L"Diese pdfium.dll kann nicht schwärzen – bitte pdf\\third_party\\get_pdfium.ps1 ausführen.", true); break; }
        g.tool = g.tool == 6 ? 0 : 6;
        g.selDrag = g.cropDrag = g.redDrag = false;
        InvalidateToolbar();
        InvalidateRect(g.canvas, nullptr, FALSE);
        if (g.tool == 6) ShowToast(L"Schwärzen: Bereich mit der Maus aufziehen (Esc beendet)");
        SetFocus(g.canvas);
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
    case C_SETTINGS: ShowSettings(); break;
    case C_COL0: case C_COL1: case C_COL2: case C_COL3: case C_COL4: case C_COL5: case C_COLMORE: {
        const int idx = cmd - C_COL0;
        if (cmd == C_COLMORE || (idx == 5 && !g.customSet)) {
            if (!PickCustomColor()) break;
        } else {
            g.hlSel = idx;
            g.hlColor = SwatchColor(idx);
            SaveMarkerColor();
        }
        if (g.doc.IsOpen() && g.tool != 1) { g.tool = 1; g.selDrag = false; InvalidateRect(g.canvas, nullptr, FALSE); }   // Farbwahl schaltet den Marker ein
        InvalidateToolbar();
        break;
    }
    case C_HILITE: case C_NOTE: case C_ERASE: {
        if (!g.doc.IsOpen()) break;
        const int want = cmd == C_HILITE ? 1 : (cmd == C_ERASE ? 4 : 2);
        g.tool = g.tool == want ? 0 : want;
        g.selDrag = false;
        InvalidateToolbar();
        InvalidateRect(g.canvas, nullptr, FALSE);
        if (g.tool == 1) ShowToast(L"Marker: Text mit der Maus überstreichen (Esc beendet)");
        else if (g.tool == 2) ShowToast(L"Notiz: auf die Stelle klicken (Esc beendet)");
        else if (g.tool == 4) ShowToast(L"Radierer: auf eine Markierung klicken oder darüberfahren (Esc beendet)");
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
    {   // frei belegbare Tastenkuerzel (Einstellungen -> Tastenkuerzel)
        const int b = FindBinding(vk, ctrl);
        if (b >= 0 && (!IsSingleKey(cfg.bind[b]) || cfg.singleKeys)) {
            const int cmd = kHot[b].cmd;
            if (cmd == C_OPEN || cmd == C_SAVE || cmd == C_FIND || cmd == C_SIDE) { RunCommand(cmd); return; }
            if (!g.doc.IsOpen()) return;
            RunCommand(cmd == C_ROTR && shift ? C_ROTL : cmd);
            return;
        }
    }
    if (ctrl) {
        switch (vk) {
        case 'M': RunCommand(C_MERGE); return;
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
        if (g.tool) { g.tool = 0; g.selDrag = g.cropDrag = g.redDrag = false; g.stampHover = false; InvalidateToolbar(); InvalidateRect(g.canvas, nullptr, FALSE); }
        else CloseSearch();
        break;
    case VK_F3: SearchStep(shift ? -1 : 1); break;
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
        if (g.tool == 5 && (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT)) {          // Stempelgroesse aendern
            g.stampFrac = std::clamp(g.stampFrac * std::pow(1.1, delta / 120.0), 0.06, 0.95);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
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
            if (g.tool == 5) {
                const RECT sr = StampRectAt(p, ox, oy);
                const RECT pr{ sr.left - ox, sr.top - oy, sr.right - ox, sr.bottom - oy };
                if (g.doc.PlaceImage(g.page, g.pageW, g.pageH, pr, g.stampImg)) {
                    RefreshAfterEdit(g.page);
                    ShowToast(L"Eingefügt – noch nicht gespeichert (weiter klicken für weitere Stellen, Esc beendet)");
                } else ShowToast(PdfDocument::LastError(), true);
            } else if (g.tool == 6) {
                g.redDrag = true;
                g.cropA = g.cropB = POINT{ p.x, p.y };
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (g.tool == 3) {
                g.cropDrag = true;
                g.cropA = g.cropB = POINT{ p.x, p.y };
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (g.tool == 4) {
                g.eraseDrag = true;
                SetCapture(hwnd);
                EraseAtPoint(p.x, p.y);
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
        if (g.tool == 5) {
            g.stampPos = POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            g.stampHover = true;
            if (!g.stampTrack) { TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, hwnd, 0 }; TrackMouseEvent(&t); g.stampTrack = true; }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (g.cropDrag || g.redDrag) {
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
        if (g.eraseDrag) { EraseAtPoint(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0; }
        if (g.selDrag) {
            int ox, oy; PageOrigin(ox, oy);
            const int c = g.doc.CharAt(g.selPage, g.pageW, g.pageH, GET_X_LPARAM(lp) - ox, GET_Y_LPARAM(lp) - oy);
            if (c >= 0 && c != g.selB) { g.selB = c; InvalidateRect(hwnd, nullptr, FALSE); }
        }
        return 0;
    case WM_MOUSELEAVE:
        g.stampTrack = false;
        if (g.stampHover) { g.stampHover = false; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    case WM_LBUTTONUP:
        if (g.redDrag) {
            g.redDrag = false;
            if (GetCapture() == hwnd) ReleaseCapture();
            int ox, oy; PageOrigin(ox, oy);
            RECT r{ std::min<int>(g.cropA.x, g.cropB.x) - ox, std::min<int>(g.cropA.y, g.cropB.y) - oy,
                    std::max<int>(g.cropA.x, g.cropB.x) - ox, std::max<int>(g.cropA.y, g.cropB.y) - oy };
            r.left = std::clamp<LONG>(r.left, 0, g.pageW); r.right = std::clamp<LONG>(r.right, 0, g.pageW);
            r.top = std::clamp<LONG>(r.top, 0, g.pageH); r.bottom = std::clamp<LONG>(r.bottom, 0, g.pageH);
            if (r.right - r.left < S(4) || r.bottom - r.top < S(4)) { InvalidateRect(hwnd, nullptr, FALSE); return 0; }   // nur ein Klick
            const wchar_t* q =
                L"Diesen Bereich dauerhaft schwärzen?\n\n"
                L"Der Text darunter wird aus der Seite entfernt und durch einen schwarzen Balken ersetzt. Nach dem Speichern lässt sich das nicht mehr rückgängig machen.\n\n"
                L"Lässt sich der Text nicht sauber herauslösen (Bilder, Text, der über den Bereich hinausgeht), wird die ganze Seite als Bild neu aufgebaut; "
                L"ihr Text ist dann nicht mehr markierbar. Beim Speichern entfallen außerdem Lesezeichen und Formularfelder, damit keine Reste in der Datei bleiben.";
            if (MessageBoxW(g.main, q, kAppTitle, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
                PdfDocument::RedactResult res;
                HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
                const bool ok = g.doc.Redact(g.page, g.pageW, g.pageH, { r }, res);
                SetCursor(old);
                if (ok) {
                    ClearSearch();
                    RefreshAfterEdit(g.page);
                    ShowToast(res.flattened ? L"Geschwärzt – die Seite wurde als Bild neu aufgebaut, noch nicht gespeichert"
                                            : L"Geschwärzt (Text entfernt) – noch nicht gespeichert");
                } else ShowToast(PdfDocument::LastError(), true);
            } else InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (g.eraseDrag) { g.eraseDrag = false; if (GetCapture() == hwnd) ReleaseCapture(); return 0; }
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
            if (g.doc.AddHighlight(g.selPage, start, count, g.hlColor)) { RefreshAfterEdit(g.selPage); ShowToast(L"Markierung hinzugefügt – noch nicht gespeichert"); }
            else { ShowToast(PdfDocument::LastError(), true); InvalidateRect(hwnd, nullptr, FALSE); }
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (g.selDrag && reinterpret_cast<HWND>(lp) != hwnd) { g.selDrag = false; InvalidateRect(hwnd, nullptr, FALSE); }
        if (g.redDrag && reinterpret_cast<HWND>(lp) != hwnd) { g.redDrag = false; InvalidateRect(hwnd, nullptr, FALSE); }
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

// ---------- Farbpalette (Popup unter dem Farbknopf) ----------
HWND g_pal = nullptr;
int g_palHot = -1;                       // 0-5 Farbpunkt, 6 = "Weitere Farben ..."
bool g_palTrack = false;
constexpr UINT WM_PALETTE_PICK = WM_APP + 30;   // wParam = Auswahl (0-5 Farbe, 6 = Weitere Farben), geht an das Hauptfenster

void PalLayout(RECT dot[6], RECT& more, SIZE& sz) {
    const int pad = S(14), d = S(26), gap = S(8);
    for (int i = 0; i < 6; ++i) dot[i] = RECT{ pad + i * (d + gap), pad, pad + i * (d + gap) + d, pad + d };
    sz.cx = 2 * pad + 6 * d + 5 * gap;
    more = RECT{ pad, pad + d + S(12), sz.cx - pad, pad + d + S(12) + S(34) };
    sz.cy = more.bottom + pad;
}

int PalHit(POINT p) {
    RECT dot[6], more; SIZE sz; PalLayout(dot, more, sz);
    for (int i = 0; i < 6; ++i) if (PtInRect(&dot[i], p)) return i;
    return PtInRect(&more, p) ? 6 : -1;
}

LRESULT CALLBACK PaletteProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps);
        RECT cr; GetClientRect(hwnd, &cr);
        HBRUSH bg = CreateSolidBrush(kPanel); FillRect(dc, &cr, bg); DeleteObject(bg);
        HBRUSH fb = CreateSolidBrush(RGB(96, 104, 126)); FrameRect(dc, &cr, fb); DeleteObject(fb);
        RECT dot[6], more; SIZE sz; PalLayout(dot, more, sz);
        for (int i = 0; i < 6; ++i) {
            const bool sel = g.hlSel == i, hot = g_palHot == i;
            const int cx = (dot[i].left + dot[i].right) / 2, cy = (dot[i].top + dot[i].bottom) / 2, d = S(hot ? 26 : 22);
            HBRUSH br = CreateSolidBrush(SwatchColor(i));
            HPEN rp = CreatePen(PS_SOLID, sel ? S(3) : 1, sel ? kText : (hot ? RGB(150, 158, 178) : kBorder));
            HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, rp);
            Ellipse(dc, cx - d / 2, cy - d / 2, cx + d / 2 + 1, cy + d / 2 + 1);
            SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(rp); DeleteObject(br);
            if (i == 5 && !g.customSet) DrawTextC(dc, g.font, L"+", dot[i], RGB(40, 40, 40));   // eigene Farbe noch nicht gewaehlt
        }
        FillRound(dc, more, S(8), g_palHot == 6 ? kCardHover : kCardRaised, g_palHot == 6 ? RGB(96, 104, 126) : kBorder);
        DrawTextC(dc, g.font, L"Weitere Farben …", more, kText);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!g_palTrack) { TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, hwnd, 0 }; TrackMouseEvent(&t); g_palTrack = true; }
        const int h = PalHit(POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        if (h != g_palHot) { g_palHot = h; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_palTrack = false;
        if (g_palHot != -1) { g_palHot = -1; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    case WM_LBUTTONDOWN: {
        const int h = PalHit(POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        DestroyWindow(hwnd);                       // erst schliessen, dann ausfuehren (der Farbdialog soll nicht unter dem Popup haengen)
        if (h >= 0) PostMessageW(g.main, WM_PALETTE_PICK, static_cast<WPARAM>(h), 0);
        return 0;
    }
    case WM_KEYDOWN: if (wp == VK_ESCAPE) DestroyWindow(hwnd); return 0;
    case WM_ACTIVATE: if (LOWORD(wp) == WA_INACTIVE) DestroyWindow(hwnd); return 0;
    case WM_DESTROY: g_pal = nullptr; g_palHot = -1; g_palTrack = false; return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void OpenPalette(HWND side, const RECT& btn) {
    if (g_pal) { DestroyWindow(g_pal); return; }
    RECT dot[6], more; SIZE sz; PalLayout(dot, more, sz);
    POINT p{ btn.right - sz.cx, btn.bottom + S(6) };      // rechtsbuendig unter dem Knopf, bleibt in der Seitenleiste
    if (p.x < 0) p.x = 0;
    ClientToScreen(side, &p);
    g_pal = CreateWindowExW(WS_EX_TOOLWINDOW, L"DuncansPdfPalette", L"", WS_POPUP, p.x, p.y, sz.cx, sz.cy, g.main, nullptr, g.inst, nullptr);
    if (!g_pal) return;
    ShowWindow(g_pal, SW_SHOW);
    SetForegroundWindow(g_pal);
}

LRESULT CALLBACK SideProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: PaintBuffered(hwnd, PaintSide); return 0;
    case WM_SIZE: LayoutSidebar(); return 0;
    case WM_VSCROLL: {
        RECT cr; GetClientRect(hwnd, &cr);
        ScrollMessage(hwnd, SB_VERT, wp, g.sideScroll, S(40), cr.bottom);
        g.sideScroll = std::clamp(g.sideScroll, 0, Max(0, SideContentH() - cr.bottom));
        SideScrollbar(); InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        RECT cr; GetClientRect(hwnd, &cr);
        g.sideScroll = std::clamp(g.sideScroll - static_cast<int>(GET_WHEEL_DELTA_WPARAM(wp) / 120.0 * S(70)), 0, Max(0, SideContentH() - cr.bottom));
        SideScrollbar(); InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!g.trackingSide) { TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, hwnd, 0 }; TrackMouseEvent(&t); g.trackingSide = true; }
        if (g.sideTab == 1) {
            int row = (GET_Y_LPARAM(lp) - S(12) + g.sideScroll) / ToolRowH();
            if (row < 0 || row >= static_cast<int>(sizeof(kTools) / sizeof(kTools[0]))) row = -1;
            RECT scr; GetClientRect(hwnd, &scr);
            const POINT mp{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            const RECT cb = ColorBtnRect(scr);
            if (PtInRect(&cb, mp)) row = kColorBtnHot;
            else if (row == HiliteRow() && mp.x >= ToolRowRect(row, scr).right) row = -1;
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
            int row = (y - S(12) + g.sideScroll) / ToolRowH();
            RECT scr; GetClientRect(hwnd, &scr);
            const POINT mp{ GET_X_LPARAM(lp), y };
            const RECT cb = ColorBtnRect(scr);
            if (PtInRect(&cb, mp)) { if (g.doc.IsOpen()) OpenPalette(hwnd, cb); }
            else if (row >= 0 && row < static_cast<int>(sizeof(kTools) / sizeof(kTools[0])) && g.doc.IsOpen()
                     && !(row == HiliteRow() && mp.x >= ToolRowRect(row, scr).right)) RunCommand(kTools[row].cmd);
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
        AppendMenuW(menu, MF_STRING, 5, L"Ausgewählte Seiten als neue PDF speichern …");
        AppendMenuW(menu, MF_STRING, 6, L"PDF teilen (Split) …");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, 4, L"Alle auswählen");
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ClientToScreen(hwnd, &pt);
        const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(menu);
        if (cmd == 1) RunCommand(C_ROTL);
        else if (cmd == 2) RunCommand(C_ROTR);
        else if (cmd == 3) RunCommand(C_DELETE);
        else if (cmd == 5) RunCommand(C_EXTRACT);
        else if (cmd == 6) RunCommand(C_SPLIT);
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
    case WM_PALETTE_PICK:                    // Auswahl aus der Farbpalette (6 = Weitere Farben)
        RunCommand(wp >= 6 ? C_COLMORE : C_COL0 + static_cast<int>(wp));
        return 0;
    case WM_CREATE: {
        g.main = hwnd;
        g.dpi = static_cast<int>(GetDpiForWindow(hwnd));
        gfx::EnableDarkTitleBar(hwnd);
        if (HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {   // dunkle Kontextmenues (undokumentierte Ordinale 135/136)
            using SetModeFn = int(WINAPI*)(int);
            using FlushFn = void(WINAPI*)();
            auto setMode = reinterpret_cast<SetModeFn>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(135))));
            auto flush = reinterpret_cast<FlushFn>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(136))));
            if (setMode) setMode(2);
            if (flush) flush();
        }
        MakeFonts();
        LoadMarkerColor();
        LoadPrefs();
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
        if (g.doc.IsOpen()) { cfg.lastFit = g.fitWidth; cfg.lastZoom = g.zoom; SavePrefs(); }   // "Letzten Zoom merken"
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
    reg(L"DuncansPdfSign", SignProc, nullptr, nullptr);
    reg(L"DuncansPdfPalette", PaletteProc, nullptr, nullptr);
    reg(L"DuncansPdfSettings", SettingsProc, nullptr, nullptr);

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
    delete g.stampGdi;
    g.stampGdi = nullptr;
    Gdiplus::GdiplusShutdown(g_gdipToken);
    return 0;
}
