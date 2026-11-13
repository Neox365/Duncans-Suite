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
#include "EqCurve.h"
#include "AppMixer.h"
#include "EqLogic.h"
#include "FilterEditor.h"
#include "Spectrum.h"

#ifndef APP_VERSION
#define APP_VERSION L"0.8.0"   // normalerweise aus CMake
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
const UINT_PTR kTimerApply = 1, kTimerDevice = 2, kTimerDisarm = 3, kTimerMixer = 4, kTimerSpectrum = 5;
constexpr int kMixRows = 5;                         // so viele Programme zeigt die Karte "Programme"
const UINT WM_TRAY = WM_APP + 2;                // Symbol im Infobereich
const UINT WM_SHOWMAIN = WM_APP + 3;            // zweiter Start: bestehendes Fenster zeigen
const UINT WM_NAME_DONE = WM_APP + 1;      // wParam = 1: Name uebernehmen, 0: abbrechen

enum HitId {
    H_NONE = -1, H_HERO = 1, H_BASS = 2, H_VOICE = 3, H_DOWNLOAD = 4, H_RECHECK = 5, H_CONFIGURATOR = 6, H_ADMIN = 7,
    H_AUTOSTART = 220, H_KEEP = 221,
    H_PRESET = 10,      // 10 .. 10 + Anzahl Voreinstellungen
    H_PROFILE = 200,    // 200 .. 205: Profil waehlen
    H_PROFNEW = 210, H_BIND = 211, H_IMPORT = 212, H_PROFDEL = 213, H_SHARE = 214,
    H_BAND = 100,       // 100 .. 109 = Baender, 110 = Preamp
    H_SPECTRUM = 330,   // Schalter "Spektrum" im Diagramm
    H_MIXSLIDER = 310,  // 310 .. 314: Lautstaerke-Regler je Programm
    H_MIXMUTE = 320,    // 320 .. 324: Stumm-Knopf je Programm
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
    RECT profCard{}, profChip[eq::kMaxProfiles]{}, profNew{}, btnBind{}, btnImport{}, btnShare{}, btnDel{}, nameRect{};
    bool hotkeysOk = false;                        // globale Tastenkuerzel sind registriert
    HWND nameEdit = nullptr;                       // Eingabefeld fuer den Namen eines neuen Profils
    WNDPROC nameOldProc = nullptr;
    RECT btnAuto{}, btnKeep{};
    RECT graph{}, info{};                          // rechte Spalte (nur bei breitem Fenster): Frequenzgang und Kurven-Infos
    int mx = -1, my = -1;                          // Mausposition (fuer die Anzeige im Diagramm)
    RECT specBtn{};                                // Schalter fuer das Live-Spektrum
    RECT mix{}, mixName[kMixRows]{}, mixSlider[kMixRows]{}, mixPct[kMixRows]{}, mixMute[kMixRows]{};   // Karte "Programme"
    std::vector<mixer::Entry> mixEntries;          // Programme, die gerade Ton ausgeben
    int mixDrag = -1;                              // Regler, der gerade gezogen wird
    NOTIFYICONDATAW nid{};
    bool autostartOn = false, quitting = false, balloonShown = false;
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
void UpdateTray();
void Changed() {                                    // speichern, kurz warten (Regler ziehen), dann uebernehmen
    SaveCfg();
    Layout();
    UpdateTray();
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

// ---------- Infobereich ----------
UINT g_taskbarCreated = 0;

void ShowMain() {
    ShowWindow(g.hwnd, SW_SHOW);
    ShowWindow(g.hwnd, SW_RESTORE);
    SetForegroundWindow(g.hwnd);
}

void UpdateTray() {
    if (!g.nid.hWnd) return;
    std::wstring tip = L"Duncan's Equalizer – " + g.profiles[static_cast<size_t>(g.active)].name + (g.cfg.enabled ? L" (an)" : L" (aus)");
    wcsncpy_s(g.nid.szTip, tip.c_str(), _TRUNCATE);
    g.nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g.nid);
}

// ---------- Globale Tastenkuerzel: Strg+Alt+F9 an/aus, F10 naechstes Profil, F8 voriges Profil ----------
// F-Tasten statt Buchstaben: Strg+Alt ist auf deutschen Tastaturen AltGr, und AltGr+E (Euro) oder AltGr+Q (@) soll weiter tippbar bleiben.
constexpr int kHkToggle = 1, kHkNext = 2, kHkPrev = 3;

void Toast(const std::wstring& text) {              // kurze Meldung neben dem Symbol im Infobereich, damit man auch ohne Fenster sieht, was passiert ist
    if (!g.nid.hWnd) return;
    g.nid.uFlags = NIF_INFO;
    wcscpy_s(g.nid.szInfoTitle, L"Duncan's Equalizer");
    wcsncpy_s(g.nid.szInfo, text.c_str(), _TRUNCATE);
    g.nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    Shell_NotifyIconW(NIM_MODIFY, &g.nid);
}

void UnregisterHotkeys() {
    for (int id : { kHkToggle, kHkNext, kHkPrev }) UnregisterHotKey(g.hwnd, id);
    g.hotkeysOk = false;
}

// Meldet die Kuerzel an; gibt die Namen der belegten zurueck (leer = alles in Ordnung oder ausgeschaltet)
std::wstring RegisterHotkeys() {
    UnregisterHotkeys();
    if (!g.cfg.hotkeys) return L"";
    std::wstring busy;
    const struct { int id; UINT vk; const wchar_t* name; } keys[] = { { kHkToggle, VK_F9, L"Strg+Alt+F9" }, { kHkNext, VK_F10, L"Strg+Alt+F10" }, { kHkPrev, VK_F8, L"Strg+Alt+F8" } };
    for (const auto& k : keys)
        if (!RegisterHotKey(g.hwnd, k.id, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, k.vk)) busy += (busy.empty() ? L"" : L", ") + std::wstring(k.name);
    g.hotkeysOk = busy.empty();
    return busy;
}

void AddTray(HINSTANCE inst) {
    g.nid = NOTIFYICONDATAW{};
    g.nid.cbSize = sizeof(g.nid);
    g.nid.hWnd = g.hwnd;
    g.nid.uID = 1;
    g.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g.nid.uCallbackMessage = WM_TRAY;
    g.nid.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wcscpy_s(g.nid.szTip, L"Duncan's Equalizer");
    Shell_NotifyIconW(NIM_ADD, &g.nid);
    UpdateTray();
}

void Changed();
void SwitchProfile(int i, const std::wstring& why);

void TrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Öffnen");
    AppendMenuW(menu, MF_STRING | (g.cfg.enabled ? MF_CHECKED : 0), 2, L"Equalizer an");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < g.profiles.size(); ++i)
        AppendMenuW(menu, MF_STRING | (static_cast<int>(i) == g.active ? MF_CHECKED : 0), static_cast<UINT_PTR>(100 + i), (L"Profil: " + g.profiles[i].name).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (g.cfg.hotkeys ? MF_CHECKED : 0), 4, L"Tastenkürzel: Strg+Alt+F9 an/aus, F10/F8 Profil");
    AppendMenuW(menu, MF_STRING, 3, L"Beenden");
    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(g.hwnd);
    const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g.hwnd, nullptr);
    PostMessageW(g.hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (cmd == 1) ShowMain();
    else if (cmd == 2) { g.cfg.enabled = !g.cfg.enabled; Changed(); }
    else if (cmd >= 100 && cmd < 100 + static_cast<int>(g.profiles.size())) SwitchProfile(cmd - 100, L"");
    else if (cmd == 3) { g.quitting = true; DestroyWindow(g.hwnd); }
    else if (cmd == 4) {
        g.cfg.hotkeys = !g.cfg.hotkeys;
        SaveCfg();
        const std::wstring busy = RegisterHotkeys();
        if (!g.cfg.hotkeys) SetStatus(L"Tastenkürzel aus", kTextDim);
        else if (!busy.empty()) SetStatus(L"Tastenkürzel schon von einem anderen Programm belegt: " + busy, kOrange);
        else SetStatus(L"Tastenkürzel an: Strg+Alt+F9 an/aus, F10 nächstes, F8 voriges Profil", kGreen);
    }
}

