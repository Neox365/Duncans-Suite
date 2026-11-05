// Duncan's Equalizer (Win32 + GDI/GDI+, dunkle Oberflaeche).
// Zehn Regler, Voreinstellungen, Bass-Schub und "Sprache klarer" fuer den ganzen PC. Der Ton wird von Equalizer APO
// (freier Audio-Filter) verarbeitet; dieses Programm ist die einfache Bedienung dafuer und schreibt nur dessen Konfiguration.
// Laeuft ohne Administratorrechte; nur wenn Windows den Ordner von Equalizer APO schuetzt, kann man die Einstellung
// einmalig mit Administratorrechten uebernehmen (--apply).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using std::max;
using std::min;
#include <objbase.h>
#include <propidl.h>
#include <gdiplus.h>

#include "../src/Icons.h"
#include "../common/Theme.h"
#include "../common/Gfx.h"
#include "EqLogic.h"

#ifndef APP_VERSION
#define APP_VERSION L"0.3.0"   // normalerweise aus CMake
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
const COLORREF kEq = RGB(125, 211, 252);            // Akzent dieses Programms (Himmelblau)
const COLORREF kOnEq = RGB(8, 24, 34);              // Schrift auf dem Akzent

const wchar_t* kTitle = L"Duncan's Equalizer";
const UINT_PTR kTimerApply = 1, kTimerDevice = 2, kTimerDisarm = 3;
const UINT WM_NAME_DONE = WM_APP + 1;      // wParam = 1: Name uebernehmen, 0: abbrechen

enum HitId {
    H_NONE = -1, H_HERO = 1, H_BASS = 2, H_VOICE = 3, H_DOWNLOAD = 4, H_RECHECK = 5, H_CONFIGURATOR = 6, H_ADMIN = 7,
    H_PRESET = 10,      // 10 .. 10 + Anzahl Voreinstellungen
    H_PROFILE = 200,    // 200 .. 205: Profil waehlen
    H_PROFNEW = 210, H_BIND = 211, H_IMPORT = 212, H_PROFDEL = 213,
    H_BAND = 100,       // 100 .. 109 = Baender, 110 = Preamp
    H_RESET = 121
};

struct App {
    HWND hwnd = nullptr;
    UINT dpi = 96;
    HFONT fTitle = nullptr, fHead = nullptr, fBody = nullptr, fSmall = nullptr, fBold = nullptr;
    eq::Settings cfg;
    std::wstring settingsPath;
    std::wstring installDir;                       // leer = Equalizer APO nicht gefunden
    bool needAdmin = false;                        // Schreiben im Ordner von Equalizer APO wurde verweigert
    RECT hero{}, heroToggle{}, adminBar{}, adminBtn{}, presetRect[8]{}, panel{}, band[eq::kBands + 1]{}, btnReset{}, cardBass{}, cardVoice{}, confBtn{},
         setup{}, dlBtn{}, recheckBtn{}, footer{};
    std::vector<eq::Profile> profiles;             // immer mindestens eines
    int active = 0;                                // aktives Profil (Regler, Schalter und Korrektur werden darin gespeichert)
    std::wstring profilesPath;
    std::wstring lastDevice;                       // zuletzt gesehenes Standard-Ausgabegeraet
    RECT profCard{}, profChip[eq::kMaxProfiles]{}, profNew{}, btnBind{}, btnImport{}, btnDel{}, nameRect{};
    HWND nameEdit = nullptr;                       // Eingabefeld fuer den Namen eines neuen Profils
    WNDPROC nameOldProc = nullptr;
    bool delArmed = false;                         // "Loeschen" wurde einmal geklickt, zweiter Klick loescht
    int hover = H_NONE, press = H_NONE, drag = -1;
    bool tracking = false;
    std::wstring status;
    COLORREF statusColor = kTextDim;
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

void FillRound(HDC dc, const RECT& r, int radius, COLORREF fill, COLORREF border) { DrawPanelAA(dc, r, radius, fill, border); }

void SetStatus(const std::wstring& t, COLORREF c = kTextDim) { g.status = t; g.statusColor = c; InvalidateRect(g.hwnd, nullptr, FALSE); }

// Equalizer APO suchen. Nur zum Testen der Oberflaeche: DUNCANS_EQ_TESTDIR zeigt auf einen nachgebildeten Ordner mit "config"
// (gilt nie im --apply-Modus mit Administratorrechten).
std::wstring Detect() {
    wchar_t t[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"DUNCANS_EQ_TESTDIR", t, MAX_PATH) && t[0]) return t;
    return eq::FindInstallDir();
}

// ---------- Einstellungen anwenden ----------
void SyncActive() {                                 // aktuelle Regler/Schalter/Korrektur ins aktive Profil schreiben
    eq::Profile& p = g.profiles[static_cast<size_t>(g.active)];
    for (int b = 0; b < eq::kBands; ++b) p.s.gains[b] = g.cfg.gains[b];
    p.s.preamp = g.cfg.preamp;
    p.s.bass = g.cfg.bass; p.s.voice = g.cfg.voice; p.s.extra = g.cfg.extra;
}

void LoadActive() {                                 // Einstellungen des aktiven Profils uebernehmen (Ein/Aus bleibt)
    const eq::Profile& p = g.profiles[static_cast<size_t>(g.active)];
    for (int b = 0; b < eq::kBands; ++b) g.cfg.gains[b] = p.s.gains[b];
    g.cfg.preamp = p.s.preamp;
    g.cfg.bass = p.s.bass; g.cfg.voice = p.s.voice; g.cfg.extra = p.s.extra;
}

void SaveCfg() {
    SyncActive();
    eq::SaveSettings(g.cfg, g.settingsPath);
    eq::SaveProfiles(g.profiles, g.active, g.profilesPath);
}

