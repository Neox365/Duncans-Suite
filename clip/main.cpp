// Duncan's Clip - Zwischenablage-Verlauf und Prompts (Win32 + GDI/GDI+, dunkle Oberflaeche).
// Merkt sich kopierten Text (nur im Arbeitsspeicher, auf Wunsch verschluesselt gespeichert), verwaltet Prompts/Textbausteine
// und fuegt sie per Tastenkuerzel (Schnellauswahl) in das aktive Programm ein. Laeuft ohne Administratorrechte im Infobereich.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <functional>
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
#include "ClipLogic.h"

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
const COLORREF kClip = RGB(34, 211, 238);           // Akzent dieses Programms (Tuerkis)

const wchar_t* kTitle = L"Duncan's Clip";
const wchar_t* kMainClass = L"DuncansClip";
const wchar_t* kPopupClass = L"DuncansClipPopup";
const UINT WM_TRAY = WM_APP + 10;
const UINT WM_SHOWMAIN = WM_APP + 11;                // zweiter Start: bestehendes Fenster zeigen
const int kHotkeyId = 1;
const UINT_PTR kTimerPaste = 1, kTimerStatus = 2, kTimerRestore = 3;
const int kIdEditTitle = 101, kIdEditText = 102, kIdSearch = 103, kIdPopupSearch = 104;

enum Tab { T_HISTORY, T_PROMPTS };
enum HitId {
    H_NONE = -1, H_TAB_HISTORY = 1000, H_TAB_PROMPTS = 1001, H_ACTION = 1002, H_SET_AUTOSTART = 1010, H_SET_SAVE = 1011, H_SET_HOTKEY = 1012,
    H_EDIT_SAVE = 2000, H_EDIT_CANCEL = 2001, H_ROW_BASE = 3000
};

struct Row { size_t idx = 0; int top = 0, height = 0; };

struct EditState { bool open = false; int index = -1; };   // index -1 = neuer Prompt

struct App {
    HWND hwnd = nullptr, hSearch = nullptr, hEditTitle = nullptr, hEditText = nullptr;
    HWND popup = nullptr, popupSearch = nullptr;
    UINT dpi = 96;
    HFONT fTitle = nullptr, fHead = nullptr, fBody = nullptr, fSmall = nullptr, fBold = nullptr;
    HBRUSH editBrush = nullptr;
    clip::Store store;
    clip::Settings cfg;
    Tab tab = T_HISTORY;
    std::wstring filter;
    std::vector<Row> rows;
    int scroll = 0, contentH = 0;
    RECT tabHistory{}, tabPrompts{}, actionBtn{}, searchRc{}, list{}, set[3]{}, footer{}, editCard{}, editSave{}, editCancel{};
    EditState edit;
    int hover = H_NONE, press = H_NONE;
    bool tracking = false;
    std::wstring status;
    COLORREF statusColor = kTextDim;
    DWORD suppressUntil = 0;                         // eigene Zwischenablage-Aenderungen nicht in den Verlauf uebernehmen
    HWND prevFg = nullptr;                           // Fenster, in das eingefuegt wird
    bool prevElevated = false;
    bool hotkeyOk = false;
    bool quitting = false;
    NOTIFYICONDATAW nid{};
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
    for (HWND h : { g.hSearch, g.hEditTitle, g.hEditText, g.popupSearch }) if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g.fBody), TRUE);
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

std::wstring GetEditText(HWND h) {
    const int n = GetWindowTextLengthW(h);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    s.resize(static_cast<size_t>(n));
    return s;
}

