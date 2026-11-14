// Duncan's Boost - Zock-Modus (Win32 + GDI/GDI+, dunkle Oberflaeche).
// Ein Schalter: Beim Einschalten werden die gewaehlten Hintergrundprogramme beendet und der Energiesparplan auf
// Hoechstleistung gestellt; beim Ausschalten wird alles wiederhergestellt und die Programme starten wieder.
// Laeuft ohne Administratorrechte. Die Spiel-Automatik ist ein eingeklappter, optionaler Bereich.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>

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

#include "../common/DarkMenus.h"
#include "../src/Icons.h"
#include "../common/Theme.h"
#include "../common/Gfx.h"
#include "BoostLogic.h"
#include "AudioFocus.h"
#include "Crosshair.h"
#include "ClipRec.h"
#include "Overlay.h"
#include "Profiles.h"
#include "LiveStats.h"

#ifndef APP_VERSION
#define APP_VERSION L"0.6.2"   // normalerweise aus CMake
#endif

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#endif

namespace {

using namespace theme;
using gfx::Blend;
using gfx::TextWidth;
const COLORREF kCard = kCardRaised;
const COLORREF kBorderHot = kBorderHover;
const COLORREF kBoost = RGB(255, 150, 50);          // Akzent dieses Programms (Orange)

const wchar_t* kTitle = L"Duncan's Boost";
const UINT WM_JOB_DONE = WM_APP + 1;
const UINT WM_CLIP_SAVED = WM_APP + 2;               // wp = 1 gespeichert, lp = new std::wstring (Pfad oder Fehlertext)
const UINT WM_OVERLAY_ACTION = WM_APP + 3;          // wp = OverlayAction
const int kOverlayHotkeyId = 12;                    // Strg+Alt+O
const int kClipHotkeyId = 11;                       // Strg+Alt+F7                // lp = JobResult*
const UINT_PTR kTimerSpin = 2, kTimerAuto = 3, kTimerList = 4, kTimerLive = 5, kTimerAudio = 6, kTimerCross = 7, kTimerClip = 8, kTimerOverlay = 9;

enum HitId { H_NONE = -1, H_HERO = 1000, H_MODAL_OK = 2000, H_MODAL_CANCEL = 2001, H_ROW_BASE = 3000, H_GROUP = 5000 };
enum RowType { R_CLIP_AUDIO, R_OV_ON, R_OV_CORNER, R_OV_STATS, R_CLIP_ON, R_CLIP_LEN, R_CLIP_FOLDER, R_HEAD, R_APP_CLOSE, R_APP_GAME, R_AUTO, R_DUCK, R_MUTE, R_CROSS_ON, R_CROSS_STYLE, R_CROSS_COLOR, R_CROSS_SIZE, R_PROF_NEW, R_PROF, R_GROUP, R_HINT };

struct Row {
    RowType type = R_HINT;
    int idx = 0;                // App-Index
    std::wstring text;
    int top = 0, height = 0;
};

struct Modal {
    bool open = false;
    std::wstring title, text, ok, cancel;
    std::function<void(bool)> done;
};

struct JobResult { bool activate = true; bool ok = true; std::wstring note; };

struct App {
    HWND hwnd = nullptr;
    UINT dpi = 96;
    HFONT fTitle = nullptr, fHead = nullptr, fBody = nullptr, fSmall = nullptr, fBold = nullptr;
    boostmode::Settings cfg;
    boostmode::State state;                       // zuletzt gelesener Zustand (state.active = Zock-Modus laeuft)
    std::vector<boostmode::AppInfo> apps;         // laufende Programme + gespeicherte, aktuell nicht laufende
    std::vector<Row> rows;
    bool autoCollapsed = true;                    // "Automatik (optional)" ist beim Start eingeklappt
    bool crossCollapsed = true;                   // "Fadenkreuz (optional)" ebenso
    bool clipCollapsed = true;                    // "Video-Clips (optional)" ebenso
    cliprec::Recorder rec;
    boostmode::GameOverlay overlay;
    bool ovCollapsed = true;
    bool ovHotkeyOn = false;
    std::wstring ovToast;
    bool ovToastGood = true;
    DWORD ovToastUntil = 0;
    DWORD ovLastSample = 0;
    bool clipHotkeyOn = false;
    bool clipSaving = false;
    DWORD clipRetryAt = 0;
    std::wstring clipErr;
    bool profCollapsed = true;                    // "Profile (optional)" ebenso
    std::vector<boostmode::Profile> profiles;
    boostmode::CrosshairOverlay cross;
    int scroll = 0, contentH = 0;
    RECT hero{}, heroToggle{}, liveRc{}, list{}, footer{};
    std::unique_ptr<boostmode::LiveStats> live;   // CPU-/RAM-/GPU-Werte fuer die Anzeige unter dem Schalter
    boostmode::Live cur;
    int hover = H_NONE, press = H_NONE;
    bool tracking = false, busy = false;
    int spin = 0;
    int missPolls = 0;                            // Auto-Modus: aufeinanderfolgende Pruefungen ohne laufendes Spiel
    boostmode::AudioFocus audio;                  // Ton nach Fokus: merkt sich, was Boost leiser/stumm gestellt hat
    std::wstring status;
    COLORREF statusColor = kTextDim;
    Modal modal;
    RECT modalOk{}, modalCancel{};
} g;

int S(int v) { return MulDiv(v, static_cast<int>(g.dpi), 96); }

void MakeFonts() {
    for (HFONT* f : { &g.fTitle, &g.fHead, &g.fBody, &g.fSmall, &g.fBold }) if (*f) { DeleteObject(*f); *f = nullptr; }
    auto make = [](int pt, int weight) {
        return CreateFontW(-MulDiv(pt, static_cast<int>(g.dpi), 72), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    };
    g.fTitle = make(20, FW_SEMIBOLD); g.fHead = make(11, FW_SEMIBOLD); g.fBody = make(10, FW_NORMAL);
    g.fSmall = make(9, FW_NORMAL); g.fBold = make(10, FW_SEMIBOLD);
}

void DrawText2(HDC dc, HFONT f, COLORREF c, RECT r, const std::wstring& t, UINT flags = DT_LEFT | DT_TOP | DT_SINGLELINE) {
    HGDIOBJ old = SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, t.c_str(), -1, &r, flags | DT_NOPREFIX);
    SelectObject(dc, old);
}

int WrappedHeight(HDC dc, HFONT f, const std::wstring& t, int width) {
    RECT r{ 0, 0, width, 0 };
    HGDIOBJ old = SelectObject(dc, f);
    DrawTextW(dc, t.c_str(), -1, &r, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    SelectObject(dc, old);
    return r.bottom;
}

void FillRound(HDC dc, const RECT& r, int radius, COLORREF fill, COLORREF border) { DrawPanelAA(dc, r, radius, fill, border); }

std::wstring MB(unsigned long long bytes) {
    const unsigned long long mb = bytes / (1024 * 1024);
    return mb >= 1024 ? std::to_wstring(mb * 10 / 1024 / 10) + L"," + std::to_wstring(mb * 10 / 1024 % 10) + L" GB" : std::to_wstring(mb) + L" MB";
}

bool Has(const std::vector<std::wstring>& v, const std::wstring& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
void Toggle(std::vector<std::wstring>& v, const std::wstring& s) {
    auto it = std::find(v.begin(), v.end(), s);
    if (it != v.end()) v.erase(it); else v.push_back(s);
}

const wchar_t* kAutoTitle = L"Automatisch starten, wenn ich ein Spiel starte";
const wchar_t* kAutoDesc = L"Hake unten dein Spiel an. Sobald es läuft, schaltet sich der Zock-Modus ohne Rückfrage ein, nach dem Spiel wieder aus.";
const wchar_t* kDuckTitle = L"Andere Programme leiser beim Spielen";
const wchar_t* kDuckDesc = L"Für die unten angehakten Spiele: Solange ein Spiel im Vordergrund ist, laufen Browser, Musik & Co. auf 40 % und sind sofort wieder normal, wenn du das Spiel verlässt. Discord, TeamSpeak & Co. bleiben unverändert.";
const wchar_t* kClipTitle = L"Video-Clips: letzte Sekunden speichern";
const wchar_t* kClipDesc = L"Boost nimmt den Hauptbildschirm im Hintergrund auf und bewahrt nur die letzten Sekunden; Strg+Alt+F7 speichert sie als MP4 in Videos\Boost-Clips. Es läuft nur, solange ein oben angehaktes Spiel vorne ist (ohne angehaktes Spiel immer). Ton ist wählbar (Systemton = alles, was der PC abspielt, auch Discord; optional mit Mikrofon). Im echten Vollbild bleibt das Bild eventuell schwarz. Kostet etwas Leistung.";
const wchar_t* kOvTitle = L"Overlay im Spiel anzeigen";
const wchar_t* kOvDesc = L"Eine kleine Statusleiste in einer Ecke zeigt Zock-Modus, laufende Aufnahme und Auslastung. Mit Strg+Alt+O öffnest du ein Panel: Clip speichern, Aufnahme an oder aus, Zock-Modus beenden, dazu Live-Werte. Es ist ein normales Fenster über dem Spiel (nichts wird in das Spiel eingeklinkt) und erscheint im Fenster- oder randlosen Modus, nicht im echten Vollbild. Manche Spiele und Turniere verbieten Overlays.";
const wchar_t* kCrossTitle = L"Fadenkreuz über dem Spiel anzeigen";
const wchar_t* kCrossDesc = L"Ein kleines Fadenkreuz in der Bildschirmmitte, nur über den oben angehakten Spielen (ist keines angehakt, immer). Es ist ein normales, durchklickbares Fenster und greift nicht in das Spiel ein. Es erscheint im Fenster- oder randlosen Modus, nicht im echten Vollbild. Manche Spiele und Turniere verbieten Overlays.";
const wchar_t* kMuteTitle = L"Spiel stumm beim Wegschalten";
const wchar_t* kMuteDesc = L"Der Ton des angehakten Spiels ist aus, solange du in einem anderen Fenster bist (Alt+Tab), und kommt beim Zurückwechseln wieder.";

// ---------- Zustand ----------
void SetStatus(const std::wstring& t, COLORREF c = kTextDim) { g.status = t; g.statusColor = c; InvalidateRect(g.hwnd, nullptr, FALSE); }

void RefreshApps() {
    g.apps = boostmode::ListApps();
    auto addMissing = [](const std::vector<std::wstring>& v) {
        for (const std::wstring& e : v) {
            const bool found = std::any_of(g.apps.begin(), g.apps.end(), [&](const boostmode::AppInfo& a) { return a.exe == e; });
            if (!found) { boostmode::AppInfo a; a.exe = e; a.title = L"(läuft gerade nicht)"; g.apps.push_back(a); }
        }
    };
    addMissing(g.cfg.closeApps);
    addMissing(g.cfg.gameApps);
    std::stable_sort(g.apps.begin(), g.apps.end(), [](const boostmode::AppInfo& x, const boostmode::AppInfo& y) { return x.exe < y.exe; });   // alphabetisch: die Zeilen springen nicht herum
}

void SaveCfg() { boostmode::SaveSettings(g.cfg); }

// ---------- Layout ----------
int CardWidth() { return g.list.right - g.list.left - S(14); }

void BuildRows(HDC dc) {
    g.rows.clear();
    int y = 0;
    const int textW = CardWidth() - S(18) * 2 - S(64) - S(16);
    auto add = [&](Row r, int h) { r.top = y; r.height = h; g.rows.push_back(r); y += h + S(8); };
    auto appRow = [&](RowType t, int i) { Row r; r.type = t; r.idx = i; add(r, S(58)); };
    auto hint = [&](const std::wstring& t) { Row r; r.type = R_HINT; r.text = t; add(r, S(40)); };

    { Row r; r.type = R_HEAD; r.text = L"Diese Programme beenden"; add(r, S(54)); }
    if (g.apps.empty()) hint(L"Keine Programme mit Fenster gefunden. Starte die Programme, die du später beenden willst; die Liste füllt sich von selbst.");
    for (size_t i = 0; i < g.apps.size(); ++i) appRow(R_APP_CLOSE, static_cast<int>(i));

    { Row r; r.type = R_GROUP; r.text = L"Automatik (optional)"; add(r, S(46)); }
    if (!g.autoCollapsed) {
        Row r; r.type = R_AUTO; add(r, S(16) + S(24) + WrappedHeight(dc, g.fSmall, kAutoDesc, textW) + S(14));
        Row d; d.type = R_DUCK; add(d, S(16) + S(24) + WrappedHeight(dc, g.fSmall, kDuckDesc, textW) + S(14));
        Row mu; mu.type = R_MUTE; add(mu, S(16) + S(24) + WrappedHeight(dc, g.fSmall, kMuteDesc, textW) + S(14));
        if (g.apps.empty()) hint(L"Starte dein Spiel einmal, dann kannst du es hier anhaken.");
        for (size_t i = 0; i < g.apps.size(); ++i) appRow(R_APP_GAME, static_cast<int>(i));
    }

    { Row r; r.type = R_GROUP; r.idx = 1; r.text = L"Fadenkreuz (optional)"; add(r, S(46)); }
    if (!g.crossCollapsed) {
        Row r; r.type = R_CROSS_ON; add(r, S(16) + S(24) + WrappedHeight(dc, g.fSmall, kCrossDesc, textW) + S(14));
        for (RowType t : { R_CROSS_STYLE, R_CROSS_COLOR, R_CROSS_SIZE }) { Row c; c.type = t; add(c, S(52)); }
    }

    { Row r; r.type = R_GROUP; r.idx = 4; r.text = L"Overlay (optional)"; add(r, S(46)); }
    if (!g.ovCollapsed) {
        Row r; r.type = R_OV_ON; add(r, S(16) + S(24) + WrappedHeight(dc, g.fSmall, kOvDesc, textW) + S(14));
        for (RowType t : { R_OV_CORNER, R_OV_STATS }) { Row c; c.type = t; add(c, S(52)); }
    }

    { Row r; r.type = R_GROUP; r.idx = 3; r.text = L"Video-Clips (optional)"; add(r, S(46)); }
    if (!g.clipCollapsed) {
        Row r; r.type = R_CLIP_ON; add(r, S(16) + S(24) + WrappedHeight(dc, g.fSmall, kClipDesc, textW) + S(14));
        for (RowType t : { R_CLIP_LEN, R_CLIP_AUDIO, R_CLIP_FOLDER }) { Row c; c.type = t; add(c, S(52)); }
    }

    { Row r; r.type = R_GROUP; r.idx = 2; r.text = L"Profile (optional)"; add(r, S(46)); }
    if (!g.profCollapsed) {
        hint(L"Ein Profil ist ein Arbeitsplatz-Set (z. B. Arbeit, Gaming). Beim Wechsel beendet Boost die Programme der anderen Profile höflich und startet die des neuen.");
        for (size_t i = 0; i < g.profiles.size(); ++i) { Row p; p.type = R_PROF; p.idx = static_cast<int>(i); add(p, S(60)); }
        if (g.profiles.size() < boostmode::kMaxProfiles) { Row n; n.type = R_PROF_NEW; add(n, S(52)); }
    }
    g.contentH = y;
}

void Layout() {
    RECT cr; GetClientRect(g.hwnd, &cr);
    const int m = S(28);
    const int hr = cr.right - m - S(14);                   // gleiche rechte Kante wie die Listenkarten (Platz fuer die Bildlaufleiste)
    g.hero = { m, S(84), hr, S(84) + S(96) };
    g.heroToggle = { g.hero.right - S(18) - S(64), g.hero.top + (S(96) - S(34)) / 2, g.hero.right - S(18), g.hero.top + (S(96) - S(34)) / 2 + S(34) };
    g.footer = { m, cr.bottom - S(34), cr.right - m, cr.bottom - S(8) };
    g.liveRc = { m, g.hero.bottom + S(10), hr, g.hero.bottom + S(10) + S(88) };
    g.list = { m, g.liveRc.bottom + S(16), cr.right - m, g.footer.top - S(6) };
    HDC dc = GetDC(g.hwnd);
    BuildRows(dc);
    ReleaseDC(g.hwnd, dc);
    const int view = static_cast<int>(g.list.bottom - g.list.top);
    g.scroll = std::clamp(g.scroll, 0, std::max(0, g.contentH - view));
}

// ---------- Aktionen ----------
void StartSpin() { SetTimer(g.hwnd, kTimerSpin, 50, nullptr); }
void StopSpin() { KillTimer(g.hwnd, kTimerSpin); }

void ShowModal(const std::wstring& title, const std::wstring& text, const std::wstring& ok, const std::wstring& cancel, std::function<void(bool)> done) {
    g.modal.open = true; g.modal.title = title; g.modal.text = text; g.modal.ok = ok; g.modal.cancel = cancel; g.modal.done = std::move(done);
    g.hover = g.press = H_NONE;
    InvalidateRect(g.hwnd, nullptr, FALSE);
}
void CloseModal(bool ok) {
    auto cb = std::move(g.modal.done);
    g.modal = Modal{};
    InvalidateRect(g.hwnd, nullptr, FALSE);
    if (cb) cb(ok);
}

void StartJob(bool activate, bool autoTriggered) {
    if (g.busy) return;
    g.busy = true; g.spin = 0;
    SetStatus(activate ? L"Zock-Modus wird eingeschaltet …" : L"Zock-Modus wird beendet und alles wiederhergestellt …", kBoost);
    StartSpin();
    UpdateWindow(g.hwnd);
    HWND hwnd = g.hwnd;
    const boostmode::Settings cfg = g.cfg;
    std::thread([hwnd, cfg, activate, autoTriggered]() {
        auto* r = new JobResult();
        r->activate = activate;
        r->ok = activate ? boostmode::Activate(cfg, autoTriggered, r->note) : boostmode::Deactivate(cfg, r->note);
        PostMessageW(hwnd, WM_JOB_DONE, 0, reinterpret_cast<LPARAM>(r));
    }).detach();
}

void OnJobDone(JobResult* res) {
    std::unique_ptr<JobResult> r(res);
    g.busy = false;
    StopSpin();
    g.state = boostmode::LoadState();
    RefreshApps();
    Layout();
    SetStatus(r->note, r->ok ? kGreen : kRed);
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void ToggleMode() {
    if (g.busy || g.modal.open) return;
    if (g.state.active) { StartJob(false, false); return; }
    std::vector<std::wstring> running;
    for (const auto& a : g.apps) if (Has(g.cfg.closeApps, a.exe) && a.procCount > 0) running.push_back(a.exe);
    if (running.empty()) { StartJob(true, false); return; }
    std::wstring list;
    for (size_t i = 0; i < running.size() && i < 8; ++i) list += L"•  " + running[i] + L"\n";
    if (running.size() > 8) list += L"… und " + std::to_wstring(running.size() - 8) + L" weitere\n";
    ShowModal(L"Programme beenden?",
              L"Diese Programme werden jetzt beendet:\n\n" + list +
              L"\nZuerst bekommen sie die Chance, sich selbst zu schließen. Was nach etwa 8 Sekunden noch läuft, wird hart beendet; nicht gespeicherte Daten gehen dann verloren. Bitte speichere vorher deine Arbeit.\n\nNach dem Zock-Modus starten sie wieder.",
              L"Zock-Modus starten", L"Abbrechen", [](bool go) { if (go) StartJob(true, false); });
}

void AutoPoll() {
    if (g.busy || g.modal.open || !g.cfg.optAuto || g.cfg.gameApps.empty()) return;
    const bool running = boostmode::AnyRunning(g.cfg.gameApps);
    if (running) {
        g.missPolls = 0;
        if (!g.state.active) { SetStatus(L"Spiel erkannt – Zock-Modus startet …", kBoost); StartJob(true, true); }
    } else if (g.state.active && g.state.auto_) {
        if (++g.missPolls >= 2) { g.missPolls = 0; StartJob(false, true); }     // erst nach zwei Pruefungen ohne Spiel (kurze Neustarts des Spiels)
    }
}

void LiveTick() {                                  // alle Sekunde neue Werte, nur solange das Fenster zu sehen ist
    if (!g.live || IsIconic(g.hwnd) || !IsWindowVisible(g.hwnd)) return;
    g.cur = g.live->Sample();
    InvalidateRect(g.hwnd, &g.liveRc, FALSE);
}

void ListPoll() {                                  // Liste von selbst aktuell halten (nur, wenn gerade nichts passiert)
    if (g.busy || g.modal.open || IsIconic(g.hwnd) || g.press != H_NONE) return;
    RefreshApps();
    Layout();
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

// Ton nach Fokus: alle 0,7 s; nur aktiv, wenn eine der beiden Optionen an ist (oder noch etwas zurueckzustellen ist)
void AudioTick() {
    if (!g.cfg.optDuck && !g.cfg.optMuteGame && !g.audio.Active()) return;
    auto backend = boostmode::MakeSystemAudio();     // jedes Mal neu: folgt einem gewechselten Wiedergabegeraet (Kopfhoerer)
    if (!backend) return;
    if (!g.cfg.optDuck && !g.cfg.optMuteGame) { g.audio.RestoreAll(*backend); return; }
    boostmode::AudioOptions o;
    o.duck = g.cfg.optDuck;
    o.muteGame = g.cfg.optMuteGame;
    g.audio.Tick(*backend, g.cfg.gameApps, boostmode::ForegroundExe(), o);
}

// ---------- Profile ----------
struct NameDlg { std::wstring text; bool ok = false, done = false; HWND edit = nullptr; };

LRESULT CALLBACK NameProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    NameDlg* d = reinterpret_cast<NameDlg*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (m) {
    case WM_CREATE: {
        d = static_cast<NameDlg*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));
        HINSTANCE inst = GetModuleHandleW(nullptr);
        d->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", d->text.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, S(14), S(14), S(300), S(26), h, reinterpret_cast<HMENU>(100), inst, nullptr);
        HWND ok = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, S(144), S(54), S(80), S(28), h, reinterpret_cast<HMENU>(IDOK), inst, nullptr);
        HWND ca = CreateWindowExW(0, L"BUTTON", L"Abbrechen", WS_CHILD | WS_VISIBLE | WS_TABSTOP, S(232), S(54), S(82), S(28), h, reinterpret_cast<HMENU>(IDCANCEL), inst, nullptr);
        for (HWND c : { d->edit, ok, ca }) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g.fBody), TRUE);
        SendMessageW(d->edit, EM_SETLIMITTEXT, 30, 0);
        SendMessageW(d->edit, EM_SETSEL, 0, -1);
        SetFocus(d->edit);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == IDOK && d) {
            wchar_t buf[64] = {};
            GetWindowTextW(d->edit, buf, 64);
            d->text = buf; d->ok = true; d->done = true;
        } else if (LOWORD(w) == IDCANCEL && d) d->done = true;
        return 0;
    case WM_CLOSE: if (d) d->done = true; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// Kleines Fenster zum Eingeben eines Namens (Boost hat sonst keine Eingabefelder)
bool AskName(const wchar_t* title, std::wstring& name) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc); wc.lpfnWndProc = NameProc; wc.hInstance = GetModuleHandleW(nullptr);
        wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE); wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = L"DuncansBoostName";
        RegisterClassExW(&wc);
        registered = true;
    }
    NameDlg d; d.text = name;
    RECT owner; GetWindowRect(g.hwnd, &owner);
    const int w = S(342), h = S(130);
    EnableWindow(g.hwnd, FALSE);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"DuncansBoostName", title, WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                               (owner.left + owner.right - w) / 2, (owner.top + owner.bottom - h) / 2, w, h, g.hwnd, nullptr, GetModuleHandleW(nullptr), &d);
    MSG m;
    while (dlg && !d.done && GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    EnableWindow(g.hwnd, TRUE);
    if (dlg) DestroyWindow(dlg);
    SetForegroundWindow(g.hwnd);
    if (d.ok) name = d.text;
    return d.ok;
}

