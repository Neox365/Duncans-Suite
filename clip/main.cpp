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
#include <commdlg.h>

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <functional>
#include <map>
#include <string>
#include <vector>

using std::max;
using std::min;
#include <objbase.h>
#include <propidl.h>
#include <gdiplus.h>

#include "../common/DarkMenus.h"
#include "../src/Icons.h"
#include "../common/ThemedBox.h"
#include "../common/Theme.h"
#include "../common/Gfx.h"
#include "ClipLogic.h"

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
const COLORREF kClip = RGB(34, 211, 238);           // Akzent dieses Programms (Tuerkis)

const wchar_t* kTitle = L"Duncan's Clip";
const wchar_t* kMainClass = L"DuncansClip";
const wchar_t* kPopupClass = L"DuncansClipPopup";
const UINT WM_TRAY = WM_APP + 10;
const UINT WM_SHOWMAIN = WM_APP + 11;                // zweiter Start: bestehendes Fenster zeigen
const int kHotkeyId = 1;
const UINT_PTR kTimerPaste = 1, kTimerStatus = 2, kTimerRestore = 3, kTimerPurge = 5;
const int kIdEditTitle = 101, kIdEditText = 102, kIdSearch = 103, kIdPopupSearch = 104, kIdEditFolder = 105;

enum Tab { T_HISTORY, T_PROMPTS };
enum HitId {
    H_NONE = -1, H_TAB_HISTORY = 1000, H_TAB_PROMPTS = 1001, H_ACTION = 1002, H_INTRO_OK = 1003, H_HELP = 1004, H_SET_BASE = 1010 /* 1010..1015 */, H_CHIP_BASE = 1100 /* 1100..1149 */,
    H_EDIT_SAVE = 2000, H_EDIT_CANCEL = 2001, H_ROW_BASE = 3000
};

struct Row { size_t idx = 0; int top = 0, height = 0; };
constexpr int kRowParts = 4;   // Treffer je Zeile: 0 Zeile (kopieren), 1 Stern/Stift, 2 Kreuz, 3 Pin

struct EditState { bool open = false; int index = -1; };   // index -1 = neuer Prompt

struct Thumb { HBITMAP bmp = nullptr; int w = 0, h = 0; };      // Vorschaubild eines Bildes im Verlauf

struct App {
    HWND hwnd = nullptr, hSearch = nullptr, hEditTitle = nullptr, hEditFolder = nullptr, hEditText = nullptr;
    HWND popup = nullptr, popupSearch = nullptr;
    UINT dpi = 96;
    HFONT fTitle = nullptr, fHead = nullptr, fBody = nullptr, fSmall = nullptr, fBold = nullptr;
    HBRUSH editBrush = nullptr;
    clip::Store store;
    clip::Settings cfg;
    Tab tab = T_HISTORY;
    std::wstring filter;
    std::wstring folderFilter;                       // Ordner-Chip im Reiter Prompts (leer = alle)
    std::vector<RECT> chipRects;
    std::vector<std::wstring> chipNames;
    std::map<unsigned, Thumb> thumbs;                // Vorschaubilder je Verlaufseintrag (Id)
    std::vector<Row> rows;
    int scroll = 0, contentH = 0;
    RECT tabHistory{}, tabPrompts{}, actionBtn{}, searchRc{}, chipsRc{}, list{}, set[6]{}, footer{}, editCard{}, editSave{}, editCancel{};
    RECT introRc{}, introOk{}, helpBtn{}, hintRc{};
    std::wstring lastOwner;   // Programm, aus dem zuletzt kopiert wurde (zum Sperren per Menue)
    EditState edit;
    int hover = H_NONE, press = H_NONE;
    bool tracking = false;
    std::wstring status;
    COLORREF statusColor = kTextDim;
    DWORD suppressUntil = 0;                         // eigene Zwischenablage-Aenderungen nicht in den Verlauf uebernehmen
    HWND prevFg = nullptr;                           // Fenster, in das eingefuegt wird
    bool prevElevated = false;
    std::wstring selection;                          // beim Oeffnen der Schnellauswahl uebernommene Markierung (ersetzt {clip})
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
    for (HWND h : { g.hSearch, g.hEditTitle, g.hEditFolder, g.hEditText, g.popupSearch }) if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g.fBody), TRUE);
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

// Holt ein Fenster zuverlaessig in den Vordergrund (Windows verweigert SetForegroundWindow oft, wenn die letzte Eingabe schon etwas her ist,
// z. B. nach dem Uebernehmen der Markierung): kurz an die Eingabe des aktuellen Vordergrundfensters ankoppeln.
void ForceForeground(HWND h) {
    if (!h || !IsWindow(h)) return;
    HWND fg = GetForegroundWindow();
    const DWORD me = GetCurrentThreadId(), fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const bool attach = fgThread && fgThread != me && AttachThreadInput(me, fgThread, TRUE);
    BringWindowToTop(h);
    SetForegroundWindow(h);
    if (attach) AttachThreadInput(me, fgThread, FALSE);
}

// ---------- Daten ----------
void SavePinsNow() {                                  // Pins werden immer gespeichert (verschluesselt), der normale Verlauf nur auf Wunsch
    std::vector<std::wstring> pins;
    for (const clip::HistoryItem& h : g.store.history) if (h.pinned) pins.push_back(h.text);
    clip::SavePins(pins);
}

void SaveHistoryIfWanted() {
    if (!g.cfg.saveHistory) return;
    std::vector<std::wstring> texts;                  // Bilder werden nie gespeichert, Pins stehen in ihrer eigenen Datei
    for (const clip::HistoryItem& h : g.store.history) if (!h.IsImage() && !h.pinned) texts.push_back(h.text);
    clip::SaveHistory(texts);
}

// Vorschaubilder: eines je Verlaufseintrag, werden beim Entfernen des Eintrags freigegeben
void FreeThumbs() {
    for (auto& t : g.thumbs) if (t.second.bmp) DeleteObject(t.second.bmp);
    g.thumbs.clear();
}

void PruneThumbs() {
    for (auto it = g.thumbs.begin(); it != g.thumbs.end();) {
        const unsigned id = it->first;
        const bool alive = std::any_of(g.store.history.begin(), g.store.history.end(), [id](const clip::HistoryItem& h) { return h.id == id; });
        if (!alive) { if (it->second.bmp) DeleteObject(it->second.bmp); it = g.thumbs.erase(it); } else ++it;
    }
}

const Thumb& GetThumb(const clip::HistoryItem& it) {
    auto f = g.thumbs.find(it.id);
    if (f != g.thumbs.end()) return f->second;
    Thumb t;
    t.bmp = clip::MakeThumbnail(it.png, S(110), S(64), t.w, t.h);
    return g.thumbs.emplace(it.id, t).first->second;
}

