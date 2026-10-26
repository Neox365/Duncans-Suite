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
#include <shellapi.h>

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
#include "../src/SuiteSafety.h"
#include "PrivacyRules.h"
#include "../common/Theme.h"
#include "../common/Gfx.h"

#ifndef APP_VERSION
#define APP_VERSION L"0.6.3"   // normalerweise aus CMake (PROJECT_VERSION)
#endif

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#endif

namespace {

// Gemeinsame Palette und Helfer aus common/; nur Privacy-Shield-eigene Farben bleiben hier
using namespace theme;
using gfx::Blend;
using gfx::TextWidth;
const COLORREF kCard       = kCardRaised;
const COLORREF kBorderHot  = kBorderHover;
const COLORREF kViolet     = RGB(167, 139, 250);

const wchar_t* kTitle = L"Duncan's Privacy Shield";
const UINT WM_RP_DONE = WM_APP + 1;     // Wiederherstellungspunkt fertig (wp = Erfolg)
const UINT WM_JOB_DONE = WM_APP + 2;    // Hintergrund-Auftrag fertig (lp = JobResult*)

enum HitId { H_NONE = -1, H_ALL = 1000, H_RESTORE = 1001, H_RP = 1002, H_FOLDER = 1003, H_MODAL_OK = 2000, H_MODAL_CANCEL = 2001, H_INFO_BASE = 3000 };

struct Row {
    bool group = false;
    int setting = -1;          // Index in AllSettings()
    std::wstring text;
    int top = 0, height = 0;   // in Inhaltskoordinaten
    int descH = 0;
    int titleW = 0, badgeW = 0;   // Breiten fuer Badge und Info-Symbol neben dem Titel
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
    RECT list{}, btnAll{}, btnRestore{}, btnRp{}, folderLink{}, toolbar{}, footer{};
    bool hasBackup = false;
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

// Orientierungshilfe je Einstellung: Badge ("Empfohlen"/"Erweitert") und optionaler Hinweis mit moeglichen Nebenwirkungen (Info-Symbol + Tooltip)
struct Meta { int id; const wchar_t* badge; const wchar_t* note; };
const Meta kMeta[] = {
    { 1, L"Empfohlen", L"Bei Windows Home und Pro bleibt technisch die Stufe „Erforderlich“ aktiv; Telemetrie lässt sich nur bei Enterprise/Education vollständig abschalten. Nebenwirkungen: keine." },
    { 2, L"Empfohlen", nullptr },
    { 3, L"Erweitert", L"Windows sendet keine Diagnosedaten mehr; Windows Update und Defender laufen normal weiter. Diagnose- und Feedback-Werkzeuge von Microsoft liefern eventuell weniger Daten. Der Schalter macht alles rückgängig." },
    { 4, L"Empfohlen", nullptr },
    { 5, L"Empfohlen", nullptr },
    { 6, L"Empfohlen", nullptr },
    { 7, L"Erweitert", L"Karten-, Wetter- und Navigations-Apps sowie „Mein Gerät suchen“ erhalten keinen Standort mehr; auch die automatische Zeitzone kann betroffen sein. Für Einsteiger nur aktivieren, wenn kein Standort gebraucht wird." },
    { 8, L"Empfohlen", nullptr },
    { 9, L"Empfohlen", nullptr },
    { 10, L"Empfohlen", nullptr },
    { 11, L"Empfohlen", nullptr },
    { 12, L"Empfohlen", nullptr },
    { 14, L"Empfohlen", L"Setzt Richtlinien für Microsoft Edge: Edge zeigt dann den Hinweis „Von deiner Organisation verwaltet“ an, funktioniert aber normal. Die Startbeschleunigung fehlt, Edge öffnet dadurch etwas langsamer. Der Schalter macht alles rückgängig." },
    { 15, L"Empfohlen", L"Updates und Store-Apps laden nur noch direkt von Microsoft; bei sehr langsamer Internetverbindung im Heimnetz mit mehreren PCs kann das Laden etwas länger dauern. Der Schalter macht alles rückgängig." },
    { 16, L"Empfohlen", L"Wirkt auf Rechnern mit Recall (Windows 11 24H2 auf Copilot+-PCs); auf anderen Rechnern ist die Richtlinie harmlos und ohne Wirkung. Der Schalter macht alles rückgängig." },
    { 17, L"Empfohlen", L"Diktat und Sprachassistenten, die Microsoft-Server nutzen, funktionieren danach nur noch mit der Offline-Erkennung (Sprachpakete nötig). Der Schalter macht alles rückgängig." },
    { 13, L"Empfohlen", L"Windows installiert Treiber dann nicht mehr automatisch; Treiber-Updates müssen manuell oder über Hersteller-Tools kommen. Kann die Treibersuche über Windows Update im Duncan's Driver Updater beeinflussen (nicht geprüft). Schalter macht alles rückgängig." },
};

const Meta* MetaFor(int settingId) {
    for (const Meta& m : kMeta) if (m.id == settingId) return &m;
    return nullptr;
}

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
        r.titleW = TextWidth(dc, g.fBold, all[i].title);
        if (const Meta* mt = MetaFor(all[i].id)) r.badgeW = TextWidth(dc, g.fSmall, mt->badge) + S(18);
        r.height = S(16) + S(24) + r.descH + S(16);
        g.rows.push_back(r);
        y += r.height + S(10);
    }
    g.contentH = y;
}