void SaveProfilesNow() { boostmode::SaveProfiles(g.profiles); }

std::vector<std::wstring> RunningOfProfiles() {      // welche Programme aus den Profilen laufen gerade (auch ohne Fenster)
    std::vector<std::wstring> out;
    for (const auto& p : g.profiles)
        for (const std::wstring& path : p.paths) {
            const std::wstring e = boostmode::ExeOfPath(path);
            if (!Has(out, e) && boostmode::AnyRunning({ e })) out.push_back(e);
        }
    return out;
}

std::wstring BulletList(const std::vector<std::wstring>& v) {
    std::wstring s;
    for (size_t i = 0; i < v.size() && i < 8; ++i) s += L"•  " + v[i] + L"\n";
    if (v.size() > 8) s += L"… und " + std::to_wstring(v.size() - 8) + L" weitere\n";
    return s;
}

void StartProfileJob(const boostmode::SwitchPlan& plan) {
    if (g.busy) return;
    g.busy = true; g.spin = 0;
    SetStatus(L"Profil wird gewechselt …", kBoost);
    StartSpin();
    HWND hwnd = g.hwnd;
    std::thread([hwnd, plan]() {
        auto* r = new JobResult();
        r->ok = boostmode::RunSwitch(plan, 6000, r->note);
        PostMessageW(hwnd, WM_JOB_DONE, 0, reinterpret_cast<LPARAM>(r));
    }).detach();
}