void DrawThumbBitmap(HDC dc, const Thumb& t, int x, int y, int w, int h) {
    if (!t.bmp) return;
    HDC m = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(m, t.bmp);
    SetStretchBltMode(dc, HALFTONE);
    StretchBlt(dc, x, y, w, h, m, 0, 0, t.w, t.h, SRCCOPY);
    SelectObject(m, old);
    DeleteDC(m);
}

void SavePromptsNow() { if (!clip::SavePrompts(g.store.prompts)) SetStatus(L"Die Prompts konnten nicht gespeichert werden.", kRed); }

void Layout();

void OnClipboardChanged() {
    if (GetTickCount() < g.suppressUntil) return;
    if (clip::ClipboardIsExcluded()) return;
    const std::wstring owner = clip::ClipboardOwnerExe();
    if (!owner.empty()) g.lastOwner = owner;
    bool changed = false;
    std::wstring text;
    if (clip::ReadClipboardText(text)) {
        const clip::SkipReason why = clip::SkipReasonFor(text, false, owner, &g.cfg.excludedExes);
        if (why == clip::SkipReason::Secret) {          // Schluessel/Token nie speichern, aber sagen, dass es so ist
            SetStatus(L"Ein " + clip::DetectSecret(text) + L" wurde erkannt und nicht gespeichert.", kOrange);
            return;
        }
        if (why != clip::SkipReason::None) return;
        std::wstring rewrite;                           // soll die Zwischenablage durch eine bereinigte Fassung ersetzt werden?
        std::wstring why2;
        if (g.cfg.cleanLinks) {                         // Link von Tracking saeubern
            rewrite = clip::CleanLink(text);
            if (!rewrite.empty()) why2 = L"Link gesäubert: Tracking entfernt.";
        }
        if (rewrite.empty() && g.cfg.plainText && clip::ClipboardHasRichText()) {   // Formatierung entfernen: nur der Text bleibt
            rewrite = text;
            why2 = L"Formatierung entfernt: nur reiner Text.";
        }
        if (!rewrite.empty()) {
            g.suppressUntil = GetTickCount() + 1500;
            if (clip::WriteClipboardText(rewrite, false)) { text = rewrite; SetStatus(why2, kGreen); }
        }
        changed = clip::AddHistory(g.store, text);
    } else if (clip::ClipboardHasImage() && !clip::IsPasswordManagerExe(owner) && !clip::IsExcludedExe(g.cfg.excludedExes, owner)) {
        std::string png; int w = 0, h = 0;
        if (clip::ReadClipboardImage(png, w, h)) changed = clip::AddImage(g.store, std::move(png), w, h);
    }
    if (changed) {
        PruneThumbs();
        SaveHistoryIfWanted();
        Layout();                                     // Zeilenliste neu aufbauen, sonst bleibt die Anzeige alt
        InvalidateRect(g.hwnd, nullptr, FALSE);
    }
}