// Rechtecke von Badge und Info-Symbol einer Karte (card = Kartenrechteck auf dem Bildschirm)
void BadgeRects(const Row& r, const RECT& card, RECT& badge, RECT& info) {
    const int x0 = card.left + S(18) + r.titleW + S(12);
    const int cy = card.top + S(14) + S(12);
    badge = { x0, cy - S(10), x0 + r.badgeW, cy + S(10) };
    const int ix = badge.right + S(10);
    info = { ix, cy - S(9), ix + S(18), cy + S(9) };
}

void Layout() {
    RECT cr; GetClientRect(g.hwnd, &cr);
    const int m = S(28);
    g.toolbar = { m, S(104), cr.right - m, S(104) + S(44) };
    g.footer = { m, cr.bottom - S(34), cr.right - m, cr.bottom - S(8) };
    g.list = { m, g.toolbar.bottom + S(14), cr.right - m, g.footer.top - S(6) };
    HDC dc = GetDC(g.hwnd);
    const int bh = S(40), top = g.toolbar.top + (S(44) - bh) / 2, gap = S(10);
    auto w = [&](const wchar_t* t) { return TextWidth(dc, g.fBold, t) + S(36); };
    g.btnAll = { g.toolbar.left, top, g.toolbar.left + w(L"Alle schützen"), top + bh };
    g.btnRestore = { g.btnAll.right + gap, top, g.btnAll.right + gap + w(L"Standard wiederherstellen"), top + bh };
    g.btnRp = { g.btnRestore.right + gap, top, g.btnRestore.right + gap + w(L"Wiederherstellungspunkt erstellen"), top + bh };
    // Link rechts in der Fusszeile: Ordner-Symbol + Text
    const std::wstring link = g.hasBackup ? L"Sicherung vorhanden" : L"Backup-Ordner";
    const int lw = TextWidth(dc, g.fSmall, link) + S(34);
    g.folderLink = { g.footer.right - lw, g.footer.top, g.footer.right, g.footer.bottom };
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
    const bool had = g.hasBackup;
    g.hasBackup = privacy::HasBackup();
    if (had != g.hasBackup && g.hwnd) Layout();        // Linktext aendert sich mit dem Sicherungsstatus
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
    bool restorePoint = false;
    int ok = 0, total = 0;
    std::wstring error;
};

