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

#include <algorithm>
#include <cmath>
#include <string>

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
#define APP_VERSION L"0.1.0"   // normalerweise aus CMake
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
const UINT_PTR kTimerApply = 1;

enum HitId {
    H_NONE = -1, H_HERO = 1, H_BASS = 2, H_VOICE = 3, H_DOWNLOAD = 4, H_RECHECK = 5, H_CONFIGURATOR = 6, H_ADMIN = 7,
    H_PRESET = 10,      // 10 .. 10 + Anzahl Voreinstellungen
    H_BAND = 100        // 100 .. 109
};

struct App {
    HWND hwnd = nullptr;
    UINT dpi = 96;
    HFONT fTitle = nullptr, fHead = nullptr, fBody = nullptr, fSmall = nullptr, fBold = nullptr;
    eq::Settings cfg;
    std::wstring settingsPath;
    std::wstring installDir;                       // leer = Equalizer APO nicht gefunden
    bool needAdmin = false;                        // Schreiben im Ordner von Equalizer APO wurde verweigert
    RECT hero{}, heroToggle{}, adminBar{}, adminBtn{}, presetRect[8]{}, panel{}, band[eq::kBands]{}, cardBass{}, cardVoice{}, confBtn{},
         setup{}, dlBtn{}, recheckBtn{}, footer{};
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
void SaveCfg() { eq::SaveSettings(g.cfg, g.settingsPath); }

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

void Changed() {                                    // speichern, kurz warten (Regler ziehen), dann uebernehmen
    SaveCfg();
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

// ---------- Layout ----------
int TrackTop() { return g.panel.top + S(42); }
int TrackBottom() { return g.panel.bottom - S(40); }

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
    g.panel = { m, y, cr.right - m, y + S(300) };
    const int inner = (g.panel.right - g.panel.left) - 2 * S(14);
    for (int b = 0; b < eq::kBands; ++b) {
        const int x0 = g.panel.left + S(14) + inner * b / eq::kBands, x1 = g.panel.left + S(14) + inner * (b + 1) / eq::kBands;
        g.band[b] = { x0, g.panel.top + S(8), x1, g.panel.bottom - S(8) };
    }
    y = g.panel.bottom + S(14);
    g.cardBass = { m, y, cr.right - m, y + S(58) };
    y = g.cardBass.bottom + S(8);
    g.cardVoice = { m, y, cr.right - m, y + S(58) };
    y = g.cardVoice.bottom + S(14);
    const int cw = std::max(S(200), TextWidth(dc, g.fBold, L"Configurator öffnen") + S(40));
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
    DrawText2(dc, g.fBold, kText, { card.left + pad, card.top + S(9), card.right - S(110), card.top + S(9) + S(24) }, title);
    DrawText2(dc, g.fSmall, kTextDim, { card.left + pad, card.top + S(9) + S(24), card.right - S(110), card.bottom - S(4) }, desc, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
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
        for (int b = 0; b < eq::kBands; ++b) {
            const RECT& c = g.band[b];
            const int cx = (c.left + c.right) / 2;
            const int gain = g.cfg.gains[b];
            const bool hot = g.hover == H_BAND + b || g.drag == b;
            const COLORREF accent = on ? kEq : Blend(kEq, kCard, 62);
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
            DrawText2(dc, g.fSmall, kTextDim, { c.left, TrackBottom() + S(14), c.right, g.panel.bottom - S(6) }, FreqText(eq::kFreqs[b]), DT_CENTER | DT_SINGLELINE);
        }

        DrawSwitchCard(dc, g.cardBass, L"Bass verstärken", L"Hebt die tiefen Töne zusätzlich an – kräftiger bei Musik und Filmen.", g.cfg.bass, g.hover == H_BASS);
        DrawSwitchCard(dc, g.cardVoice, L"Sprache klarer", L"Macht Stimmen deutlicher – gut für Streams, Calls und Serien.", g.cfg.voice, g.hover == H_VOICE);
        DrawButton(dc, g.confBtn, L"Configurator öffnen", false, g.hover == H_CONFIGURATOR, g.press == H_CONFIGURATOR && g.hover == H_CONFIGURATOR);
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
    for (int b = 0; b < eq::kBands; ++b) if (PtInRect(&g.band[b], p)) return H_BAND + b;
    if (PtInRect(&g.cardBass, p)) return H_BASS;
    if (PtInRect(&g.cardVoice, p)) return H_VOICE;
    if (PtInRect(&g.confBtn, p)) return H_CONFIGURATOR;
    return H_NONE;
}

void SetBand(int b, int db) {
    db = eq::ClampGain(db);
    if (g.cfg.gains[b] == db) return;
    g.cfg.gains[b] = db;
    Changed();
}

void Activate(int id) {
    if (id == H_HERO) { g.cfg.enabled = !g.cfg.enabled; Changed(); }
    else if (id == H_BASS) { g.cfg.bass = !g.cfg.bass; Changed(); }
    else if (id == H_VOICE) { g.cfg.voice = !g.cfg.voice; Changed(); }
    else if (id >= H_PRESET && id < H_PRESET + eq::PresetCount()) {
        for (int b = 0; b < eq::kBands; ++b) g.cfg.gains[b] = eq::PresetAt(id - H_PRESET).gains[b];
        Changed();
    }
    else if (id == H_DOWNLOAD) ShellExecuteW(g.hwnd, L"open", L"https://sourceforge.net/projects/equalizerapo/", nullptr, nullptr, SW_SHOWNORMAL);
    else if (id == H_RECHECK) { Recheck(); Layout(); }
    else if (id == H_ADMIN) { ApplyAsAdmin(); Layout(); }
    else if (id == H_CONFIGURATOR) {
        const std::wstring exe = g.installDir + L"\\Configurator.exe";
        if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) SetStatus(L"Configurator.exe nicht gefunden", kRed);
        else ShellExecuteW(g.hwnd, L"open", exe.c_str(), nullptr, g.installDir.c_str(), SW_SHOWNORMAL);
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
        if (h >= H_BAND && h < H_BAND + eq::kBands) SetBand(h - H_BAND, 0);      // Doppelklick auf einen Regler: zurueck auf 0
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        g.press = HitTest(p);
        if (g.press >= H_BAND && g.press < H_BAND + eq::kBands) { g.drag = g.press - H_BAND; SetBand(g.drag, YToGain(p.y)); }
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
        if (h >= H_BAND && h < H_BAND + eq::kBands) SetBand(h - H_BAND, g.cfg.gains[h - H_BAND] + (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1));
        return 0;
    }
    case WM_TIMER:
        if (wp == kTimerApply) ApplyNow();
        return 0;
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
        RECT r{ 0, 0, MulDiv(640, dpi, 96), MulDiv(800, dpi, 96) };
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
    eq::LoadSettings(g.cfg, g.settingsPath);
    g.installDir = Detect();
    Layout();
    if (!g.installDir.empty()) ApplyNow();      // beim Start die gespeicherten Einstellungen (neu) eintragen
    ShowWindow(g.hwnd, show);
    UpdateWindow(g.hwnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    IconsShutdown();
    return static_cast<int>(m.wParam);
}