void SwitchToProfile(size_t i) {
    if (i >= g.profiles.size()) return;
    const boostmode::SwitchPlan plan = boostmode::PlanSwitch(g.profiles, i, RunningOfProfiles());
    if (plan.closeExes.empty() && plan.startPaths.empty()) { SetStatus(L"Es läuft schon alles wie in diesem Profil.", kGreen); return; }
    std::wstring text;
    if (!plan.closeExes.empty()) text += L"Wird beendet:\n" + BulletList(plan.closeExes) + L"\n";
    if (!plan.startPaths.empty()) {
        std::vector<std::wstring> names;
        for (const std::wstring& p : plan.startPaths) names.push_back(boostmode::ExeOfPath(p));
        text += L"Wird gestartet:\n" + BulletList(names) + L"\n";
    }
    text += L"Die Programme bekommen Zeit, sich selbst zu schließen, und fragen bei ungespeicherter Arbeit nach. Was nicht reagiert, bleibt offen; hart beendet wird nichts.";
    ShowModal(L"Zu „" + g.profiles[i].name + L"“ wechseln?", text, L"Wechseln", L"Abbrechen", [plan](bool go) { if (go) StartProfileJob(plan); });
}

std::vector<std::wstring> RunningPaths(std::vector<std::wstring>* names) {   // laufende Fensterprogramme samt Pfad
    std::vector<std::wstring> paths;
    for (const auto& a : g.apps) {
        if (a.procCount <= 0 || paths.size() >= boostmode::kMaxProfilePrograms) continue;
        const std::wstring p = boostmode::PathOfRunning(a.exe);
        if (p.empty()) continue;
        paths.push_back(p);
        if (names) names->push_back(a.exe);
    }
    return paths;
}