// Aufbewahrung: alte Eintraege nach der gewaehlten Zeit entfernen (jede Minute geprueft)
void PurgeNow() {
    if (clip::PurgeHistory(g.store, clip::RetentionMs(g.cfg.retention)) == 0) return;
    PruneThumbs();
    SaveHistoryIfWanted();
    Layout();
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

// ---------- Layout Hauptfenster ----------
int CardWidth() { return g.list.right - g.list.left - S(14); }

bool RowMatches(size_t i) {
    if (g.tab == T_HISTORY) return clip::Matches(g.store.history[i].text, g.filter);
    const clip::Prompt& p = g.store.prompts[i];
    if (!g.folderFilter.empty() && p.folder != g.folderFilter) return false;
    return clip::Matches(p.title + L" " + p.text + L" " + p.folder, g.filter);
}

void BuildRows() {
    g.rows.clear();
    int y = 0;
    const size_t n = g.tab == T_HISTORY ? g.store.history.size() : g.store.prompts.size();
    for (size_t i = 0; i < n; ++i) {
        if (!RowMatches(i)) continue;
        Row r; r.idx = i; r.top = y; r.height = (g.tab == T_HISTORY && g.store.history[i].IsImage()) ? S(88) : S(66);
        g.rows.push_back(r);
        y += r.height + S(8);
    }
    g.contentH = y;
}

void PositionEditControls() {
    const bool open = g.edit.open;
    ShowWindow(g.hEditTitle, open ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hEditFolder, open ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hEditText, open ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hSearch, open ? SW_HIDE : SW_SHOW);
    if (!open) return;
    const RECT& c = g.editCard;
    SetWindowPos(g.hEditTitle, nullptr, c.left + S(24), c.top + S(86), c.right - c.left - S(48), S(30), SWP_NOZORDER);
    SetWindowPos(g.hEditFolder, nullptr, c.left + S(24), c.top + S(150), c.right - c.left - S(48), S(30), SWP_NOZORDER);
    SetWindowPos(g.hEditText, nullptr, c.left + S(24), c.top + S(214), c.right - c.left - S(48), c.bottom - c.top - S(214) - S(86), SWP_NOZORDER);
}

// ---------- Anleitung ("Was ist Clip?") und Erklaerzeile zu den Einstellungen ----------
const wchar_t* const kSetHints[6] = {
    L"Mit Windows starten: Clip startet beim Anmelden von selbst im Infobereich (ohne Fenster), damit das Kürzel immer bereit ist.",
    L"Verlauf speichern: Behält den Verlauf auch nach dem Beenden (verschlüsselt, nur auf diesem PC). Aus: nur im Arbeitsspeicher.",
    L"Kürzel: Mit dieser Tastenkombination öffnest du die Schnellauswahl in jedem Programm. Ein Klick wechselt das Kürzel.",
    L"Verlauf löschen nach: Entfernt kopierte Einträge nach der gewählten Zeit automatisch. Ein Klick wechselt (nie, 1 Stunde, 1 Tag).",
    L"Markierung übernehmen: Markierten Text übernimmt Clip beim Öffnen der Schnellauswahl und setzt ihn für {clip} ein – ohne vorher Strg+C.",
    L"Mehr: Prompts als Textdatei sichern oder laden, Links beim Kopieren von Tracking säubern, und Programme sperren – aus gesperrten Programmen nimmt Clip nie etwas auf.",
};

std::vector<std::wstring> IntroSteps() {
    const std::wstring key = clip::HotkeyPreset(g.cfg.hotkey).name;
    return {
        L"Kopiere wie immer (Strg+C). Alles Kopierte erscheint im Reiter „Verlauf“.",
        L"Drücke in irgendeinem Programm " + key + L": Die Schnellauswahl öffnet sich. Tippen filtert, Enter fügt den Eintrag dort ein.",
        L"Im Reiter „Prompts“ legst du Textbausteine an. {clip} wird beim Einfügen durch deinen kopierten oder markierten Text ersetzt, zum Beispiel „Fasse zusammen: {clip}“.",
    };
}
const wchar_t* kIntroTop = L"Clip merkt sich, was du kopierst, und fügt es per Tastenkürzel in jedes Programm ein. Dazu gibt es Textbausteine („Prompts“) für alles, was du oft schreibst, etwa Fragen an eine KI.";
const wchar_t* kIntroPrivacy = L"Privat: Passwörter, Schlüssel und Kartennummern speichert Clip nie. Der Verlauf liegt nur im Arbeitsspeicher, solange du „Verlauf speichern“ nicht einschaltest.";

int IntroHeight(HDC dc, int width) {                   // Hoehe der Karte bei dieser Breite
    const int tw = width - S(44);
    int h = S(18) + S(30) + WrappedHeight(dc, g.fBody, kIntroTop, tw) + S(12);
    for (const std::wstring& st : IntroSteps()) h += std::max(S(26), WrappedHeight(dc, g.fBody, st, tw - S(36))) + S(8);
    h += S(4) + WrappedHeight(dc, g.fSmall, kIntroPrivacy, tw) + S(14) + S(38) + S(16);
    return h;
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
    const int hw = TextWidth(dc, g.fBold, L"Hilfe") + S(36);
    g.helpBtn = { g.tabPrompts.right + S(10), ty, g.tabPrompts.right + S(10) + hw, ty + th };
    ReleaseDC(g.hwnd, dc);
    g.searchRc = { m, ty + th + S(14), right, ty + th + S(14) + S(36) };
    // Ordner-Chips (nur im Reiter Prompts, wenn es Ordner gibt)
    g.chipRects.clear(); g.chipNames.clear(); g.chipsRc = RECT{};
    int listTop = g.searchRc.bottom + S(14);
    if (g.tab == T_PROMPTS) {
        const std::vector<std::wstring> folders = clip::FolderNames(g.store.prompts);
        if (!g.folderFilter.empty() && std::find(folders.begin(), folders.end(), g.folderFilter) == folders.end()) g.folderFilter.clear();   // Ordner gibt es nicht mehr
        if (!folders.empty()) {
            std::vector<std::wstring> names = { L"" };
            names.insert(names.end(), folders.begin(), folders.end());
            const int chH = S(32), chY = g.searchRc.bottom + S(12);
            int x = m;
            HDC cdc = GetDC(g.hwnd);
            for (const std::wstring& n : names) {
                const std::wstring label = n.empty() ? L"Alle" : n;
                const int cw = TextWidth(cdc, g.fSmall, label) + S(28);
                if (x + cw > right) break;
                g.chipRects.push_back({ x, chY, x + cw, chY + chH });
                g.chipNames.push_back(n);
                x += cw + S(8);
            }
            ReleaseDC(g.hwnd, cdc);
            g.chipsRc = { m, chY, right, chY + chH };
            listTop = chY + chH + S(12);
        }
    }
    g.footer = { m, cr.bottom - S(34), right, cr.bottom - S(8) };
    const int setH = S(40), setGapY = S(8), row2Top = g.footer.top - S(10) - setH, row1Top = row2Top - setGapY - setH;
    g.hintRc = { m, row1Top - S(40), right, row1Top - S(8) };
    const int gap = S(10), setW = (right - m - 2 * gap) / 3;
    for (int i = 0; i < 6; ++i) {
        const int top = i < 3 ? row1Top : row2Top, col = i % 3;
        g.set[i] = { m + col * (setW + gap), top, m + col * (setW + gap) + setW, top + setH };
    }
    g.introRc = g.introOk = RECT{};
    if (!g.cfg.hideIntro) {
        HDC idc = GetDC(g.hwnd);
        const int ih = IntroHeight(idc, right - m);
        ReleaseDC(g.hwnd, idc);
        if (g.hintRc.top - S(8) - (listTop + ih + S(12)) >= S(90)) {      // nur zeigen, wenn die Liste daneben noch Platz hat
            g.introRc = { m, listTop, right, listTop + ih };
            g.introOk = { right - S(24) - S(150), g.introRc.bottom - S(16) - S(38), right - S(24), g.introRc.bottom - S(16) };
            listTop = g.introRc.bottom + S(12);
        }
    }
    g.list = { m, listTop, right, g.hintRc.top - S(8) };
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
    SetWindowTextW(g.hEditFolder, index >= 0 ? g.store.prompts[static_cast<size_t>(index)].folder.c_str() : g.folderFilter.c_str());   // neuer Prompt: aktueller Ordner-Chip vorbelegt
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
        std::wstring folder = GetEditText(g.hEditFolder);
        const size_t fa = folder.find_first_not_of(L" \t"), fb = folder.find_last_not_of(L" \t");
        p.folder = fa == std::wstring::npos ? L"" : folder.substr(fa, fb - fa + 1);
        if (g.edit.index >= 0) { p.lastUsed = g.store.prompts[static_cast<size_t>(g.edit.index)].lastUsed; g.store.prompts[static_cast<size_t>(g.edit.index)] = p; }
        else g.store.prompts.push_back(p);
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

void CopyImageToClipboard(const std::string& png, const std::wstring& okMsg) {
    g.suppressUntil = GetTickCount() + 1500;
    if (clip::WriteClipboardImage(png)) SetStatus(okMsg, kGreen);
    else SetStatus(L"Das Bild konnte nicht in die Zwischenablage gelegt werden.", kOrange);
}

void MarkUsed(size_t promptIdx) {                    // fuer die Reihenfolge "zuletzt benutzt zuerst" in der Schnellauswahl
    if (promptIdx >= g.store.prompts.size()) return;
    g.store.prompts[promptIdx].lastUsed = clip::NowSeconds();
    clip::SavePrompts(g.store.prompts);
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
    if (g.cfg.saveHistory) { SaveHistoryIfWanted(); SetStatus(L"Verlauf wird verschlüsselt gespeichert.", kGreen); }
    else { clip::DeleteHistoryFile(); SetStatus(L"Verlauf bleibt nur im Arbeitsspeicher; die gespeicherte Datei wurde gelöscht.", kGreen); }
}

void CycleRetention() {
    g.cfg.retention = (g.cfg.retention + 1) % 3;
    clip::SaveSettings(g.cfg);
    PurgeNow();
    SetStatus(g.cfg.retention == 0 ? L"Der Verlauf wird nicht automatisch gelöscht."
                                   : std::wstring(L"Einträge im Verlauf werden nach ") + clip::RetentionName(g.cfg.retention) + L" gelöscht.", kGreen);
}

void ToggleCapture() {
    g.cfg.captureSelection = !g.cfg.captureSelection;
    clip::SaveSettings(g.cfg);
    SetStatus(g.cfg.captureSelection ? L"Markierter Text wird beim Öffnen der Schnellauswahl automatisch übernommen (nicht in Terminals)."
                                     : L"Markierung wird nicht mehr automatisch übernommen: vorher Strg+C drücken.", kGreen);
}

void ToggleCleanLinks() {
    g.cfg.cleanLinks = !g.cfg.cleanLinks;
    clip::SaveSettings(g.cfg);
    SetStatus(g.cfg.cleanLinks ? L"Kopierte Links werden von Tracking (utm_, fbclid, Amazon-ref …) gesäubert."
                               : L"Links werden wieder unverändert gelassen.", kGreen);
}

void TogglePlainText() {
    g.cfg.plainText = !g.cfg.plainText;
    clip::SaveSettings(g.cfg);
    SetStatus(g.cfg.plainText ? L"Beim Kopieren bleibt nur reiner Text (ohne Formatierung). Achtung: Auch Tabellen aus Excel kommen dann ohne Zellen an."
                              : L"Kopierter Text behält wieder seine Formatierung.", kGreen);
}

void ExportPromptsToFile() {
    wchar_t file[MAX_PATH] = L"Clip-Prompts.txt";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = L"Textdatei (*.txt)\0*.txt\0\0"; ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"txt"; ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;
    if (clip::WriteTextFile(file, clip::ExportPrompts(g.store.prompts))) SetStatus(std::to_wstring(g.store.prompts.size()) + L" Prompts exportiert.", kGreen);
    else SetStatus(L"Die Datei konnte nicht geschrieben werden.", kRed);
}

void ImportPromptsFromFile() {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = L"Textdatei (*.txt)\0*.txt\0Alle Dateien\0*.*\0\0"; ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    std::wstring text;
    if (!clip::ReadTextFile(file, text)) { SetStatus(L"Die Datei konnte nicht gelesen werden.", kRed); return; }
    const std::vector<clip::Prompt> imported = clip::ImportPrompts(text);
    if (imported.empty()) { SetStatus(L"In der Datei wurden keine Prompts gefunden.", kOrange); return; }
    const int added = clip::MergePrompts(g.store.prompts, imported);
    if (added > 0) SavePromptsNow();
    Layout();
    SetStatus(std::to_wstring(added) + L" neue Prompts importiert" + (added < static_cast<int>(imported.size()) ? L" (Doppelte übersprungen)." : L"."), added ? kGreen : kTextDim);
}

void ExportImportMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"Prompts exportieren …");
    AppendMenuW(m, MF_STRING, 2, L"Prompts importieren …");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (g.cfg.cleanLinks ? MF_CHECKED : 0), 4, L"Links beim Kopieren säubern (Tracking entfernen)");
    AppendMenuW(m, MF_STRING | (g.cfg.plainText ? MF_CHECKED : 0), 5, L"Beim Kopieren nur reinen Text behalten (ohne Formatierung)");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    const bool canBlock = !g.lastOwner.empty() && !clip::IsExcludedExe(g.cfg.excludedExes, g.lastOwner) && !clip::IsPasswordManagerExe(g.lastOwner);
    AppendMenuW(m, canBlock ? MF_STRING : MF_STRING | MF_GRAYED, 3,
                canBlock ? (L"Zuletzt kopierendes Programm sperren: " + g.lastOwner).c_str() : L"Programm sperren (erst etwas kopieren)");
    for (size_t i = 0; i < g.cfg.excludedExes.size() && i < 20; ++i)
        AppendMenuW(m, MF_STRING, static_cast<UINT_PTR>(100 + i), (L"Entsperren: " + g.cfg.excludedExes[i]).c_str());
    POINT pt{ g.set[5].left, g.set[5].top };
    ClientToScreen(g.hwnd, &pt);
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y - S(4), 0, g.hwnd, nullptr);
    DestroyMenu(m);
    if (cmd == 1) ExportPromptsToFile();
    else if (cmd == 2) ImportPromptsFromFile();
    else if (cmd == 4) ToggleCleanLinks();
    else if (cmd == 5) TogglePlainText();
    else if (cmd == 3 && clip::AddExcludedExe(g.cfg.excludedExes, g.lastOwner)) {
        clip::SaveSettings(g.cfg);
        SetStatus(g.lastOwner + L" ist gesperrt: daraus nimmt Clip nichts mehr auf.", kGreen);
    } else if (cmd >= 100 && static_cast<size_t>(cmd - 100) < g.cfg.excludedExes.size()) {
        const std::wstring name = g.cfg.excludedExes[static_cast<size_t>(cmd - 100)];
        clip::RemoveExcludedExe(g.cfg.excludedExes, name);
        clip::SaveSettings(g.cfg);
        SetStatus(name + L" ist wieder freigegeben.", kGreen);
    }
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
    if (themedbox::Show(g.hwnd, (L"Prompt „" + g.store.prompts[idx].title + L"“ löschen?").c_str(), kTitle, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    g.store.prompts.erase(g.store.prompts.begin() + static_cast<std::ptrdiff_t>(idx));
    SavePromptsNow();
    Layout();
}

void ActivateRow(int rowIndex, int part) {
    if (rowIndex < 0 || rowIndex >= static_cast<int>(g.rows.size())) return;
    const size_t i = g.rows[static_cast<size_t>(rowIndex)].idx;
    if (g.tab == T_HISTORY) {
        const clip::HistoryItem& it = g.store.history[i];
        if (part == 0) { if (it.IsImage()) CopyImageToClipboard(it.png, L"Bild in die Zwischenablage kopiert."); else CopyToClipboard(it.text, L"In die Zwischenablage kopiert."); }
        else if (part == 1) { if (!it.IsImage()) { g.store.prompts.push_back(clip::MakePrompt(it.text)); SavePromptsNow(); SetStatus(L"Als Prompt gespeichert (Reiter „Prompts“).", kGreen); } }
        else if (part == 3) {
            const bool was = it.pinned;
            if (clip::TogglePin(g.store, i)) {
                SavePinsNow(); SaveHistoryIfWanted(); Layout();
                SetStatus(was ? L"Pin entfernt." : L"Angepinnt: bleibt oben, auch nach „Verlauf leeren“ und beim Neustart.", kGreen);
            } else SetStatus(L"Mehr als " + std::to_wstring(clip::kMaxPinned) + L" Pins sind nicht möglich.", kOrange);
        }
        else { const bool wasPinned = it.pinned; clip::RemoveHistory(g.store, i); if (wasPinned) SavePinsNow(); PruneThumbs(); SaveHistoryIfWanted(); Layout(); }
    } else {
        if (part == 0) { CopyToClipboard(clip::Expand(g.store.prompts[i].text, CurrentClipText()), L"Prompt kopiert ({clip}, {datum} … ersetzt)."); MarkUsed(i); }
        else if (part == 1) OpenEditor(static_cast<int>(i));
        else ConfirmDeletePrompt(i);
    }
}

void SetTab(Tab t) {
    g.tab = t; g.scroll = 0; g.folderFilter.clear();
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

void DrawChip(HDC dc, const RECT& r, const std::wstring& text, bool active, bool hot) {
    FillRound(dc, r, S(10), active ? Blend(kCard, kClip, 22) : (hot ? kCardHover : kCard), active ? kClip : (hot ? kBorderHot : kBorder));
    DrawText2(dc, g.fSmall, active || hot ? kText : kTextDim, r, text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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

void RowButtons(const RECT& card, RECT& b1, RECT& b2, RECT* b3 = nullptr) {
    const int sz = S(32), pad = S(14);
    b2 = { card.right - pad - sz, card.top + (card.bottom - card.top - sz) / 2, card.right - pad, card.top + (card.bottom - card.top - sz) / 2 + sz };
    b1 = { b2.left - S(6) - sz, b2.top, b2.left - S(6), b2.bottom };
    if (b3) *b3 = { b1.left - S(6) - sz, b1.top, b1.left - S(6), b1.bottom };   // Pin (nur Verlauf, nur Text)
}

// Pinnnadel: Kopf und Spitze; angepinnt = gefuellt in der Akzentfarbe
void DrawPin(HDC dc, const RECT& r, COLORREF c, bool filled) {
    Gdiplus::Graphics gr(dc);
    gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float cx = (r.left + r.right) / 2.0f, cy = (r.top + r.bottom) / 2.0f, u = (r.right - r.left) / 32.0f;
    Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)), 1.7f);
    pen.SetStartCap(Gdiplus::LineCapRound); pen.SetEndCap(Gdiplus::LineCapRound); pen.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::SolidBrush br(Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)));
    const Gdiplus::PointF head[4] = { { cx - 4.5f * u, cy - 9.0f * u }, { cx + 4.5f * u, cy - 9.0f * u }, { cx + 3.0f * u, cy + 1.0f * u }, { cx - 3.0f * u, cy + 1.0f * u } };
    if (filled) gr.FillPolygon(&br, head, 4);
    gr.DrawPolygon(&pen, head, 4);
    gr.DrawLine(&pen, cx - 6.5f * u, cy + 1.0f * u, cx + 6.5f * u, cy + 1.0f * u);
    gr.DrawLine(&pen, cx, cy + 1.0f * u, cx, cy + 9.5f * u);
}