void Log(const std::wstring& line) { suite_safety::Log(L"PrivacyShield", line); }   // %ProgramData%\DuncansSuite\duncans_suite.log

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
    if (r->restorePoint) {
        if (r->error.empty()) { g.restorePointDone = true; SetStatus(L"Wiederherstellungspunkt „Duncan's Suite Pre-Optimization“ angefordert. Windows legt standardmäßig nur einen Punkt pro 24 Stunden an.", kGreen); }
        else SetStatus(L"Kein Wiederherstellungspunkt: " + r->error, kRed);
    } else if (r->restoreAll) {
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
    if (!suite_safety::AutoRestorePointEnabled()) {       // Suite-Einstellung: automatischer Wiederherstellungspunkt aus
        Log(L"Wiederherstellungspunkt übersprungen (Suite-Einstellung)");
        g.restorePointDone = true;
        next();
        return;
    }
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

// Handler von "Wiederherstellungspunkt erstellen": loest SRSetRestorePoint manuell aus (im Hintergrund)
void CreateRestorePointNow() {
    StartJob([](JobResult& res) {
        res.restorePoint = true;
        std::wstring err;
        if (!privacy::CreateRestorePoint(L"", err)) res.error = err;
    }, L"Wiederherstellungspunkt wird erstellt … (das kann einige Sekunden dauern)");
}

// Link in der Fusszeile: Backup-Ordner im Explorer oeffnen
void OpenBackupFolder() {
    const std::wstring dir = suite_safety::BackupsDir();      // legt %ProgramData%\DuncansSuite\Backups bei Bedarf an
    if (dir.empty() || reinterpret_cast<INT_PTR>(ShellExecuteW(g.hwnd, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
        SetStatus(L"Der Backup-Ordner konnte nicht geöffnet werden.", kRed);
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
    DrawButton(dc, g.btnRp, L"Wiederherstellungspunkt erstellen", false, !busy && g.hover == H_RP, g.press == H_RP && g.hover == H_RP, !g.busy);
    const std::wstring count = std::to_wstring(AppliedCount()) + L" von " + std::to_wstring(g.applied.size()) + L" aktiv";
    DrawText2(dc, g.fBold, AppliedCount() == static_cast<int>(g.applied.size()) ? kGreen : kTextDim,
              { cr.right - S(28) - S(200), S(30), cr.right - S(28), S(58) }, count, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

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
        if (const Meta* mt = MetaFor(all[r.setting].id)) {        // Badge und Info-Symbol neben dem Titel
            RECT badge, info;
            BadgeRects(r, card, badge, info);
            const bool rec = wcscmp(mt->badge, L"Empfohlen") == 0;
            const COLORREF bc = rec ? kGreen : kOrange;
            FillRound(dc, badge, S(10), Blend(fill, bc, 16), Blend(fill, bc, 45));
            DrawText2(dc, g.fSmall, bc, badge, mt->badge, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (mt->note) {
                const bool ih = g.hover == H_INFO_BASE + r.setting;
                Gdiplus::Graphics gr(dc);
                gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                Gdiplus::Pen ring(ih ? Gdiplus::Color(255, 235, 237, 242) : Gdiplus::Color(255, 150, 155, 168), static_cast<float>(S(1)) + 0.5f);
                gr.DrawEllipse(&ring, static_cast<int>(info.left), static_cast<int>(info.top), static_cast<int>(info.right - info.left - 1), static_cast<int>(info.bottom - info.top - 1));
                DrawText2(dc, g.fBold, ih ? kText : kTextDim, info, L"i", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
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
    DrawText2(dc, g.fSmall, g.statusColor, { g.footer.left + S(190), g.footer.top, g.folderLink.left - S(12), g.footer.bottom }, g.status,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    {   // Link "Sicherung / Ordner oeffnen" mit kleinem Ordner-Symbol: oeffnet %ProgramData%\DuncansSuite\Backups im Explorer
        const bool hot = !busy && g.hover == H_FOLDER;
        const COLORREF lc = hot ? kText : (g.hasBackup ? kGreen : kTextDim);
        Gdiplus::Graphics gr(dc);
        gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(lc), GetGValue(lc), GetBValue(lc)), static_cast<float>(S(1)) + 0.4f);
        const int fx = g.folderLink.left, fy = (g.folderLink.top + g.folderLink.bottom) / 2;
        Gdiplus::GraphicsPath fp;                       // Ordner: Reiter links oben, Koerper darunter
        fp.AddLine(fx, fy - S(5), fx + S(6), fy - S(5));
        fp.AddLine(fx + S(6), fy - S(5), fx + S(8), fy - S(3));
        fp.AddLine(fx + S(8), fy - S(3), fx + S(15), fy - S(3));
        fp.AddLine(fx + S(15), fy - S(3), fx + S(15), fy + S(6));
        fp.AddLine(fx + S(15), fy + S(6), fx, fy + S(6));
        fp.CloseFigure();
        gr.DrawPath(&pen, &fp);
        DrawText2(dc, g.fSmall, lc, { fx + S(24), g.folderLink.top, g.folderLink.right, g.folderLink.bottom },
                  g.hasBackup ? L"Sicherung vorhanden" : L"Backup-Ordner", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    // Tooltip zum Info-Symbol (ueber allem anderen)
    if (!busy && g.hover >= H_INFO_BASE) {
        const int si = g.hover - H_INFO_BASE;
        const auto& allS = privacy::AllSettings();
        if (si < static_cast<int>(allS.size())) {
            const Meta* mt = MetaFor(allS[si].id);
            for (const Row& r : g.rows) {
                if (r.setting != si || !mt || !mt->note) continue;
                RECT card{ g.list.left, g.list.top + r.top - g.scroll, g.list.left + CardWidth(), g.list.top + r.top - g.scroll + r.height };
                RECT badge, info;
                BadgeRects(r, card, badge, info);
                const int w = std::min<int>(S(340), cr.right - S(40));
                const int th = WrappedHeight(dc, g.fSmall, mt->note, w - S(28)) + S(24);
                RECT tip{ info.left - S(10), info.bottom + S(8), info.left - S(10) + w, info.bottom + S(8) + th };
                if (tip.right > cr.right - S(12)) OffsetRect(&tip, cr.right - S(12) - tip.right, 0);
                if (tip.bottom > cr.bottom - S(40)) OffsetRect(&tip, 0, -(th + S(8) + (info.bottom - info.top) + S(8)));   // nach oben ausweichen
                DrawShadow(dc, tip, S(10), static_cast<int>(g.dpi));
                FillRound(dc, tip, S(10), RGB(30, 33, 41), kBorderHot);
                DrawText2(dc, g.fSmall, kText, { tip.left + S(14), tip.top + S(12), tip.right - S(14), tip.bottom - S(8) }, mt->note, DT_LEFT | DT_TOP | DT_WORDBREAK);
            }
        }
    }

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
    if (PtInRect(&g.btnRp, p)) return H_RP;
    if (PtInRect(&g.folderLink, p)) return H_FOLDER;
    if (PtInRect(&g.list, p)) {
        const auto& allS = privacy::AllSettings();
        for (const Row& r : g.rows) {                  // Info-Symbole haben Vorrang vor der Karte
            if (r.group) continue;
            const Meta* mt = MetaFor(allS[r.setting].id);
            if (!mt || !mt->note) continue;
            RECT card{ g.list.left, g.list.top + r.top - g.scroll, g.list.left + CardWidth(), g.list.top + r.top - g.scroll + r.height };
            RECT badge, info;
            BadgeRects(r, card, badge, info);
            InflateRect(&info, S(3), S(3));
            if (PtInRect(&info, p)) return H_INFO_BASE + r.setting;
        }
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
    else if (id == H_RP) CreateRestorePointNow();
    else if (id == H_FOLDER) OpenBackupFolder();
    else if (id >= H_INFO_BASE) return;                 // Info-Symbol: nur Tooltip
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
        mmi->ptMinTrackSize.x = S(780);
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
    g.hwnd = CreateWindowExW(0, wc.lpszClassName, kTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, S(800), S(700), nullptr, nullptr, inst, nullptr);
    if (!g.hwnd) return 1;
    g.dpi = GetDpiForWindow(g.hwnd);
    MakeFonts();
    gfx::EnableDarkTitleBar(g.hwnd);
    RefreshApplied();
    Layout();
    SetStatus(g.hasBackup ? L"" : L"Vor der ersten Änderung wird ein Wiederherstellungspunkt erstellt.", kTextDim);
    ShowWindow(g.hwnd, show);
    UpdateWindow(g.hwnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    IconsShutdown();
    return static_cast<int>(m.wParam);
}