void NewProfile() {
    if (g.profiles.size() >= boostmode::kMaxProfiles) { SetStatus(L"Mehr als 12 Profile sind nicht möglich.", kOrange); return; }
    RefreshApps();
    std::vector<std::wstring> names;
    const std::vector<std::wstring> paths = RunningPaths(&names);
    if (paths.empty()) { SetStatus(L"Es laufen keine Programme mit Fenster, die ins Profil könnten.", kOrange); return; }
    ShowModal(L"Neues Profil speichern?", L"Diese gerade laufenden Programme kommen ins Profil:\n\n" + BulletList(names) +
              L"\nTipp: Starte vorher genau die Programme, die zum Profil gehören, und schließe den Rest. Danach gibst du dem Profil einen Namen.",
              L"Weiter", L"Abbrechen", [paths](bool go) {
        if (!go) return;
        std::wstring name = L"Profil " + std::to_wstring(g.profiles.size() + 1);
        if (!AskName(L"Name des Profils", name)) return;
        boostmode::Profile p;
        p.name = boostmode::UniqueProfileName(g.profiles, name);
        p.paths = paths;
        g.profiles.push_back(p);
        SaveProfilesNow();
        SetStatus(L"Profil „" + p.name + L"“ gespeichert.", kGreen);
        Layout();
    });
}

void ProfileMenu(size_t i) {
    if (i >= g.profiles.size()) return;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"Zu diesem Profil wechseln");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 2, L"Umbenennen …");
    AppendMenuW(m, MF_STRING, 3, L"Mit den jetzt laufenden Programmen überschreiben");
    AppendMenuW(m, MF_STRING, 4, L"Löschen");
    POINT pt; GetCursorPos(&pt);
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, g.hwnd, nullptr);
    DestroyMenu(m);
    if (cmd == 1) SwitchToProfile(i);
    else if (cmd == 2) {
        std::wstring name = g.profiles[i].name;
        if (AskName(L"Profil umbenennen", name)) {
            std::vector<boostmode::Profile> others = g.profiles;
            others.erase(others.begin() + static_cast<std::ptrdiff_t>(i));
            g.profiles[i].name = boostmode::UniqueProfileName(others, name);
            SaveProfilesNow(); Layout();
        }
    } else if (cmd == 3) {
        RefreshApps();
        std::vector<std::wstring> names;
        const std::vector<std::wstring> paths = RunningPaths(&names);
        if (paths.empty()) { SetStatus(L"Es laufen keine Programme mit Fenster.", kOrange); return; }
        ShowModal(L"Profil überschreiben?", L"„" + g.profiles[i].name + L"“ enthält danach genau diese Programme:\n\n" + BulletList(names), L"Überschreiben", L"Abbrechen",
                  [i, paths](bool go) { if (go && i < g.profiles.size()) { g.profiles[i].paths = paths; SaveProfilesNow(); Layout(); SetStatus(L"Profil aktualisiert.", kGreen); } });
    } else if (cmd == 4) {
        ShowModal(L"Profil löschen?", L"„" + g.profiles[i].name + L"“ wird gelöscht. Die Programme selbst bleiben unberührt.", L"Löschen", L"Abbrechen",
                  [i](bool go) { if (go && i < g.profiles.size()) { g.profiles.erase(g.profiles.begin() + static_cast<std::ptrdiff_t>(i)); SaveProfilesNow(); Layout(); } });
    }
}

// Fadenkreuz: alle 0,5 s pruefen, ob es zu sehen sein soll, und auf den Monitor des Vordergrundfensters setzen
void CrossTick() {
    static const std::wstring self = [] {
        wchar_t p[MAX_PATH * 2] = {};
        GetModuleFileNameW(nullptr, p, MAX_PATH * 2);
        std::wstring s = p;
        const size_t slash = s.find_last_of(L'\\');
        if (slash != std::wstring::npos) s.erase(0, slash + 1);
        for (wchar_t& c : s) if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + 32);
        return s;
    }();
    if (!g.cfg.optCross) { g.cross.Update(false, {}, nullptr, g.dpi); return; }
    boostmode::CrosshairSpec spec;
    spec.on = true; spec.style = g.cfg.crossStyle; spec.color = g.cfg.crossColor; spec.size = g.cfg.crossSize;
    spec = boostmode::ClampCrosshair(spec);
    HWND fgw = GetForegroundWindow();
    const bool vis = boostmode::CrosshairVisible(spec, g.cfg.gameApps, boostmode::ForegroundExe(), self);
    g.cross.Update(vis, spec, MonitorFromWindow(fgw ? fgw : g.hwnd, MONITOR_DEFAULTTOPRIMARY), g.dpi);
}

// Video-Clips: Aufnahme laeuft nur, wenn die Funktion an ist und (bei angehakten Spielen) eines vorne ist
void ClipTick() {
    const bool want = g.cfg.optClip && boostmode::ClipShouldRecord(g.cfg.gameApps, boostmode::ForegroundExe());
    if (want && !g.rec.Running() && GetTickCount() >= g.clipRetryAt) {
        cliprec::Options o;
        o.seconds = g.cfg.clipSeconds;
        o.audio = g.cfg.clipAudio;
        std::wstring err;
        if (!g.rec.Start(o, err)) {
            g.clipRetryAt = GetTickCount() + 20000;                     // nicht dauernd neu versuchen
            if (err != g.clipErr) SetStatus(L"Video-Clips: " + err, kRed);
            g.clipErr = err;
        } else {
            g.clipErr.clear();
            const std::wstring pr = g.rec.Problem();                    // z. B. Mikrofon nicht verfuegbar: Bild laeuft trotzdem
            if (!pr.empty()) SetStatus(L"Video-Clips: " + pr, kOrange);
        }
    } else if (!want && g.rec.Running()) g.rec.Stop();
    const bool hk = g.cfg.optClip;
    if (hk && !g.clipHotkeyOn) g.clipHotkeyOn = RegisterHotKey(g.hwnd, kClipHotkeyId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F7) != 0;
    else if (!hk && g.clipHotkeyOn) { UnregisterHotKey(g.hwnd, kClipHotkeyId); g.clipHotkeyOn = false; }
}

void SaveClipNow() {
    if (g.clipSaving) return;
    if (!g.rec.Running()) { SetStatus(L"Video-Clips: Es läuft gerade keine Aufnahme (ist ein angehaktes Spiel vorne?).", kOrange); return; }
    g.clipSaving = true;
    SetStatus(L"Clip wird gespeichert …", kBoost);
    HWND hw = g.hwnd;
    std::thread([hw]() {                                                // Zusammensetzen im Hintergrund, die Aufnahme laeuft weiter
        std::wstring file, err;
        const bool ok = g.rec.Save(file, err);
        PostMessageW(hw, WM_CLIP_SAVED, ok ? 1 : 0, reinterpret_cast<LPARAM>(new std::wstring(ok ? file : err)));
    }).detach();
}