void SetStatus(const std::wstring& t, COLORREF c = kTextDim) {
    g.status = t; g.statusColor = c;
    SetTimer(g.hwnd, kTimerStatus, 6000, nullptr);   // Meldung nach einigen Sekunden wieder ausblenden
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

bool IsElevatedWindow(HWND h) {                      // laeuft das Zielfenster mit hoeheren Rechten? Dann blockiert Windows SendInput
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (!pid) return false;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return true;                             // kein Zugriff: vermutlich erhoeht
    HANDLE tok = nullptr; bool elevated = false;
    if (OpenProcessToken(p, TOKEN_QUERY, &tok)) {
        TOKEN_ELEVATION e{}; DWORD n = 0;
        if (GetTokenInformation(tok, TokenElevation, &e, sizeof e, &n)) elevated = e.TokenIsElevated != 0;
        CloseHandle(tok);
    }
    CloseHandle(p);
    return elevated;
}

// ---------- Daten ----------
void SaveHistoryIfWanted() { if (g.cfg.saveHistory) clip::SaveHistory(g.store.history); }

void SavePromptsNow() { if (!clip::SavePrompts(g.store.prompts)) SetStatus(L"Die Prompts konnten nicht gespeichert werden.", kRed); }

void Layout();

void OnClipboardChanged() {
    if (GetTickCount() < g.suppressUntil) return;
    std::wstring text;
    const bool excluded = clip::ClipboardIsExcluded();
    if (excluded || !clip::ReadClipboardText(text)) return;
    if (clip::ShouldSkip(text, excluded, clip::ClipboardOwnerExe())) return;
    if (clip::AddHistory(g.store, text)) {
        SaveHistoryIfWanted();
        Layout();                                     // Zeilenliste neu aufbauen, sonst bleibt die Anzeige alt
        InvalidateRect(g.hwnd, nullptr, FALSE);
    }
}

// ---------- Layout Hauptfenster ----------
int CardWidth() { return g.list.right - g.list.left - S(14); }

bool RowMatches(size_t i) {
    if (g.tab == T_HISTORY) return clip::Matches(g.store.history[i], g.filter);
    return clip::Matches(g.store.prompts[i].title + L" " + g.store.prompts[i].text, g.filter);
}

void BuildRows() {
    g.rows.clear();
    int y = 0;
    const size_t n = g.tab == T_HISTORY ? g.store.history.size() : g.store.prompts.size();
    for (size_t i = 0; i < n; ++i) {
        if (!RowMatches(i)) continue;
        Row r; r.idx = i; r.top = y; r.height = S(66);
        g.rows.push_back(r);
        y += r.height + S(8);
    }
    g.contentH = y;
}

void PositionEditControls() {
    const bool open = g.edit.open;
    ShowWindow(g.hEditTitle, open ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hEditText, open ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hSearch, open ? SW_HIDE : SW_SHOW);
    if (!open) return;
    const RECT& c = g.editCard;
    SetWindowPos(g.hEditTitle, nullptr, c.left + S(24), c.top + S(86), c.right - c.left - S(48), S(30), SWP_NOZORDER);
    SetWindowPos(g.hEditText, nullptr, c.left + S(24), c.top + S(150), c.right - c.left - S(48), c.bottom - c.top - S(150) - S(86), SWP_NOZORDER);
}

void Layout() {
    RECT cr; GetClientRect(g.hwnd, &cr);
    const int m = S(28);
    const int right = cr.right - m;
    // Reiter
    HDC dc = GetDC(g.hwnd);
    const int th = S(38), ty = S(92);
    const int w1 = TextWidth(dc, g.fBold, L"Verlauf") + S(40), w2 = TextWidth(dc, g.fBold, L"Prompts") + S(40);
    g.tabHistory = { m, ty, m + w1, ty + th };
    g.tabPrompts = { g.tabHistory.right + S(10), ty, g.tabHistory.right + S(10) + w2, ty + th };
    const std::wstring act = g.tab == T_HISTORY ? L"Verlauf leeren" : L"+ Neuer Prompt";
    const int aw = TextWidth(dc, g.fBold, act) + S(36);
    g.actionBtn = { right - aw, ty, right, ty + th };
    ReleaseDC(g.hwnd, dc);
    g.searchRc = { m, ty + th + S(14), right, ty + th + S(14) + S(36) };
    g.footer = { m, cr.bottom - S(34), right, cr.bottom - S(8) };
    const int setH = S(40), setTop = g.footer.top - S(10) - setH;
    const int gap = S(10), setW = (right - m - 2 * gap) / 3;
    for (int i = 0; i < 3; ++i) g.set[i] = { m + i * (setW + gap), setTop, m + i * (setW + gap) + setW, setTop + setH };
    g.list = { m, g.searchRc.bottom + S(14), right, setTop - S(14) };
    // Prompt-Editor (Karte ueber der Liste)
    g.editCard = { m, S(84), right, cr.bottom - S(50) };
    const int bh = S(40), bw = S(130);
    g.editSave = { g.editCard.right - S(24) - bw, g.editCard.bottom - S(24) - bh, g.editCard.right - S(24), g.editCard.bottom - S(24) };
    g.editCancel = { g.editSave.left - S(12) - bw, g.editSave.top, g.editSave.left - S(12), g.editSave.bottom };
    SetWindowPos(g.hSearch, nullptr, g.searchRc.left + S(82), g.searchRc.top + S(7), g.searchRc.right - g.searchRc.left - S(98), S(22), SWP_NOZORDER);
    PositionEditControls();
    BuildRows();
    const int view = static_cast<int>(g.list.bottom - g.list.top);
    g.scroll = std::clamp(g.scroll, 0, std::max(0, g.contentH - view));
}

// ---------- Prompt-Editor ----------
void OpenEditor(int index) {
    g.edit.open = true; g.edit.index = index;
    SetWindowTextW(g.hEditTitle, index >= 0 ? g.store.prompts[static_cast<size_t>(index)].title.c_str() : L"");
    std::wstring text = index >= 0 ? g.store.prompts[static_cast<size_t>(index)].text : L"";
    std::wstring crlf;                                    // EDIT-Steuerelemente erwarten \r\n
    for (size_t i = 0; i < text.size(); ++i) { if (text[i] == L'\n' && (i == 0 || text[i - 1] != L'\r')) crlf += L'\r'; crlf += text[i]; }
    SetWindowTextW(g.hEditText, crlf.c_str());
    PositionEditControls();
    SetFocus(g.hEditTitle);
    InvalidateRect(g.hwnd, nullptr, TRUE);
}

void CloseEditor(bool save) {
    if (save) {
        const std::wstring title = GetEditText(g.hEditTitle), text = GetEditText(g.hEditText);
        if (text.find_first_not_of(L" \t\r\n") == std::wstring::npos) { SetStatus(L"Der Prompt braucht einen Text.", kOrange); return; }
        clip::Prompt p = clip::MakePrompt(text);
        if (title.find_first_not_of(L" \t") != std::wstring::npos) p.title = title;
        if (g.edit.index >= 0) g.store.prompts[static_cast<size_t>(g.edit.index)] = p; else g.store.prompts.push_back(p);
        SavePromptsNow();
        SetStatus(L"Prompt gespeichert.", kGreen);
    }
    g.edit.open = false;
    PositionEditControls();
    Layout();
    InvalidateRect(g.hwnd, nullptr, TRUE);
}

// ---------- Aktionen ----------
std::wstring CurrentClipText() { std::wstring t; clip::ReadClipboardText(t); return t; }

void CopyToClipboard(const std::wstring& text, const std::wstring& okMsg) {
    g.suppressUntil = GetTickCount() + 1500;
    if (clip::WriteClipboardText(text)) SetStatus(okMsg, kGreen);
    else SetStatus(L"Die Zwischenablage ist gerade belegt – bitte noch einmal versuchen.", kOrange);
}

void ToggleAutostart() {
    wchar_t exe[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    const bool want = !clip::GetAutostart();
    if (clip::SetAutostart(want, exe)) { g.cfg.autostart = want; SetStatus(want ? L"Clip startet jetzt mit Windows im Hintergrund." : L"Autostart ausgeschaltet.", kGreen); }
    else SetStatus(L"Der Autostart konnte nicht geändert werden.", kRed);
}

void ToggleSaveHistory() {
    g.cfg.saveHistory = !g.cfg.saveHistory;
    clip::SaveSettings(g.cfg);
    if (g.cfg.saveHistory) { clip::SaveHistory(g.store.history); SetStatus(L"Verlauf wird verschlüsselt gespeichert.", kGreen); }
    else { clip::DeleteHistoryFile(); SetStatus(L"Verlauf bleibt nur im Arbeitsspeicher; die gespeicherte Datei wurde gelöscht.", kGreen); }
}

bool RegisterHotkeyFor(int index) {
    const clip::Hotkey& k = clip::HotkeyPreset(index);
    return RegisterHotKey(g.hwnd, kHotkeyId, k.mods, k.vk) != 0;
}

void CycleHotkey() {
    UnregisterHotKey(g.hwnd, kHotkeyId);
    for (int step = 1; step <= clip::HotkeyCount(); ++step) {     // naechstes freies Kuerzel
        const int idx = (g.cfg.hotkey + step) % clip::HotkeyCount();
        if (RegisterHotkeyFor(idx)) {
            g.cfg.hotkey = idx; g.hotkeyOk = true;
            clip::SaveSettings(g.cfg);
            SetStatus(std::wstring(L"Kürzel: ") + clip::HotkeyPreset(idx).name, kGreen);
            InvalidateRect(g.hwnd, nullptr, FALSE);
            return;
        }
    }
    g.hotkeyOk = false;
    SetStatus(L"Alle Kürzel sind von anderen Programmen belegt.", kRed);
}

void ConfirmDeletePrompt(size_t idx) {
    if (MessageBoxW(g.hwnd, (L"Prompt „" + g.store.prompts[idx].title + L"“ löschen?").c_str(), kTitle, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    g.store.prompts.erase(g.store.prompts.begin() + static_cast<std::ptrdiff_t>(idx));
    SavePromptsNow();
    Layout();
}

void ActivateRow(int rowIndex, int part) {
    if (rowIndex < 0 || rowIndex >= static_cast<int>(g.rows.size())) return;
    const size_t i = g.rows[static_cast<size_t>(rowIndex)].idx;
    if (g.tab == T_HISTORY) {
        if (part == 0) CopyToClipboard(g.store.history[i], L"In die Zwischenablage kopiert.");
        else if (part == 1) { g.store.prompts.push_back(clip::MakePrompt(g.store.history[i])); SavePromptsNow(); SetStatus(L"Als Prompt gespeichert (Reiter „Prompts“).", kGreen); }
        else { clip::RemoveHistory(g.store, i); SaveHistoryIfWanted(); Layout(); }
    } else {
        if (part == 0) CopyToClipboard(clip::Expand(g.store.prompts[i].text, CurrentClipText()), L"Prompt kopiert ({clip} ersetzt).");
        else if (part == 1) OpenEditor(static_cast<int>(i));
        else ConfirmDeletePrompt(i);
    }
}

void SetTab(Tab t) {
    g.tab = t; g.scroll = 0;
    SetWindowTextW(g.hSearch, L"");
    g.filter.clear();
    Layout();
    InvalidateRect(g.hwnd, nullptr, TRUE);
}

// ---------- Zeichnen ----------
void DrawPill(HDC dc, const RECT& r, const std::wstring& text, bool active, bool hot, COLORREF accent) {
    FillRound(dc, r, S(12), active ? Blend(kCard, accent, 22) : (hot ? kCardHover : kCard), active ? accent : (hot ? kBorderHot : kBorder));
    DrawText2(dc, g.fBold, active || hot ? kText : kTextDim, r, text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawButton(HDC dc, const RECT& r, const std::wstring& text, bool primary, bool hot, bool pressed) {
    COLORREF fill = primary ? (pressed ? Blend(kClip, RGB(0, 0, 0), 25) : hot ? Blend(kClip, RGB(255, 255, 255), 14) : kClip)
                            : (pressed ? Blend(kCard, RGB(0, 0, 0), 18) : hot ? kCardHover : kCard);
    FillRound(dc, r, S(12), fill, primary ? fill : (hot ? kBorderHot : kBorder));
    DrawText2(dc, g.fBold, primary ? RGB(4, 24, 30) : kText, r, text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawStar(HDC dc, const RECT& r, COLORREF c, bool filled) {
    Gdiplus::Graphics gr(dc);
    gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float cx = (r.left + r.right) / 2.0f, cy = (r.top + r.bottom) / 2.0f + 1, ro = (r.right - r.left) / 2.0f - 5, ri = ro * 0.45f;
    Gdiplus::PointF pts[10];
    for (int i = 0; i < 10; ++i) {
        const float a = -3.14159265f / 2 + i * 3.14159265f / 5, rad = i % 2 ? ri : ro;
        pts[i] = { cx + rad * cosf(a), cy + rad * sinf(a) };
    }
    Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)), 1.6f);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    if (filled) { Gdiplus::SolidBrush b(Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c))); gr.FillPolygon(&b, pts, 10); }
    gr.DrawPolygon(&pen, pts, 10);
}

void DrawCross(HDC dc, const RECT& r, COLORREF c) {
    Gdiplus::Graphics gr(dc);
    gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)), 1.8f);
    pen.SetStartCap(Gdiplus::LineCapRound); pen.SetEndCap(Gdiplus::LineCapRound);
    const int p = S(9), L = static_cast<int>(r.left), T = static_cast<int>(r.top), R = static_cast<int>(r.right), B = static_cast<int>(r.bottom);
    gr.DrawLine(&pen, L + p, T + p, R - p, B - p);
    gr.DrawLine(&pen, R - p, T + p, L + p, B - p);
}