void HandleHotkey(int id) {
    if (id == kHkToggle) {
        g.cfg.enabled = !g.cfg.enabled;
        Changed();
        Toast(g.cfg.enabled ? L"Equalizer an" : L"Equalizer aus");
    } else if (id == kHkNext || id == kHkPrev) {
        const int n = static_cast<int>(g.profiles.size());
        if (n < 2) { Toast(L"Es gibt nur ein Profil"); return; }
        SwitchProfile((g.active + (id == kHkNext ? 1 : n - 1)) % n, L"");
        Toast(L"Profil: " + g.profiles[static_cast<size_t>(g.active)].name);
    }
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
    UpdateTray();
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

// ---------- Profil weitergeben (Datei) ----------
void ExportProfile() {
    SyncActive();
    const eq::Profile& p = g.profiles[static_cast<size_t>(g.active)];
    wchar_t file[MAX_PATH] = {};
    wcsncpy_s(file, (p.name + L".eqprofil").c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = L"Equalizer-Profil (*.eqprofil)\0*.eqprofil\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"eqprofil";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return;
    const std::string text = eq::ProfileToText(p);
    HANDLE h = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD n = 0;
    const bool ok = h != INVALID_HANDLE_VALUE && WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &n, nullptr) && n == text.size();
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    if (ok) SetStatus(L"Profil „" + p.name + L"“ gespeichert (ohne Gerätebindung)", kGreen);
    else SetStatus(L"Die Datei konnte nicht geschrieben werden", kRed);
}

void ImportProfile() {
    if (static_cast<int>(g.profiles.size()) >= eq::kMaxProfiles) { SetStatus(L"Es sind schon sechs Profile vorhanden – bitte erst eines löschen", kOrange); return; }
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = L"Equalizer-Profil (*.eqprofil)\0*.eqprofil\0Alle Dateien\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    HANDLE h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { SetStatus(L"Die Datei konnte nicht gelesen werden", kRed); return; }
    std::string text;
    char buf[8192];
    DWORD n = 0;
    while (text.size() <= 64 * 1024 && ReadFile(h, buf, sizeof(buf), &n, nullptr) && n) text.append(buf, n);
    CloseHandle(h);
    eq::Profile p;
    if (!eq::ProfileFromText(text, p)) { SetStatus(L"Das ist keine gültige Profil-Datei", kOrange); return; }
    const std::wstring name = eq::UniqueProfileName(g.profiles, p.name);
    if (name.empty()) { SetStatus(L"Der Profilname ist ungültig", kOrange); return; }
    p.name = name;
    SyncActive();
    g.profiles.push_back(p);
    g.active = static_cast<int>(g.profiles.size()) - 1;
    LoadActive();
    g.delArmed = false;
    SaveCfg();
    Layout();
    SetTimer(g.hwnd, kTimerApply, 150, nullptr);
    SetStatus(L"Profil „" + name + L"“ geladen und ausgewählt", kGreen);
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void ShareMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Aktuelles Profil als Datei speichern …");
    AppendMenuW(menu, MF_STRING | (static_cast<int>(g.profiles.size()) >= eq::kMaxProfiles ? MF_GRAYED : 0), 2, L"Profil aus Datei laden …");
    POINT pt{ g.btnShare.left, g.btnShare.bottom };
    ClientToScreen(g.hwnd, &pt);
    const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, g.hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd == 1) ExportProfile();
    else if (cmd == 2) ImportProfile();
}