void OpenClipFolder() {
    const std::wstring dir = cliprec::DefaultClipDir();
    if (dir.empty()) { SetStatus(L"Der Videos-Ordner wurde nicht gefunden.", kRed); return; }
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void OverlayToast(const std::wstring& t, bool good) {
    g.ovToast = t; g.ovToastGood = good; g.ovToastUntil = GetTickCount() + 4000;
}

// Overlay: alle 0,5 s Zustand sammeln und Leiste/Panel aktualisieren
void OverlayTick() {
    if (!g.cfg.optOverlay) {
        if (g.overlay.PanelOpen()) g.overlay.SetPanelOpen(false);
        boostmode::OverlayState off;
        g.overlay.Update(false, off, nullptr, g.dpi);
        if (g.ovHotkeyOn) { UnregisterHotKey(g.hwnd, kOverlayHotkeyId); g.ovHotkeyOn = false; }
        return;
    }
    if (!g.ovHotkeyOn) g.ovHotkeyOn = RegisterHotKey(g.hwnd, kOverlayHotkeyId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'O') != 0;
    if (!g.ovToast.empty() && GetTickCount() >= g.ovToastUntil) g.ovToast.clear();
    boostmode::OverlayState st;
    st.boostActive = g.state.active;
    st.recording = g.rec.Running();
    st.clipEnabled = g.cfg.optClip;
    st.clipSeconds = g.cfg.clipSeconds;
    st.showStats = g.cfg.overlayStats;
    st.corner = g.cfg.overlayCorner;
    st.toast = g.ovToast;
    st.toastGood = g.ovToastGood;
    const bool mainVisible = IsWindowVisible(g.hwnd) && !IsIconic(g.hwnd);
    const bool bar = boostmode::OverlayHudVisible(g.cfg.gameApps, boostmode::ForegroundExe(), st);
    if (!mainVisible && g.live && (bar || g.overlay.PanelOpen()) && (st.showStats || g.overlay.PanelOpen()) && GetTickCount() - g.ovLastSample >= 1000) {
        g.cur = g.live->Sample();                      // ist das Boost-Fenster zu sehen, misst dessen eigener Takt
        g.ovLastSample = GetTickCount();
    }
    st.live = g.cur;
    HWND fgw = GetForegroundWindow();
    g.overlay.Update(bar, st, MonitorFromWindow(fgw ? fgw : g.hwnd, MONITOR_DEFAULTTOPRIMARY), g.dpi);
}

void ActivateRow(const Row& r) {
    if (r.type == R_OV_ON) { g.cfg.optOverlay = !g.cfg.optOverlay; SaveCfg(); OverlayTick(); }
    else if (r.type == R_OV_CORNER) { g.cfg.overlayCorner = boostmode::NextOverlayCorner(g.cfg.overlayCorner); SaveCfg(); OverlayTick(); }
    else if (r.type == R_OV_STATS) { g.cfg.overlayStats = !g.cfg.overlayStats; SaveCfg(); OverlayTick(); }
    else if (r.type == R_CLIP_ON) { g.cfg.optClip = !g.cfg.optClip; SaveCfg(); ClipTick(); }
    else if (r.type == R_CLIP_LEN) { g.cfg.clipSeconds = boostmode::NextClipSeconds(g.cfg.clipSeconds); SaveCfg(); if (g.rec.Running()) g.rec.Stop(); ClipTick(); }
    else if (r.type == R_CLIP_AUDIO) { g.cfg.clipAudio = (boostmode::ClampClipAudio(g.cfg.clipAudio) + 1) % 3; SaveCfg(); if (g.rec.Running()) g.rec.Stop(); ClipTick(); }
    else if (r.type == R_CLIP_FOLDER) OpenClipFolder();
    else if (r.type == R_APP_CLOSE) { Toggle(g.cfg.closeApps, g.apps[r.idx].exe); SaveCfg(); }
    else if (r.type == R_APP_GAME) { Toggle(g.cfg.gameApps, g.apps[r.idx].exe); SaveCfg(); }
    else if (r.type == R_AUTO) { g.cfg.optAuto = !g.cfg.optAuto; g.missPolls = 0; SaveCfg(); }
    else if (r.type == R_DUCK) { g.cfg.optDuck = !g.cfg.optDuck; SaveCfg(); AudioTick(); }
    else if (r.type == R_MUTE) { g.cfg.optMuteGame = !g.cfg.optMuteGame; SaveCfg(); AudioTick(); }
    else if (r.type == R_PROF_NEW) NewProfile();
    else if (r.type == R_PROF) ProfileMenu(static_cast<size_t>(r.idx));
    else if (r.type == R_CROSS_ON) { g.cfg.optCross = !g.cfg.optCross; SaveCfg(); CrossTick(); }
    else if (r.type == R_CROSS_STYLE) { g.cfg.crossStyle = (g.cfg.crossStyle + 1) % boostmode::CrosshairStyleCount(); SaveCfg(); CrossTick(); }
    else if (r.type == R_CROSS_COLOR) { g.cfg.crossColor = (g.cfg.crossColor + 1) % boostmode::CrosshairColorCount(); SaveCfg(); CrossTick(); }
    else if (r.type == R_CROSS_SIZE) { g.cfg.crossSize = (g.cfg.crossSize + 1) % boostmode::CrosshairSizeCount(); SaveCfg(); CrossTick(); }
}

// ---------- Zeichnen ----------
void DrawToggle(HDC dc, RECT r, bool on, bool hot, COLORREF accent) {
    Gdiplus::Graphics gr(dc);
    gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const COLORREF track = on ? accent : Blend(kCard, RGB(255, 255, 255), hot ? 22 : 14);
    Gdiplus::SolidBrush tb(Gdiplus::Color(255, GetRValue(track), GetGValue(track), GetBValue(track)));
    const float h = static_cast<float>(r.bottom - r.top);
    Gdiplus::GraphicsPath p;
    p.AddArc(static_cast<float>(r.left), static_cast<float>(r.top), h, h, 90, 180);
    p.AddArc(static_cast<float>(r.right) - h, static_cast<float>(r.top), h, h, 270, 180);
    p.CloseFigure();
    gr.FillPath(&tb, &p);
    const float k = h - 8.0f * g.dpi / 96.0f;
    const float kx = on ? r.right - k - 4.0f * g.dpi / 96.0f : r.left + 4.0f * g.dpi / 96.0f;
    Gdiplus::SolidBrush knob(on ? Gdiplus::Color(255, 18, 19, 22) : Gdiplus::Color(255, 235, 237, 242));
    gr.FillEllipse(&knob, kx, r.top + (h - k) / 2, k, k);
}

void DrawCheck(HDC dc, RECT r, bool on, bool hot) {
    FillRound(dc, r, S(6), on ? kBoost : Blend(kCard, kBackground, 30), on ? kBoost : (hot ? kBorderHot : kBorder));
    if (!on) return;
    Gdiplus::Graphics gr(dc);
    gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Pen pen(Gdiplus::Color(255, 20, 14, 8), static_cast<float>(S(2)));
    pen.SetStartCap(Gdiplus::LineCapRound); pen.SetEndCap(Gdiplus::LineCapRound); pen.SetLineJoin(Gdiplus::LineJoinRound);
    const int w = r.right - r.left, h = r.bottom - r.top;
    Gdiplus::Point pts[3] = { { r.left + w * 25 / 100, r.top + h * 52 / 100 }, { r.left + w * 43 / 100, r.top + h * 70 / 100 }, { r.left + w * 76 / 100, r.top + h * 32 / 100 } };
    gr.DrawLines(&pen, pts, 3);
}

void DrawButton(HDC dc, const RECT& r, const std::wstring& text, bool primary, bool hot, bool pressed, bool enabled) {
    COLORREF fill = primary ? (pressed ? Blend(kBoost, RGB(0, 0, 0), 25) : hot ? Blend(kBoost, RGB(255, 255, 255), 14) : kBoost)
                            : (pressed ? Blend(kCard, RGB(0, 0, 0), 18) : hot ? kCardHover : kCard);
    if (!enabled) fill = Blend(kCard, kBackground, 40);
    FillRound(dc, r, S(12), fill, primary ? fill : (hot ? kBorderHot : kBorder));
    DrawText2(dc, g.fBold, !enabled ? kTextDim : (primary ? RGB(30, 16, 4) : kText), r, text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawModal(HDC dc, const RECT& cr) {
    FillAlphaRect(dc, cr, RGB(0, 0, 0), 150);
    const int w = std::min<int>(S(520), cr.right - S(40));
    const int th = WrappedHeight(dc, g.fBody, g.modal.text, w - S(48));
    const int h = S(24) + S(30) + S(12) + th + S(24) + S(44) + S(24);
    RECT box{ (cr.right - w) / 2, (cr.bottom - h) / 2, (cr.right + w) / 2, (cr.bottom + h) / 2 };
    DrawShadow(dc, box, S(16), static_cast<int>(g.dpi));
    FillRound(dc, box, S(16), RGB(30, 33, 41), kBorderHot);
    DrawText2(dc, g.fHead, kText, { box.left + S(24), box.top + S(22), box.right - S(24), box.top + S(52) }, g.modal.title);
    DrawText2(dc, g.fBody, kTextDim, { box.left + S(24), box.top + S(60), box.right - S(24), box.bottom - S(80) }, g.modal.text, DT_LEFT | DT_TOP | DT_WORDBREAK);
    const int bh = S(40);
    const int okW = std::max(S(120), TextWidth(dc, g.fBold, g.modal.ok) + S(40));
    g.modalOk = { box.right - S(24) - okW, box.bottom - S(24) - bh, box.right - S(24), box.bottom - S(24) };
    DrawButton(dc, g.modalOk, g.modal.ok, true, g.hover == H_MODAL_OK, g.press == H_MODAL_OK && g.hover == H_MODAL_OK, true);
    const int cW = std::max(S(110), TextWidth(dc, g.fBold, g.modal.cancel) + S(40));
    g.modalCancel = { g.modalOk.left - S(12) - cW, g.modalOk.top, g.modalOk.left - S(12), g.modalOk.bottom };
    DrawButton(dc, g.modalCancel, g.modal.cancel, false, g.hover == H_MODAL_CANCEL, g.press == H_MODAL_CANCEL && g.hover == H_MODAL_CANCEL, true);
}

void Paint(HDC dc, const RECT& cr) {
    const bool busy = g.busy || g.modal.open;
    HBRUSH bg = CreateSolidBrush(kBackground);
    FillRect(dc, &cr, bg);
    DeleteObject(bg);

    DrawText2(dc, g.fTitle, kText, { S(28), S(18), cr.right - S(28), S(56) }, kTitle);
    DrawText2(dc, g.fBody, kTextDim, { S(28), S(54), cr.right - S(28), S(78) }, L"Mehr Leistung zum Zocken – ein Schalter, ein Klick zurück");

    // Hero-Karte mit grossem Schalter
    const bool on = g.state.active;
    const bool heroHot = !busy && g.hover == H_HERO;
    FillRound(dc, g.hero, S(16), heroHot ? kCardHover : kCard, on ? Blend(kBorder, kBoost, 70) : (heroHot ? kBorderHot : kBorder));
    DrawText2(dc, g.fHead, kText, { g.hero.left + S(22), g.hero.top + S(18), g.heroToggle.left - S(60), g.hero.top + S(46) },
              on ? L"Zock-Modus ist an" : L"Zock-Modus ist aus");
    std::wstring sub = g.state.active && g.state.auto_ ? L"Automatisch gestartet (Spiel erkannt)." : L"";
    if (sub.empty()) {
        const int n = static_cast<int>(g.cfg.closeApps.size());
        sub = on ? L"Alles ist umgestellt. Zum Beenden den Schalter ausschalten."
                 : (n ? std::to_wstring(n) + (n == 1 ? L" Programm wird beim Einschalten beendet." : L" Programme werden beim Einschalten beendet.")
                      : L"Hake unten Programme an, die beim Einschalten beendet werden sollen.");
    }
    DrawText2(dc, g.fSmall, kTextDim, { g.hero.left + S(22), g.hero.top + S(48), g.heroToggle.left - S(60), g.hero.bottom - S(10) }, sub, DT_LEFT | DT_TOP | DT_WORDBREAK);
    DrawToggle(dc, g.heroToggle, on, heroHot, kBoost);

    if (g.busy) {                                      // Ladekreis links neben dem Schalter
        Gdiplus::Graphics gr(dc);
        gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::Pen track(Gdiplus::Color(60, 255, 150, 50), static_cast<float>(S(3)));
        Gdiplus::Pen arc(Gdiplus::Color(255, 255, 150, 50), static_cast<float>(S(3)));
        arc.SetStartCap(Gdiplus::LineCapRound); arc.SetEndCap(Gdiplus::LineCapRound);
        const int d = S(24), cx = g.heroToggle.left - S(30), cy = (g.heroToggle.top + g.heroToggle.bottom) / 2;
        gr.DrawEllipse(&track, cx - d / 2, cy - d / 2, d, d);
        gr.DrawArc(&arc, cx - d / 2, cy - d / 2, d, d, static_cast<float>(g.spin), 100.0f);
    }

    // Live-Werte: vier Kacheln (CPU, Arbeitsspeicher, GPU, GPU-Temperatur)
    {
        FillRound(dc, g.liveRc, S(14), kCard, kBorder);
        const boostmode::Live& l = g.cur;
        struct Tile { std::wstring label, value, extra; COLORREF color; };
        auto pct = [](int v) { return v < 0 ? std::wstring(L"–") : std::to_wstring(v) + L" %"; };
        auto colorFor = [](int level) { return level >= 2 ? kRed : (level == 1 ? kBoost : kText); };
        std::wstring gpuShort = l.gpuName;
        if (gpuShort.rfind(L"NVIDIA GeForce ", 0) == 0) gpuShort = gpuShort.substr(15);
        auto gb = [](unsigned long long mb) { const unsigned long long t = (mb * 10 + 512) / 1024; return std::to_wstring(t / 10) + L"," + std::to_wstring(t % 10); };
        const Tile tiles[4] = {
            { L"CPU-Auslastung", pct(l.cpu), L"", l.cpu < 0 ? kTextDim : colorFor(boostmode::LoadLevel(l.cpu)) },
            { L"Arbeitsspeicher", pct(l.ram), l.ramTotalMb ? gb(l.ramUsedMb) + L" von " + gb(l.ramTotalMb) + L" GB" : L"", l.ram < 0 ? kTextDim : colorFor(boostmode::LoadLevel(l.ram)) },
            { L"GPU-Auslastung", pct(l.gpu), L"", l.gpu < 0 ? kTextDim : colorFor(boostmode::LoadLevel(l.gpu)) },
            { l.gpuTemp < 0 ? L"GPU-Temperatur (nur NVIDIA)" : L"GPU-Temperatur", l.gpuTemp < 0 ? std::wstring(L"–") : std::to_wstring(l.gpuTemp) + L" °C", gpuShort,
              l.gpuTemp < 0 ? kTextDim : colorFor(boostmode::TempLevel(l.gpuTemp)) },
        };
        const int w = (g.liveRc.right - g.liveRc.left) / 4;
        for (int i = 0; i < 4; ++i) {
            const int x = g.liveRc.left + i * w;
            if (i > 0) { RECT sep{ x, g.liveRc.top + S(14), x + 1, g.liveRc.bottom - S(14) }; FillRound(dc, sep, 0, kBorder, kBorder); }
            DrawText2(dc, g.fSmall, kTextDim, { x + S(16), g.liveRc.top + S(10), x + w - S(8), g.liveRc.top + S(30) }, tiles[i].label, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
            DrawText2(dc, g.fTitle, tiles[i].color, { x + S(16), g.liveRc.top + S(28), x + w - S(8), g.liveRc.top + S(62) }, tiles[i].value, DT_LEFT | DT_TOP | DT_SINGLELINE);
            if (!tiles[i].extra.empty())
                DrawText2(dc, g.fSmall, kTextDim, { x + S(16), g.liveRc.top + S(62), x + w - S(8), g.liveRc.bottom - S(4) }, tiles[i].extra, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }

    // Liste
    SaveDC(dc);
    IntersectClipRect(dc, g.list.left - S(4), g.list.top, g.list.right + S(4), g.list.bottom);
    const int cardW = CardWidth();
    for (size_t ri = 0; ri < g.rows.size(); ++ri) {
        const Row& r = g.rows[ri];
        const int top = g.list.top + r.top - g.scroll;
        if (top + r.height < g.list.top || top > g.list.bottom) continue;
        RECT card{ g.list.left, top, g.list.left + cardW, top + r.height };
        const int hid = r.type == R_GROUP ? H_GROUP + r.idx : H_ROW_BASE + static_cast<int>(ri);
        const bool hot = !busy && g.hover == hid;
        const int pad = S(18);
        if (r.type == R_HEAD) {
            DrawText2(dc, g.fHead, kText, { card.left + S(4), card.top + S(2), card.right, card.top + S(28) }, r.text);
            DrawText2(dc, g.fSmall, kTextDim, { card.left + S(4), card.top + S(28), card.right, card.bottom }, L"Beim Ausschalten starten sie wieder von selbst.");
            continue;
        }
        if (r.type == R_GROUP) {
            const bool collapsed = r.idx == 0 ? g.autoCollapsed : (r.idx == 1 ? g.crossCollapsed : (r.idx == 3 ? g.clipCollapsed : (r.idx == 4 ? g.ovCollapsed : g.profCollapsed)));
            const bool grpOn = r.idx == 0 ? g.cfg.optAuto : (r.idx == 1 ? g.cfg.optCross : (r.idx == 3 ? g.cfg.optClip : (r.idx == 4 ? g.cfg.optOverlay : !g.profiles.empty())));
            FillRound(dc, card, S(12), hot ? kCardHover : Blend(kCard, kBackground, 40), hot ? kBorderHot : kBorder);
            {
                Gdiplus::Graphics gr(dc);
                gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                Gdiplus::Pen pen(Gdiplus::Color(255, 255, 150, 50), static_cast<float>(S(2)));
                pen.SetStartCap(Gdiplus::LineCapRound); pen.SetEndCap(Gdiplus::LineCapRound); pen.SetLineJoin(Gdiplus::LineJoinRound);
                const int cx = card.left + S(26), cy = (card.top + card.bottom) / 2, a = S(4);
                Gdiplus::Point pts[3];
                if (!collapsed) { pts[0] = { cx - a, cy - a / 2 }; pts[1] = { cx, cy + a / 2 + 1 }; pts[2] = { cx + a, cy - a / 2 }; }
                else                  { pts[0] = { cx - a / 2, cy - a }; pts[1] = { cx + a / 2 + 1, cy }; pts[2] = { cx - a / 2, cy + a }; }
                gr.DrawLines(&pen, pts, 3);
            }
            DrawText2(dc, g.fHead, kText, { card.left + S(48), card.top, card.right - S(180), card.bottom }, r.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            DrawText2(dc, g.fSmall, grpOn ? kBoost : kTextDim, { card.right - S(170), card.top, card.right - S(18), card.bottom },
                      r.idx == 2 ? (g.profiles.empty() ? std::wstring(L"keine") : std::to_wstring(g.profiles.size()) + (g.profiles.size() == 1 ? L" Profil" : L" Profile"))
                                 : std::wstring(grpOn ? L"an" : L"aus"), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            continue;
        }
        if (r.type == R_HINT) {
            DrawText2(dc, g.fSmall, kTextDim, { card.left + S(6), card.top + S(6), card.right - S(6), card.bottom }, r.text, DT_LEFT | DT_TOP | DT_WORDBREAK);
            continue;
        }
        if (r.type == R_PROF_NEW) {
            FillRound(dc, card, S(14), hot ? kCardHover : kCard, hot ? kBorderHot : kBorder);
            DrawText2(dc, g.fBold, kBoost, { card.left + pad, card.top, card.right - pad, card.bottom }, L"+  Laufende Programme als neues Profil speichern …", DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            continue;
        }
        if (r.type == R_PROF) {
            const boostmode::Profile& p = g.profiles[static_cast<size_t>(r.idx)];
            FillRound(dc, card, S(14), hot ? kCardHover : kCard, hot ? kBorderHot : kBorder);
            std::wstring list;
            for (size_t k = 0; k < p.paths.size() && k < 6; ++k) list += (k ? L", " : L"") + boostmode::ExeOfPath(p.paths[k]);
            if (p.paths.size() > 6) list += L" …";
            DrawText2(dc, g.fBold, kText, { card.left + pad, card.top + S(9), card.right - S(150), card.top + S(9) + S(24) }, p.name, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            DrawText2(dc, g.fSmall, kTextDim, { card.left + pad, card.top + S(9) + S(24), card.right - pad, card.bottom - S(4) }, list, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
            DrawText2(dc, g.fSmall, kBoost, { card.right - S(150), card.top + S(9), card.right - pad, card.top + S(9) + S(24) },
                      std::to_wstring(p.paths.size()) + (p.paths.size() == 1 ? L" Programm" : L" Programme"), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            continue;
        }
        if (r.type == R_OV_CORNER || r.type == R_OV_STATS) {
            FillRound(dc, card, S(14), hot ? kCardHover : kCard, hot ? kBorderHot : kBorder);
            DrawText2(dc, g.fBold, kText, { card.left + pad, card.top, card.left + pad + S(200), card.bottom }, r.type == R_OV_CORNER ? L"Ecke" : L"Auslastung zeigen", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            DrawText2(dc, g.fBold, kBoost, { card.left + pad + S(200), card.top, card.right - pad, card.bottom },
                      r.type == R_OV_CORNER ? std::wstring(boostmode::OverlayCornerName(g.cfg.overlayCorner)) : std::wstring(g.cfg.overlayStats ? L"an" : L"aus"), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            continue;
        }
        if (r.type == R_CLIP_LEN || r.type == R_CLIP_FOLDER || r.type == R_CLIP_AUDIO) {
            FillRound(dc, card, S(14), hot ? kCardHover : kCard, hot ? kBorderHot : kBorder);
            DrawText2(dc, g.fBold, kText, { card.left + pad, card.top, card.left + pad + S(160), card.bottom }, r.type == R_CLIP_LEN ? L"Rückblick" : (r.type == R_CLIP_AUDIO ? L"Ton" : L"Clip-Ordner"), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            DrawText2(dc, g.fBold, kBoost, { card.left + pad + S(160), card.top, card.right - pad, card.bottom },
                      r.type == R_CLIP_LEN ? (std::to_wstring(g.cfg.clipSeconds) + L" Sekunden") : (r.type == R_CLIP_AUDIO ? std::wstring(boostmode::ClipAudioName(g.cfg.clipAudio)) : std::wstring(L"Öffnen")), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            continue;
        }
        if (r.type == R_CROSS_STYLE || r.type == R_CROSS_COLOR || r.type == R_CROSS_SIZE) {
            FillRound(dc, card, S(14), hot ? kCardHover : kCard, hot ? kBorderHot : kBorder);
            const wchar_t* label = r.type == R_CROSS_STYLE ? L"Form" : (r.type == R_CROSS_COLOR ? L"Farbe" : L"Größe");
            const wchar_t* val = r.type == R_CROSS_STYLE ? boostmode::CrosshairStyleName(g.cfg.crossStyle)
                               : (r.type == R_CROSS_COLOR ? boostmode::CrosshairColorName(g.cfg.crossColor) : boostmode::CrosshairSizeName(g.cfg.crossSize));
            DrawText2(dc, g.fBold, kText, { card.left + pad, card.top, card.left + pad + S(120), card.bottom }, label, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            int right = card.right - pad;
            if (r.type == R_CROSS_COLOR) {                  // Farbmuster
                const int d = S(18);
                RECT sw{ right - d, card.top + (r.height - d) / 2, right, card.top + (r.height - d) / 2 + d };
                FillRound(dc, sw, d / 2, boostmode::CrosshairColorValue(g.cfg.crossColor), kBorderHot);
                right -= d + S(10);
            }
            DrawText2(dc, g.fBold, kBoost, { card.left + pad + S(120), card.top, right, card.bottom }, val, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            continue;
        }
        if (r.type == R_AUTO || r.type == R_DUCK || r.type == R_MUTE || r.type == R_CROSS_ON || r.type == R_CLIP_ON || r.type == R_OV_ON) {
            const bool v = r.type == R_AUTO ? g.cfg.optAuto : (r.type == R_DUCK ? g.cfg.optDuck : (r.type == R_MUTE ? g.cfg.optMuteGame : (r.type == R_CLIP_ON ? g.cfg.optClip : (r.type == R_OV_ON ? g.cfg.optOverlay : g.cfg.optCross))));
            const wchar_t* title = r.type == R_AUTO ? kAutoTitle : (r.type == R_DUCK ? kDuckTitle : (r.type == R_MUTE ? kMuteTitle : (r.type == R_CLIP_ON ? kClipTitle : (r.type == R_OV_ON ? kOvTitle : kCrossTitle))));
            const wchar_t* desc = r.type == R_AUTO ? kAutoDesc : (r.type == R_DUCK ? kDuckDesc : (r.type == R_MUTE ? kMuteDesc : (r.type == R_CLIP_ON ? kClipDesc : (r.type == R_OV_ON ? kOvDesc : kCrossDesc))));
            const int textW = cardW - pad * 2 - S(64) - S(16);
            FillRound(dc, card, S(14), hot ? kCardHover : kCard, v ? Blend(kBorder, kBoost, 55) : (hot ? kBorderHot : kBorder));
            DrawText2(dc, g.fBold, kText, { card.left + pad, card.top + S(14), card.left + pad + textW, card.top + S(14) + S(24) }, title);
            DrawText2(dc, g.fSmall, kTextDim, { card.left + pad, card.top + S(14) + S(24), card.left + pad + textW, card.bottom - S(8) }, desc, DT_LEFT | DT_TOP | DT_WORDBREAK);
            RECT tg{ card.right - pad - S(52), card.top + (r.height - S(28)) / 2, card.right - pad, card.top + (r.height - S(28)) / 2 + S(28) };
            DrawToggle(dc, tg, v, hot, kBoost);
            continue;
        }
        // Programmzeile
        const boostmode::AppInfo& a = g.apps[r.idx];
        const bool sel = r.type == R_APP_CLOSE ? Has(g.cfg.closeApps, a.exe) : Has(g.cfg.gameApps, a.exe);
        const bool running = a.procCount > 0;
        FillRound(dc, card, S(14), hot ? kCardHover : kCard, sel ? Blend(kBorder, kBoost, 55) : (hot ? kBorderHot : kBorder));
        DrawCheck(dc, { card.left + pad, card.top + (r.height - S(22)) / 2, card.left + pad + S(22), card.top + (r.height - S(22)) / 2 + S(22) }, sel, hot);
        const int tx = card.left + pad + S(22) + S(14);
        DrawText2(dc, g.fBold, running ? kText : kTextDim, { tx, card.top + S(9), card.right - S(130), card.top + S(9) + S(24) }, a.exe, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        DrawText2(dc, g.fSmall, kTextDim, { tx, card.top + S(9) + S(24), card.right - S(130), card.bottom - S(4) }, a.title, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (running) {
            DrawText2(dc, g.fBold, kTextDim, { card.right - S(120), card.top + S(9), card.right - pad, card.top + S(9) + S(24) }, MB(a.ramBytes), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            if (a.procCount > 1)
                DrawText2(dc, g.fSmall, kTextDim, { card.right - S(120), card.top + S(9) + S(24), card.right - pad, card.bottom - S(4) }, std::to_wstring(a.procCount) + L" Prozesse", DT_RIGHT | DT_TOP | DT_SINGLELINE);
        }
    }
    const int view = static_cast<int>(g.list.bottom - g.list.top);
    if (g.contentH > view) {
        const int trackH = view - S(4);
        const int thumbH = std::max(S(36), trackH * view / g.contentH);
        const int thumbY = g.list.top + S(2) + (trackH - thumbH) * g.scroll / std::max(1, g.contentH - view);
        RECT th{ g.list.right - S(6), thumbY, g.list.right - S(2), thumbY + thumbH };
        FillRound(dc, th, S(4), Blend(kBackground, RGB(255, 255, 255), 24), Blend(kBackground, RGB(255, 255, 255), 24));
    }
    RestoreDC(dc, -1);

    const std::wstring base = L"© 2026 Duncan · v" APP_VERSION;
    DrawText2(dc, g.fSmall, kTextDim, { g.footer.left, g.footer.top, g.footer.left + S(260), g.footer.bottom }, base, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawText2(dc, g.fSmall, g.statusColor, { g.footer.left + S(270), g.footer.top, g.footer.right, g.footer.bottom }, g.status, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (g.modal.open) DrawModal(dc, cr);
}

// ---------- Eingabe ----------
int HitTest(POINT p) {
    if (g.modal.open) {
        if (PtInRect(&g.modalOk, p)) return H_MODAL_OK;
        if (g.modalCancel.right > g.modalCancel.left && PtInRect(&g.modalCancel, p)) return H_MODAL_CANCEL;
        return H_NONE;
    }
    if (PtInRect(&g.hero, p)) return H_HERO;
    if (PtInRect(&g.list, p) && p.x < g.list.left + CardWidth()) {
        const int y = static_cast<int>(p.y - g.list.top) + g.scroll;
        for (size_t i = 0; i < g.rows.size(); ++i) {
            const Row& r = g.rows[i];
            if (y < r.top || y >= r.top + r.height) continue;
            if (r.type == R_GROUP) return H_GROUP + r.idx;
            if (r.type != R_HINT && r.type != R_HEAD) return H_ROW_BASE + static_cast<int>(i);
        }
    }
    return H_NONE;
}

void Scroll(int delta) {
    const int view = static_cast<int>(g.list.bottom - g.list.top);
    g.scroll = std::clamp(g.scroll + delta, 0, std::max(0, g.contentH - view));
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void Activate(int id) {
    if (id == H_MODAL_OK) { CloseModal(true); return; }
    if (id == H_MODAL_CANCEL) { CloseModal(false); return; }
    if (g.modal.open) return;
    if (id >= H_GROUP && id <= H_GROUP + 4) {
        bool& collapsed = id == H_GROUP ? g.autoCollapsed : (id == H_GROUP + 1 ? g.crossCollapsed : (id == H_GROUP + 3 ? g.clipCollapsed : (id == H_GROUP + 4 ? g.ovCollapsed : g.profCollapsed)));
        collapsed = !collapsed;
        g.hover = H_NONE;
        Layout();
        if (!collapsed) Scroll(g.contentH);      // aufgeklappt: nach unten scrollen, damit der neue Bereich sichtbar ist
        InvalidateRect(g.hwnd, nullptr, FALSE);
        return;
    }
    if (g.busy) return;
    if (id == H_HERO) ToggleMode();
    else if (id >= H_ROW_BASE && id - H_ROW_BASE < static_cast<int>(g.rows.size())) {
        ActivateRow(g.rows[id - H_ROW_BASE]);
        Layout();
        InvalidateRect(g.hwnd, nullptr, FALSE);
    }
}

void RestoreAudio() {                              // beim Beenden: Lautstaerken und Stummschaltung wie vorher
    if (!g.audio.Active()) return;
    if (auto backend = boostmode::MakeSystemAudio()) g.audio.RestoreAll(*backend);
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
        mmi->ptMinTrackSize.x = S(560);
        mmi->ptMinTrackSize.y = S(480);
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
    case WM_ACTIVATE:                                  // beim Zurueckkehren ins Fenster: Programmliste neu einlesen
        if (LOWORD(wp) != WA_INACTIVE && !g.busy && g.hwnd) { RefreshApps(); Layout(); InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
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
        else if (wp == VK_NEXT) Scroll(static_cast<int>(g.list.bottom - g.list.top)); else if (wp == VK_PRIOR) Scroll(-static_cast<int>(g.list.bottom - g.list.top));
        else if (wp == VK_HOME) Scroll(-g.contentH); else if (wp == VK_END) Scroll(g.contentH);
        return 0;
    case WM_JOB_DONE: OnJobDone(reinterpret_cast<JobResult*>(lp)); return 0;
    case WM_TIMER:
        if (wp == kTimerSpin) { g.spin = (g.spin + 30) % 360; InvalidateRect(hwnd, &g.hero, FALSE); }
        else if (wp == kTimerAuto) AutoPoll();
        else if (wp == kTimerList) ListPoll();
        else if (wp == kTimerLive) LiveTick();
        else if (wp == kTimerAudio) AudioTick();
        else if (wp == kTimerCross) CrossTick();
        else if (wp == kTimerClip) ClipTick();
        else if (wp == kTimerOverlay) OverlayTick();
        return 0;
    case WM_ENDSESSION:
        if (wp) RestoreAudio();
        return 0;
    case WM_HOTKEY:
        if (wp == kClipHotkeyId) SaveClipNow();
        else if (wp == kOverlayHotkeyId) { g.overlay.SetPanelOpen(!g.overlay.PanelOpen()); OverlayTick(); }
        return 0;
    case WM_OVERLAY_ACTION:
        if (wp == boostmode::OA_CLOSE) g.overlay.SetPanelOpen(false);
        else if (wp == boostmode::OA_END_BOOST) { if (g.state.active && !g.busy) StartJob(false, false); }
        else if (wp == boostmode::OA_SAVE_CLIP) SaveClipNow();
        else if (wp == boostmode::OA_TOGGLE_REC) { g.cfg.optClip = !g.cfg.optClip; SaveCfg(); ClipTick(); }
        OverlayTick();
        return 0;
    case WM_CLIP_SAVED: {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lp));
        g.clipSaving = false;
        if (wp) {
            MessageBeep(MB_OK);
            const size_t k = text->find_last_of(L"\\");
            SetStatus(L"Clip gespeichert: " + (k == std::wstring::npos ? *text : text->substr(k + 1)), kGreen);
            OverlayToast(L"Clip gespeichert", true);
        } else { SetStatus(L"Clip konnte nicht gespeichert werden: " + *text, kRed); OverlayToast(L"Clip nicht gespeichert", false); }
        OverlayTick();
        return 0;
    }
    case WM_DESTROY: g.overlay.Destroy(); if (g.ovHotkeyOn) UnregisterHotKey(hwnd, kOverlayHotkeyId); g.rec.Stop(); if (g.clipHotkeyOn) UnregisterHotKey(hwnd, kClipHotkeyId); RestoreAudio(); g.cross.Destroy(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    darkmenus::Enable();
    HANDLE single = CreateMutexW(nullptr, TRUE, L"DuncansBoost.Single");
    if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(L"DuncansBoost", nullptr)) { ShowWindow(other, SW_RESTORE); SetForegroundWindow(other); }
        return 0;
    }
    IconsInit();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"DuncansBoost";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = wc.hIconSm = LoadIconW(inst, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);

    g.dpi = GetDpiForSystem();
    MakeFonts();
    g.hwnd = CreateWindowExW(0, wc.lpszClassName, kTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, S(640), S(700), nullptr, nullptr, inst, nullptr);
    if (!g.hwnd) return 1;
    g.dpi = GetDpiForWindow(g.hwnd);
    MakeFonts();
    gfx::EnableDarkTitleBar(g.hwnd);

    const bool firstRun = !boostmode::SettingsFileExists();
    g.cfg = boostmode::LoadSettings();
    g.cfg.optPower = true;          // einfach halten: Hoechstleistung und Neustart der Programme sind immer an,
    g.cfg.optRestart = true;        // Benachrichtigungen werden nicht angefasst
    g.cfg.optToast = false;
    g.state = boostmode::LoadState();
    g.profiles = boostmode::LoadProfiles();
    RefreshApps();
    if (firstRun) {                 // erster Start: laufende, unkritische Hintergrundhelfer (Cloud-Sync, Firmenchat) vorhaken
        for (const std::wstring& e : boostmode::SuggestedHelpers())
            if (std::any_of(g.apps.begin(), g.apps.end(), [&](const boostmode::AppInfo& a) { return a.exe == e && a.procCount > 0; })) g.cfg.closeApps.push_back(e);
        SaveCfg();
    }
    Layout();
    if (g.state.active) SetStatus(L"Der Zock-Modus war noch aktiv (frühere Sitzung). Zum Wiederherstellen ausschalten.", kOrange);
    SetTimer(g.hwnd, kTimerAuto, 3000, nullptr);
    SetTimer(g.hwnd, kTimerList, 5000, nullptr);
    g.live = std::make_unique<boostmode::LiveStats>();
    g.cur = g.live->Sample();
    SetTimer(g.hwnd, kTimerLive, 1000, nullptr);
    SetTimer(g.hwnd, kTimerAudio, 700, nullptr);
    SetTimer(g.hwnd, kTimerCross, 500, nullptr);
    SetTimer(g.hwnd, kTimerClip, 1500, nullptr);
    SetTimer(g.hwnd, kTimerOverlay, 500, nullptr);
    g.overlay.SetActionHandler([](int a) { PostMessageW(g.hwnd, WM_OVERLAY_ACTION, static_cast<WPARAM>(a), 0); });
    ShowWindow(g.hwnd, show);
    UpdateWindow(g.hwnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    IconsShutdown();
    CoUninitialize();
    return static_cast<int>(m.wParam);
}