void DrawPencil(HDC dc, const RECT& r, COLORREF c) {
    Gdiplus::Graphics gr(dc);
    gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)), 1.6f);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    const float cx = (r.left + r.right) / 2.0f, cy = (r.top + r.bottom) / 2.0f, u = static_cast<float>(S(7));
    gr.TranslateTransform(cx, cy);
    gr.RotateTransform(45.0f);                                   // Stift zeigt nach links unten
    const float w = u * 0.8f;
    gr.DrawRectangle(&pen, -w / 2, -u * 1.15f, w, u * 1.75f);   // Koerper
    Gdiplus::PointF tip[3] = { { -w / 2, u * 0.6f }, { w / 2, u * 0.6f }, { 0.0f, u * 1.3f } };
    gr.DrawPolygon(&pen, tip, 3);                                // Spitze
    gr.DrawLine(&pen, -w / 2, -u * 0.7f, w / 2, -u * 0.7f);      // Kappe
    gr.ResetTransform();
}

void RowButtons(const RECT& card, RECT& b1, RECT& b2) {
    const int sz = S(32), pad = S(14);
    b2 = { card.right - pad - sz, card.top + (card.bottom - card.top - sz) / 2, card.right - pad, card.top + (card.bottom - card.top - sz) / 2 + sz };
    b1 = { b2.left - S(6) - sz, b2.top, b2.left - S(6), b2.bottom };
}