void EditFilters() {                                // Fenster mit Frequenzkurve, eigenen Filtern und Datei-Korrektur
    if (eq::ShowFilterEditor(g.hwnd, g.cfg)) {
        Changed();
        Layout();
        SetStatus(g.cfg.extra.empty() ? L"Filter und Korrektur entfernt" : std::to_wstring(g.cfg.extra.size()) + L" Filterzeilen übernommen", kGreen);
    }
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

// ---------- Programme (Lautstaerke je Programm) ----------
void LayoutMixerRows() {
    const int n = std::min(static_cast<int>(g.mixEntries.size()), kMixRows);
    for (int i = 0; i < kMixRows; ++i) g.mixName[i] = g.mixSlider[i] = g.mixPct[i] = g.mixMute[i] = RECT{};
    if (g.mix.right <= g.mix.left) return;
    for (int i = 0; i < n; ++i) {
        const int y0 = g.mix.top + S(52) + i * S(32), h = S(28);
        const int l = g.mix.left + S(20), r = g.mix.right - S(20);
        g.mixMute[i] = { r - S(64), y0, r, y0 + h };
        g.mixPct[i] = { g.mixMute[i].left - S(52), y0, g.mixMute[i].left - S(8), y0 + h };
        g.mixName[i] = { l, y0, l + S(110), y0 + h };
        g.mixSlider[i] = { g.mixName[i].right + S(8), y0, g.mixPct[i].left - S(8), y0 + h };
    }
}

void RefreshMixer() {
    if (g.mix.right <= g.mix.left || g.mixDrag >= 0) return;
    std::vector<mixer::Entry> now = mixer::List();
    bool same = now.size() == g.mixEntries.size();
    for (size_t i = 0; same && i < now.size(); ++i)
        same = now[i].key == g.mixEntries[i].key && now[i].muted == g.mixEntries[i].muted && std::fabs(now[i].volume - g.mixEntries[i].volume) < 0.005f;
    if (same) return;
    g.mixEntries = std::move(now);
    LayoutMixerRows();
    InvalidateRect(g.hwnd, &g.mix, FALSE);
}

void SetMixVolume(int i, float v) {
    if (i < 0 || i >= static_cast<int>(g.mixEntries.size()) || i >= kMixRows) return;
    mixer::Entry& e = g.mixEntries[static_cast<size_t>(i)];
    e.volume = std::clamp(v, 0.f, 1.f);
    if (e.muted && e.volume > 0) { e.muted = false; mixer::SetMuted(e.key, false); }      // Regler anfassen hebt Stumm auf
    mixer::SetVolume(e.key, e.volume);
    InvalidateRect(g.hwnd, &g.mix, FALSE);
}

float MixFromX(int i, int x) {
    const RECT& r = g.mixSlider[i];
    return r.right > r.left ? std::clamp(static_cast<float>(x - r.left) / static_cast<float>(r.right - r.left), 0.f, 1.f) : 0.f;
}

void Layout() {
    RECT cr; GetClientRect(g.hwnd, &cr);
    const bool wide = cr.right >= S(900);
    const int R = wide ? S(640) : static_cast<int>(cr.right);          // rechter Rand der linken Spalte
    const int m = S(28);
    g.footer = { m, cr.bottom - S(34), cr.right - m, cr.bottom - S(8) };
    g.hero = { m, S(84), R - m, S(84) + S(96) };
    g.heroToggle = { g.hero.right - S(18) - S(64), g.hero.top + (S(96) - S(34)) / 2, g.hero.right - S(18), g.hero.top + (S(96) - S(34)) / 2 + S(34) };
    int y = g.hero.bottom + S(14);
    g.adminBar = g.adminBtn = RECT{};
    HDC dc = GetDC(g.hwnd);
    if (g.installDir.empty()) {
        g.setup = { m, y, R - m, y + S(292) };
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
        g.adminBar = { m, y, R - m, y + S(54) };
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
    g.panel = { m, y, R - m, y + S(292) };
    const int inner = (g.panel.right - g.panel.left) - 2 * S(14);
    for (int slot = 0; slot <= eq::kBands; ++slot) {              // Spalte 0 = Preamp, danach die zehn Baender
        const int x0 = g.panel.left + S(14) + inner * slot / (eq::kBands + 1), x1 = g.panel.left + S(14) + inner * (slot + 1) / (eq::kBands + 1);
        g.band[slot == 0 ? eq::kBands : slot - 1] = { x0, g.panel.top + S(8), x1, g.panel.bottom - S(8) };
    }
    g.btnReset = { g.panel.right - S(14) - S(150), g.panel.bottom - S(40), g.panel.right - S(14), g.panel.bottom - S(10) };
    y = g.panel.bottom + S(14);
    const int half = (R - 2 * m - S(12)) / 2;
    g.cardBass = { m, y, m + half, y + S(58) };
    g.cardVoice = { m + half + S(12), y, R - m, y + S(58) };
    y = g.cardBass.bottom + S(14);

    // Profile: Titelzeile, Reihe mit Profil-Schaltflaechen, Reihe mit drei Knoepfen
    g.profCard = { m, y, R - m, y + S(120) };
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
    const int bw1 = TextWidth(dc, g.fBold, hasDev ? L"Gerät lösen" : L"An Gerät binden") + S(28);
    const int bw2 = TextWidth(dc, g.fBold, L"Filter & Korrektur …") + S(28);
    const int bwS = TextWidth(dc, g.fBold, L"Teilen …") + S(28);
    const int bw3 = TextWidth(dc, g.fBold, g.delArmed ? L"Wirklich?" : L"Löschen") + S(28);
    g.btnBind = { ix, row2, ix + bw1, row2 + h2 };
    g.btnImport = { g.btnBind.right + S(8), row2, g.btnBind.right + S(8) + bw2, row2 + h2 };
    g.btnShare = { g.btnImport.right + S(8), row2, g.btnImport.right + S(8) + bwS, row2 + h2 };
    g.btnDel = { ir - bw3, row2, ir, row2 + h2 };
    y = g.profCard.bottom + S(14);
    const int cw = TextWidth(dc, g.fBold, L"Geräte auswählen") + S(36);
    g.confBtn = { m, y, m + cw, y + S(36) };
    const std::wstring autoLabel = std::wstring(L"Mit Windows starten: ") + (g.autostartOn ? L"an" : L"aus");
    const std::wstring keepLabel = std::wstring(L"Im Hintergrund: ") + (g.cfg.keepRunning ? L"an" : L"aus");
    g.btnAuto = { g.confBtn.right + S(10), y, g.confBtn.right + S(10) + TextWidth(dc, g.fBold, autoLabel) + S(28), y + S(36) };
    g.btnKeep = { g.btnAuto.right + S(10), y, g.btnAuto.right + S(10) + TextWidth(dc, g.fBold, keepLabel) + S(28), y + S(36) };
    ReleaseDC(g.hwnd, dc);
    g.graph = g.info = g.specBtn = RECT{};
    if (wide && !g.installDir.empty() && g.profCard.bottom > 0) {   // rechte Spalte: Diagramm oben, Kurven-Infos unten bis zur Unterkante der Profile
        const int x0 = R + S(2), x1 = cr.right - m;
        g.info = { x0, g.profCard.bottom - S(132), x1, g.profCard.bottom };
        g.mix = { x0, g.info.top - S(14) - S(252), x1, g.info.top - S(14) };
        g.graph = { x0, g.hero.top, x1, g.mix.top - S(14) };
        g.specBtn = { g.graph.right - S(138), g.graph.top + S(10), g.graph.right - S(20), g.graph.top + S(36) };
    } else g.mix = RECT{};
    LayoutMixerRows();
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

        if (g.graph.right > g.graph.left) {   // Frequenzgang: Gesamtwirkung von Reglern, Bass, Sprache und Filtern (ohne Vorverstaerkung)
            FillRound(dc, g.graph, S(16), kCard, kBorder);
            FillRound(dc, g.info, S(16), kCard, kBorder);
            DrawText2(dc, g.fHead, kText, { g.graph.left + S(20), g.graph.top + S(12), g.graph.left + S(240), g.graph.top + S(38) }, L"Frequenzgang");
            const RECT pl{ g.graph.left + S(48), g.graph.top + S(48), g.graph.right - S(22), g.graph.bottom - S(32) };
            const float pw = static_cast<float>(pl.right - pl.left), ph = static_cast<float>(pl.bottom - pl.top);
            const std::vector<double> fr = eq::LogFreqs(300, 20.0, 20000.0);
            const std::vector<double> db = eq::ResponseDb(g.cfg, fr);
            double peak = 0, hi = -1e9, lo = 1e9, hiF = 0, loF = 0;
            for (size_t k = 0; k < db.size(); ++k) {
                peak = std::max(peak, std::fabs(db[k]));
                if (db[k] > hi) { hi = db[k]; hiF = fr[k]; }
                if (db[k] < lo) { lo = db[k]; loF = fr[k]; }
            }
            int range = eq::kMaxGain;
            while (range < peak && range < 24) range += 6;
            auto xOf = [&](double f) { return pl.left + static_cast<float>(std::log10(f / 20.0) / 3.0) * pw; };
            auto yOf = [&](double d) { return pl.top + static_cast<float>((range - std::clamp(d, -static_cast<double>(range), static_cast<double>(range))) / (2.0 * range)) * ph; };
            auto num = [](double v, const wchar_t* fmt) { wchar_t b[48]; swprintf(b, 48, fmt, v); std::wstring t = b; for (wchar_t& ch : t) if (ch == L'.') ch = L','; return t; };
            auto freqText = [&](double f) { return f >= 1000 ? num(f / 1000.0, L"%.2f kHz") : num(f, L"%.0f Hz"); };
            Gdiplus::Graphics gr(dc);
            gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            const COLORREF gridC = Blend(kCard, RGB(255, 255, 255), 9), zeroC = Blend(kCard, RGB(255, 255, 255), 24);
            Gdiplus::Pen grid(Gdiplus::Color(255, GetRValue(gridC), GetGValue(gridC), GetBValue(gridC)), 1.f);
            Gdiplus::Pen zero(Gdiplus::Color(255, GetRValue(zeroC), GetGValue(zeroC), GetBValue(zeroC)), 1.f);
            for (int d = -range; d <= range; d += 6) {   // waagerechte Linien alle 6 dB
                const float yy = yOf(d);
                gr.DrawLine(d == 0 ? &zero : &grid, static_cast<float>(pl.left), yy, static_cast<float>(pl.right), yy);
                DrawText2(dc, g.fSmall, kTextDim, { g.graph.left + S(8), static_cast<int>(yy) - S(9), pl.left - S(8), static_cast<int>(yy) + S(9) },
                          d > 0 ? L"+" + std::to_wstring(d) : std::to_wstring(d), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            }
            static const struct { double f; const wchar_t* t; } kMarks[] = { {50, L"50"}, {100, L"100"}, {200, L"200"}, {500, L"500"}, {1000, L"1k"}, {2000, L"2k"}, {5000, L"5k"}, {10000, L"10k"} };
            for (const auto& mk : kMarks) {
                const float xx = xOf(mk.f);
                gr.DrawLine(&grid, xx, static_cast<float>(pl.top), xx, static_cast<float>(pl.bottom));
                DrawText2(dc, g.fSmall, kTextDim, { static_cast<int>(xx) - S(24), pl.bottom + S(6), static_cast<int>(xx) + S(24), pl.bottom + S(26) }, mk.t, DT_CENTER | DT_SINGLELINE);
            }
            const spectrum::Snapshot sp = spectrum::Get();
            const bool showSpec = g.cfg.spectrum && sp.running;
            if (showSpec) {   // Live-Spektrum: Anzeige von -90 dBFS (unten) bis -10 dBFS (oben), hinter der Kurve
                std::vector<Gdiplus::PointF> sp2;
                const float bottomY = static_cast<float>(pl.bottom);
                sp2.push_back({ xOf(spectrum::BandEdgeHz(0)), bottomY });
                for (int b = 0; b < spectrum::kBands; ++b) {
                    const float v = std::clamp((sp.bands[b] + 90.f) / 80.f, 0.f, 1.f);
                    sp2.push_back({ xOf(spectrum::BandCenterHz(b)), bottomY - v * ph });
                }
                sp2.push_back({ xOf(spectrum::BandEdgeHz(spectrum::kBands)), bottomY });
                const COLORREF sc = on ? Blend(kCard, RGB(255, 255, 255), 55) : Blend(kCard, RGB(255, 255, 255), 30);
                Gdiplus::SolidBrush specFill(Gdiplus::Color(on ? 42 : 24, GetRValue(sc), GetGValue(sc), GetBValue(sc)));
                gr.FillPolygon(&specFill, sp2.data(), static_cast<INT>(sp2.size()));
                Gdiplus::Pen specLine(Gdiplus::Color(on ? 120 : 70, GetRValue(sc), GetGValue(sc), GetBValue(sc)), static_cast<float>(S(1)));
                gr.DrawLines(&specLine, sp2.data() + 1, static_cast<INT>(sp2.size() - 2));
            }
            {   // Schalter und Pegelanzeige in der Kopfzeile
                const bool hotB = g.hover == H_SPECTRUM;
                FillRound(dc, g.specBtn, S(10), g.cfg.spectrum ? Blend(kCard, kEq, 16) : (hotB ? kCardHover : Blend(kCard, kBackground, 25)),
                          g.cfg.spectrum ? Blend(kBorder, kEq, 60) : (hotB ? kBorderHot : kBorder));
                DrawText2(dc, g.fSmall, g.cfg.spectrum ? kEq : kText, g.specBtn, g.cfg.spectrum ? L"Spektrum: an" : L"Spektrum: aus", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                if (showSpec) {
                    const int mx0 = g.graph.left + S(190), mw = S(70);
                    const float pk[2] = { sp.peakL, sp.peakR };
                    for (int c = 0; c < 2; ++c) {
                        const int my0 = g.graph.top + S(c == 0 ? 14 : 24);
                        DrawText2(dc, g.fSmall, kTextDim, { mx0 - S(16), my0 - S(6), mx0 - S(4), my0 + S(12) }, c == 0 ? L"L" : L"R", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                        RECT bar{ mx0, my0, mx0 + mw, my0 + S(5) };
                        FillRound(dc, bar, S(2), Blend(kCard, kBackground, 55), Blend(kCard, kBackground, 55));
                        const float v = std::clamp((pk[c] + 60.f) / 60.f, 0.f, 1.f);
                        if (v > 0.01f) {
                            const COLORREF mc = pk[c] > -1.f ? kRed : (pk[c] > -6.f ? kOrange : kGreen);
                            RECT fillBar{ mx0, my0, mx0 + std::max(S(3), static_cast<int>(v * mw)), my0 + S(5) };
                            FillRound(dc, fillBar, S(2), mc, mc);
                        }
                    }
                }
            }
            std::vector<Gdiplus::PointF> pts;
            for (size_t k = 0; k < fr.size(); ++k) pts.push_back({ xOf(fr[k]), yOf(db[k]) });
            const COLORREF lc = on ? kEq : Blend(kEq, kCard, 55);
            gr.SetClip(Gdiplus::RectF(static_cast<float>(pl.left), static_cast<float>(pl.top - 2), pw, ph + 4));
            std::vector<Gdiplus::PointF> area = pts;
            area.push_back({ pts.back().X, yOf(0) });
            area.push_back({ pts.front().X, yOf(0) });
            Gdiplus::SolidBrush fillB(Gdiplus::Color(on ? 50 : 26, GetRValue(lc), GetGValue(lc), GetBValue(lc)));
            gr.FillPolygon(&fillB, area.data(), static_cast<INT>(area.size()));
            Gdiplus::Pen glow(Gdiplus::Color(on ? 55 : 30, GetRValue(lc), GetGValue(lc), GetBValue(lc)), static_cast<float>(S(8)));
            Gdiplus::Pen line(Gdiplus::Color(on ? 240 : 140, GetRValue(lc), GetGValue(lc), GetBValue(lc)), static_cast<float>(S(2)));
            glow.SetLineJoin(Gdiplus::LineJoinRound);
            line.SetLineJoin(Gdiplus::LineJoinRound);
            gr.DrawLines(&glow, pts.data(), static_cast<INT>(pts.size()));
            gr.DrawLines(&line, pts.data(), static_cast<INT>(pts.size()));
            gr.ResetClip();
            Gdiplus::SolidBrush dot(Gdiplus::Color(255, GetRValue(lc), GetGValue(lc), GetBValue(lc)));
            for (int b = 0; b < eq::kBands; ++b) {      // Punkte an den zehn Reglerfrequenzen
                const double f = eq::kFreqs[b];
                const float r = static_cast<float>(S(3));
                gr.FillEllipse(&dot, xOf(f) - r, yOf(eq::ResponseDb(g.cfg, { f })[0]) - r, r * 2, r * 2);
            }
            if (g.mx >= pl.left && g.mx <= pl.right && g.my >= g.graph.top && g.my <= g.graph.bottom) {   // Anzeige unter der Maus
                const double f = 20.0 * std::pow(10.0, (g.mx - pl.left) / pw * 3.0);
                const double d = eq::ResponseDb(g.cfg, { f })[0];
                Gdiplus::Pen cur(Gdiplus::Color(90, 255, 255, 255), 1.f);
                gr.DrawLine(&cur, static_cast<float>(g.mx), static_cast<float>(pl.top), static_cast<float>(g.mx), static_cast<float>(pl.bottom));
                const float r = static_cast<float>(S(5));
                gr.FillEllipse(&dot, g.mx - r, yOf(d) - r, r * 2, r * 2);
                DrawText2(dc, g.fBold, kText, { pl.right - S(220), pl.top + S(4), pl.right - S(8), pl.top + S(26) }, freqText(f) + L"   " + num(d, L"%+.1f") + L" dB", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            }

            // Kurven-Infos
            const int ix = g.info.left + S(20), vx = g.info.left + S(150);
            const int rowH = S(26);
            int ry = g.info.top + S(14);
            auto row = [&](const wchar_t* label, const std::wstring& value) {
                DrawText2(dc, g.fSmall, kTextDim, { ix, ry, vx, ry + rowH }, label, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                DrawText2(dc, g.fSmall, kText, { vx, ry, g.info.right - S(16), ry + rowH }, value, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                ry += rowH;
            };
            row(L"Stärkste Anhebung", hi < 0.05 ? std::wstring(L"keine") : num(hi, L"%+.1f") + L" dB bei " + freqText(hiF));
            row(L"Stärkste Absenkung", lo > -0.05 ? std::wstring(L"keine") : num(lo, L"%+.1f") + L" dB bei " + freqText(loF));
            row(L"Vorverstärkung", std::to_wstring(eq::PreampDb(g.cfg)) + L" dB automatisch, gesamt " + std::to_wstring(eq::TotalPreampDb(g.cfg)) + L" dB");
            row(L"Zusatzfilter", g.cfg.extra.empty() ? std::wstring(L"keine") : std::to_wstring(g.cfg.extra.size()) + (g.cfg.extra.size() == 1 ? L" Zeile" : L" Zeilen"));
        }

        if (g.mix.right > g.mix.left) {   // Programme: Lautstaerke je Programm (Windows-Lautstaerkemixer)
            FillRound(dc, g.mix, S(16), kCard, kBorder);
            DrawText2(dc, g.fHead, kText, { g.mix.left + S(20), g.mix.top + S(12), g.mix.left + S(240), g.mix.top + S(38) }, L"Programme");
            const int total = static_cast<int>(g.mixEntries.size());
            if (total == 0) {
                DrawText2(dc, g.fSmall, kTextDim, { g.mix.left + S(20), g.mix.top + S(60), g.mix.right - S(20), g.mix.top + S(180) },
                          L"Gerade gibt kein Programm Ton aus.\nSobald eines spielt, erscheint es hier mit eigenem Regler.", DT_CENTER | DT_VCENTER | DT_WORDBREAK);
            }
            Gdiplus::Graphics gr(dc);
            gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            for (int i = 0; i < std::min(total, kMixRows); ++i) {
                const mixer::Entry& e = g.mixEntries[static_cast<size_t>(i)];
                const bool hotS = g.hover == H_MIXSLIDER + i || g.mixDrag == i, hotM = g.hover == H_MIXMUTE + i;
                const COLORREF ac = e.muted ? Blend(kEq, kCard, 40) : kEq;
                DrawText2(dc, g.fSmall, e.muted ? kTextDim : kText, g.mixName[i], e.name, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                const RECT& sl = g.mixSlider[i];
                const int cy = (sl.top + sl.bottom) / 2, th = S(4);
                RECT track{ sl.left, cy - th / 2, sl.right, cy + th / 2 + (th & 1) };
                FillRound(dc, track, th / 2, Blend(kCard, kBackground, 55), Blend(kCard, kBackground, 55));
                const int hx = sl.left + static_cast<int>(std::lround(e.volume * (sl.right - sl.left)));
                if (hx > sl.left) { RECT fill{ sl.left, track.top, hx, track.bottom }; FillRound(dc, fill, th / 2, ac, ac); }
                const float rad = static_cast<float>(S(hotS ? 8 : 7));
                const COLORREF hc = hotS ? Blend(ac, RGB(255, 255, 255), 25) : ac;
                Gdiplus::SolidBrush hb(Gdiplus::Color(255, GetRValue(hc), GetGValue(hc), GetBValue(hc)));
                gr.FillEllipse(&hb, hx - rad, static_cast<float>(cy) - rad, rad * 2, rad * 2);
                DrawText2(dc, g.fSmall, e.muted ? kTextDim : kText, g.mixPct[i], std::to_wstring(static_cast<int>(std::lround(e.volume * 100))) + L" %", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                const RECT& mb = g.mixMute[i];
                FillRound(dc, mb, S(10), e.muted ? Blend(kCard, kOrange, 22) : (hotM ? kCardHover : Blend(kCard, kBackground, 25)), e.muted ? Blend(kBorder, kOrange, 70) : (hotM ? kBorderHot : kBorder));
                DrawText2(dc, g.fSmall, e.muted ? kOrange : kText, mb, L"Stumm", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            if (total > kMixRows)
                DrawText2(dc, g.fSmall, kTextDim, { g.mix.right - S(160), g.mix.top + S(14), g.mix.right - S(20), g.mix.top + S(36) }, L"+ " + std::to_wstring(total - kMixRows) + L" weitere", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            DrawText2(dc, g.fSmall, kTextDim, { g.mix.left + S(20), g.mix.bottom - S(36), g.mix.right - S(20), g.mix.bottom - S(8) },
                      L"Über 100 % geht nicht – stelle die anderen leiser.", DT_LEFT | DT_VCENTER | DT_WORDBREAK);
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

        {   // Frequenzkurve: Gesamtwirkung von Reglern, Bass, Sprache und Filtern (ohne Vorverstaerkung) ueber den Reglerspalten
            const RECT& first = g.band[0];
            const RECT& last = g.band[eq::kBands - 1];
            const float cx0 = (first.left + first.right) / 2.f, cx9 = (last.left + last.right) / 2.f, colW = (cx9 - cx0) / (eq::kBands - 1);
            const float top = static_cast<float>(TrackTop()), bottom = static_cast<float>(TrackBottom());
            const std::vector<double> fr = eq::LogFreqs(140, 22.0, 22000.0);
            const std::vector<double> db = eq::ResponseDb(g.cfg, fr);
            std::vector<Gdiplus::PointF> pts;
            for (size_t i = 0; i < fr.size(); ++i) {
                const float pos = static_cast<float>(std::log2(fr[i] / 31.25));            // Baender liegen je eine Oktave auseinander
                const float d = static_cast<float>(std::clamp(db[i], -static_cast<double>(eq::kMaxGain), static_cast<double>(eq::kMaxGain)));
                pts.push_back({ cx0 + pos * colW, top + (bottom - top) * (eq::kMaxGain - d) / (2.f * eq::kMaxGain) });
            }
            Gdiplus::Graphics gr(dc);
            gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            const COLORREF lc = on ? kEq : Blend(kEq, kCard, 55);
            Gdiplus::Pen glow(Gdiplus::Color(on ? 55 : 30, GetRValue(lc), GetGValue(lc), GetBValue(lc)), static_cast<float>(S(7)));
            Gdiplus::Pen line(Gdiplus::Color(on ? 230 : 140, GetRValue(lc), GetGValue(lc), GetBValue(lc)), static_cast<float>(S(2)));
            glow.SetLineJoin(Gdiplus::LineJoinRound);
            line.SetLineJoin(Gdiplus::LineJoinRound);
            gr.DrawLines(&glow, pts.data(), static_cast<INT>(pts.size()));
            gr.DrawLines(&line, pts.data(), static_cast<INT>(pts.size()));
        }

        {
            const int shown = g.drag >= 0 ? g.drag : (g.hover >= H_BAND && g.hover <= H_BAND + eq::kBands ? g.hover - H_BAND : -1);
            const std::wstring preHint = L"Preamp: Gesamtlautstärke des Equalizers. Senke sie, wenn es verzerrt. Automatisch abgesenkt wird um " +
                                         std::to_wstring(-eq::PreampDb(g.cfg)) + L" dB (Anhebungen, Bass, Sprache); dein Wert kommt dazu. Gesamt: " + std::to_wstring(eq::TotalPreampDb(g.cfg)) + L" dB.";
            RECT line{ g.panel.left + S(14), g.panel.bottom - S(48), g.panel.right - S(14), g.panel.bottom - S(47) };
            FillRound(dc, line, 1, Blend(kCard, RGB(255, 255, 255), 12), Blend(kCard, RGB(255, 255, 255), 12));
            DrawText2(dc, g.fSmall, shown >= 0 ? kText : kTextDim, { g.panel.left + S(18), g.panel.bottom - S(44), g.btnReset.left - S(12), g.panel.bottom - S(4) },
                      shown == eq::kBands ? preHint : shown >= 0 ? std::wstring(eq::BandHint(shown)) : std::wstring(L"Maus über einen Regler: Erklärung. Doppelklick setzt ihn auf 0. Die Linie zeigt die Gesamtkurve."),
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
        smallBtn(g.btnBind, g.profiles[static_cast<size_t>(g.active)].device.empty() ? L"An Gerät binden" : L"Gerät lösen", H_BIND, false);
        smallBtn(g.btnImport, L"Filter & Korrektur …", H_IMPORT, false);
        smallBtn(g.btnShare, L"Teilen …", H_SHARE, false);
        smallBtn(g.btnDel, g.delArmed ? L"Wirklich?" : L"Löschen", H_PROFDEL, g.delArmed);
        DrawButton(dc, g.confBtn, L"Geräte auswählen", false, g.hover == H_CONFIGURATOR, g.press == H_CONFIGURATOR && g.hover == H_CONFIGURATOR);
        {
            auto pill = [&](const RECT& r, const std::wstring& t, bool on2, int id) {
                const bool hot = g.hover == id;
                FillRound(dc, r, S(12), on2 ? Blend(kCard, kEq, 22) : (hot ? kCardHover : Blend(kCard, kBackground, 25)), on2 ? kEq : (hot ? kBorderHot : kBorder));
                DrawText2(dc, g.fBold, on2 || hot ? kText : kTextDim, r, t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            };
            pill(g.btnAuto, std::wstring(L"Mit Windows starten: ") + (g.autostartOn ? L"an" : L"aus"), g.autostartOn, H_AUTOSTART);
            pill(g.btnKeep, std::wstring(L"Im Hintergrund: ") + (g.cfg.keepRunning ? L"an" : L"aus"), g.cfg.keepRunning, H_KEEP);
        }
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
    if (PtInRect(&g.specBtn, p)) return H_SPECTRUM;
    for (int i = 0; i < kMixRows; ++i) {
        if (PtInRect(&g.mixMute[i], p)) return H_MIXMUTE + i;
        if (PtInRect(&g.mixSlider[i], p)) return H_MIXSLIDER + i;
    }
    for (int b = 0; b <= eq::kBands; ++b) if (PtInRect(&g.band[b], p)) return H_BAND + b;
    if (PtInRect(&g.cardBass, p)) return H_BASS;
    if (PtInRect(&g.cardVoice, p)) return H_VOICE;
    if (PtInRect(&g.confBtn, p)) return H_CONFIGURATOR;
    if (PtInRect(&g.btnAuto, p)) return H_AUTOSTART;
    if (PtInRect(&g.btnKeep, p)) return H_KEEP;
    if (!g.nameEdit) {
        for (int i = 0; i < static_cast<int>(g.profiles.size()) && i < eq::kMaxProfiles; ++i) if (PtInRect(&g.profChip[i], p)) return H_PROFILE + i;
        if (g.profNew.right > g.profNew.left && PtInRect(&g.profNew, p)) return H_PROFNEW;
    }
    if (PtInRect(&g.btnBind, p)) return H_BIND;
    if (PtInRect(&g.btnImport, p)) return H_IMPORT;
    if (PtInRect(&g.btnShare, p)) return H_SHARE;
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
    if (id >= H_MIXMUTE && id < H_MIXMUTE + kMixRows) {
        const size_t i = static_cast<size_t>(id - H_MIXMUTE);
        if (i < g.mixEntries.size()) { g.mixEntries[i].muted = !g.mixEntries[i].muted; mixer::SetMuted(g.mixEntries[i].key, g.mixEntries[i].muted); InvalidateRect(g.hwnd, &g.mix, FALSE); }
        return;
    }
    if (id == H_HERO) { g.cfg.enabled = !g.cfg.enabled; Changed(); }
    else if (id == H_SPECTRUM) { g.cfg.spectrum = !g.cfg.spectrum; Changed(); }
    else if (id == H_BASS) { g.cfg.bass = !g.cfg.bass; Changed(); }
    else if (id == H_VOICE) { g.cfg.voice = !g.cfg.voice; Changed(); }
    else if (id == H_RESET) { for (int b = 0; b < eq::kBands; ++b) g.cfg.gains[b] = 0; g.cfg.preamp = 0; Changed(); SetStatus(L"Alle Regler auf 0 gesetzt", kTextDim); }
    else if (id >= H_PRESET && id < H_PRESET + eq::PresetCount()) {
        for (int b = 0; b < eq::kBands; ++b) g.cfg.gains[b] = eq::PresetAt(id - H_PRESET).gains[b];
        Changed();
    }
    else if (id >= H_PROFILE && id < H_PROFILE + eq::kMaxProfiles) SwitchProfile(id - H_PROFILE);
    else if (id == H_AUTOSTART) {
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        if (eq::SetAutostart(!g.autostartOn, exe)) g.autostartOn = !g.autostartOn;
        else SetStatus(L"Der Autostart konnte nicht geändert werden", kRed);
        if (g.autostartOn) SetStatus(L"Startet mit Windows im Hintergrund (Infobereich)", kGreen);
        else SetStatus(L"Startet nicht mehr mit Windows", kTextDim);
        Layout();
    }
    else if (id == H_KEEP) {
        g.cfg.keepRunning = !g.cfg.keepRunning;
        SaveCfg();
        SetStatus(g.cfg.keepRunning ? L"Schließen des Fensters lässt den Equalizer im Infobereich weiterlaufen" : L"Schließen des Fensters beendet den Equalizer", kTextDim);
        Layout();
    }
    else if (id == H_PROFNEW) BeginNewProfile();
    else if (id == H_BIND) BindDevice();
    else if (id == H_IMPORT) EditFilters();
    else if (id == H_SHARE) ShareMenu();
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
        {   // Diagramm: Anzeige folgt der Maus
            const bool was = g.mx >= 0, now = g.graph.right > g.graph.left && PtInRect(&g.graph, p);
            g.mx = now ? p.x : -1; g.my = now ? p.y : -1;
            if (was || now) InvalidateRect(hwnd, &g.graph, FALSE);
        }
        if (g.mixDrag >= 0) { SetMixVolume(g.mixDrag, MixFromX(g.mixDrag, p.x)); return 0; }
        if (g.drag >= 0) { SetBand(g.drag, YToGain(p.y)); return 0; }
        const int h = HitTest(p);
        if (h != g.hover) { g.hover = h; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE: g.tracking = false; g.hover = H_NONE; g.mx = g.my = -1; InvalidateRect(hwnd, nullptr, FALSE); return 0;
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
        if (g.press >= H_MIXSLIDER && g.press < H_MIXSLIDER + kMixRows) { g.mixDrag = g.press - H_MIXSLIDER; SetMixVolume(g.mixDrag, MixFromX(g.mixDrag, p.x)); }
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (GetCapture() == hwnd) ReleaseCapture();
        const int h = HitTest({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        const int pressed = g.press;
        const bool wasDrag = g.drag >= 0 || g.mixDrag >= 0;
        g.press = H_NONE; g.drag = -1; g.mixDrag = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        if (!wasDrag && pressed != H_NONE && pressed == h) Activate(h);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &p);
        const int h = HitTest(p);
        if (h >= H_BAND && h <= H_BAND + eq::kBands) { const int b = h - H_BAND; SetBand(b, (b == eq::kBands ? g.cfg.preamp : g.cfg.gains[b]) + (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1)); }
        else if (h >= H_MIXSLIDER && h < H_MIXSLIDER + kMixRows && static_cast<size_t>(h - H_MIXSLIDER) < g.mixEntries.size()) {
            const int i = h - H_MIXSLIDER;
            SetMixVolume(i, g.mixEntries[static_cast<size_t>(i)].volume + (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 0.05f : -0.05f));
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == kTimerApply) ApplyNow();
        else if (wp == kTimerDevice) PollDevice();
        else if (wp == kTimerSpectrum) {   // Mitschnitt nur, solange das Fenster mit Diagramm sichtbar ist; sonst Geraet freigeben
            const bool want = g.cfg.spectrum && g.graph.right > g.graph.left && IsWindowVisible(hwnd) && !IsIconic(hwnd);
            if (want) { spectrum::Start(); InvalidateRect(hwnd, &g.graph, FALSE); } else if (spectrum::Running()) spectrum::Stop();
        }
        else if (wp == kTimerMixer) { if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) RefreshMixer(); }
        else if (wp == kTimerDisarm) { KillTimer(hwnd, kTimerDisarm); g.delArmed = false; Layout(); InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    case WM_NAME_DONE: FinishNewProfile(wp == 1); return 0;
    case WM_HOTKEY: HandleHotkey(static_cast<int>(wp)); return 0;
    case WM_TRAY:
        if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == NIN_SELECT) ShowMain();
        else if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) TrayMenu();
        return 0;
    case WM_SHOWMAIN: ShowMain(); return 0;
    case WM_CLOSE:
        if (g.quitting || !g.cfg.keepRunning) { DestroyWindow(hwnd); return 0; }
        ShowWindow(hwnd, SW_HIDE);                          // im Infobereich weiterlaufen
        if (!g.balloonShown) {
            g.balloonShown = true;
            g.nid.uFlags = NIF_INFO;
            wcscpy_s(g.nid.szInfoTitle, L"Duncan's Equalizer");
            wcscpy_s(g.nid.szInfo, L"Läuft im Infobereich weiter. Rechtsklick auf das Symbol zum Beenden.");
            g.nid.dwInfoFlags = NIIF_INFO;
            Shell_NotifyIconW(NIM_MODIFY, &g.nid);
        }
        return 0;
    case WM_DESTROY:
        spectrum::Stop();
        UnregisterHotkeys();
        if (!g.installDir.empty()) ApplyNow();
        Shell_NotifyIconW(NIM_DELETE, &g.nid);
        PostQuitMessage(0);
        return 0;
    default:
        if (msg == g_taskbarCreated && g_taskbarCreated) { Shell_NotifyIconW(NIM_ADD, &g.nid); UpdateTray(); return 0; }
        break;
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
    const bool background = argv && argc >= 2 && std::wstring(argv[1]) == L"--background";
    if (argv) LocalFree(argv);

    wchar_t testDir[8] = {};
    const bool testRun = GetEnvironmentVariableW(L"DUNCANS_EQ_TESTDIR", testDir, 8) > 0;   // Testlauf: darf neben einer echten Instanz laufen
    HANDLE single = CreateMutexW(nullptr, TRUE, testRun ? L"DuncansEqualizer.Single.Test" : L"DuncansEqualizer.Single");
    if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(L"DuncansEqualizer", nullptr)) if (!background) PostMessageW(other, WM_SHOWMAIN, 0, 0);
        return 0;
    }
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
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
        RECT wa{};
        const bool room = SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0) && (wa.right - wa.left) >= MulDiv(1120, dpi, 96);   // sonst nur die schmale Fassung ohne Diagramm
        RECT r{ 0, 0, MulDiv(room ? 1080 : 640, dpi, 96), MulDiv(864, dpi, 96) };
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
    SetTimer(g.hwnd, kTimerMixer, 1200, nullptr);
    SetTimer(g.hwnd, kTimerSpectrum, 60, nullptr);
    RefreshMixer();
    if (!g.installDir.empty()) ApplyNow();      // beim Start die gespeicherten Einstellungen (neu) eintragen
    g.autostartOn = eq::GetAutostart();
    AddTray(inst);
    {
        const std::wstring busy = RegisterHotkeys();
        if (!busy.empty()) SetStatus(L"Tastenkürzel schon von einem anderen Programm belegt: " + busy + L" (im Infobereich-Menü abschaltbar)", kOrange);
    }
    ShowWindow(g.hwnd, background ? SW_HIDE : show);
    UpdateWindow(g.hwnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    IconsShutdown();
    return static_cast<int>(m.wParam);
}