void PaintEditor(HDC dc, const RECT& cr) {
    FillAlphaRect(dc, cr, RGB(0, 0, 0), 120);
    FillRound(dc, g.editCard, S(16), RGB(30, 33, 41), kBorderHot);
    const RECT& c = g.editCard;
    DrawText2(dc, g.fHead, kText, { c.left + S(24), c.top + S(20), c.right - S(24), c.top + S(48) }, g.edit.index >= 0 ? L"Prompt bearbeiten" : L"Neuer Prompt");
    DrawText2(dc, g.fSmall, kTextDim, { c.left + S(24), c.top + S(62), c.right - S(24), c.top + S(84) }, L"Titel");
    DrawText2(dc, g.fSmall, kTextDim, { c.left + S(24), c.top + S(126), c.right - S(24), c.top + S(148) }, L"Ordner (optional, z. B. „Programmieren“)");
    DrawText2(dc, g.fSmall, kTextDim, { c.left + S(24), c.top + S(190), c.right - S(24), c.top + S(212) }, L"Text  –  {clip} = Zwischenablage oder Markierung, {datum}, {zeit}, {wochentag}");
    FillRound(dc, { c.left + S(20), c.top + S(82), c.right - S(20), c.top + S(120) }, S(10), Blend(kCard, kBackground, 30), kBorder);
    FillRound(dc, { c.left + S(20), c.top + S(146), c.right - S(20), c.top + S(184) }, S(10), Blend(kCard, kBackground, 30), kBorder);
    FillRound(dc, { c.left + S(20), c.top + S(210), c.right - S(20), c.bottom - S(82) }, S(10), Blend(kCard, kBackground, 30), kBorder);
    DrawButton(dc, g.editCancel, L"Abbrechen", false, g.hover == H_EDIT_CANCEL, g.press == H_EDIT_CANCEL && g.hover == H_EDIT_CANCEL);
    DrawButton(dc, g.editSave, L"Speichern", true, g.hover == H_EDIT_SAVE, g.press == H_EDIT_SAVE && g.hover == H_EDIT_SAVE);
}