void PaintEditor(HDC dc, const RECT& cr) {
    FillAlphaRect(dc, cr, RGB(0, 0, 0), 120);
    FillRound(dc, g.editCard, S(16), RGB(30, 33, 41), kBorderHot);
    const RECT& c = g.editCard;
    DrawText2(dc, g.fHead, kText, { c.left + S(24), c.top + S(20), c.right - S(24), c.top + S(48) }, g.edit.index >= 0 ? L"Prompt bearbeiten" : L"Neuer Prompt");
    DrawText2(dc, g.fSmall, kTextDim, { c.left + S(24), c.top + S(62), c.right - S(24), c.top + S(84) }, L"Titel");
    DrawText2(dc, g.fSmall, kTextDim, { c.left + S(24), c.top + S(126), c.right - S(24), c.top + S(148) }, L"Text  –  {clip} wird beim Einfügen durch die Zwischenablage ersetzt");
    FillRound(dc, { c.left + S(20), c.top + S(82), c.right - S(20), c.top + S(120) }, S(10), Blend(kCard, kBackground, 30), kBorder);
    FillRound(dc, { c.left + S(20), c.top + S(146), c.right - S(20), c.bottom - S(82) }, S(10), Blend(kCard, kBackground, 30), kBorder);
    DrawButton(dc, g.editCancel, L"Abbrechen", false, g.hover == H_EDIT_CANCEL, g.press == H_EDIT_CANCEL && g.hover == H_EDIT_CANCEL);
    DrawButton(dc, g.editSave, L"Speichern", true, g.hover == H_EDIT_SAVE, g.press == H_EDIT_SAVE && g.hover == H_EDIT_SAVE);
}