void ApplyNow() {
    KillTimer(g.hwnd, kTimerApply);
    if (g.installDir.empty()) return;
    switch (eq::ApplyToDir(g.installDir, g.cfg)) {
    case eq::ApplyResult::Applied:
        g.needAdmin = false;
        SetStatus(g.cfg.enabled ? L"Übernommen – wirkt sofort" : L"Equalizer ist aus", kGreen);
        break;
    case eq::ApplyResult::AccessDenied:
        g.needAdmin = true;
        SetStatus(L"Noch nicht übernommen: Windows schützt den Ordner von Equalizer APO", kOrange);
        break;
    case eq::ApplyResult::NotInstalled:
        g.installDir.clear();
        SetStatus(L"Equalizer APO wurde nicht mehr gefunden", kRed);
        break;
    default:
        SetStatus(L"Die Einstellung konnte nicht geschrieben werden", kRed);
        break;
    }
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void Layout();
void Changed() {                                    // speichern, kurz warten (Regler ziehen), dann uebernehmen
    SaveCfg();
    Layout();
    SetTimer(g.hwnd, kTimerApply, 350, nullptr);
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void ApplyAsAdmin() {                               // dasselbe noch einmal, aber mit Administratorrechten (UAC-Abfrage)
    SaveCfg();
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::wstring params = L"--apply \"" + g.settingsPath + L"\"";
    SHELLEXECUTEINFOW sei{ sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = g.hwnd;
    sei.lpVerb = L"runas";
    sei.lpFile = exe;
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) { SetStatus(L"Abgebrochen – nichts geändert", kTextDim); return; }
    WaitForSingleObject(sei.hProcess, 20000);
    DWORD code = 3;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    if (code == static_cast<DWORD>(eq::ApplyResult::Applied)) { g.needAdmin = false; SetStatus(L"Übernommen – wirkt sofort", kGreen); }
    else SetStatus(L"Auch mit Administratorrechten nicht möglich (Fehler " + std::to_wstring(code) + L")", kRed);
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void Recheck() {
    g.installDir = Detect();
    if (g.installDir.empty()) SetStatus(L"Equalizer APO wurde noch nicht gefunden", kOrange);
    else { SetStatus(L"Equalizer APO gefunden", kGreen); ApplyNow(); }
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

// ---------- Profile ----------
void StopNameEdit() {
    if (!g.nameEdit) return;
    HWND e = g.nameEdit;
    g.nameEdit = nullptr;
    SetWindowLongPtrW(e, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g.nameOldProc));
    DestroyWindow(e);
    SetFocus(g.hwnd);
}

void SwitchProfile(int i, const std::wstring& why = L"") {
    if (i < 0 || i >= static_cast<int>(g.profiles.size()) || i == g.active) return;
    SyncActive();
    g.active = i;
    LoadActive();
    g.delArmed = false;
    SaveCfg();
    SetTimer(g.hwnd, kTimerApply, 150, nullptr);
    if (!why.empty()) SetStatus(why, kGreen);
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

LRESULT CALLBACK NameProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_KEYDOWN && (w == VK_RETURN || w == VK_ESCAPE)) { PostMessageW(g.hwnd, WM_NAME_DONE, w == VK_RETURN ? 1 : 0, 0); return 0; }
    if (m == WM_CHAR && (w == VK_RETURN || w == VK_ESCAPE)) return 0;
    if (m == WM_KILLFOCUS) PostMessageW(g.hwnd, WM_NAME_DONE, 0, 0);
    return CallWindowProcW(g.nameOldProc, h, m, w, l);
}

void BeginNewProfile() {
    if (g.nameEdit || static_cast<int>(g.profiles.size()) >= eq::kMaxProfiles) return;
    g.nameEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, g.nameRect.left, g.nameRect.top,
                                 g.nameRect.right - g.nameRect.left, g.nameRect.bottom - g.nameRect.top, g.hwnd, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g.nameEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g.fBody), TRUE);
    SendMessageW(g.nameEdit, EM_SETLIMITTEXT, eq::kMaxNameLen, 0);
    SendMessageW(g.nameEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(S(10), S(10)));
    g.nameOldProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g.nameEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(NameProc)));
    SetFocus(g.nameEdit);
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void FinishNewProfile(bool ok) {
    if (!g.nameEdit) return;
    wchar_t buf[64] = {};
    GetWindowTextW(g.nameEdit, buf, 64);
    StopNameEdit();
    if (ok) {
        const std::wstring name = eq::CleanName(buf);
        if (name.empty()) SetStatus(L"Bitte einen Namen eingeben", kOrange);
        else if (eq::FindProfile(g.profiles, name) >= 0) SetStatus(L"Ein Profil mit diesem Namen gibt es schon", kOrange);
        else {
            SyncActive();
            eq::Profile p = g.profiles[static_cast<size_t>(g.active)];      // Kopie der aktuellen Einstellungen
            p.name = name;
            p.device.clear();
            g.profiles.push_back(p);
            g.active = static_cast<int>(g.profiles.size()) - 1;
            SaveCfg();
            SetStatus(L"Profil „" + name + L"“ angelegt", kGreen);
        }
    }
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void BindDevice() {
    eq::Profile& p = g.profiles[static_cast<size_t>(g.active)];
    if (!p.device.empty()) { p.device.clear(); SaveCfg(); SetStatus(L"Gerät gelöst – dieses Profil wird nicht mehr automatisch gewählt", kTextDim); return; }
    if (g.lastDevice.empty()) { SetStatus(L"Kein Ausgabegerät erkannt", kOrange); return; }
    for (eq::Profile& o : g.profiles) if (&o != &p && o.device == g.lastDevice) o.device.clear();      // ein Gerät gehört zu einem Profil
    p.device = g.lastDevice;
    SaveCfg();
    SetStatus(L"Wird jetzt automatisch gewählt, sobald „" + g.lastDevice + L"“ das Standardgerät ist", kGreen);
}

void ImportFilters() {
    if (!g.cfg.extra.empty()) { g.cfg.extra.clear(); Changed(); SetStatus(L"Korrektur entfernt", kTextDim); return; }
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = L"Textdatei (*.txt)\0*.txt\0Alle Dateien\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    HANDLE h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { SetStatus(L"Datei konnte nicht gelesen werden", kRed); return; }
    std::string text;
    char buf[8192]; DWORD n = 0;
    while (text.size() < 300000 && ReadFile(h, buf, sizeof(buf), &n, nullptr) && n) text.append(buf, n);
    CloseHandle(h);
    int ignored = 0;
    std::vector<std::string> lines = eq::ParseFilterFile(text, ignored);
    if (lines.empty()) { SetStatus(L"In der Datei stehen keine nutzbaren Filterzeilen (erwartet: „Filter: ON PK Fc … Hz Gain … dB Q …“)", kOrange); return; }
    g.cfg.extra = lines;
    Changed();
    SetStatus(std::to_wstring(lines.size()) + L" Zeilen übernommen" + (ignored ? L", " + std::to_wstring(ignored) + L" ignoriert" : std::wstring()), kGreen);
}

void DeleteProfile() {
    if (g.profiles.size() <= 1) { SetStatus(L"Das letzte Profil bleibt erhalten", kOrange); return; }
    if (!g.delArmed) { g.delArmed = true; SetTimer(g.hwnd, kTimerDisarm, 3000, nullptr); return; }
    g.delArmed = false;
    KillTimer(g.hwnd, kTimerDisarm);
    const std::wstring name = g.profiles[static_cast<size_t>(g.active)].name;
    g.profiles.erase(g.profiles.begin() + g.active);
    g.active = 0;
    LoadActive();
    SaveCfg();
    SetTimer(g.hwnd, kTimerApply, 150, nullptr);
    SetStatus(L"Profil „" + name + L"“ gelöscht", kTextDim);
}

void PollDevice() {                                 // Geraetewechsel erkennen und ein daran gebundenes Profil waehlen
    const std::wstring now = eq::DefaultOutputName();
    if (now.empty() || now == g.lastDevice) return;
    g.lastDevice = now;
    const int idx = eq::ProfileForDevice(g.profiles, now);
    if (idx >= 0 && idx != g.active)
        SwitchProfile(idx, L"Profil „" + g.profiles[static_cast<size_t>(idx)].name + L"“ gewählt (" + now + L")");
    else InvalidateRect(g.hwnd, nullptr, FALSE);
}

// ---------- Layout ----------
int TrackTop() { return g.panel.top + S(42); }
int TrackBottom() { return g.panel.bottom - S(106); }

void Layout() {
    RECT cr; GetClientRect(g.hwnd, &cr);
    const int m = S(28);
    g.footer = { m, cr.bottom - S(34), cr.right - m, cr.bottom - S(8) };
    g.hero = { m, S(84), cr.right - m, S(84) + S(96) };
    g.heroToggle = { g.hero.right - S(18) - S(64), g.hero.top + (S(96) - S(34)) / 2, g.hero.right - S(18), g.hero.top + (S(96) - S(34)) / 2 + S(34) };
    int y = g.hero.bottom + S(14);
    g.adminBar = g.adminBtn = RECT{};
    HDC dc = GetDC(g.hwnd);
    if (g.installDir.empty()) {
        g.setup = { m, y, cr.right - m, y + S(292) };
        const int bh = S(40);
        const int dlW = std::max(S(220), TextWidth(dc, g.fBold, L"Equalizer APO herunterladen") + S(40));
        const int rcW = std::max(S(140), TextWidth(dc, g.fBold, L"Erneut prüfen") + S(40));
        g.dlBtn = { g.setup.left + S(22), g.setup.bottom - S(22) - bh, g.setup.left + S(22) + dlW, g.setup.bottom - S(22) };
        g.recheckBtn = { g.dlBtn.right + S(12), g.dlBtn.top, g.dlBtn.right + S(12) + rcW, g.dlBtn.bottom };
        ReleaseDC(g.hwnd, dc);
        return;
    }
    g.setup = g.dlBtn = g.recheckBtn = RECT{};
    if (g.needAdmin) {
        g.adminBar = { m, y, cr.right - m, y + S(54) };
        const int w = std::max(S(250), TextWidth(dc, g.fBold, L"Mit Administratorrechten übernehmen") + S(36));
        g.adminBtn = { g.adminBar.right - S(10) - w, g.adminBar.top + S(7), g.adminBar.right - S(10), g.adminBar.bottom - S(7) };
        y = g.adminBar.bottom + S(14);
    }
    // Voreinstellungen als Reihe von Schaltflaechen
    int x = m;
    const int ph = S(36);
    for (int i = 0; i < eq::PresetCount() && i < 8; ++i) {
        const int w = TextWidth(dc, g.fBold, eq::PresetAt(i).name) + S(34);
        g.presetRect[i] = { x, y, x + w, y + ph };
        x += w + S(8);
    }
    y += ph + S(14);
    g.panel = { m, y, cr.right - m, y + S(292) };
    const int inner = (g.panel.right - g.panel.left) - 2 * S(14);
    for (int slot = 0; slot <= eq::kBands; ++slot) {              // Spalte 0 = Preamp, danach die zehn Baender
        const int x0 = g.panel.left + S(14) + inner * slot / (eq::kBands + 1), x1 = g.panel.left + S(14) + inner * (slot + 1) / (eq::kBands + 1);
        g.band[slot == 0 ? eq::kBands : slot - 1] = { x0, g.panel.top + S(8), x1, g.panel.bottom - S(8) };
    }
    g.btnReset = { g.panel.right - S(14) - S(150), g.panel.bottom - S(40), g.panel.right - S(14), g.panel.bottom - S(10) };
    y = g.panel.bottom + S(14);
    const int half = (cr.right - 2 * m - S(12)) / 2;
    g.cardBass = { m, y, m + half, y + S(58) };
    g.cardVoice = { m + half + S(12), y, cr.right - m, y + S(58) };
    y = g.cardBass.bottom + S(14);

    // Profile: Titelzeile, Reihe mit Profil-Schaltflaechen, Reihe mit drei Knoepfen
    g.profCard = { m, y, cr.right - m, y + S(120) };
    const int ix = g.profCard.left + S(18), ir = g.profCard.right - S(18);
    const int n = static_cast<int>(g.profiles.size());
    const bool canNew = n < eq::kMaxProfiles;
    const int avail = (ir - ix) - (canNew ? S(78) + S(8) : 0) - (n - 1) * S(8);
    std::vector<int> w(static_cast<size_t>(n));
    int sum = 0;
    for (int i = 0; i < n; ++i) { w[static_cast<size_t>(i)] = TextWidth(dc, g.fBold, g.profiles[static_cast<size_t>(i)].name) + S(30); sum += w[static_cast<size_t>(i)]; }
    for (int i = 0; i < n; ++i) {
        int wi = sum > avail ? std::max(S(56), w[static_cast<size_t>(i)] * avail / std::max(1, sum)) : w[static_cast<size_t>(i)];
        w[static_cast<size_t>(i)] = wi;
    }
    const int row1 = g.profCard.top + S(34), h1 = S(34);
    int px = ix;
    for (int i = 0; i < n && i < eq::kMaxProfiles; ++i) {
        g.profChip[i] = { px, row1, px + w[static_cast<size_t>(i)], row1 + h1 };
        px += w[static_cast<size_t>(i)] + S(8);
    }
    for (int i = n; i < eq::kMaxProfiles; ++i) g.profChip[i] = RECT{};
    g.profNew = canNew ? RECT{ px, row1, px + S(78), row1 + h1 } : RECT{};
    g.nameRect = { ix, row1, ir, row1 + h1 };
    const int row2 = g.profCard.top + S(76), h2 = S(32);
    const bool hasDev = !g.profiles[static_cast<size_t>(g.active)].device.empty();
    const int bw1 = TextWidth(dc, g.fBold, hasDev ? L"Gerät lösen" : L"An aktuelles Gerät binden") + S(28);
    const int bw2 = TextWidth(dc, g.fBold, g.cfg.extra.empty() ? L"Kopfhörer-Korrektur laden …" : L"Korrektur entfernen") + S(28);
    const int bw3 = TextWidth(dc, g.fBold, g.delArmed ? L"Wirklich löschen?" : L"Löschen") + S(28);
    g.btnBind = { ix, row2, ix + bw1, row2 + h2 };
    g.btnImport = { g.btnBind.right + S(8), row2, g.btnBind.right + S(8) + bw2, row2 + h2 };
    g.btnDel = { ir - bw3, row2, ir, row2 + h2 };
    y = g.profCard.bottom + S(14);
    const int cw = std::max(S(200), TextWidth(dc, g.fBold, L"Geräte auswählen") + S(40));
    g.confBtn = { m, y, m + cw, y + S(36) };
    ReleaseDC(g.hwnd, dc);
}

// ---------- Zeichnen ----------
void DrawToggle(HDC dc, RECT r, bool on, bool hot) {
    Gdiplus::Graphics gr(dc);
    gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const COLORREF track = on ? kEq : Blend(kCard, RGB(255, 255, 255), hot ? 22 : 14);
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

void DrawButton(HDC dc, const RECT& r, const std::wstring& text, bool primary, bool hot, bool pressed) {
    COLORREF fill = primary ? (pressed ? Blend(kEq, RGB(0, 0, 0), 25) : hot ? Blend(kEq, RGB(255, 255, 255), 14) : kEq)
                            : (pressed ? Blend(kCard, RGB(0, 0, 0), 18) : hot ? kCardHover : kCard);
    FillRound(dc, r, S(12), fill, primary ? fill : (hot ? kBorderHot : kBorder));
    DrawText2(dc, g.fBold, primary ? kOnEq : kText, r, text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

std::wstring GainText(int db) { return db > 0 ? L"+" + std::to_wstring(db) : std::to_wstring(db); }
std::wstring FreqText(int hz) { return hz >= 1000 ? std::to_wstring(hz / 1000) + L"k" : std::to_wstring(hz); }

int GainToY(int db) { return TrackTop() + (TrackBottom() - TrackTop()) * (eq::kMaxGain - db) / (2 * eq::kMaxGain); }
int YToGain(int y) {
    const double f = static_cast<double>(y - TrackTop()) / std::max(1, TrackBottom() - TrackTop());
    return eq::ClampGain(static_cast<int>(std::lround(eq::kMaxGain - f * 2 * eq::kMaxGain)));
}

void DrawSwitchCard(HDC dc, const RECT& card, const wchar_t* title, const wchar_t* desc, bool on, bool hot) {
    FillRound(dc, card, S(14), hot ? kCardHover : kCard, on ? Blend(kBorder, kEq, 55) : (hot ? kBorderHot : kBorder));
    const int pad = S(18);
    DrawText2(dc, g.fBold, kText, { card.left + pad, card.top + S(9), card.right - S(86), card.top + S(9) + S(24) }, title);
    DrawText2(dc, g.fSmall, kTextDim, { card.left + pad, card.top + S(9) + S(24), card.right - S(86), card.bottom - S(4) }, desc, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawToggle(dc, { card.right - pad - S(52), card.top + (card.bottom - card.top - S(28)) / 2, card.right - pad, card.top + (card.bottom - card.top - S(28)) / 2 + S(28) }, on, hot);
}

void Paint(HDC dc, const RECT& cr) {
    HBRUSH bg = CreateSolidBrush(kBackground);
    FillRect(dc, &cr, bg);
    DeleteObject(bg);

    DrawText2(dc, g.fTitle, kText, { S(28), S(18), cr.right - S(28), S(56) }, kTitle);
    DrawText2(dc, g.fBody, kTextDim, { S(28), S(54), cr.right - S(28), S(78) }, L"Klang für den ganzen PC – Regler, Voreinstellungen, Bass und Stimmen");

    // Hero-Karte mit grossem Schalter
    const bool on = g.cfg.enabled;
    const bool heroHot = g.hover == H_HERO;
    FillRound(dc, g.hero, S(16), heroHot ? kCardHover : kCard, on ? Blend(kBorder, kEq, 70) : (heroHot ? kBorderHot : kBorder));
    DrawText2(dc, g.fHead, kText, { g.hero.left + S(22), g.hero.top + S(18), g.heroToggle.left - S(30), g.hero.top + S(46) },
              on ? L"Equalizer ist an" : L"Equalizer ist aus");
    const wchar_t* sub = g.installDir.empty() ? L"Equalizer APO fehlt noch – siehe unten."
                       : on ? L"Der Klang wird für alle Programme angepasst."
                            : L"Stelle die Regler ein und schalte den Equalizer dann an.";
    DrawText2(dc, g.fSmall, kTextDim, { g.hero.left + S(22), g.hero.top + S(48), g.heroToggle.left - S(30), g.hero.bottom - S(10) }, sub, DT_LEFT | DT_TOP | DT_WORDBREAK);
    DrawToggle(dc, g.heroToggle, on, heroHot);

    if (g.installDir.empty()) {
        FillRound(dc, g.setup, S(16), kCard, kBorder);
        DrawText2(dc, g.fHead, kText, { g.setup.left + S(22), g.setup.top + S(18), g.setup.right - S(22), g.setup.top + S(46) }, L"Einmalig: Equalizer APO installieren");
        DrawText2(dc, g.fBody, kTextDim, { g.setup.left + S(22), g.setup.top + S(54), g.setup.right - S(22), g.dlBtn.top - S(8) },
                  L"Der Equalizer greift direkt in den Windows-Ton ein. Dafür nutzt dieses Programm das kostenlose Equalizer APO.\n\n"
                  L"1.  Herunterladen und installieren; im Installer die Lautsprecher oder Kopfhörer anhaken, die du benutzt.\n"
                  L"2.  Windows neu starten.\n"
                  L"3.  Hier auf „Erneut prüfen“ klicken.",
                  DT_LEFT | DT_TOP | DT_WORDBREAK);
        DrawButton(dc, g.dlBtn, L"Equalizer APO herunterladen", true, g.hover == H_DOWNLOAD, g.press == H_DOWNLOAD && g.hover == H_DOWNLOAD);
        DrawButton(dc, g.recheckBtn, L"Erneut prüfen", false, g.hover == H_RECHECK, g.press == H_RECHECK && g.hover == H_RECHECK);
    } else {
        if (g.needAdmin) {
            FillRound(dc, g.adminBar, S(14), Blend(kCard, kOrange, 10), Blend(kBorder, kOrange, 70));
            DrawText2(dc, g.fSmall, kText, { g.adminBar.left + S(16), g.adminBar.top, g.adminBtn.left - S(10), g.adminBar.bottom },
                      L"Windows schützt den Ordner von Equalizer APO. Einmal mit Administratorrechten übernehmen:", DT_LEFT | DT_VCENTER | DT_WORDBREAK);
            DrawButton(dc, g.adminBtn, L"Mit Administratorrechten übernehmen", true, g.hover == H_ADMIN, g.press == H_ADMIN && g.hover == H_ADMIN);
        }
        const int match = eq::MatchPreset(g.cfg);
        for (int i = 0; i < eq::PresetCount() && i < 8; ++i) {
            const bool active = i == match, hot = g.hover == H_PRESET + i;
            FillRound(dc, g.presetRect[i], S(12), active ? kEq : (hot ? kCardHover : kCard), active ? kEq : (hot ? kBorderHot : kBorder));
            DrawText2(dc, g.fBold, active ? kOnEq : kText, g.presetRect[i], eq::PresetAt(i).name, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        // Regler
        FillRound(dc, g.panel, S(16), kCard, kBorder);
        const int tw = S(4);
        {   // dezente Trennlinie zwischen Preamp und den Frequenzbaendern
            RECT sep{ g.band[eq::kBands].right - 1, TrackTop(), g.band[eq::kBands].right, TrackBottom() + S(48) };
            FillRound(dc, sep, 1, Blend(kCard, RGB(255, 255, 255), 10), Blend(kCard, RGB(255, 255, 255), 10));
        }
        for (int slot = 0; slot <= eq::kBands; ++slot) {
            const int b = slot == 0 ? eq::kBands : slot - 1;           // kBands = Preamp
            const bool pre = b == eq::kBands;
            const RECT& c = g.band[b];
            const int cx = (c.left + c.right) / 2;
            const int gain = pre ? g.cfg.preamp : g.cfg.gains[b];
            const bool hot = g.hover == H_BAND + b || g.drag == b;
            const COLORREF accent = on ? (pre ? kOrange : kEq) : Blend(pre ? kOrange : kEq, kCard, 62);
            DrawText2(dc, g.fSmall, gain ? kText : kTextDim, { c.left, g.panel.top + S(12), c.right, g.panel.top + S(34) }, GainText(gain), DT_CENTER | DT_SINGLELINE);
            RECT track{ cx - tw / 2, TrackTop(), cx + tw / 2 + (tw & 1), TrackBottom() };
            FillRound(dc, track, tw / 2, Blend(kCard, kBackground, 55), Blend(kCard, kBackground, 55));
            const int zeroY = GainToY(0), hy = GainToY(gain);
            if (gain != 0) {
                RECT fill{ cx - tw / 2, std::min(zeroY, hy), cx + tw / 2 + (tw & 1), std::max(zeroY, hy) };
                FillRound(dc, fill, tw / 2, accent, accent);
            }
            RECT zero{ cx - S(9), zeroY - 1, cx + S(9), zeroY + 1 };
            FillRound(dc, zero, 1, Blend(kCard, RGB(255, 255, 255), 25), Blend(kCard, RGB(255, 255, 255), 25));
            Gdiplus::Graphics gr(dc);
            gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            const float rad = static_cast<float>(S(hot ? 10 : 9));
            const COLORREF hc = hot ? Blend(accent, RGB(255, 255, 255), 25) : accent;
            Gdiplus::SolidBrush hb(Gdiplus::Color(255, GetRValue(hc), GetGValue(hc), GetBValue(hc)));
            gr.FillEllipse(&hb, cx - rad, hy - rad, rad * 2, rad * 2);
            DrawText2(dc, g.fSmall, hot ? kText : kTextDim, { c.left, TrackBottom() + S(12), c.right, TrackBottom() + S(32) }, pre ? std::wstring(L"Preamp") : FreqText(eq::kFreqs[b]), DT_CENTER | DT_SINGLELINE);
            DrawText2(dc, g.fSmall, hot ? (pre ? kOrange : kEq) : Blend(kTextDim, kCard, 25), { c.left - S(4), TrackBottom() + S(32), c.right + S(4), TrackBottom() + S(52) },
                      pre ? L"auto " + std::to_wstring(eq::PreampDb(g.cfg)) : std::wstring(eq::BandName(b)), DT_CENTER | DT_SINGLELINE);
        }

        {
            const int shown = g.drag >= 0 ? g.drag : (g.hover >= H_BAND && g.hover <= H_BAND + eq::kBands ? g.hover - H_BAND : -1);
            const std::wstring preHint = L"Preamp: Gesamtlautstärke des Equalizers. Senke sie, wenn es verzerrt. Automatisch abgesenkt wird um " +
                                         std::to_wstring(-eq::PreampDb(g.cfg)) + L" dB (Anhebungen, Bass, Sprache); dein Wert kommt dazu. Gesamt: " + std::to_wstring(eq::TotalPreampDb(g.cfg)) + L" dB.";
            RECT line{ g.panel.left + S(14), g.panel.bottom - S(48), g.panel.right - S(14), g.panel.bottom - S(47) };
            FillRound(dc, line, 1, Blend(kCard, RGB(255, 255, 255), 12), Blend(kCard, RGB(255, 255, 255), 12));
            DrawText2(dc, g.fSmall, shown >= 0 ? kText : kTextDim, { g.panel.left + S(18), g.panel.bottom - S(44), g.btnReset.left - S(12), g.panel.bottom - S(4) },
                      shown == eq::kBands ? preHint : shown >= 0 ? std::wstring(eq::BandHint(shown)) : std::wstring(L"Fahre mit der Maus über einen Regler: Hier steht, was er macht. Doppelklick setzt ihn auf 0."),
                      DT_LEFT | DT_VCENTER | DT_WORDBREAK);
            const bool rh = g.hover == H_RESET;
            FillRound(dc, g.btnReset, S(12), rh ? kCardHover : Blend(kCard, kBackground, 25), rh ? kBorderHot : kBorder);
            DrawText2(dc, g.fBold, kText, g.btnReset, L"Alle auf 0", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        DrawSwitchCard(dc, g.cardBass, L"Bass verstärken", L"Mehr Druck in den Tiefen", g.cfg.bass, g.hover == H_BASS);
        DrawSwitchCard(dc, g.cardVoice, L"Sprache klarer", L"Stimmen deutlicher", g.cfg.voice, g.hover == H_VOICE);

        // Profile
        FillRound(dc, g.profCard, S(16), kCard, kBorder);
        {
            const eq::Profile& ap = g.profiles[static_cast<size_t>(g.active)];
            std::wstring info;
            if (g.nameEdit) info = L"Name für das neue Profil eingeben – Enter speichert, Esc bricht ab";
            else {
                info = ap.device.empty() ? (g.lastDevice.empty() ? L"kein Gerät festgelegt" : L"kein Gerät festgelegt (aktuell: " + g.lastDevice + L")")
                                         : L"wird automatisch gewählt für: " + ap.device;
                if (!g.cfg.extra.empty()) info += L"  ·  Korrektur: " + std::to_wstring(g.cfg.extra.size()) + L" Zeilen";
            }
            DrawText2(dc, g.fBold, kText, { g.profCard.left + S(18), g.profCard.top + S(9), g.profCard.left + S(86), g.profCard.top + S(31) }, L"Profile");
            DrawText2(dc, g.fSmall, kTextDim, { g.profCard.left + S(84), g.profCard.top + S(10), g.profCard.right - S(18), g.profCard.top + S(32) }, info,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        if (g.nameEdit) {
            FillRound(dc, g.nameRect, S(12), Blend(kCard, kBackground, 40), kEq);
        } else {
            for (size_t i = 0; i < g.profiles.size() && i < static_cast<size_t>(eq::kMaxProfiles); ++i) {
                const bool act = static_cast<int>(i) == g.active, hot = g.hover == H_PROFILE + static_cast<int>(i);
                FillRound(dc, g.profChip[i], S(12), act ? kEq : (hot ? kCardHover : Blend(kCard, kBackground, 25)), act ? kEq : (hot ? kBorderHot : kBorder));
                DrawText2(dc, g.fBold, act ? kOnEq : kText, g.profChip[i], g.profiles[i].name, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
            if (g.profNew.right > g.profNew.left) {
                const bool hot = g.hover == H_PROFNEW;
                FillRound(dc, g.profNew, S(12), hot ? kCardHover : Blend(kCard, kBackground, 25), hot ? kBorderHot : kBorder);
                DrawText2(dc, g.fBold, kEq, g.profNew, L"+ Neu", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
        auto smallBtn = [&](const RECT& r, const std::wstring& t, int id, bool warn) {
            const bool hot = g.hover == id;
            FillRound(dc, r, S(12), hot ? kCardHover : Blend(kCard, kBackground, 25), warn ? kOrange : (hot ? kBorderHot : kBorder));
            DrawText2(dc, g.fBold, warn ? kOrange : kText, r, t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        };
        smallBtn(g.btnBind, g.profiles[static_cast<size_t>(g.active)].device.empty() ? L"An aktuelles Gerät binden" : L"Gerät lösen", H_BIND, false);
        smallBtn(g.btnImport, g.cfg.extra.empty() ? L"Kopfhörer-Korrektur laden …" : L"Korrektur entfernen", H_IMPORT, false);
        smallBtn(g.btnDel, g.delArmed ? L"Wirklich löschen?" : L"Löschen", H_PROFDEL, g.delArmed);
        DrawButton(dc, g.confBtn, L"Geräte auswählen", false, g.hover == H_CONFIGURATOR, g.press == H_CONFIGURATOR && g.hover == H_CONFIGURATOR);
        DrawText2(dc, g.fSmall, kTextDim, { g.confBtn.right + S(14), g.confBtn.top, g.footer.right, g.confBtn.bottom },
                  L"Dort stellst du ein, auf welchen Geräten der Equalizer wirkt.", DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    const std::wstring base = L"© 2026 Duncan · v" APP_VERSION;
    DrawText2(dc, g.fSmall, kTextDim, { g.footer.left, g.footer.top, g.footer.left + S(200), g.footer.bottom }, base, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawText2(dc, g.fSmall, g.statusColor, { g.footer.left + S(210), g.footer.top, g.footer.right, g.footer.bottom }, g.status, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

// ---------- Eingabe ----------
int HitTest(POINT p) {
    if (PtInRect(&g.hero, p)) return H_HERO;
    if (g.installDir.empty()) {
        if (PtInRect(&g.dlBtn, p)) return H_DOWNLOAD;
        if (PtInRect(&g.recheckBtn, p)) return H_RECHECK;
        return H_NONE;
    }
    if (g.needAdmin && PtInRect(&g.adminBtn, p)) return H_ADMIN;
    for (int i = 0; i < eq::PresetCount() && i < 8; ++i) if (PtInRect(&g.presetRect[i], p)) return H_PRESET + i;
    if (PtInRect(&g.btnReset, p)) return H_RESET;
    for (int b = 0; b <= eq::kBands; ++b) if (PtInRect(&g.band[b], p)) return H_BAND + b;
    if (PtInRect(&g.cardBass, p)) return H_BASS;
    if (PtInRect(&g.cardVoice, p)) return H_VOICE;
    if (PtInRect(&g.confBtn, p)) return H_CONFIGURATOR;
    if (!g.nameEdit) {
        for (int i = 0; i < static_cast<int>(g.profiles.size()) && i < eq::kMaxProfiles; ++i) if (PtInRect(&g.profChip[i], p)) return H_PROFILE + i;
        if (g.profNew.right > g.profNew.left && PtInRect(&g.profNew, p)) return H_PROFNEW;
    }
    if (PtInRect(&g.btnBind, p)) return H_BIND;
    if (PtInRect(&g.btnImport, p)) return H_IMPORT;
    if (PtInRect(&g.btnDel, p)) return H_PROFDEL;
    return H_NONE;
}

void SetBand(int b, int db) {                       // b == kBands: der Preamp-Regler
    db = eq::ClampGain(db);
    int& target = b == eq::kBands ? g.cfg.preamp : g.cfg.gains[b];
    if (target == db) return;
    target = db;
    Changed();
}

void Activate(int id) {
    if (id == H_HERO) { g.cfg.enabled = !g.cfg.enabled; Changed(); }
    else if (id == H_BASS) { g.cfg.bass = !g.cfg.bass; Changed(); }
    else if (id == H_VOICE) { g.cfg.voice = !g.cfg.voice; Changed(); }
    else if (id == H_RESET) { for (int b = 0; b < eq::kBands; ++b) g.cfg.gains[b] = 0; g.cfg.preamp = 0; Changed(); SetStatus(L"Alle Regler auf 0 gesetzt", kTextDim); }
    else if (id >= H_PRESET && id < H_PRESET + eq::PresetCount()) {
        for (int b = 0; b < eq::kBands; ++b) g.cfg.gains[b] = eq::PresetAt(id - H_PRESET).gains[b];
        Changed();
    }
    else if (id >= H_PROFILE && id < H_PROFILE + eq::kMaxProfiles) SwitchProfile(id - H_PROFILE);
    else if (id == H_PROFNEW) BeginNewProfile();
    else if (id == H_BIND) BindDevice();
    else if (id == H_IMPORT) ImportFilters();
    else if (id == H_PROFDEL) { DeleteProfile(); Layout(); }
    else if (id == H_DOWNLOAD) ShellExecuteW(g.hwnd, L"open", L"https://sourceforge.net/projects/equalizerapo/", nullptr, nullptr, SW_SHOWNORMAL);
    else if (id == H_RECHECK) { Recheck(); Layout(); }
    else if (id == H_ADMIN) { ApplyAsAdmin(); Layout(); }
    else if (id == H_CONFIGURATOR) {
        // Das Programm zur Geraeteauswahl heisst je nach Version DeviceSelector.exe (ab 1.3) oder Configurator.exe (aelter)
        std::wstring exe;
        for (const wchar_t* name : { L"DeviceSelector.exe", L"Configurator.exe" }) {
            const std::wstring cand = g.installDir + L"\\" + name;
            if (GetFileAttributesW(cand.c_str()) != INVALID_FILE_ATTRIBUTES) { exe = cand; break; }
        }
        if (exe.empty()) SetStatus(L"Die Geräteauswahl von Equalizer APO wurde nicht gefunden (DeviceSelector.exe)", kRed);
        else {
            const HINSTANCE r = ShellExecuteW(g.hwnd, L"open", exe.c_str(), nullptr, g.installDir.c_str(), SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(r) <= 32) SetStatus(L"Die Geräteauswahl konnte nicht gestartet werden (Administrator-Abfrage abgebrochen?)", kOrange);
            else SetStatus(L"Geräteauswahl geöffnet – dort die Geräte anhaken, auf denen der Equalizer wirken soll", kTextDim);
        }
    }
    InvalidateRect(g.hwnd, nullptr, FALSE);
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
    case WM_DPICHANGED: {
        g.dpi = HIWORD(wp);
        MakeFonts();
        const RECT* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Layout();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_ACTIVATE:                                  // beim Zurueckkehren (z. B. nach der Installation) neu pruefen
        if (LOWORD(wp) != WA_INACTIVE && g.hwnd && g.installDir.empty()) { g.installDir = Detect(); if (!g.installDir.empty()) ApplyNow(); Layout(); InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    case WM_MOUSEMOVE: {
        if (!g.tracking) { TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, hwnd, 0 }; TrackMouseEvent(&t); g.tracking = true; }
        const POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (g.drag >= 0) { SetBand(g.drag, YToGain(p.y)); return 0; }
        const int h = HitTest(p);
        if (h != g.hover) { g.hover = h; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE: g.tracking = false; g.hover = H_NONE; InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, g.hover == H_NONE ? IDC_ARROW : IDC_HAND)); return TRUE; }
        break;
    case WM_LBUTTONDBLCLK: {
        const int h = HitTest({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        if (h >= H_BAND && h <= H_BAND + eq::kBands) SetBand(h - H_BAND, 0);      // Doppelklick auf einen Regler: zurueck auf 0
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        g.press = HitTest(p);
        if (g.press >= H_BAND && g.press <= H_BAND + eq::kBands) { g.drag = g.press - H_BAND; SetBand(g.drag, YToGain(p.y)); }
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (GetCapture() == hwnd) ReleaseCapture();
        const int h = HitTest({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        const int pressed = g.press;
        const bool wasDrag = g.drag >= 0;
        g.press = H_NONE; g.drag = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        if (!wasDrag && pressed != H_NONE && pressed == h) Activate(h);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &p);
        const int h = HitTest(p);
        if (h >= H_BAND && h <= H_BAND + eq::kBands) { const int b = h - H_BAND; SetBand(b, (b == eq::kBands ? g.cfg.preamp : g.cfg.gains[b]) + (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1)); }
        return 0;
    }
    case WM_TIMER:
        if (wp == kTimerApply) ApplyNow();
        else if (wp == kTimerDevice) PollDevice();
        else if (wp == kTimerDisarm) { KillTimer(hwnd, kTimerDisarm); g.delArmed = false; Layout(); InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    case WM_NAME_DONE: FinishNewProfile(wp == 1); return 0;
    case WM_DESTROY: if (!g.installDir.empty()) ApplyNow(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Mit Administratorrechten gestartet (--apply "<Einstellungsdatei>"): schreibt nur die Konfiguration und beendet sich
int RunApplyMode(const std::wstring& path) {
    eq::Settings s;
    if (!eq::LoadSettings(s, path)) return static_cast<int>(eq::ApplyResult::Failed);
    return static_cast<int>(eq::ApplyToDir(eq::FindInstallDir(), s));
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 3 && std::wstring(argv[1]) == L"--apply") {
        const int code = RunApplyMode(argv[2]);
        LocalFree(argv);
        return code;
    }
    if (argv) LocalFree(argv);

    HANDLE single = CreateMutexW(nullptr, TRUE, L"DuncansEqualizer.Single");
    if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(L"DuncansEqualizer", nullptr)) { ShowWindow(other, SW_RESTORE); SetForegroundWindow(other); }
        return 0;
    }
    IconsInit();
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"DuncansEqualizer";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = wc.hIconSm = LoadIconW(inst, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);

    g.dpi = GetDpiForSystem();
    MakeFonts();
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;     // feste Groesse: alles passt immer
    auto windowSize = [&](UINT dpi, int& w, int& h) {
        RECT r{ 0, 0, MulDiv(640, dpi, 96), MulDiv(864, dpi, 96) };
        AdjustWindowRectExForDpi(&r, style, FALSE, 0, dpi);
        w = r.right - r.left; h = r.bottom - r.top;
    };
    int ww = 0, wh = 0;
    windowSize(g.dpi, ww, wh);
    g.hwnd = CreateWindowExW(0, wc.lpszClassName, kTitle, style, CW_USEDEFAULT, CW_USEDEFAULT, ww, wh, nullptr, nullptr, inst, nullptr);
    if (!g.hwnd) return 1;
    g.dpi = GetDpiForWindow(g.hwnd);
    MakeFonts();
    windowSize(g.dpi, ww, wh);
    SetWindowPos(g.hwnd, nullptr, 0, 0, ww, wh, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    gfx::EnableDarkTitleBar(g.hwnd);

    g.settingsPath = eq::SettingsPath();
    g.profilesPath = eq::ProfilesPath();
    eq::LoadSettings(g.cfg, g.settingsPath);          // Ein/Aus und (aus aelteren Versionen) die Regler
    if (!eq::LoadProfiles(g.profiles, g.active, g.profilesPath)) {
        g.profiles.assign(1, eq::Profile{});
        g.profiles[0].name = L"Standard";
        g.active = 0;
        SyncActive();                                  // Regler der Vorversion werden zum Profil "Standard"
    } else LoadActive();
    g.lastDevice = eq::DefaultOutputName();
    {
        const int dev = eq::ProfileForDevice(g.profiles, g.lastDevice);
        if (dev >= 0 && dev != g.active) { g.active = dev; LoadActive(); }
    }
    g.installDir = Detect();
    Layout();
    SetTimer(g.hwnd, kTimerDevice, 2500, nullptr);
    if (!g.installDir.empty()) ApplyNow();      // beim Start die gespeicherten Einstellungen (neu) eintragen
    ShowWindow(g.hwnd, show);
    UpdateWindow(g.hwnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    IconsShutdown();
    return static_cast<int>(m.wParam);
}