void Paint(HDC dc, const RECT& cr) {
    HBRUSH bg = CreateSolidBrush(kBackground);
    FillRect(dc, &cr, bg);
    DeleteObject(bg);
    DrawText2(dc, g.fTitle, kText, { S(28), S(18), cr.right - S(28), S(56) }, kTitle);
    DrawText2(dc, g.fBody, kTextDim, { S(28), S(54), cr.right - S(28), S(80) },
              std::wstring(L"Dein Verlauf und deine Textbausteine – in jedem Programm mit ") + clip::HotkeyPreset(g.cfg.hotkey).name + (g.hotkeyOk ? L"" : L"  (Kürzel belegt!)"));

    DrawPill(dc, g.tabHistory, L"Verlauf", g.tab == T_HISTORY, g.hover == H_TAB_HISTORY, kClip);
    DrawPill(dc, g.tabPrompts, L"Prompts", g.tab == T_PROMPTS, g.hover == H_TAB_PROMPTS, kClip);
    DrawPill(dc, g.helpBtn, L"Hilfe", !g.cfg.hideIntro && g.introRc.right > g.introRc.left, g.hover == H_HELP, kClip);
    DrawButton(dc, g.actionBtn, g.tab == T_HISTORY ? L"Verlauf leeren" : L"+ Neuer Prompt", g.tab == T_PROMPTS, g.hover == H_ACTION, g.press == H_ACTION && g.hover == H_ACTION);

    // Suchfeld (das EDIT liegt darueber)
    FillRound(dc, g.searchRc, S(12), Blend(kCard, kBackground, 30), kBorder);
    DrawText2(dc, g.fSmall, kTextDim, { g.searchRc.left + S(16), g.searchRc.top, g.searchRc.left + S(80), g.searchRc.bottom }, L"Suchen:", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    for (size_t ci = 0; ci < g.chipRects.size(); ++ci)                                  // Ordner-Chips
        DrawChip(dc, g.chipRects[ci], g.chipNames[ci].empty() ? L"Alle" : g.chipNames[ci], g.chipNames[ci] == g.folderFilter, g.hover == H_CHIP_BASE + static_cast<int>(ci));

    // Anleitung
    if (g.introRc.right > g.introRc.left) {
        const RECT& c = g.introRc;
        FillRound(dc, c, S(16), Blend(kCard, kClip, 7), Blend(kBorder, kClip, 45));
        const int pad = S(22), tw = (c.right - c.left) - 2 * pad;
        int y = c.top + S(18);
        DrawText2(dc, g.fHead, kText, { c.left + pad, y, c.right - pad, y + S(28) }, L"Was ist Clip?");
        y += S(30);
        int h = WrappedHeight(dc, g.fBody, kIntroTop, tw);
        DrawText2(dc, g.fBody, kTextDim, { c.left + pad, y, c.right - pad, y + h }, kIntroTop, DT_LEFT | DT_TOP | DT_WORDBREAK);
        y += h + S(12);
        int n = 1;
        for (const std::wstring& st : IntroSteps()) {
            const int sh = std::max(S(26), WrappedHeight(dc, g.fBody, st, tw - S(36)));
            RECT badge{ c.left + pad, y, c.left + pad + S(24), y + S(24) };
            FillRound(dc, badge, S(12), Blend(kCard, kClip, 30), kClip);
            DrawText2(dc, g.fBold, kClip, badge, std::to_wstring(n++), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DrawText2(dc, g.fBody, kText, { c.left + pad + S(36), y, c.right - pad, y + sh }, st, DT_LEFT | DT_TOP | DT_WORDBREAK);
            y += sh + S(8);
        }
        y += S(4);
        DrawText2(dc, g.fSmall, kTextDim, { c.left + pad, y, c.right - pad, y + WrappedHeight(dc, g.fSmall, kIntroPrivacy, tw) }, kIntroPrivacy, DT_LEFT | DT_TOP | DT_WORDBREAK);
        DrawButton(dc, g.introOk, L"Verstanden", true, g.hover == H_INTRO_OK, g.press == H_INTRO_OK && g.hover == H_INTRO_OK);
    }

    // Liste
    SaveDC(dc);
    IntersectClipRect(dc, g.list.left - S(4), g.list.top, g.list.right + S(4), g.list.bottom);
    const int cardW = CardWidth();
    if (g.rows.empty()) {
        const bool filtered = !g.filter.empty();
        std::wstring msg = filtered ? L"Nichts gefunden."
                         : g.tab == T_HISTORY ? L"Noch nichts kopiert. Kopiere irgendwo einen Text (Strg+C), dann erscheint er hier."
                                              : L"Noch keine Prompts. Mit „+ Neuer Prompt“ legst du einen an, oder du heftest einen kopierten Text im Verlauf mit dem Stern an.";
        DrawText2(dc, g.fSmall, kTextDim, { g.list.left + S(6), g.list.top + S(6), g.list.right - S(6), g.list.bottom }, msg, DT_LEFT | DT_TOP | DT_WORDBREAK);
    }
    for (size_t ri = 0; ri < g.rows.size(); ++ri) {
        const Row& r = g.rows[ri];
        const int top = g.list.top + r.top - g.scroll;
        if (top + r.height < g.list.top || top > g.list.bottom) continue;
        RECT card{ g.list.left, top, g.list.left + cardW, top + r.height };
        RECT b1, b2, b3;
        RowButtons(card, b1, b2, &b3);
        const int base = H_ROW_BASE + static_cast<int>(ri) * kRowParts;
        const bool hot = g.hover == base, h1 = g.hover == base + 1, h2 = g.hover == base + 2, h3 = g.hover == base + 3;
        const bool pinnedRow = g.tab == T_HISTORY && g.store.history[r.idx].pinned;
        FillRound(dc, card, S(14), (hot || h1 || h2 || h3) ? kCardHover : kCard, pinnedRow ? Blend(kBorder, kClip, 60) : (hot || h1 || h2 || h3) ? kBorderHot : kBorder);
        const int tx = card.left + S(18), tr = (g.tab == T_HISTORY ? b3.left : b1.left) - S(12);
        if (g.tab == T_HISTORY) {
            const clip::HistoryItem& hi = g.store.history[r.idx];
            if (hi.IsImage()) {                                                         // Bild: Vorschau links, Beschriftung rechts daneben, kein Stern
                const Thumb& t = GetThumb(hi);
                int textLeft = tx;
                if (t.bmp) { DrawThumbBitmap(dc, t, tx, card.top + (r.height - t.h) / 2, t.w, t.h); textLeft = tx + t.w + S(16); }
                DrawText2(dc, g.fBold, kText, { textLeft, card.top + S(20), b2.left - S(12), card.top + S(20) + S(26) }, hi.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                DrawText2(dc, g.fSmall, kTextDim, { textLeft, card.top + S(20) + S(26), b2.left - S(12), card.bottom - S(8) },
                          (hi.png.size() < 1024 ? std::wstring(L"< 1 KB") : std::to_wstring(hi.png.size() / 1024) + L" KB") + L"  ·  klicken kopiert das Bild", DT_LEFT | DT_TOP | DT_SINGLELINE);
            } else {
                DrawText2(dc, g.fBody, kText, { tx, card.top + S(10), tr, card.bottom - S(10) }, clip::Preview(hi.text, 160), DT_LEFT | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS);
                FillRound(dc, b1, S(10), h1 ? kCardHover : kCard, h1 ? kBorderHot : kBorder);
                DrawStar(dc, b1, h1 ? kClip : kTextDim, false);
                FillRound(dc, b3, S(10), h3 ? kCardHover : kCard, (h3 || hi.pinned) ? kClip : kBorder);
                DrawPin(dc, b3, (h3 || hi.pinned) ? kClip : kTextDim, hi.pinned);
            }
        } else {
            const clip::Prompt& p = g.store.prompts[r.idx];
            DrawText2(dc, g.fBold, kText, { tx, card.top + S(10), tr, card.top + S(10) + S(24) }, p.title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            DrawText2(dc, g.fSmall, kTextDim, { tx, card.top + S(10) + S(24), tr, card.bottom - S(8) },
                      (p.folder.empty() ? L"" : p.folder + L"  ·  ") + clip::Preview(p.text, 140), DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
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

    {
        const int hs = g.hover >= H_SET_BASE && g.hover < H_SET_BASE + 6 ? g.hover - H_SET_BASE : -1;
        DrawText2(dc, g.fSmall, hs >= 0 ? kText : kTextDim, g.hintRc,
                  hs >= 0 ? std::wstring(kSetHints[hs]) : std::wstring(L"Einstellungen – fahre mit der Maus über einen Knopf, um zu sehen, was er macht."),
                  DT_LEFT | DT_BOTTOM | DT_WORDBREAK);
    }

    // Einstellungen als sechs Schalter-Pillen (zwei Zeilen)
    DrawPill(dc, g.set[0], std::wstring(L"Mit Windows starten: ") + (g.cfg.autostart ? L"an" : L"aus"), g.cfg.autostart, g.hover == H_SET_BASE, kClip);
    DrawPill(dc, g.set[1], std::wstring(L"Verlauf speichern: ") + (g.cfg.saveHistory ? L"an" : L"aus"), g.cfg.saveHistory, g.hover == H_SET_BASE + 1, kClip);
    DrawPill(dc, g.set[2], std::wstring(L"Kürzel: ") + clip::HotkeyPreset(g.cfg.hotkey).name, false, g.hover == H_SET_BASE + 2, kClip);
    DrawPill(dc, g.set[3], std::wstring(L"Verlauf löschen nach: ") + clip::RetentionName(g.cfg.retention), g.cfg.retention > 0, g.hover == H_SET_BASE + 3, kClip);
    DrawPill(dc, g.set[4], std::wstring(L"Markierung übernehmen: ") + (g.cfg.captureSelection ? L"an" : L"aus"), g.cfg.captureSelection, g.hover == H_SET_BASE + 4, kClip);
    DrawPill(dc, g.set[5], g.cfg.excludedExes.empty() ? L"Mehr: Prompts & Optionen" : (L"Mehr: Prompts, " + std::to_wstring(g.cfg.excludedExes.size()) + L" gesperrt").c_str(), !g.cfg.excludedExes.empty(), g.hover == H_SET_BASE + 5, kClip);

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
    if (PtInRect(&g.helpBtn, p)) return H_HELP;
    if (g.introOk.right > g.introOk.left && PtInRect(&g.introOk, p)) return H_INTRO_OK;
    for (size_t i = 0; i < g.chipRects.size(); ++i) if (PtInRect(&g.chipRects[i], p)) return H_CHIP_BASE + static_cast<int>(i);
    for (int i = 0; i < 6; ++i) if (PtInRect(&g.set[i], p)) return H_SET_BASE + i;
    if (PtInRect(&g.list, p) && p.x < g.list.left + CardWidth()) {
        const int y = static_cast<int>(p.y - g.list.top) + g.scroll;
        for (size_t i = 0; i < g.rows.size(); ++i) {
            const Row& r = g.rows[i];
            if (y < r.top || y >= r.top + r.height) continue;
            RECT card{ g.list.left, g.list.top + r.top - g.scroll, g.list.left + CardWidth(), g.list.top + r.top - g.scroll + r.height };
            RECT b1, b2, b3;
            RowButtons(card, b1, b2, &b3);
            const int base = H_ROW_BASE + static_cast<int>(i) * kRowParts;
            const bool image = g.tab == T_HISTORY && g.store.history[r.idx].IsImage();     // Bilder haben weder Stern noch Pin
            if (g.tab == T_HISTORY && !image && PtInRect(&b3, p)) return base + 3;
            if (PtInRect(&b1, p)) return image ? base : base + 1;
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
    else if (id == H_INTRO_OK) { g.cfg.hideIntro = true; clip::SaveSettings(g.cfg); Layout(); }
    else if (id == H_HELP) { g.cfg.hideIntro = !g.cfg.hideIntro; clip::SaveSettings(g.cfg); Layout(); }
    else if (id == H_ACTION) {
        if (g.tab == T_HISTORY) {
            clip::ClearHistory(g.store);
            PruneThumbs(); SaveHistoryIfWanted(); Layout();
            SetStatus(clip::PinnedCount(g.store) ? L"Verlauf geleert (angepinnte Einträge bleiben)." : L"Verlauf geleert.", kGreen);
        }
        else OpenEditor(-1);
    }
    else if (id >= H_CHIP_BASE && id < H_CHIP_BASE + 50) {
        const size_t ci = static_cast<size_t>(id - H_CHIP_BASE);
        if (ci < g.chipNames.size()) { g.folderFilter = g.chipNames[ci]; g.scroll = 0; Layout(); }
    }
    else if (id >= H_SET_BASE && id < H_SET_BASE + 6) {
        switch (id - H_SET_BASE) {
        case 0: ToggleAutostart(); break;
        case 1: ToggleSaveHistory(); break;
        case 2: CycleHotkey(); break;
        case 3: CycleRetention(); break;
        case 4: ToggleCapture(); break;
        default: ExportImportMenu(); break;
        }
    }
    else if (id >= H_ROW_BASE) ActivateRow((id - H_ROW_BASE) / kRowParts, (id - H_ROW_BASE) % kRowParts);
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
    for (size_t i : clip::PromptOrderByRecent(g.store.prompts))                        // zuletzt benutzte Prompts zuerst
        if (clip::Matches(g.store.prompts[i].title + L" " + g.store.prompts[i].text + L" " + g.store.prompts[i].folder, pop.filter)) pop.items.push_back({ true, i });
    for (size_t i = 0; i < g.store.history.size(); ++i)
        if (clip::Matches(g.store.history[i].text, pop.filter)) pop.items.push_back({ false, i });
    pop.sel = pop.items.empty() ? 0 : std::clamp(pop.sel, 0, static_cast<int>(pop.items.size()) - 1);
    pop.scroll = std::clamp(pop.scroll, 0, std::max(0, static_cast<int>(pop.items.size()) - kPopRows));
}

void PopupEnsureVisible() {
    if (pop.sel < pop.scroll) pop.scroll = pop.sel;
    if (pop.sel >= pop.scroll + kPopRows) pop.scroll = pop.sel - kPopRows + 1;
}

void HidePopup() { if (g.popup) ShowWindow(g.popup, SW_HIDE); }

void DoPaste(const std::wstring& text, const std::string* png = nullptr) {
    std::wstring prev;
    const bool hadPrev = clip::ReadClipboardText(prev);
    g.suppressUntil = GetTickCount() + 2500;
    const bool written = png ? clip::WriteClipboardImage(*png) : clip::WriteClipboardText(text);
    if (!written) { SetStatus(L"Die Zwischenablage ist gerade belegt – bitte noch einmal versuchen.", kOrange); return; }
    if (g.prevFg && IsWindow(g.prevFg)) ForceForeground(g.prevFg);
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

// copyOnly (Umschalt+Enter): nur in die Zwischenablage legen, nicht einfuegen
void PopupChoose(int sel, bool copyOnly = false) {
    if (sel < 0 || sel >= static_cast<int>(pop.items.size())) return;
    const PopupItem it = pop.items[static_cast<size_t>(sel)];
    std::wstring text;
    std::string png;
    bool isImage = false;
    if (it.prompt) {
        const std::wstring clipText = !g.selection.empty() ? g.selection : CurrentClipText();   // Markierung hat Vorrang vor der Zwischenablage
        text = clip::Expand(g.store.prompts[it.idx].text, clipText);
        MarkUsed(it.idx);
    } else if (g.store.history[it.idx].IsImage()) { png = g.store.history[it.idx].png; isImage = true; }
    else text = g.store.history[it.idx].text;
    HidePopup();
    if (copyOnly) {
        g.suppressUntil = GetTickCount() + 1500;
        if (isImage ? clip::WriteClipboardImage(png) : clip::WriteClipboardText(text)) SetStatus(L"In die Zwischenablage kopiert.", kGreen);
        if (g.prevFg && IsWindow(g.prevFg)) ForceForeground(g.prevFg);
        return;
    }
    DoPaste(text, isImage ? &png : nullptr);
}

void ShowPopup() {
    g.prevFg = GetForegroundWindow();
    g.prevElevated = g.prevFg && IsElevatedWindow(g.prevFg);
    g.selection.clear();
    if (g.cfg.captureSelection && g.prevFg && IsWindow(g.prevFg) && g.prevFg != g.hwnd && g.prevFg != g.popup && !g.prevElevated) {
        g.suppressUntil = GetTickCount() + 3000;      // das Kopieren der Markierung soll nicht im Verlauf landen
        clip::CaptureSelection(g.prevFg, g.selection);
    }
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
    ForceForeground(g.popup);
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
            DrawText2(dc, g.fBold, kText, { r.left + S(32), r.top + S(3), r.right - S(70), r.top + S(23) }, p.title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            DrawText2(dc, g.fSmall, kTextDim, { r.left + S(32), r.top + S(22), r.right - S(70), r.bottom }, (p.folder.empty() ? L"" : p.folder + L"  ·  ") + clip::Preview(p.text, 90), DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else {
            const clip::HistoryItem& hi = g.store.history[it.idx];
            int textLeft = r.left + S(12);
            if (hi.IsImage()) {
                const Thumb& t = GetThumb(hi);
                if (t.bmp) {                                                      // kleine Vorschau, auf Zeilenhoehe verkleinert
                    const int th = r.bottom - r.top - S(8), tw = std::max(1, t.w * th / std::max(1, t.h));
                    DrawThumbBitmap(dc, t, textLeft, r.top + S(4), std::min(tw, S(70)), th);
                    textLeft += std::min(tw, S(70)) + S(12);
                }
            }
            DrawText2(dc, g.fBody, kText, { textLeft, r.top, r.right - S(70), r.bottom }, clip::Preview(hi.text, 120), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        if (i < 8) DrawText2(dc, g.fSmall, kTextDim, { r.right - S(64), r.top, r.right - S(10), r.bottom }, L"Alt+" + std::to_wstring(i + 1), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    if (!g.selection.empty())
        DrawText2(dc, g.fSmall, kClip, { S(16), cr.bottom - S(28), cr.right - S(16), cr.bottom - S(6) }, L"Markierung übernommen: " + clip::Preview(g.selection, 50), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    else
        DrawText2(dc, g.fSmall, kTextDim, { S(16), cr.bottom - S(28), cr.right - S(16), cr.bottom - S(6) },
                  L"Enter einfügen  ·  Umschalt+Enter kopieren  ·  Alt+1–8  ·  ↑ ↓  ·  Esc", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

LRESULT CALLBACK PopupEditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_DOWN) { if (pop.sel + 1 < static_cast<int>(pop.items.size())) ++pop.sel; PopupEnsureVisible(); InvalidateRect(g.popup, nullptr, FALSE); return 0; }
        if (wp == VK_UP) { if (pop.sel > 0) --pop.sel; PopupEnsureVisible(); InvalidateRect(g.popup, nullptr, FALSE); return 0; }
        if (wp == VK_NEXT) { pop.sel = std::min<int>(pop.sel + kPopRows, std::max(0, static_cast<int>(pop.items.size()) - 1)); PopupEnsureVisible(); InvalidateRect(g.popup, nullptr, FALSE); return 0; }
        if (wp == VK_PRIOR) { pop.sel = std::max(0, pop.sel - kPopRows); PopupEnsureVisible(); InvalidateRect(g.popup, nullptr, FALSE); return 0; }
        if (wp == VK_RETURN) { PopupChoose(pop.sel, (GetKeyState(VK_SHIFT) & 0x8000) != 0); return 0; }
        if (wp == VK_ESCAPE) { HidePopup(); if (g.prevFg && IsWindow(g.prevFg)) ForceForeground(g.prevFg); return 0; }
    }
    if (msg == WM_SYSKEYDOWN && wp >= '1' && wp <= '8') { PopupChoose(pop.scroll + static_cast<int>(wp - '1')); return 0; }   // Alt+1 ... Alt+8
    if (msg == WM_SYSCHAR || (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE))) return 0;       // kein Piepton
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
        mmi->ptMinTrackSize.y = S(740);
        return 0;
    }
    case WM_DPICHANGED: {
        g.dpi = HIWORD(wp);
        MakeFonts();
        FreeThumbs();                                       // Vorschaubilder haengen von der DPI ab
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
        else if (wp == kTimerPurge) PurgeNow();
        else if (wp == kTimerPaste) {
            static int tries = 0;                           // erst einfuegen, wenn das Zielfenster wirklich vorn ist (hoechstens ~1,3 s warten)
            if (g.prevFg && IsWindow(g.prevFg) && GetForegroundWindow() != g.prevFg && tries < 10) { ++tries; ForceForeground(g.prevFg); return 0; }
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
        FreeThumbs();
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
    darkmenus::Enable();
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
    g.hwnd = CreateWindowExW(0, kMainClass, kTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, S(680), S(880), nullptr, nullptr, inst, nullptr);
    if (!g.hwnd) return 1;
    g.dpi = GetDpiForWindow(g.hwnd);
    g.hSearch = MakeEdit(g.hwnd, kIdSearch, WS_VISIBLE | ES_AUTOHSCROLL, 200);
    g.hEditTitle = MakeEdit(g.hwnd, kIdEditTitle, ES_AUTOHSCROLL, 80);
    g.hEditFolder = MakeEdit(g.hwnd, kIdEditFolder, ES_AUTOHSCROLL, 60);
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
    for (const std::wstring& pin : clip::LoadPins()) clip::AddPinned(g.store, pin);   // Pins zuerst (stehen vorn)
    if (g.cfg.saveHistory) {                         // gespeicherte Texte (neueste zuerst) wieder aufnehmen
        const std::vector<std::wstring> saved = clip::LoadHistory();
        for (size_t i = saved.size(); i-- > 0;) clip::AddHistory(g.store, saved[i]);
    }
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
    SetTimer(g.hwnd, kTimerPurge, 60000, nullptr);   // Aufbewahrung: jede Minute alte Eintraege entfernen
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