void Paint(HDC dc, const RECT& cr) {
    HBRUSH bg = CreateSolidBrush(kBackground);
    FillRect(dc, &cr, bg);
    DeleteObject(bg);
    DrawText2(dc, g.fTitle, kText, { S(28), S(18), cr.right - S(28), S(56) }, kTitle);
    DrawText2(dc, g.fBody, kTextDim, { S(28), S(54), cr.right - S(28), S(80) },
              std::wstring(L"Schnellauswahl überall mit ") + clip::HotkeyPreset(g.cfg.hotkey).name + (g.hotkeyOk ? L"" : L"  (Kürzel belegt!)"));

    DrawPill(dc, g.tabHistory, L"Verlauf", g.tab == T_HISTORY, g.hover == H_TAB_HISTORY, kClip);
    DrawPill(dc, g.tabPrompts, L"Prompts", g.tab == T_PROMPTS, g.hover == H_TAB_PROMPTS, kClip);
    DrawButton(dc, g.actionBtn, g.tab == T_HISTORY ? L"Verlauf leeren" : L"+ Neuer Prompt", g.tab == T_PROMPTS, g.hover == H_ACTION, g.press == H_ACTION && g.hover == H_ACTION);

    // Suchfeld (das EDIT liegt darueber)
    FillRound(dc, g.searchRc, S(12), Blend(kCard, kBackground, 30), kBorder);
    DrawText2(dc, g.fSmall, kTextDim, { g.searchRc.left + S(16), g.searchRc.top, g.searchRc.left + S(80), g.searchRc.bottom }, L"Suchen:", DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Liste
    SaveDC(dc);
    IntersectClipRect(dc, g.list.left - S(4), g.list.top, g.list.right + S(4), g.list.bottom);
    const int cardW = CardWidth();
    if (g.rows.empty()) {
        const bool filtered = !g.filter.empty();
        std::wstring msg = filtered ? L"Nichts gefunden."
                         : g.tab == T_HISTORY ? L"Noch nichts kopiert. Kopierte Texte erscheinen hier – nur im Arbeitsspeicher, solange du „Verlauf speichern“ nicht einschaltest."
                                              : L"Noch keine Prompts. Mit „+ Neuer Prompt“ legst du einen an, oder du heftest einen kopierten Text im Verlauf mit dem Stern an.";
        DrawText2(dc, g.fSmall, kTextDim, { g.list.left + S(6), g.list.top + S(6), g.list.right - S(6), g.list.bottom }, msg, DT_LEFT | DT_TOP | DT_WORDBREAK);
    }
    for (size_t ri = 0; ri < g.rows.size(); ++ri) {
        const Row& r = g.rows[ri];
        const int top = g.list.top + r.top - g.scroll;
        if (top + r.height < g.list.top || top > g.list.bottom) continue;
        RECT card{ g.list.left, top, g.list.left + cardW, top + r.height };
        RECT b1, b2;
        RowButtons(card, b1, b2);
        const int base = H_ROW_BASE + static_cast<int>(ri) * 3;
        const bool hot = g.hover == base, h1 = g.hover == base + 1, h2 = g.hover == base + 2;
        FillRound(dc, card, S(14), (hot || h1 || h2) ? kCardHover : kCard, (hot || h1 || h2) ? kBorderHot : kBorder);
        const int tx = card.left + S(18), tr = b1.left - S(12);
        if (g.tab == T_HISTORY) {
            DrawText2(dc, g.fBody, kText, { tx, card.top + S(10), tr, card.bottom - S(10) }, clip::Preview(g.store.history[r.idx], 160), DT_LEFT | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS);
            FillRound(dc, b1, S(10), h1 ? kCardHover : kCard, h1 ? kBorderHot : kBorder);
            DrawStar(dc, b1, h1 ? kClip : kTextDim, false);
        } else {
            const clip::Prompt& p = g.store.prompts[r.idx];
            DrawText2(dc, g.fBold, kText, { tx, card.top + S(10), tr, card.top + S(10) + S(24) }, p.title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            DrawText2(dc, g.fSmall, kTextDim, { tx, card.top + S(10) + S(24), tr, card.bottom - S(8) }, clip::Preview(p.text, 140), DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
            FillRound(dc, b1, S(10), h1 ? kCardHover : kCard, h1 ? kBorderHot : kBorder);
            DrawPencil(dc, b1, h1 ? kClip : kTextDim);
        }
        FillRound(dc, b2, S(10), h2 ? kCardHover : kCard, h2 ? kBorderHot : kBorder);
        DrawCross(dc, b2, h2 ? kRed : kTextDim);
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

    // Einstellungen als drei Schalter-Pillen
    DrawPill(dc, g.set[0], std::wstring(L"Mit Windows starten: ") + (g.cfg.autostart ? L"an" : L"aus"), g.cfg.autostart, g.hover == H_SET_AUTOSTART, kClip);
    DrawPill(dc, g.set[1], std::wstring(L"Verlauf speichern: ") + (g.cfg.saveHistory ? L"an" : L"aus"), g.cfg.saveHistory, g.hover == H_SET_SAVE, kClip);
    DrawPill(dc, g.set[2], std::wstring(L"Kürzel: ") + clip::HotkeyPreset(g.cfg.hotkey).name, false, g.hover == H_SET_HOTKEY, kClip);

    DrawText2(dc, g.fSmall, kTextDim, { g.footer.left, g.footer.top, g.footer.left + S(200), g.footer.bottom }, L"© 2026 Duncan · v" APP_VERSION, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawText2(dc, g.fSmall, g.statusColor, { g.footer.left + S(210), g.footer.top, g.footer.right, g.footer.bottom }, g.status, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (g.edit.open) PaintEditor(dc, cr);
}

// ---------- Eingabe Hauptfenster ----------
int HitTest(POINT p) {
    if (g.edit.open) {
        if (PtInRect(&g.editSave, p)) return H_EDIT_SAVE;
        if (PtInRect(&g.editCancel, p)) return H_EDIT_CANCEL;
        return H_NONE;
    }
    if (PtInRect(&g.tabHistory, p)) return H_TAB_HISTORY;
    if (PtInRect(&g.tabPrompts, p)) return H_TAB_PROMPTS;
    if (PtInRect(&g.actionBtn, p)) return H_ACTION;
    for (int i = 0; i < 3; ++i) if (PtInRect(&g.set[i], p)) return H_SET_AUTOSTART + i;
    if (PtInRect(&g.list, p) && p.x < g.list.left + CardWidth()) {
        const int y = static_cast<int>(p.y - g.list.top) + g.scroll;
        for (size_t i = 0; i < g.rows.size(); ++i) {
            const Row& r = g.rows[i];
            if (y < r.top || y >= r.top + r.height) continue;
            RECT card{ g.list.left, g.list.top + r.top - g.scroll, g.list.left + CardWidth(), g.list.top + r.top - g.scroll + r.height };
            RECT b1, b2;
            RowButtons(card, b1, b2);
            const int base = H_ROW_BASE + static_cast<int>(i) * 3;
            if (PtInRect(&b1, p)) return base + 1;
            if (PtInRect(&b2, p)) return base + 2;
            return base;
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
    if (id == H_EDIT_SAVE) CloseEditor(true);
    else if (id == H_EDIT_CANCEL) CloseEditor(false);
    else if (g.edit.open) return;
    else if (id == H_TAB_HISTORY) SetTab(T_HISTORY);
    else if (id == H_TAB_PROMPTS) SetTab(T_PROMPTS);
    else if (id == H_ACTION) {
        if (g.tab == T_HISTORY) { g.store.history.clear(); SaveHistoryIfWanted(); Layout(); SetStatus(L"Verlauf geleert.", kGreen); }
        else OpenEditor(-1);
    }
    else if (id == H_SET_AUTOSTART) ToggleAutostart();
    else if (id == H_SET_SAVE) ToggleSaveHistory();
    else if (id == H_SET_HOTKEY) CycleHotkey();
    else if (id >= H_ROW_BASE) ActivateRow((id - H_ROW_BASE) / 3, (id - H_ROW_BASE) % 3);
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

// ---------- Schnellauswahl (Popup) ----------
struct PopupItem { bool prompt; size_t idx; };
struct PopupState { std::vector<PopupItem> items; int sel = 0, scroll = 0; std::wstring filter; } pop;
const int kPopRows = 8;
WNDPROC g_oldPopupEdit = nullptr;
POINT g_popupMouse{};                                // Mausposition beim Oeffnen (Bildschirmkoordinaten)
std::wstring g_restoreText;                          // Zwischenablage vor dem Einfuegen (wird danach zurueckgegeben)
bool g_hasRestore = false;

int PopRowH() { return S(46); }
int PopListTop() { return S(54); }
int PopWidth() { return S(500); }
int PopHeight() { return PopListTop() + kPopRows * PopRowH() + S(34); }

void PopupRebuild() {
    pop.items.clear();
    for (size_t i = 0; i < g.store.prompts.size(); ++i)
        if (clip::Matches(g.store.prompts[i].title + L" " + g.store.prompts[i].text, pop.filter)) pop.items.push_back({ true, i });
    for (size_t i = 0; i < g.store.history.size(); ++i)
        if (clip::Matches(g.store.history[i], pop.filter)) pop.items.push_back({ false, i });
    pop.sel = pop.items.empty() ? 0 : std::clamp(pop.sel, 0, static_cast<int>(pop.items.size()) - 1);
    pop.scroll = std::clamp(pop.scroll, 0, std::max(0, static_cast<int>(pop.items.size()) - kPopRows));
}

void PopupEnsureVisible() {
    if (pop.sel < pop.scroll) pop.scroll = pop.sel;
    if (pop.sel >= pop.scroll + kPopRows) pop.scroll = pop.sel - kPopRows + 1;
}

void HidePopup() { if (g.popup) ShowWindow(g.popup, SW_HIDE); }

void DoPaste(const std::wstring& text) {
    std::wstring prev;
    const bool hadPrev = clip::ReadClipboardText(prev);
    g.suppressUntil = GetTickCount() + 2500;
    if (!clip::WriteClipboardText(text)) { SetStatus(L"Die Zwischenablage ist gerade belegt – bitte noch einmal versuchen.", kOrange); return; }
    if (g.prevFg && IsWindow(g.prevFg)) SetForegroundWindow(g.prevFg);
    if (g.prevElevated) {            // Windows blockiert das automatische Einfuegen in Programme mit hoeheren Rechten
        SetStatus(L"Das Zielprogramm läuft mit Administratorrechten: Der Text liegt in der Zwischenablage, bitte Strg+V drücken.", kOrange);
        if (g.nid.cbSize) {
            NOTIFYICONDATAW n = g.nid;
            n.uFlags = NIF_INFO;
            wcscpy_s(n.szInfoTitle, L"Duncan's Clip");
            wcscpy_s(n.szInfo, L"Der Text liegt in der Zwischenablage. Bitte Strg+V drücken.");
            n.dwInfoFlags = NIIF_INFO;
            Shell_NotifyIconW(NIM_MODIFY, &n);
        }
        return;
    }
    g_restoreText = prev;
    g_hasRestore = hadPrev;
    SetTimer(g.hwnd, kTimerPaste, 130, nullptr);
}

void PopupChoose(int sel) {
    if (sel < 0 || sel >= static_cast<int>(pop.items.size())) return;
    const PopupItem it = pop.items[static_cast<size_t>(sel)];
    std::wstring text = it.prompt ? clip::Expand(g.store.prompts[it.idx].text, CurrentClipText()) : g.store.history[it.idx];
    HidePopup();
    DoPaste(text);
}

void ShowPopup() {
    g.prevFg = GetForegroundWindow();
    g.prevElevated = g.prevFg && IsElevatedWindow(g.prevFg);
    pop.filter.clear(); pop.sel = 0; pop.scroll = 0;
    SetWindowTextW(g.popupSearch, L"");
    PopupRebuild();
    POINT pt; GetCursorPos(&pt);
    g_popupMouse = pt;
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    const int w = PopWidth(), h = PopHeight();
    int x = std::min<int>(pt.x, mi.rcWork.right - w - S(8)), y = std::min<int>(pt.y, mi.rcWork.bottom - h - S(8));
    x = std::max<int>(x, mi.rcWork.left + S(8)); y = std::max<int>(y, mi.rcWork.top + S(8));
    SetWindowPos(g.popup, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
    SetForegroundWindow(g.popup);
    SetFocus(g.popupSearch);
    InvalidateRect(g.popup, nullptr, TRUE);
}

void PopupPaint(HDC dc, const RECT& cr) {
    HBRUSH bg = CreateSolidBrush(RGB(30, 33, 41));
    FillRect(dc, &cr, bg);
    DeleteObject(bg);
    FillRound(dc, cr, S(14), RGB(30, 33, 41), kBorderHot);
    FillRound(dc, { S(12), S(10), cr.right - S(12), S(10) + S(34) }, S(10), Blend(kCard, kBackground, 30), kBorder);
    const int rowH = PopRowH(), top = PopListTop();
    if (pop.items.empty())
        DrawText2(dc, g.fSmall, kTextDim, { S(20), top + S(8), cr.right - S(20), top + S(60) },
                  pop.filter.empty() ? L"Noch nichts kopiert und keine Prompts angelegt." : L"Nichts gefunden.", DT_LEFT | DT_TOP | DT_WORDBREAK);
    for (int i = 0; i < kPopRows; ++i) {
        const int idx = pop.scroll + i;
        if (idx >= static_cast<int>(pop.items.size())) break;
        const PopupItem& it = pop.items[static_cast<size_t>(idx)];
        RECT r{ S(10), top + i * rowH, cr.right - S(10), top + (i + 1) * rowH - S(4) };
        const bool sel = idx == pop.sel;
        FillRound(dc, r, S(10), sel ? Blend(kCard, kClip, 18) : RGB(30, 33, 41), sel ? kClip : RGB(30, 33, 41));
        if (it.prompt) {
            const clip::Prompt& p = g.store.prompts[it.idx];
            DrawText2(dc, g.fBold, kClip, { r.left + S(12), r.top + S(2), r.left + S(30), r.bottom }, L"★", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            DrawText2(dc, g.fBold, kText, { r.left + S(32), r.top + S(3), r.right - S(10), r.top + S(23) }, p.title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            DrawText2(dc, g.fSmall, kTextDim, { r.left + S(32), r.top + S(22), r.right - S(10), r.bottom }, clip::Preview(p.text, 90), DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else {
            DrawText2(dc, g.fBody, kText, { r.left + S(12), r.top, r.right - S(10), r.bottom }, clip::Preview(g.store.history[it.idx], 120), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }
    DrawText2(dc, g.fSmall, kTextDim, { S(16), cr.bottom - S(28), cr.right - S(16), cr.bottom - S(6) }, L"Tippen zum Suchen  ·  ↑ ↓ wählen  ·  Enter einfügen  ·  Esc schließen", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

LRESULT CALLBACK PopupEditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_DOWN) { if (pop.sel + 1 < static_cast<int>(pop.items.size())) ++pop.sel; PopupEnsureVisible(); InvalidateRect(g.popup, nullptr, FALSE); return 0; }
        if (wp == VK_UP) { if (pop.sel > 0) --pop.sel; PopupEnsureVisible(); InvalidateRect(g.popup, nullptr, FALSE); return 0; }
        if (wp == VK_NEXT) { pop.sel = std::min<int>(pop.sel + kPopRows, std::max(0, static_cast<int>(pop.items.size()) - 1)); PopupEnsureVisible(); InvalidateRect(g.popup, nullptr, FALSE); return 0; }
        if (wp == VK_PRIOR) { pop.sel = std::max(0, pop.sel - kPopRows); PopupEnsureVisible(); InvalidateRect(g.popup, nullptr, FALSE); return 0; }
        if (wp == VK_RETURN) { PopupChoose(pop.sel); return 0; }
        if (wp == VK_ESCAPE) { HidePopup(); if (g.prevFg && IsWindow(g.prevFg)) SetForegroundWindow(g.prevFg); return 0; }
    }
    if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;   // kein Piepton
    return CallWindowProcW(g_oldPopupEdit, h, msg, wp, lp);
}

LRESULT CALLBACK PopupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT cr; GetClientRect(hwnd, &cr);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        PopupPaint(mem, cr);
        BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == kIdPopupSearch && HIWORD(wp) == EN_CHANGE) {
            pop.filter = GetEditText(g.popupSearch); pop.sel = 0; pop.scroll = 0;
            PopupRebuild();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, kText);
        SetBkColor(dc, Blend(kCard, kBackground, 30));
        return reinterpret_cast<LRESULT>(g.editBrush);
    }
    case WM_MOUSEMOVE: {
        POINT now; GetCursorPos(&now);
        if (std::abs(now.x - g_popupMouse.x) < 3 && std::abs(now.y - g_popupMouse.y) < 3) return 0;   // Maus steht still: Auswahl per Tastatur bleibt
        g_popupMouse = now;
        const int y = GET_Y_LPARAM(lp) - PopListTop();
        if (y >= 0) {
            const int idx = pop.scroll + y / PopRowH();
            if (idx < static_cast<int>(pop.items.size()) && idx != pop.sel) { pop.sel = idx; InvalidateRect(hwnd, nullptr, FALSE); }
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        const int y = GET_Y_LPARAM(lp) - PopListTop();
        if (y >= 0) PopupChoose(pop.scroll + y / PopRowH());
        return 0;
    }
    case WM_MOUSEWHEEL: {
        pop.scroll = std::clamp(pop.scroll - (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1), 0, std::max(0, static_cast<int>(pop.items.size()) - kPopRows));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------- Infobereich ----------
UINT g_taskbarCreated = 0;

void ShowMain() {
    ShowWindow(g.hwnd, SW_SHOW);
    ShowWindow(g.hwnd, SW_RESTORE);
    SetForegroundWindow(g.hwnd);
}

void AddTray(HINSTANCE inst) {
    g.nid = NOTIFYICONDATAW{};
    g.nid.cbSize = sizeof(g.nid);
    g.nid.hWnd = g.hwnd;
    g.nid.uID = 1;
    g.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g.nid.uCallbackMessage = WM_TRAY;
    g.nid.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wcscpy_s(g.nid.szTip, L"Duncan's Clip");
    Shell_NotifyIconW(NIM_ADD, &g.nid);
}

void TrayMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"Öffnen");
    AppendMenuW(m, MF_STRING, 2, L"Schnellauswahl");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 3, L"Beenden");
    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(g.hwnd);
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g.hwnd, nullptr);
    PostMessageW(g.hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (cmd == 1) ShowMain();
    else if (cmd == 2) ShowPopup();
    else if (cmd == 3) { g.quitting = true; DestroyWindow(g.hwnd); }
}

// ---------- Hauptfenster ----------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_taskbarCreated && msg == g_taskbarCreated) { Shell_NotifyIconW(NIM_ADD, &g.nid); return 0; }   // Explorer neu gestartet
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
    case WM_SIZE: if (g.hwnd) { Layout(); InvalidateRect(hwnd, nullptr, TRUE); } return 0;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = S(620);
        mmi->ptMinTrackSize.y = S(560);
        return 0;
    }
    case WM_DPICHANGED: {
        g.dpi = HIWORD(wp);
        MakeFonts();
        const RECT* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Layout();
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == kIdSearch && HIWORD(wp) == EN_CHANGE) {
            g.filter = GetEditText(g.hSearch); g.scroll = 0;
            Layout();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, kText);
        SetBkColor(dc, Blend(kCard, kBackground, 30));
        return reinterpret_cast<LRESULT>(g.editBrush);
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
        if (wp == VK_ESCAPE && g.edit.open) { CloseEditor(false); return 0; }
        if (!g.edit.open) {
            if (wp == VK_DOWN) Scroll(S(60)); else if (wp == VK_UP) Scroll(-S(60));
            else if (wp == VK_NEXT) Scroll(static_cast<int>(g.list.bottom - g.list.top)); else if (wp == VK_PRIOR) Scroll(-static_cast<int>(g.list.bottom - g.list.top));
        }
        return 0;
    case WM_CLIPBOARDUPDATE: OnClipboardChanged(); return 0;
    case WM_HOTKEY:
        if (wp == kHotkeyId) { if (IsWindowVisible(g.popup)) HidePopup(); else ShowPopup(); }
        return 0;
    case WM_TIMER:
        if (wp == kTimerStatus) { KillTimer(hwnd, kTimerStatus); g.status.clear(); InvalidateRect(hwnd, nullptr, FALSE); }
        else if (wp == kTimerPaste) {
            static int tries = 0;                           // erst einfuegen, wenn das Zielfenster wirklich vorn ist (hoechstens ~1,3 s warten)
            if (g.prevFg && IsWindow(g.prevFg) && GetForegroundWindow() != g.prevFg && tries < 10) { ++tries; SetForegroundWindow(g.prevFg); return 0; }
            tries = 0;
            KillTimer(hwnd, kTimerPaste);
            clip::SendPasteKeys();
            SetTimer(hwnd, kTimerRestore, 600, nullptr);    // danach die alte Zwischenablage zurueckgeben
        }
        else if (wp == kTimerRestore) {
            KillTimer(hwnd, kTimerRestore);
            if (g_hasRestore) { g.suppressUntil = GetTickCount() + 1500; clip::WriteClipboardText(g_restoreText, false); }
            g_hasRestore = false;
        }
        return 0;
    case WM_TRAY:
        if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == NIN_SELECT) ShowMain();
        else if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) TrayMenu();
        return 0;
    case WM_SHOWMAIN: ShowMain(); return 0;
    case WM_CLOSE:
        if (g.quitting) { DestroyWindow(hwnd); return 0; }
        ShowWindow(hwnd, SW_HIDE);                          // im Infobereich weiterlaufen
        return 0;
    case WM_DESTROY:
        RemoveClipboardFormatListener(hwnd);
        UnregisterHotKey(hwnd, kHotkeyId);
        Shell_NotifyIconW(NIM_DELETE, &g.nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND MakeEdit(HWND parent, int id, DWORD style, int limit) {
    HWND h = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | style, 0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, EM_SETLIMITTEXT, static_cast<WPARAM>(limit), 0);
    return h;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR cmdLine, int show) {
    const bool background = cmdLine && wcsstr(cmdLine, L"--background") != nullptr;
    HANDLE single = CreateMutexW(nullptr, TRUE, L"DuncansClip.Single");
    if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(kMainClass, nullptr)) if (!background) PostMessageW(other, WM_SHOWMAIN, 0, 0);
        return 0;
    }
    IconsInit();
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kMainClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = wc.hIconSm = LoadIconW(inst, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);
    WNDCLASSEXW pc{};
    pc.cbSize = sizeof(pc);
    pc.lpfnWndProc = PopupProc;
    pc.hInstance = inst;
    pc.lpszClassName = kPopupClass;
    pc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&pc);

    g.dpi = GetDpiForSystem();
    g.editBrush = CreateSolidBrush(Blend(kCard, kBackground, 30));
    g.hwnd = CreateWindowExW(0, kMainClass, kTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, S(680), S(760), nullptr, nullptr, inst, nullptr);
    if (!g.hwnd) return 1;
    g.dpi = GetDpiForWindow(g.hwnd);
    g.hSearch = MakeEdit(g.hwnd, kIdSearch, WS_VISIBLE | ES_AUTOHSCROLL, 200);
    g.hEditTitle = MakeEdit(g.hwnd, kIdEditTitle, ES_AUTOHSCROLL, 80);
    g.hEditText = MakeEdit(g.hwnd, kIdEditText, ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, static_cast<int>(clip::kMaxTextLen));
    g.popup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kPopupClass, L"", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, inst, nullptr);
    g.popupSearch = MakeEdit(g.popup, kIdPopupSearch, WS_VISIBLE | ES_AUTOHSCROLL, 200);
    g_oldPopupEdit = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g.popupSearch, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(PopupEditProc)));
    MakeFonts();
    SetWindowPos(g.popupSearch, nullptr, S(22), S(17), PopWidth() - S(44), S(22), SWP_NOZORDER);
    gfx::EnableDarkTitleBar(g.hwnd);

    const bool firstRun = GetFileAttributesW((clip::DataDir() + L"\\settings.ini").c_str()) == INVALID_FILE_ATTRIBUTES;
    g.cfg = clip::LoadSettings();
    g.store.prompts = clip::LoadPrompts();
    if (g.cfg.saveHistory) g.store.history = clip::LoadHistory();
    if (firstRun && g.store.prompts.empty()) {       // drei Beispiele, damit der Reiter nicht leer startet
        g.store.prompts = {
            { L"Zusammenfassen", L"Fasse den folgenden Text kurz und klar zusammen:\r\n\r\n{clip}" },
            { L"Übersetzen (Englisch)", L"Übersetze den folgenden Text ins Englische:\r\n\r\n{clip}" },
            { L"Verbessern", L"Verbessere Rechtschreibung und Ausdruck, ohne den Inhalt zu ändern:\r\n\r\n{clip}" },
        };
        clip::SavePrompts(g.store.prompts);
        clip::SaveSettings(g.cfg);
    }
    g.hotkeyOk = RegisterHotkeyFor(g.cfg.hotkey);
    AddClipboardFormatListener(g.hwnd);
    AddTray(inst);
    Layout();
    if (!g.hotkeyOk) SetStatus(L"Das Kürzel ist von einem anderen Programm belegt – unten „Kürzel“ klicken, um ein anderes zu wählen.", kOrange);
    else if (firstRun) SetStatus(std::wstring(L"Tipp: Mit ") + clip::HotkeyPreset(g.cfg.hotkey).name + L" öffnest du die Schnellauswahl in jedem Programm.", kClip);
    if (!background) { ShowWindow(g.hwnd, show); UpdateWindow(g.hwnd); }

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    IconsShutdown();
    return static_cast<int>(m.wParam);
}
