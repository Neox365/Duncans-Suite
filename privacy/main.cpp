// Duncan's Privacy Shield - Datenschutz- & Telemetrie-Manager (Win32 + GDI/GDI+, dunkle Oberflaeche).
// Schaltet Telemetrie, Startmenue-Websuche und Tracking per Registry ab. Vor der ersten Aenderung wird ein
// Wiederherstellungspunkt angelegt; zusaetzlich sichert PrivacyRules die Originalwerte, "Standard wiederherstellen"
// stellt exakt diesen Zustand wieder her. Das Programm laeuft mit Administratorrechten (Richtlinien unter HKLM).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

#include <algorithm>
#include <cwchar>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using std::max;
using std::min;
#include <objbase.h>
#include <propidl.h>
#include <gdiplus.h>

#include "../src/Icons.h"
#include "PrivacyRules.h"

#ifndef APP_VERSION
#define APP_VERSION L"0.5.0"   // normalerweise aus CMake (PROJECT_VERSION)
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#endif

namespace {

const COLORREF kBackground = RGB(18, 19, 22);
const COLORREF kCard       = RGB(35, 38, 47);
const COLORREF kCardHover  = RGB(43, 47, 59);
const COLORREF kBorder     = RGB(45, 49, 61);
const COLORREF kBorderHot  = RGB(96, 104, 126);
const COLORREF kText       = RGB(235, 237, 242);
const COLORREF kTextDim    = RGB(150, 155, 168);
const COLORREF kAccent     = RGB(64, 156, 255);
const COLORREF kViolet     = RGB(167, 139, 250);
const COLORREF kGreen      = RGB(52, 199, 120);
const COLORREF kRed        = RGB(240, 84, 84);
const COLORREF kOrange     = RGB(255, 150, 50);

const wchar_t* kTitle = L"Duncan's Privacy Shield";
const UINT WM_RP_DONE = WM_APP + 1;     // Wiederherstellungspunkt fertig (wp = Erfolg)
const UINT WM_JOB_DONE = WM_APP + 2;    // Hintergrund-Auftrag fertig (lp = JobResult*)

enum HitId { H_NONE = -1, H_ALL = 1000, H_RESTORE = 1001, H_MODAL_OK = 2000, H_MODAL_CANCEL = 2001 };

struct Row {
    bool group = false;
    int setting = -1;          // Index in AllSettings()
    std::wstring text;
    int top = 0, height = 0;   // in Inhaltskoordinaten
    int descH = 0;
};

struct Modal {
    bool open = false;
    std::wstring title, text, ok, cancel;
    std::function<void(bool)> done;
};

struct App {
    HWND hwnd = nullptr;
    UINT dpi = 96;
    HFONT fTitle = nullptr, fHead = nullptr, fBody = nullptr, fSmall = nullptr, fBold = nullptr;
    std::vector<bool> applied;
    std::vector<Row> rows;
    int scroll = 0, contentH = 0;
    RECT list{}, btnAll{}, btnRestore{}, toolbar{}, footer{};
    int hover = H_NONE, press = H_NONE;
    bool tracking = false;
    bool busy = false;                 // ein Hintergrund-Auftrag laeuft (Wiederherstellungspunkt / Registry)
    int spin = 0;                      // Winkel des Ladekreises
    bool restorePointDone = false;
    std::wstring status;
    COLORREF statusColor = kTextDim;
    Modal modal;
    RECT modalOk{}, modalCancel{};
    std::function<void()> afterRestorePoint;
} g;

int S(int v) { return MulDiv(v, static_cast<int>(g.dpi), 96); }

COLORREF Blend(COLORREF a, COLORREF b, int pctB) {
    auto mix = [&](int x, int y) { return (x * (100 - pctB) + y * pctB) / 100; };
    return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

void MakeFonts() {
    for (HFONT* f : { &g.fTitle, &g.fHead, &g.fBody, &g.fSmall, &g.fBold })
        if (*f) { DeleteObject(*f); *f = nullptr; }
    auto make = [](int pt, int weight) {
        return CreateFontW(-MulDiv(pt, static_cast<int>(g.dpi), 72), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    };
    g.fTitle = make(20, FW_SEMIBOLD);
    g.fHead = make(11, FW_SEMIBOLD);
    g.fBody = make(10, FW_NORMAL);
    g.fSmall = make(9, FW_NORMAL);
    g.fBold = make(10, FW_SEMIBOLD);
}

void DrawText2(HDC dc, HFONT f, COLORREF c, RECT r, const std::wstring& t, UINT flags = DT_LEFT | DT_TOP | DT_SINGLELINE) {
    HGDIOBJ old = SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, t.c_str(), -1, &r, flags | DT_NOPREFIX);
    SelectObject(dc, old);
}

int TextWidth(HDC dc, HFONT f, const std::wstring& t) {
    HGDIOBJ old = SelectObject(dc, f);
    SIZE sz{};
    GetTextExtentPoint32W(dc, t.c_str(), static_cast<int>(t.size()), &sz);
    SelectObject(dc, old);
    return sz.cx;
}

int WrappedHeight(HDC dc, HFONT f, const std::wstring& t, int width) {
    RECT r{ 0, 0, width, 0 };
    HGDIOBJ old = SelectObject(dc, f);
    DrawTextW(dc, t.c_str(), -1, &r, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    SelectObject(dc, old);
    return r.bottom;
}

void FillRound(HDC dc, const RECT& r, int radius, COLORREF fill, COLORREF border) { DrawPanelAA(dc, r, radius, fill, border); }

// ---------- Layout ----------
int CardWidth() { return g.list.right - g.list.left - S(14); }       // rechts Platz fuer die Bildlaufleiste

void BuildRows(HDC dc) {
    g.rows.clear();
    const auto& all = privacy::AllSettings();
    const wchar_t* lastGroup = nullptr;
    int y = 0;
    const int textW = CardWidth() - S(18) * 2 - S(64) - S(16);
    for (size_t i = 0; i < all.size(); ++i) {
        if (!lastGroup || wcscmp(lastGroup, all[i].group) != 0) {
            Row gr; gr.group = true; gr.text = all[i].group; gr.top = y; gr.height = S(36);
            g.rows.push_back(gr);
            y += gr.height;
            lastGroup = all[i].group;
        }
        Row r; r.setting = static_cast<int>(i); r.top = y;
        r.descH = WrappedHeight(dc, g.fSmall, all[i].description, textW);
        r.height = S(16) + S(24) + r.descH + S(16);
        g.rows.push_back(r);
        y += r.height + S(10);
    }
    g.contentH = y;
}

void Layout() {
    RECT cr; GetClientRect(g.hwnd, &cr);
    const int m = S(28);
    g.toolbar = { m, S(104), cr.right - m, S(104) + S(44) };
    g.footer = { m, cr.bottom - S(34), cr.right - m, cr.bottom - S(8) };
    g.list = { m, g.toolbar.bottom + S(14), cr.right - m, g.footer.top - S(6) };
    const int bh = S(40);
    const int allW = S(190), restoreW = S(250);
    g.btnAll = { g.toolbar.left, g.toolbar.top + (S(44) - bh) / 2, g.toolbar.left + allW, g.toolbar.top + (S(44) - bh) / 2 + bh };
    g.btnRestore = { g.btnAll.right + S(12), g.btnAll.top, g.btnAll.right + S(12) + restoreW, g.btnAll.bottom };
    HDC dc = GetDC(g.hwnd);
    BuildRows(dc);
    ReleaseDC(g.hwnd, dc);
    const int view = g.list.bottom - g.list.top;
    g.scroll = std::clamp(g.scroll, 0, std::max(0, g.contentH - view));
}

// ---------- Zustand ----------
void RefreshApplied() {
    const auto& all = privacy::AllSettings();
    g.applied.assign(all.size(), false);
    for (size_t i = 0; i < all.size(); ++i) g.applied[i] = privacy::IsApplied(all[i]);
}

int AppliedCount() { return static_cast<int>(std::count(g.applied.begin(), g.applied.end(), true)); }

void SetStatus(const std::wstring& t, COLORREF c = kTextDim) {
    g.status = t;
    g.statusColor = c;
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void ShowModal(const std::wstring& title, const std::wstring& text, const std::wstring& ok, const std::wstring& cancel, std::function<void(bool)> done) {
    g.modal.open = true;
    g.modal.title = title; g.modal.text = text; g.modal.ok = ok; g.modal.cancel = cancel; g.modal.done = std::move(done);
    g.hover = g.press = H_NONE;
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void CloseModal(bool ok) {
    auto cb = std::move(g.modal.done);
    g.modal = Modal{};
    InvalidateRect(g.hwnd, nullptr, FALSE);
    if (cb) cb(ok);
}

// ---------- Aktionen ----------
// Alle Registry-Arbeiten laufen in einem Hintergrund-Thread (Dienste stoppen kann Sekunden dauern), das Fenster bleibt bedienbar.
// Nach dem Thread meldet WM_JOB_DONE das Ergebnis; dort werden Schalterstand, Zaehler und Status neu gelesen und das Fenster neu gezeichnet.
struct JobResult {
    bool apply = true;
    bool restoreAll = false;
    int ok = 0, total = 0;
    std::wstring error;
};

void Log(const std::wstring& line) {
    const std::wstring path = privacy::BackupPath().substr(0, privacy::BackupPath().find_last_of(L'\\')) + L"\\log.txt";
    if (FILE* f = _wfopen(path.c_str(), L"a, ccs=UTF-8")) {
        SYSTEMTIME st; GetLocalTime(&st);
        fwprintf(f, L"%04d-%02d-%02d %02d:%02d:%02d  %ls\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, line.c_str());
        fclose(f);
    }
}

void StartSpinner() { SetTimer(g.hwnd, 2, 50, nullptr); }
void StopSpinner() { KillTimer(g.hwnd, 2); }

void StartJob(std::function<void(JobResult&)> work, const std::wstring& message) {
    g.busy = true;
    g.spin = 0;
    SetStatus(message, kAccent);
    StartSpinner();
    UpdateWindow(g.hwnd);
    HWND hwnd = g.hwnd;
    std::thread([hwnd, work]() {
        auto* res = new JobResult();
        work(*res);
        PostMessageW(hwnd, WM_JOB_DONE, 0, reinterpret_cast<LPARAM>(res));
    }).detach();
}

void OnJobDone(JobResult* res) {
    std::unique_ptr<JobResult> r(res);
    g.busy = false;
    StopSpinner();
    RefreshApplied();
    if (r->restoreAll) {
        if (r->error.empty()) SetStatus(L"Standard wiederhergestellt: " + std::to_wstring(r->ok) + L" Einstellung(en) auf den ursprünglichen Zustand zurückgesetzt.", kGreen);
        else SetStatus(L"Wiederherstellen unvollständig: " + r->error, kRed);
    } else if (!r->error.empty()) {
        SetStatus(std::to_wstring(r->ok) + L" von " + std::to_wstring(r->total) + L" Einstellung(en) geändert – Fehler: " + r->error, kRed);
    } else {
        SetStatus(r->apply ? L"Geschützt: " + std::to_wstring(r->ok) + L" Einstellung(en) aktiviert. Abmelden und wieder Anmelden genügt, ein Neustart ist nicht nötig."
                           : L"Zurückgenommen: " + std::to_wstring(r->ok) + L" Einstellung(en).", kGreen);
    }
    InvalidateRect(g.hwnd, nullptr, FALSE);
    UpdateWindow(g.hwnd);      // Schalter und Zaehler sofort neu zeichnen
}

// Fuehrt Aenderungen aus (apply = anwenden, sonst zuruecknehmen).
void RunChanges(const std::vector<int>& idxs, bool apply) {
    StartJob([idxs, apply](JobResult& res) {
        const auto& all = privacy::AllSettings();
        res.apply = apply;
        res.total = static_cast<int>(idxs.size());
        for (int i : idxs) {
            std::wstring err;
            const bool done = apply ? privacy::Apply(all[i], err) : privacy::Revert(all[i], err);
            Log(std::wstring(apply ? L"Anwenden " : L"Zurücknehmen ") + all[i].title + (done ? L": ok" : L": FEHLER – " + err));
            if (done) ++res.ok; else if (res.error.empty()) res.error = std::wstring(all[i].title) + L": " + err;
        }
    }, apply ? L"Einstellungen werden angewendet …" : L"Einstellungen werden zurückgenommen …");
}

// Vor der ersten Aenderung einer Sitzung: Wiederherstellungspunkt (im Hintergrund), danach 'next'.
void EnsureRestorePoint(std::function<void()> next) {
    if (g.restorePointDone || privacy::HasBackup()) { g.restorePointDone = true; next(); return; }   // Sicherung vorhanden: Originalzustand ist gesichert
    g.busy = true;
    g.spin = 0;
    g.afterRestorePoint = std::move(next);
    SetStatus(L"Wiederherstellungspunkt wird erstellt … (das kann einige Sekunden dauern)", kAccent);
    StartSpinner();
    SetTimer(g.hwnd, 1, 90000, nullptr);   // nach 90 s aufgeben, falls der Systemschutz haengt
    HWND hwnd = g.hwnd;
    std::thread([hwnd]() {
        std::wstring err;
        const bool ok = privacy::CreateRestorePoint(L"Duncan's Privacy Shield – vor Datenschutz-Änderungen", err);
        Log(ok ? L"Wiederherstellungspunkt: ok" : L"Wiederherstellungspunkt: FEHLER – " + err);
        static std::wstring s_err;
        s_err = err;
        PostMessageW(hwnd, WM_RP_DONE, ok ? 1 : 0, reinterpret_cast<LPARAM>(s_err.c_str()));
    }).detach();
}

void ApplyList(const std::vector<int>& idxs, bool apply) {
    if (idxs.empty()) { SetStatus(L"Es gibt nichts zu ändern.", kTextDim); return; }
    if (!apply) { RunChanges(idxs, false); return; }
    EnsureRestorePoint([idxs]() { RunChanges(idxs, true); });
}

void OnRestorePointDone(bool ok, const std::wstring& err) {
    KillTimer(g.hwnd, 1);
    if (!g.busy) return;               // verspaetetes Ergebnis nach einer Zeitueberschreitung
    g.busy = false;
    StopSpinner();
    auto next = std::move(g.afterRestorePoint);
    g.afterRestorePoint = nullptr;
    if (ok) { g.restorePointDone = true; if (next) next(); return; }
    SetStatus(L"Kein Wiederherstellungspunkt: " + err, kOrange);
    ShowModal(L"Kein Wiederherstellungspunkt",
              err + L"\n\nTrotzdem fortfahren? Die ursprünglichen Registry-Werte werden in jedem Fall gesichert, und „Standard wiederherstellen“ stellt sie wieder her.",
              L"Trotzdem fortfahren", L"Abbrechen",
              [next](bool go) { if (go) { g.restorePointDone = true; if (next) next(); } else SetStatus(L"Abgebrochen – es wurde nichts geändert.", kTextDim); });
}

// Handler von "Alle schützen"
void ProtectAll() {
    std::vector<int> todo;
    for (size_t i = 0; i < g.applied.size(); ++i) if (!g.applied[i]) todo.push_back(static_cast<int>(i));
    if (todo.empty()) { SetStatus(L"Alle Einstellungen sind bereits aktiv.", kGreen); return; }
    ApplyList(todo, true);
}

// Handler von "Standard wiederherstellen"
void RestoreDefaults() {
    if (!privacy::HasBackup()) { SetStatus(L"Es gibt keine Sicherung – es wurde noch nichts geändert.", kTextDim); return; }
    StartJob([](JobResult& res) {
        res.restoreAll = true;
        std::wstring err;
        const bool ok = privacy::RevertAll(res.ok, err);
        Log(ok ? L"Standard wiederhergestellt: " + std::to_wstring(res.ok) + L" Einstellung(en)" : L"Wiederherstellen FEHLER – " + err);
        if (!ok) res.error = err;
    }, L"Standard wird wiederhergestellt …");
}

void ToggleSetting(int index) {
    if (g.busy || g.modal.open) return;
    const bool apply = !g.applied[index];
    ApplyList({ index }, apply);
}

// ---------- Zeichnen ----------
void DrawToggle(HDC dc, RECT r, bool on, bool hot) {
    Gdiplus::Graphics gr(dc);
    gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const COLORREF track = on ? kViolet : Blend(kCard, RGB(255, 255, 255), hot ? 22 : 14);
    Gdiplus::SolidBrush tb(Gdiplus::Color(255, GetRValue(track), GetGValue(track), GetBValue(track)));
    const float h = static_cast<float>(r.bottom - r.top), w = static_cast<float>(r.right - r.left);
    Gdiplus::GraphicsPath p;
    p.AddArc(static_cast<float>(r.left), static_cast<float>(r.top), h, h, 90, 180);
    p.AddArc(static_cast<float>(r.right) - h, static_cast<float>(r.top), h, h, 270, 180);
    p.CloseFigure();
    gr.FillPath(&tb, &p);
    const float k = h - 8.0f * g.dpi / 96.0f;
    const float kx = on ? r.right - k - 4.0f * g.dpi / 96.0f : r.left + 4.0f * g.dpi / 96.0f;
    Gdiplus::SolidBrush knob(on ? Gdiplus::Color(255, 18, 19, 22) : Gdiplus::Color(255, 235, 237, 242));
    gr.FillEllipse(&knob, kx, r.top + (h - k) / 2, k, k);
    (void)w;
}

void DrawButton(HDC dc, const RECT& r, const std::wstring& text, bool primary, bool hot, bool pressed, bool enabled) {
    COLORREF fill = primary ? (pressed ? Blend(kViolet, RGB(0, 0, 0), 25) : hot ? Blend(kViolet, RGB(255, 255, 255), 14) : kViolet)
                            : (pressed ? Blend(kCard, RGB(0, 0, 0), 18) : hot ? kCardHover : kCard);
    if (!enabled) fill = Blend(kCard, kBackground, 40);
    FillRound(dc, r, S(12), fill, primary ? fill : (hot ? kBorderHot : kBorder));
    DrawText2(dc, g.fBold, !enabled ? kTextDim : (primary ? RGB(20, 14, 40) : kText), r, text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawModal(HDC dc, const RECT& cr) {
    FillAlphaRect(dc, cr, RGB(0, 0, 0), 150);
    const int w = std::min<int>(S(520), cr.right - S(40));
    HDC mdc = dc;
    RECT probe{ 0, 0, w - S(48), 0 };
    HGDIOBJ old = SelectObject(mdc, g.fBody);
    DrawTextW(mdc, g.modal.text.c_str(), -1, &probe, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    SelectObject(mdc, old);
    const int h = S(24) + S(30) + S(12) + probe.bottom + S(24) + S(44) + S(24);
    RECT box{ (cr.right - w) / 2, (cr.bottom - h) / 2, (cr.right + w) / 2, (cr.bottom + h) / 2 };
    DrawShadow(dc, box, S(16), static_cast<int>(g.dpi));
    FillRound(dc, box, S(16), RGB(30, 33, 41), kBorderHot);
    DrawText2(dc, g.fHead, kText, { box.left + S(24), box.top + S(22), box.right - S(24), box.top + S(52) }, g.modal.title);
    DrawText2(dc, g.fBody, kTextDim, { box.left + S(24), box.top + S(60), box.right - S(24), box.bottom - S(80) }, g.modal.text, DT_LEFT | DT_TOP | DT_WORDBREAK);
    const int bh = S(40);
    const int okW = std::max(S(120), TextWidth(dc, g.fBold, g.modal.ok) + S(40));
    g.modalOk = { box.right - S(24) - okW, box.bottom - S(24) - bh, box.right - S(24), box.bottom - S(24) };
    DrawButton(dc, g.modalOk, g.modal.ok, true, g.hover == H_MODAL_OK, g.press == H_MODAL_OK && g.hover == H_MODAL_OK, true);
    if (!g.modal.cancel.empty()) {
        const int cW = std::max(S(110), TextWidth(dc, g.fBold, g.modal.cancel) + S(40));
        g.modalCancel = { g.modalOk.left - S(12) - cW, g.modalOk.top, g.modalOk.left - S(12), g.modalOk.bottom };
        DrawButton(dc, g.modalCancel, g.modal.cancel, false, g.hover == H_MODAL_CANCEL, g.press == H_MODAL_CANCEL && g.hover == H_MODAL_CANCEL, true);
    } else g.modalCancel = RECT{};
}

void Paint(HDC dc, RECT cr) {
    HBRUSH bg = CreateSolidBrush(kBackground);
    FillRect(dc, &cr, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);

    // Kopf: Schild-Kachel, Titel, Untertitel
    RECT tile{ S(28), S(22), S(28) + S(64), S(22) + S(64) };
    DrawCategoryTile(dc, tile, CategoryIcon::Shield, kViolet, kBackground);
    DrawText2(dc, g.fTitle, kText, { tile.right + S(18), S(22), cr.right - S(28), S(58) }, kTitle);
    DrawText2(dc, g.fBody, kTextDim, { tile.right + S(18), S(58), cr.right - S(28), S(82) }, L"Datenschutz- & Telemetrie-Manager für Windows");

    // Werkzeugzeile
    const bool busy = g.busy || g.modal.open;
    DrawButton(dc, g.btnAll, L"Alle schützen", true, !busy && g.hover == H_ALL, g.press == H_ALL && g.hover == H_ALL, !g.busy);
    DrawButton(dc, g.btnRestore, L"Standard wiederherstellen", false, !busy && g.hover == H_RESTORE, g.press == H_RESTORE && g.hover == H_RESTORE, !g.busy);
    const std::wstring count = std::to_wstring(AppliedCount()) + L" von " + std::to_wstring(g.applied.size()) + L" aktiv";
    DrawText2(dc, g.fBold, AppliedCount() == static_cast<int>(g.applied.size()) ? kGreen : kTextDim,
              { g.btnRestore.right + S(18), g.toolbar.top, g.toolbar.right, g.toolbar.bottom }, count, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (g.busy) {                                      // Ladekreis rechts in der Werkzeugzeile
        Gdiplus::Graphics gr(dc);
        gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::Pen track(Gdiplus::Color(60, 167, 139, 250), static_cast<float>(S(3)));
        Gdiplus::Pen arc(Gdiplus::Color(255, 167, 139, 250), static_cast<float>(S(3)));
        arc.SetStartCap(Gdiplus::LineCapRound);
        arc.SetEndCap(Gdiplus::LineCapRound);
        const int d = S(24);
        const int cx = g.toolbar.right - d, cy = (g.toolbar.top + g.toolbar.bottom) / 2;
        gr.DrawEllipse(&track, cx - d / 2, cy - d / 2, d, d);
        gr.DrawArc(&arc, cx - d / 2, cy - d / 2, d, d, static_cast<float>(g.spin), 100.0f);
    }

    // Liste (mit Clipping)
    SaveDC(dc);
    IntersectClipRect(dc, g.list.left - S(4), g.list.top, g.list.right + S(4), g.list.bottom);
    const auto& all = privacy::AllSettings();
    const int cardW = CardWidth();
    for (const Row& r : g.rows) {
        const int top = g.list.top + r.top - g.scroll;
        if (top + r.height < g.list.top || top > g.list.bottom) continue;
        if (r.group) {
            DrawText2(dc, g.fHead, kTextDim, { g.list.left + S(4), top + S(8), g.list.left + cardW, top + r.height }, r.text);
            continue;
        }
        const bool on = g.applied[r.setting];
        RECT card{ g.list.left, top, g.list.left + cardW, top + r.height };
        const bool hot = !busy && g.hover == r.setting;
        const COLORREF fill = (g.press == r.setting && hot) ? Blend(kCard, RGB(0, 0, 0), 18) : (hot ? kCardHover : kCard);
        FillRound(dc, card, S(14), fill, on ? Blend(kBorder, kViolet, 55) : (hot ? kBorderHot : kBorder));
        const int pad = S(18);
        const int textW = cardW - pad * 2 - S(64) - S(16);
        DrawText2(dc, g.fBold, kText, { card.left + pad, card.top + S(14), card.left + pad + textW, card.top + S(14) + S(24) }, all[r.setting].title);
        DrawText2(dc, g.fSmall, kTextDim, { card.left + pad, card.top + S(14) + S(24), card.left + pad + textW, card.bottom - S(10) },
                  all[r.setting].description, DT_LEFT | DT_TOP | DT_WORDBREAK);
        RECT tg{ card.right - pad - S(52), card.top + (r.height - S(28)) / 2, card.right - pad, card.top + (r.height - S(28)) / 2 + S(28) };
        DrawToggle(dc, tg, on, hot);
    }
    // schlanke Bildlaufleiste
    const int view = g.list.bottom - g.list.top;
    if (g.contentH > view) {
        const int trackH = view - S(4);
        const int thumbH = std::max(S(36), trackH * view / g.contentH);
        const int thumbY = g.list.top + S(2) + (trackH - thumbH) * g.scroll / std::max(1, g.contentH - view);
        RECT th{ g.list.right - S(6), thumbY, g.list.right - S(2), thumbY + thumbH };
        FillRound(dc, th, S(4), Blend(kBackground, RGB(255, 255, 255), 24), Blend(kBackground, RGB(255, 255, 255), 24));
    }
    RestoreDC(dc, -1);

    // Fusszeile: Urheber/Version links, Status rechts
    DrawText2(dc, g.fSmall, kTextDim, { g.footer.left, g.footer.top, g.footer.left + S(200), g.footer.bottom }, L"© 2026 Duncan · v" APP_VERSION,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawText2(dc, g.fSmall, g.statusColor, { g.footer.left + S(190), g.footer.top, g.footer.right, g.footer.bottom }, g.status,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (g.modal.open) DrawModal(dc, cr);
}

// ---------- Eingabe ----------
int HitTest(POINT p) {
    if (g.modal.open) {
        if (PtInRect(&g.modalOk, p)) return H_MODAL_OK;
        if (g.modalCancel.right > g.modalCancel.left && PtInRect(&g.modalCancel, p)) return H_MODAL_CANCEL;
        return H_NONE;
    }
    if (PtInRect(&g.btnAll, p)) return H_ALL;
    if (PtInRect(&g.btnRestore, p)) return H_RESTORE;
    if (PtInRect(&g.list, p)) {
        const int y = p.y - g.list.top + g.scroll;
        for (const Row& r : g.rows)
            if (!r.group && y >= r.top && y < r.top + r.height && p.x < g.list.left + CardWidth()) return r.setting;
    }
    return H_NONE;
}

void Scroll(int delta) {
    const int view = g.list.bottom - g.list.top;
    g.scroll = std::clamp(g.scroll + delta, 0, std::max(0, g.contentH - view));
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void Activate(int id) {
    if (id == H_MODAL_OK) CloseModal(true);
    else if (id == H_MODAL_CANCEL) CloseModal(false);
    else if (g.busy || g.modal.open) return;
    else if (id == H_ALL) ProtectAll();
    else if (id == H_RESTORE) RestoreDefaults();
    else if (id >= 0 && id < static_cast<int>(g.applied.size())) ToggleSetting(id);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT cr; GetClientRect(hwnd, &cr);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        Paint(mem, cr);
        BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE: if (g.hwnd) { Layout(); InvalidateRect(hwnd, nullptr, FALSE); } return 0;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = S(620);
        mmi->ptMinTrackSize.y = S(520);
        return 0;
    }
    case WM_DPICHANGED: {
        g.dpi = HIWORD(wp);
        MakeFonts();
        const RECT* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Layout();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!g.tracking) { TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, hwnd, 0 }; TrackMouseEvent(&t); g.tracking = true; }
        const int h = HitTest({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        if (h != g.hover) { g.hover = h; SetCursor(LoadCursorW(nullptr, h == H_NONE ? IDC_ARROW : IDC_HAND)); InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE: g.tracking = false; g.hover = H_NONE; InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, g.hover == H_NONE ? IDC_ARROW : IDC_HAND)); return TRUE; }
        break;
    case WM_LBUTTONDOWN: g.press = HitTest({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }); SetCapture(hwnd); InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_LBUTTONUP: {
        if (GetCapture() == hwnd) ReleaseCapture();
        const int h = HitTest({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        const int pressed = g.press;
        g.press = H_NONE;
        InvalidateRect(hwnd, nullptr, FALSE);
        if (pressed != H_NONE && pressed == h) Activate(h);
        return 0;
    }
    case WM_MOUSEWHEEL: Scroll(-GET_WHEEL_DELTA_WPARAM(wp) * S(60) / 120); return 0;
    case WM_KEYDOWN:
        if (g.modal.open) { if (wp == VK_ESCAPE) CloseModal(false); else if (wp == VK_RETURN) CloseModal(true); return 0; }
        if (wp == VK_DOWN) Scroll(S(60)); else if (wp == VK_UP) Scroll(-S(60));
        else if (wp == VK_NEXT) Scroll(g.list.bottom - g.list.top); else if (wp == VK_PRIOR) Scroll(-(g.list.bottom - g.list.top));
        else if (wp == VK_HOME) Scroll(-g.contentH); else if (wp == VK_END) Scroll(g.contentH);
        return 0;
    case WM_RP_DONE: OnRestorePointDone(wp != 0, reinterpret_cast<const wchar_t*>(lp)); return 0;
    case WM_JOB_DONE: OnJobDone(reinterpret_cast<JobResult*>(lp)); return 0;
    case WM_TIMER:
        if (wp == 1) OnRestorePointDone(false, L"Zeitüberschreitung: Der Systemschutz antwortet nicht.");
        else if (wp == 2) { g.spin = (g.spin + 30) % 360; InvalidateRect(hwnd, &g.toolbar, FALSE); }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    HANDLE single = CreateMutexW(nullptr, TRUE, L"DuncansPrivacyShield.Single");
    if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(L"DuncansPrivacyShield", nullptr)) { ShowWindow(other, SW_RESTORE); SetForegroundWindow(other); }
        return 0;
    }
    IconsInit();
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"DuncansPrivacyShield";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = wc.hIconSm = LoadIconW(inst, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);

    g.dpi = GetDpiForSystem();
    MakeFonts();
    g.hwnd = CreateWindowExW(0, wc.lpszClassName, kTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, S(760), S(700), nullptr, nullptr, inst, nullptr);
    if (!g.hwnd) return 1;
    g.dpi = GetDpiForWindow(g.hwnd);
    MakeFonts();
    BOOL dark = TRUE;
    DwmSetWindowAttribute(g.hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    RefreshApplied();
    Layout();
    SetStatus(privacy::HasBackup() ? L"Eine Sicherung der Originalwerte liegt vor." : L"Vor der ersten Änderung wird ein Wiederherstellungspunkt erstellt.", kTextDim);
    ShowWindow(g.hwnd, show);
    UpdateWindow(g.hwnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    IconsShutdown();
    return static_cast<int>(m.wParam);
}
