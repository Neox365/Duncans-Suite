// Duncan's Vault (Win32 + Direct2D + DirectWrite): lokaler Passwort- und Lizenz-Manager.
//
// Alle Eintraege liegen in EINER verschluesselten Datei (%LOCALAPPDATA%\DuncansVault\vault.dat): AES-256-GCM, Schluessel per
// PBKDF2-HMAC-SHA256 aus dem Master-Passwort (siehe Crypto.h / Vault.h). Keine Cloud, kein Sync, keine Telemetrie.
// Oberflaeche wie in den anderen Modulen komplett selbst gezeichnet; Textfelder nutzen docs/TextEdit.h.
//   Entsperrmaske | Kategorien | Liste | Detailansicht mit Schnellkopie | Dialoge (Eintrag, Master-Passwort, Loeschen)
// Sicherheit im Betrieb: Auto-Lock nach 5 Minuten Inaktivitaet (abschaltbar), Zwischenablage wird 15 s nach dem Kopieren eines
// Geheimnisses geleert (und von Verlauf/Cloud-Zwischenablage ausgenommen), beim Sperren werden Daten und Schluessel aus dem
// Speicher geloescht.
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>

#include <algorithm>
#include <ctime>
#include <new>
#include <filesystem>
#include <string>
#include <vector>

#include "../docs/TextEdit.h"
#include "../docs/Theme.h"
#include "Vault.h"

#ifndef APP_VERSION
#define APP_VERSION L"0.4.0"   // normalerweise aus CMake (VAULT_VER)
#endif

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace {

constexpr UINT_PTR TIMER_CARET = 1, TIMER_SEC = 2;
constexpr int kNoFocus = -1;
constexpr ULONGLONG kAutoLockMs = 5ull * 60 * 1000, kClipClearMs = 15ull * 1000, kAutoTypeDelayMs = 5ull * 1000;   // Auto-Type: so lange hat man Zeit, das Eingabefeld im Zielfenster anzuklicken
constexpr float kTopH = 60, kStatusH = 28, kCatW = 240, kDetailW = 430, kRowH = 58, kCatH = 38;

enum Id {
    B_NEW = 1, B_GEAR, B_LOCK,
    C_CAT0 = 10,                    // 10..14: Alle, dann die vier Kategorien
    DT_COPYUSER = 20, DT_COPYSECRET, DT_SHOW, DT_OPENURL, DT_EDIT, DT_DELETE, DT_LOGIN = 26,   // DT_LOGIN: "Einloggen / Daten kopieren"
    L_UNLOCK = 30, L_REVEAL, S_CREATE, S_REVEAL, S_SKIP,
    D_OK = 40, D_CANCEL, D_CAT0 = 42,   // 42..45: Kategorie im Eintragsdialog
    D_REVEAL = 46, D_GENTOGGLE, D_GENRUN, D_LENMINUS, D_LENPLUS, D_GU, D_GL, D_GD, D_GS
};
enum FieldId {
    F_SEARCH = 1, F_LOCKPW = 10, F_SETPW = 11, F_SETPW2 = 12,
    F_TITLE = 20, F_USER, F_SECRET, F_URL, F_TAGS, F_NOTES,
    F_OLD = 30, F_NEW, F_NEW2, F_IMPPW
};

struct Field {
    int id = 0;
    std::wstring label, hint;
    docs::TextEdit ed;
    D2D1_RECT_F rc{};
    float scroll = 0, vscroll = 0;
    bool mask = false;        // Passwortfeld: Punkte statt Text, kein Kopieren
    bool multiline = false;   // Notizfeld mit Zeilenumbruechen
    bool hidden = false;
};
struct HitBtn { int id; D2D1_RECT_F rc; };

enum class Screen { Setup, Lock, Main };
enum class DlgKind { None, Entry, ChangePw, Confirm, Import };

struct Dialog {
    DlgKind kind = DlgKind::None;
    std::wstring title, message, error;
    std::vector<Field> fields;
    vault::Cat cat = vault::Cat::Password;
    std::wstring editId;          // leer = neuer Eintrag
    bool reveal = false;          // Geheimnis im Klartext zeigen
    bool genOpen = false;
    int mode = 0;                 // ChangePw: 0 aendern, 1 festlegen (bisher ohne Passwort), 2 entfernen
    bool noPw = false;            // Import: Backup ohne Master-Passwort (Windows-Konto)
    float msgH = 0;               // Hoehe des Hinweistexts im einfachen Dialog
    D2D1_RECT_F box{};
};

struct Settings {
    bool autoLock = true;
    bool autoType = true;     // Auto-Type (Tastatursimulation per SendInput) im Menue "Einloggen" anbieten
    vault::GenOptions gen;
};

inline bool In(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
inline float W(const D2D1_RECT_F& r) { return r.right - r.left; }
inline float H(const D2D1_RECT_F& r) { return r.bottom - r.top; }
inline D2D1_RECT_F Inset(D2D1_RECT_F r, float dx, float dy) { return {r.left + dx, r.top + dy, r.right - dx, r.bottom - dy}; }

const unsigned kCatColor[vault::kCatCount] = {Theme::Accent, 0xA78BFA, 0x22D3EE, 0xFB923C};   // Blau, Violett, Cyan, Orange
const wchar_t* const kCatLetter[vault::kCatCount] = {L"P", L"L", L"W", L"N"};
const unsigned kStrengthColor[5] = {Theme::Danger, Theme::Warn, 0xFBBF24, Theme::Paid, 0x22D3EE};

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring s;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) s = p;
    CoTaskMemFree(p);
    return s;
}
fs::path DataDir() {
    wchar_t over[MAX_PATH]{};   // Umgebungsvariable DUNCANSVAULT_DIR (fuer Tests) ersetzt den Standardordner
    if (GetEnvironmentVariableW(L"DUNCANSVAULT_DIR", over, MAX_PATH) > 0) return over;
    const std::wstring base = KnownFolder(FOLDERID_LocalAppData);
    return fs::path(base.empty() ? L"." : base) / L"DuncansVault";
}
fs::path VaultPath() { return DataDir() / L"vault.dat"; }
std::wstring SettingsPath() {
    std::error_code ec;
    fs::create_directories(DataDir(), ec);
    return (DataDir() / L"settings.ini").wstring();
}

void LoadSettings(Settings& st) {
    const std::wstring ini = SettingsPath();
    st.autoLock = GetPrivateProfileIntW(L"Allgemein", L"AutoSperre", 1, ini.c_str()) != 0;
    st.autoType = GetPrivateProfileIntW(L"Allgemein", L"AutoType", 1, ini.c_str()) != 0;
    st.gen.length = std::clamp((int)GetPrivateProfileIntW(L"Generator", L"Laenge", 20, ini.c_str()), vault::kGenMin, vault::kGenMax);
    st.gen.upper = GetPrivateProfileIntW(L"Generator", L"Gross", 1, ini.c_str()) != 0;
    st.gen.lower = GetPrivateProfileIntW(L"Generator", L"Klein", 1, ini.c_str()) != 0;
    st.gen.digits = GetPrivateProfileIntW(L"Generator", L"Ziffern", 1, ini.c_str()) != 0;
    st.gen.symbols = GetPrivateProfileIntW(L"Generator", L"Sonderzeichen", 1, ini.c_str()) != 0;
}
void SaveSettings(const Settings& st) {
    const std::wstring ini = SettingsPath();
    auto put = [&](const wchar_t* sec, const wchar_t* k, int v) { WritePrivateProfileStringW(sec, k, std::to_wstring(v).c_str(), ini.c_str()); };
    put(L"Allgemein", L"AutoSperre", st.autoLock);
    put(L"Allgemein", L"AutoType", st.autoType);
    put(L"Generator", L"Laenge", st.gen.length);
    put(L"Generator", L"Gross", st.gen.upper);
    put(L"Generator", L"Klein", st.gen.lower);
    put(L"Generator", L"Ziffern", st.gen.digits);
    put(L"Generator", L"Sonderzeichen", st.gen.symbols);
}

std::wstring ClipboardText() {
    std::wstring out;
    if (!OpenClipboard(nullptr)) return out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            out = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

// Text per SendInput als Unicode-Tastendruecke tippen (keine Tastaturlayout-Abhaengigkeit); der Puffer wird danach genullt.
// false, wenn Windows die Eingabe blockiert (z. B. Zielfenster mit hoeheren Rechten).
bool TypeText(const std::wstring& text) {
    std::vector<INPUT> in;
    in.reserve(text.size() * 2);
    for (wchar_t c : text) {
        INPUT d{};
        d.type = INPUT_KEYBOARD;
        d.ki.wScan = c;
        d.ki.dwFlags = KEYEVENTF_UNICODE;
        in.push_back(d);
        d.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        in.push_back(d);
    }
    bool ok = true;
    for (size_t i = 0; i < in.size() && ok; i += 64) {   // in kleinen Bloecken, damit Browser keine Zeichen verschlucken
        const UINT n = static_cast<UINT>(std::min<size_t>(64, in.size() - i));
        ok = SendInput(n, in.data() + i, sizeof(INPUT)) == n;
        Sleep(8);
    }
    SecureZeroMemory(in.data(), in.size() * sizeof(INPUT));
    return ok;
}

bool TypeKey(WORD vk) {
    INPUT in[2]{};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = vk;
    in[0].ki.wScan = in[1].ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(2, in, sizeof(INPUT)) == 2;
}

// sensitive: nicht in den Windows-Zwischenablageverlauf und nicht in die Cloud-Zwischenablage
void SetClipboardText(HWND hwnd, const std::wstring& s, bool sensitive) {
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    const size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(h)) {
            memcpy(p, s.c_str(), bytes);
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        } else GlobalFree(h);
    }
    if (sensitive) {
        auto put = [](const wchar_t* name, DWORD v) {
            const UINT fmt = RegisterClipboardFormatW(name);
            if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD))) {
                if (DWORD* p = static_cast<DWORD*>(GlobalLock(h))) { *p = v; GlobalUnlock(h); SetClipboardData(fmt, h); }
                else GlobalFree(h);
            }
        };
        put(L"ExcludeClipboardContentFromMonitorProcessing", 1);
        put(L"CanIncludeInClipboardHistory", 0);
        put(L"CanUploadToCloudClipboard", 0);
    }
    CloseClipboard();
}

std::wstring TimeText(long long t) {
    if (t <= 0) return L"–";
    const time_t tt = static_cast<time_t>(t);
    tm lt{};
    localtime_s(&lt, &tt);
    wchar_t buf[40];
    swprintf_s(buf, L"%02d.%02d.%04d %02d:%02d", lt.tm_mday, lt.tm_mon + 1, lt.tm_year + 1900, lt.tm_hour, lt.tm_min);
    return buf;
}

// Passwort-/Notizfeld wirklich leeren: Text ueberschreiben, nicht nur die Laenge auf 0 setzen
void WipeField(Field& f) {
    vault::Wipe(f.ed.text);
    f.ed.caret = f.ed.anchor = 0;
    f.scroll = f.vscroll = 0;
}

class App {
public:
    bool Create(HINSTANCE hi, int show);
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    // --- Geraet / Zeichnen
    void CreateFormats();
    void EnsureDevice();
    void DiscardDevice() { brush.Reset(); rt.Reset(); }
    void Paint();
    void SetCol(unsigned rgb, float a = 1.f) { brush->SetColor(D2D1::ColorF(rgb, a)); }
    void Fill(const D2D1_RECT_F& r, unsigned c, float a = 1.f) { SetCol(c, a); rt->FillRectangle(r, brush.Get()); }
    void FillRR(const D2D1_RECT_F& r, float rad, unsigned c, float a = 1.f) {
        SetCol(c, a);
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), brush.Get());
    }
    void StrokeRR(const D2D1_RECT_F& r, float rad, unsigned c, float w = 1.f) {
        SetCol(c);
        rt->DrawRoundedRectangle(D2D1::RoundedRect(r, rad, rad), brush.Get(), w);
    }
    void Txt(const std::wstring& s, const D2D1_RECT_F& r, IDWriteTextFormat* f, unsigned c, float a = 1.f) {
        SetCol(c, a);
        rt->DrawText(s.c_str(), (UINT32)s.size(), f, r, brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    float TextW(const std::wstring& s, IDWriteTextFormat* f);
    void Button(int id, const D2D1_RECT_F& r, const std::wstring& label, bool accent, bool danger = false);
    void Chip(int id, const D2D1_RECT_F& r, const std::wstring& label, bool on, unsigned color);
    void LabeledField(Field& f, float x, float y, float w);
    float DrawStrength(float x, float y, float w, const std::wstring& pw);
    void DrawScrollbar(const D2D1_RECT_F& area, float scroll, float content);
    void DrawField(Field& f, bool focused);
    ComPtr<IDWriteTextLayout> MakeLayout(const Field& f, const std::wstring& shown);
    void DrawSetup();
    void DrawLock();
    void DrawMain();
    void DrawTop();
    void DrawCats();
    void DrawList();
    void DrawDetail();
    void DrawStatusBar();
    void DrawDialog();
    void DrawEntryDialog();
    void DrawSimpleDialog();
    float EntryDialogHeight(float fh, float k2, float availH);

    // --- Tresor
    void TryUnlock();
    void CreateVaultNoPw();
    void CreateVault();
    bool Persist();
    void Lock(bool automatic);
    void Rebuild();
    const vault::Entry* Sel() const;
    vault::Entry* FindEntry(const std::wstring& id);
    int CountIn(int cat) const;

    // --- Aktionen
    void BeginEntry(const vault::Entry* e);
    void ApplyEntry();
    void BeginChangePw(int mode);
    void ApplyChangePw();
    void BeginConfirmDelete();
    void ExportBackup();
    void BeginImport();
    void ApplyImport();
    void CloseDialog();
    void GenerateInto();
    void CopyText(const std::wstring& text, bool secret, const wchar_t* what);
    void ClearClipboardIfOurs();
    void OpenUrl(const std::wstring& url);
    void ShowLoginMenu();
    void BeginAutoType(bool enter);
    void RunAutoType();
    void ShowGearMenu();
    void SetNote(const std::wstring& s, bool sticky = false) { m_note = s; m_noteUntil = sticky ? 0 : GetTickCount64() + 7000; }
    void Invalidate() { InvalidateRect(m_hwnd, nullptr, FALSE); }

    // --- Eingabe
    std::vector<Field*> ActiveFields();
    Field* FieldById(int id);
    Field* FocusedField() { return m_focusId != kNoFocus ? FieldById(m_focusId) : nullptr; }
    void FieldKey(Field& f, WPARAM vk);
    void FieldMouse(Field& f, float x, float y, bool extend);
    void FocusNext(bool back);
    void KeyDown(WPARAM vk);
    void ResetCaret() { m_caretOn = true; }
    void Touch() { m_lastInput = GetTickCount64(); }
    int RowAt(float x, float y) const;
    void EnsureRowVisible(int viewIdx);
    void ClampScroll();

    HWND m_hwnd = nullptr;
    float k = 1.f;
    ComPtr<ID2D1Factory> d2d;
    ComPtr<IDWriteFactory> dw;
    ComPtr<ID2D1HwndRenderTarget> rt;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteTextFormat> fNormal, fSmall, fBold, fTitle, fBig, fLogo, fCenter, fRight, fBadge, fTop, fWrap;

    D2D1_RECT_F rcTop{}, rcCats{}, rcList{}, rcDetail{}, rcStatus{};
    std::vector<HitBtn> m_hits;
    int m_hoverBtn = 0, m_hoverRow = -1;
    bool m_tracking = false;

    Settings m_settings;
    Screen m_screen = Screen::Lock;
    vault::Session m_session;
    std::vector<vault::Entry> m_entries;
    std::vector<int> m_view;
    int m_cat = -1;                       // -1 = alle
    std::wstring m_selId;
    bool m_reveal = false;                // Detailansicht: Geheimnis anzeigen
    float m_scroll = 0;

    Field m_search, m_lockPw, m_setPw, m_setPw2;
    std::wstring m_lockErr, m_setErr;
    bool m_lockNoPw = false;             // Tresor ohne Master-Passwort: die Sperrmaske hat nur einen Entsperren-Knopf
    int m_fails = 0;
    ULONGLONG m_nextTry = 0, m_lastInput = 0, m_clipUntil = 0;
    std::wstring m_autoId;                // Auto-Type: Eintrag, der nach der Wartezeit getippt wird (nur die Id, keine Geheimnisse)
    bool m_autoEnter = false;
    ULONGLONG m_autoAt = 0;               // Zeitpunkt (GetTickCount64) fuer den Start, 0 = nichts geplant
    std::wstring m_clipText;              // was wir in die Zwischenablage gelegt haben (zum Vergleichen beim Leeren)

    int m_focusId = kNoFocus;
    bool m_caretOn = true;
    int m_dragField = kNoFocus;
    wchar_t m_hiSurrogate = 0;
    Dialog m_dlg;
    std::wstring m_note;
    ULONGLONG m_noteUntil = 0;           // Meldung verschwindet dann von selbst (0 = bleibt stehen)
};

// ============================================================================
// Setup
// ============================================================================

bool App::Create(HINSTANCE hi, int show) {
    LoadSettings(m_settings);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"DuncansVault";
    wc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(1));
    if (!wc.hIcon) wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);
    m_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Duncan's Vault", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800, nullptr, nullptr, hi, this);
    if (!m_hwnd) return false;
    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hwnd, 20, &dark, sizeof(dark));
    k = GetDpiForWindow(m_hwnd) / 96.f;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf()))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dw.GetAddressOf())))) return false;
    CreateFormats();
    m_search.id = F_SEARCH;
    m_search.hint = L"Suchen: Titel, Benutzername, Tags …";
    m_search.ed.maxLen = 80;
    m_lockPw.id = F_LOCKPW;
    m_lockPw.hint = L"Master-Passwort";
    m_lockPw.ed.maxLen = 128;
    m_lockPw.mask = true;
    m_setPw.id = F_SETPW;
    m_setPw.label = L"Master-Passwort (mindestens 8 Zeichen)";
    m_setPw.ed.maxLen = 128;
    m_setPw.mask = true;
    m_setPw2.id = F_SETPW2;
    m_setPw2.label = L"Master-Passwort wiederholen";
    m_setPw2.ed.maxLen = 128;
    m_setPw2.mask = true;
    const vault::FileMode fm = vault::ReadMode(VaultPath());
    m_screen = fm == vault::FileMode::Missing ? Screen::Setup : Screen::Lock;
    m_lockNoPw = fm == vault::FileMode::Dpapi;
    m_focusId = m_screen == Screen::Setup ? F_SETPW : (m_lockNoPw ? kNoFocus : F_LOCKPW);
    Touch();
    RECT work{};   // Startgroesse nie groesser als der Arbeitsbereich (sonst verschwindet die Statusleiste unter der Taskleiste)
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int winW = std::min((int)(1280 * k), (int)(work.right - work.left) - (int)(20 * k)), winH = std::min((int)(800 * k), (int)(work.bottom - work.top) - (int)(20 * k));
    SetWindowPos(m_hwnd, nullptr, work.left + (work.right - work.left - winW) / 2, work.top + (work.bottom - work.top - winH) / 2, winW, winH, SWP_NOZORDER);   // mittig im Arbeitsbereich
    ShowWindow(m_hwnd, show);
    UpdateWindow(m_hwnd);
    SetTimer(m_hwnd, TIMER_CARET, 530, nullptr);
    SetTimer(m_hwnd, TIMER_SEC, 1000, nullptr);
    if (m_lockNoPw) TryUnlock();   // Tresor ohne Master-Passwort: sofort oeffnen
    return true;
}

void App::CreateFormats() {
    auto make = [&](ComPtr<IDWriteTextFormat>& f, float size, DWRITE_FONT_WEIGHT w, DWRITE_TEXT_ALIGNMENT align, bool wrap = false, bool top = false) {
        f.Reset();
        dw->CreateTextFormat(L"Segoe UI", nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size * k, L"de-de", &f);
        f->SetTextAlignment(align);
        f->SetParagraphAlignment(top ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR : DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        f->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        if (!wrap) {
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            ComPtr<IDWriteInlineObject> ell;
            dw->CreateEllipsisTrimmingSign(f.Get(), &ell);
            f->SetTrimming(&trim, ell.Get());
        }
    };
    make(fNormal, 13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING);
    make(fSmall, 11, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING);
    make(fBold, 13, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING);
    make(fTitle, 17, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING);
    make(fBig, 24, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    make(fLogo, 18, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    make(fCenter, 13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    make(fRight, 13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
    make(fBadge, 11, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    make(fTop, 13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
    make(fWrap, 13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true, true);
}

void App::EnsureDevice() {
    if (rt) return;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_IGNORE), 96.f, 96.f);
    d2d->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(m_hwnd, D2D1::SizeU(rc.right, rc.bottom)), &rt);
    if (rt) rt->CreateSolidColorBrush(D2D1::ColorF(0), &brush);
}

float App::TextW(const std::wstring& s, IDWriteTextFormat* f) {
    ComPtr<IDWriteTextLayout> l;
    if (FAILED(dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, 10000.f, 100.f, &l))) return 0;
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

void App::Paint() {
    EnsureDevice();
    if (!rt) return;
    RECT cr;
    GetClientRect(m_hwnd, &cr);
    const float Wd = (float)cr.right, Ht = (float)cr.bottom;
    rcTop = {0, 0, Wd, kTopH * k};
    rcStatus = {0, Ht - kStatusH * k, Wd, Ht};
    rcCats = {0, rcTop.bottom, kCatW * k, rcStatus.top};
    rcDetail = {std::max(rcCats.right + 320 * k, Wd - kDetailW * k), rcTop.bottom, Wd, rcStatus.top};
    rcList = {rcCats.right, rcTop.bottom, rcDetail.left, rcStatus.top};
    m_hits.clear();
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(Theme::Bg));
    if (m_screen == Screen::Setup) DrawSetup();
    else if (m_screen == Screen::Lock) DrawLock();
    else {
        DrawMain();
        if (m_dlg.kind != DlgKind::None) DrawDialog();
    }
    if (rt->EndDraw() == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) {
        DiscardDevice();
        Invalidate();
    }
}

// ============================================================================
// Bausteine
// ============================================================================

void App::Button(int id, const D2D1_RECT_F& r, const std::wstring& label, bool accent, bool danger) {
    const bool hover = m_hoverBtn == id;
    const unsigned bg = accent ? Theme::Accent : (hover ? Theme::CardHover : Theme::Card);
    FillRR(r, 8 * k, bg);
    if (accent && hover) FillRR(r, 8 * k, 0xFFFFFF, 0.12f);
    Txt(label, r, fCenter.Get(), accent ? 0xFFFFFF : (danger ? Theme::Danger : Theme::Text));
    m_hits.push_back({id, r});
}

void App::Chip(int id, const D2D1_RECT_F& r, const std::wstring& label, bool on, unsigned color) {
    const bool hover = m_hoverBtn == id;
    FillRR(r, H(r) / 2, color, on ? 0.30f : (hover ? 0.14f : 0.07f));
    StrokeRR(r, H(r) / 2, color, on ? 2.f : 1.f);
    Txt(label, r, fBadge.Get(), on ? Theme::Text : color);
    m_hits.push_back({id, r});
}

void App::LabeledField(Field& f, float x, float y, float w) {
    Txt(f.label, {x, y, x + w, y + 18 * k}, fSmall.Get(), Theme::TextDim);
    f.rc = {x, y + 20 * k, x + w, y + 20 * k + 36 * k};
}

// Staerkebalken (5 Segmente) mit Beschriftung; Rueckgabe: belegte Hoehe
float App::DrawStrength(float x, float y, float w, const std::wstring& pw) {
    const vault::Strength s = vault::Rate(pw);
    const float seg = (w - 4 * 4 * k) / 5, h = 5 * k;
    for (int i = 0; i < 5; ++i) {
        const D2D1_RECT_F r{x + i * (seg + 4 * k), y, x + i * (seg + 4 * k) + seg, y + h};
        if (!pw.empty() && i <= s.level) FillRR(r, h / 2, kStrengthColor[s.level]);
        else FillRR(r, h / 2, Theme::Border);
    }
    std::wstring label = L"Stärke: ";
    label += pw.empty() ? L"–" : s.label;
    if (!pw.empty()) label += L"  (ca. " + std::to_wstring((int)s.bits) + L" Bit)";
    Txt(label, {x, y + h + 3 * k, x + w, y + h + 21 * k}, fSmall.Get(), pw.empty() ? Theme::TextDim : kStrengthColor[s.level]);
    return h + 24 * k;
}

void App::DrawScrollbar(const D2D1_RECT_F& area, float scroll, float content) {
    const float view = H(area);
    if (content <= view + 1) return;
    const float th = std::max(30 * k, view * view / content);
    const float ty = area.top + (view - th) * (scroll / (content - view));
    FillRR({area.right - 8 * k, ty, area.right - 3 * k, ty + th}, 3 * k, Theme::TextDim, 0.45f);
}

ComPtr<IDWriteTextLayout> App::MakeLayout(const Field& f, const std::wstring& shown) {
    ComPtr<IDWriteTextLayout> layout;
    if (f.multiline) {
        const float w = std::max(10.f, W(f.rc) - 22 * k - 6 * k);
        if (FAILED(dw->CreateTextLayout(shown.c_str(), (UINT32)shown.size(), fNormal.Get(), w, 100000.f, &layout))) return nullptr;
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    } else {
        if (FAILED(dw->CreateTextLayout(shown.c_str(), (UINT32)shown.size(), fNormal.Get(), 10000.f, H(f.rc), &layout))) return nullptr;
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    return layout;
}

// Textfeld: Rahmen, Hinweis, Markierung, Cursor; einzeilig scrollt waagerecht, mehrzeilig senkrecht. Passwortfelder zeigen Punkte.
void App::DrawField(Field& f, bool focused) {
    if (f.hidden) return;
    FillRR(f.rc, 8 * k, Theme::Bg);
    StrokeRR(f.rc, 8 * k, focused ? Theme::Accent : Theme::Border, focused ? 1.5f : 1.f);
    const D2D1_RECT_F in = f.multiline ? Inset(f.rc, 11 * k, 8 * k) : Inset(f.rc, 11 * k, 0);
    if (f.ed.text.empty()) {
        if (!f.hint.empty()) Txt(f.hint, in, f.multiline ? fTop.Get() : fNormal.Get(), Theme::TextDim, 0.8f);
        if (focused && m_caretOn) Fill({in.left, f.multiline ? in.top : f.rc.top + 9 * k, in.left + 1.5f * k, f.multiline ? in.top + 18 * k : f.rc.bottom - 9 * k}, Theme::Text);
        return;
    }
    const std::wstring shown = f.mask ? std::wstring(f.ed.text.size(), L'●') : f.ed.text;
    ComPtr<IDWriteTextLayout> layout = MakeLayout(f, shown);
    if (!layout) return;
    float cx = 0, cy = 0;
    DWRITE_HIT_TEST_METRICS hm{};
    layout->HitTestTextPosition((UINT32)f.ed.caret, FALSE, &cx, &cy, &hm);
    float ox, oy;
    if (f.multiline) {
        const float inner = H(in);
        if (cy + hm.height - f.vscroll > inner) f.vscroll = cy + hm.height - inner;
        if (cy - f.vscroll < 0) f.vscroll = cy;
        f.vscroll = std::max(0.f, f.vscroll);
        ox = in.left;
        oy = in.top - f.vscroll;
    } else {
        const float inner = W(in) - 2 * k;
        if (cx - f.scroll > inner) f.scroll = cx - inner;
        if (cx - f.scroll < 0) f.scroll = cx;
        f.scroll = std::max(0.f, f.scroll);
        ox = in.left - f.scroll;
        oy = f.rc.top;
    }
    rt->PushAxisAlignedClip(in, D2D1_ANTIALIAS_MODE_ALIASED);
    if (focused && f.ed.HasSelection()) {
        UINT32 n = 0;
        layout->HitTestTextRange((UINT32)f.ed.SelBegin(), (UINT32)(f.ed.SelEnd() - f.ed.SelBegin()), 0, 0, nullptr, 0, &n);
        std::vector<DWRITE_HIT_TEST_METRICS> ms(n);
        if (n && SUCCEEDED(layout->HitTestTextRange((UINT32)f.ed.SelBegin(), (UINT32)(f.ed.SelEnd() - f.ed.SelBegin()), ox, oy, ms.data(), n, &n)))
            for (UINT32 i = 0; i < n; ++i) {
                const float t = f.multiline ? ms[i].top : f.rc.top + 7 * k, b = f.multiline ? ms[i].top + ms[i].height : f.rc.bottom - 7 * k;
                Fill({ms[i].left, t, ms[i].left + ms[i].width, b}, Theme::Accent, 0.45f);
            }
    }
    SetCol(Theme::Text);
    rt->DrawTextLayout({ox, oy}, layout.Get(), brush.Get());
    if (focused && m_caretOn) {
        const float t = f.multiline ? oy + cy : f.rc.top + 9 * k, b = f.multiline ? oy + cy + hm.height : f.rc.bottom - 9 * k;
        Fill({ox + cx, t, ox + cx + 1.5f * k, b}, Theme::Text);
    }
    rt->PopAxisAlignedClip();
}

// ============================================================================
// Entsperren / Einrichten
// ============================================================================

namespace {
// Karte in der Fenstermitte
D2D1_RECT_F CenterCard(const RECT& cr, float w, float h) {
    return {(cr.right - w) / 2, std::max(10.f, (cr.bottom - h) / 2), (cr.right + w) / 2, std::max(10.f, (cr.bottom - h) / 2) + h};
}
}  // namespace

void App::DrawSetup() {
    RECT cr;
    GetClientRect(m_hwnd, &cr);
    const float cw = std::min(560 * k, cr.right - 40 * k), ch = 600 * k;
    const D2D1_RECT_F card = CenterCard(cr, cw, ch);
    FillRR(card, 16 * k, Theme::Panel);
    StrokeRR(card, 16 * k, Theme::Border);
    const float pad = 32 * k, x0 = card.left + pad, fw = cw - 2 * pad;
    float y = card.top + 28 * k;
    FillRR({card.left + cw / 2 - 28 * k, y, card.left + cw / 2 + 28 * k, y + 56 * k}, 14 * k, Theme::Accent);
    Txt(L"V", {card.left + cw / 2 - 28 * k, y, card.left + cw / 2 + 28 * k, y + 56 * k}, fBig.Get(), 0xFFFFFF);
    y += 68 * k;
    Txt(L"Tresor einrichten", {card.left, y, card.right, y + 34 * k}, fBig.Get(), Theme::Text);
    y += 40 * k;
    Txt(L"Lege dein Master-Passwort fest. Alle Einträge werden damit mit AES-256 verschlüsselt und nur auf diesem Gerät gespeichert.",
        {x0, y, x0 + fw, y + 44 * k}, fWrap.Get(), Theme::TextDim);
    y += 56 * k;
    LabeledField(m_setPw, x0, y, fw);
    y += 60 * k;
    LabeledField(m_setPw2, x0, y, fw);
    y += 62 * k;
    y += DrawStrength(x0, y, fw, m_setPw.ed.text);
    Txt(L"Wichtig: Das Master-Passwort lässt sich nicht wiederherstellen. Wer es vergisst, verliert den Zugriff auf alle Einträge.",
        {x0, y, x0 + fw, y + 40 * k}, fWrap.Get(), Theme::Warn);
    y += 40 * k;
    if (!m_setErr.empty()) Txt(m_setErr, {x0, y, x0 + fw, y + 22 * k}, fNormal.Get(), Theme::Danger);
    Button(S_CREATE, {x0, card.bottom - 24 * k - 42 * 2 * k - 10 * k, x0 + fw, card.bottom - 24 * k - 42 * k - 10 * k}, L"Tresor anlegen", true);
    Button(S_SKIP, {x0, card.bottom - 24 * k - 42 * k, x0 + fw, card.bottom - 24 * k}, L"Ohne Master-Passwort fortfahren", false);
    Txt(L"Ohne Master-Passwort ist der Tresor zwar verschlüsselt, aber nur durch dein Windows-Konto geschützt: Wer bei diesem PC unter deinem Konto angemeldet ist, kann ihn öffnen. Du kannst später jederzeit ein Master-Passwort festlegen.",
        {x0, card.bottom - 24 * k - 42 * 2 * k - 10 * k - 62 * k, x0 + fw, card.bottom - 24 * k - 42 * 2 * k - 16 * k}, fWrap.Get(), Theme::TextDim);
    DrawField(m_setPw, m_focusId == F_SETPW);
    DrawField(m_setPw2, m_focusId == F_SETPW2);
}

void App::DrawLock() {
    RECT cr;
    GetClientRect(m_hwnd, &cr);
    const float cw = std::min(480 * k, cr.right - 40 * k), ch = 400 * k;
    const D2D1_RECT_F card = CenterCard(cr, cw, ch);
    FillRR(card, 16 * k, Theme::Panel);
    StrokeRR(card, 16 * k, Theme::Border);
    const float pad = 32 * k, x0 = card.left + pad, fw = cw - 2 * pad;
    float y = card.top + 28 * k;
    {   // Schloss-Symbol: Buegel + Koerper
        const float cx = card.left + cw / 2;
        SetCol(Theme::Accent);
        rt->DrawRoundedRectangle(D2D1::RoundedRect({cx - 12 * k, y, cx + 12 * k, y + 30 * k}, 12 * k, 12 * k), brush.Get(), 4 * k);
        FillRR({cx - 22 * k, y + 20 * k, cx + 22 * k, y + 52 * k}, 8 * k, Theme::Accent);
        FillRR({cx - 3 * k, y + 29 * k, cx + 3 * k, y + 42 * k}, 3 * k, Theme::Panel);
    }
    y += 68 * k;
    Txt(L"Duncan's Vault", {card.left, y, card.right, y + 34 * k}, fBig.Get(), Theme::Text);
    y += 40 * k;
    if (m_lockNoPw) {
        Txt(L"Dieser Tresor hat kein Master-Passwort und ist an dein Windows-Konto gebunden.", {x0, y, x0 + fw, y + 44 * k}, fWrap.Get(), Theme::TextDim);
        y += 52 * k;
    } else {
        Txt(L"Gib dein Master-Passwort ein, um den Tresor zu entsperren.", {x0, y, x0 + fw, y + 24 * k}, fCenter.Get(), Theme::TextDim);
        y += 40 * k;
        const float revW = 92 * k;
        m_lockPw.rc = {x0, y, x0 + fw - revW - 8 * k, y + 40 * k};
        Button(L_REVEAL, {x0 + fw - revW, y, x0 + fw, y + 40 * k}, m_lockPw.mask ? L"Anzeigen" : L"Verbergen", false);
        y += 52 * k;
    }
    if (!m_lockErr.empty()) Txt(m_lockErr, {x0, y, x0 + fw, y + 40 * k}, fWrap.Get(), Theme::Danger);
    Button(L_UNLOCK, {x0, card.bottom - 24 * k - 42 * k, x0 + fw, card.bottom - 24 * k}, L"Entsperren", true);
    Txt(L"100 % lokal – keine Cloud, kein Sync, keine Telemetrie", {card.left, card.bottom + 10 * k, card.right, card.bottom + 32 * k}, fSmall.Get(), Theme::TextDim);
    if (!m_lockNoPw) DrawField(m_lockPw, m_focusId == F_LOCKPW);
}

// ============================================================================
// Hauptansicht
// ============================================================================

void App::DrawMain() {
    DrawList();
    DrawCats();
    DrawDetail();
    DrawTop();
    DrawStatusBar();
}

void App::DrawTop() {
    Fill(rcTop, Theme::Topbar);
    Fill({0, rcTop.bottom - 1, rcTop.right, rcTop.bottom}, Theme::Border);
    FillRR({16 * k, 16 * k, 44 * k, 44 * k}, 8 * k, Theme::Accent);
    Txt(L"V", {16 * k, 16 * k, 44 * k, 44 * k}, fLogo.Get(), 0xFFFFFF);
    Txt(L"Duncan's Vault", {54 * k, 0, 235 * k, rcTop.bottom}, fTitle.Get(), Theme::Text);
    const float bh = 36 * k, by = (rcTop.bottom - bh) / 2;
    float x = rcTop.right - 16 * k;
    D2D1_RECT_F gear{x - 38 * k, by, x, by + bh};
    Button(B_GEAR, gear, L"⚙", false);
    x = gear.left - 10 * k;
    D2D1_RECT_F lock{x - 104 * k, by, x, by + bh};
    Button(B_LOCK, lock, L"Sperren", false);
    x = lock.left - 10 * k;
    D2D1_RECT_F nw{x - 150 * k, by, x, by + bh};
    Button(B_NEW, nw, L"+ Neuer Eintrag", true);
    const float sl = 250 * k, sr = std::max(sl + 160 * k, nw.left - 18 * k);
    m_search.rc = {sl, by, sr, by + bh};
    DrawField(m_search, m_focusId == F_SEARCH);
}

int App::CountIn(int cat) const {
    if (cat < 0) return (int)m_entries.size();
    int n = 0;
    for (const auto& e : m_entries) n += (int)e.cat == cat;
    return n;
}

void App::DrawCats() {
    Fill(rcCats, Theme::Panel);
    Fill({rcCats.right - 1, rcCats.top, rcCats.right, rcCats.bottom}, Theme::Border);
    Txt(L"KATEGORIEN", {16 * k, rcCats.top + 10 * k, rcCats.right - 16 * k, rcCats.top + 30 * k}, fSmall.Get(), Theme::TextDim);
    float y = rcCats.top + 38 * k;
    for (int i = -1; i < vault::kCatCount; ++i) {
        const D2D1_RECT_F row{8 * k, y, rcCats.right - 10 * k, y + kCatH * k};
        const bool on = m_cat == i, hover = m_hoverBtn == C_CAT0 + i + 1;
        if (on) FillRR(row, 8 * k, Theme::Accent, 0.22f);
        else if (hover) FillRR(row, 8 * k, Theme::CardHover);
        const unsigned c = i < 0 ? Theme::TextDim : kCatColor[i];
        SetCol(c);
        rt->FillEllipse(D2D1::Ellipse({row.left + 16 * k, (row.top + row.bottom) / 2}, 5 * k, 5 * k), brush.Get());
        Txt(i < 0 ? L"Alle Einträge" : vault::CatName(static_cast<vault::Cat>(i)), {row.left + 32 * k, row.top, row.right - 46 * k, row.bottom}, fNormal.Get(),
            on ? Theme::Text : Theme::TextDim);
        Txt(std::to_wstring(CountIn(i)), {row.right - 44 * k, row.top, row.right - 10 * k, row.bottom}, fRight.Get(), Theme::TextDim);
        m_hits.push_back({C_CAT0 + i + 1, row});
        y += (kCatH + 2) * k;
    }
}

void App::DrawList() {
    Fill(rcList, Theme::Bg);
    const D2D1_RECT_F head{rcList.left, rcList.top, rcList.right, rcList.top + 36 * k};
    std::wstring title = m_cat < 0 ? L"Alle Einträge" : vault::CatName(static_cast<vault::Cat>(m_cat));
    title += L"  ·  " + std::to_wstring(m_view.size());
    Txt(title, {head.left + 18 * k, head.top, head.right - 10 * k, head.bottom}, fSmall.Get(), Theme::TextDim);
    const D2D1_RECT_F body{rcList.left, head.bottom, rcList.right, rcList.bottom};
    rt->PushAxisAlignedClip(body, D2D1_ANTIALIAS_MODE_ALIASED);
    const float rh = kRowH * k;
    const int first = std::max(0, (int)std::floor(m_scroll / rh)), last = std::min((int)m_view.size() - 1, (int)std::floor((m_scroll + H(body)) / rh));
    for (int vi = first; vi <= last; ++vi) {
        const vault::Entry& e = m_entries[m_view[vi]];
        const float y = body.top + vi * rh - m_scroll;
        const D2D1_RECT_F row{rcList.left + 8 * k, y + 2 * k, rcList.right - 14 * k, y + rh - 2 * k};
        const bool sel = e.id == m_selId;
        if (sel) { FillRR(row, 8 * k, Theme::Accent, 0.20f); StrokeRR(row, 8 * k, Theme::Accent, 1.f); }
        else if (vi == m_hoverRow) FillRR(row, 8 * k, Theme::CardHover);
        const unsigned c = kCatColor[(int)e.cat];
        const D2D1_RECT_F tile{row.left + 10 * k, row.top + 9 * k, row.left + 10 * k + 36 * k, row.top + 9 * k + 36 * k};
        FillRR(tile, 9 * k, c, 0.20f);
        Txt(kCatLetter[(int)e.cat], tile, fBadge.Get(), c);
        Txt(e.title, {tile.right + 12 * k, row.top + 5 * k, row.right - 8 * k, row.top + 28 * k}, fBold.Get(), Theme::Text);
        std::wstring sub = !e.user.empty() ? e.user : std::wstring(vault::CatName(e.cat));
        if (!e.tags.empty()) sub += L"   #" + e.tags;
        Txt(sub, {tile.right + 12 * k, row.top + 28 * k, row.right - 8 * k, row.bottom - 4 * k}, fSmall.Get(), Theme::TextDim);
    }
    rt->PopAxisAlignedClip();
    if (m_view.empty()) {
        Txt(m_entries.empty() ? L"Der Tresor ist noch leer.\nMit „+ Neuer Eintrag“ legst du den ersten an."
                              : L"Keine Einträge für diesen Filter.",
            body, fCenter.Get(), Theme::TextDim);
    }
    DrawScrollbar(body, m_scroll, (float)m_view.size() * rh);
}

void App::DrawDetail() {
    Fill(rcDetail, Theme::Panel);
    Fill({rcDetail.left, rcDetail.top, rcDetail.left + 1, rcDetail.bottom}, Theme::Border);
    const vault::Entry* e = Sel();
    if (!e) {
        Txt(L"Wähle einen Eintrag aus der Liste.", rcDetail, fCenter.Get(), Theme::TextDim);
        return;
    }
    const float pad = 22 * k, x0 = rcDetail.left + pad, fw = W(rcDetail) - 2 * pad;
    float y = rcDetail.top + 18 * k;
    Txt(e->title, {x0, y, x0 + fw, y + 32 * k}, fTitle.Get(), Theme::Text);
    y += 34 * k;
    {
        const std::wstring cn = vault::CatName(e->cat);
        const float cw = TextW(cn, fBadge.Get()) + 24 * k;
        const unsigned c = kCatColor[(int)e->cat];
        const D2D1_RECT_F r{x0, y, x0 + cw, y + 22 * k};
        FillRR(r, 11 * k, c, 0.16f);
        StrokeRR(r, 11 * k, c, 1.f);
        Txt(cn, r, fBadge.Get(), c);
    }
    y += 36 * k;
    const vault::Schema& sc = vault::SchemaOf(e->cat);
    const float bh = 40 * k;
    // Wert-Block: Beschriftung, Kasten mit Wert und Knoepfen rechts
    auto block = [&](const wchar_t* label, const std::wstring& value, int copyId, const wchar_t* copyLabel, int extraId, const wchar_t* extraLabel) {
        Txt(label, {x0, y, x0 + fw, y + 18 * k}, fSmall.Get(), Theme::TextDim);
        y += 20 * k;
        const D2D1_RECT_F box{x0, y, x0 + fw, y + bh};
        FillRR(box, 8 * k, Theme::Bg);
        StrokeRR(box, 8 * k, Theme::Border);
        float right = box.right - 6 * k;
        if (copyId) {
            const float w = 78 * k;
            Button(copyId, {right - w, box.top + 5 * k, right, box.bottom - 5 * k}, copyLabel, false);
            right -= w + 6 * k;
        }
        if (extraId) {
            const float w = std::max(78.f * k, TextW(extraLabel, fCenter.Get()) + 24 * k);
            Button(extraId, {right - w, box.top + 5 * k, right, box.bottom - 5 * k}, extraLabel, false);
            right -= w + 6 * k;
        }
        const bool link = copyId == DT_OPENURL && !value.empty();   // Web-Adresse: Text ist ein Link (Klick oeffnet im Standardbrowser)
        Txt(value.empty() ? L"–" : value, {box.left + 12 * k, box.top, right, box.bottom}, fNormal.Get(),
            value.empty() ? Theme::TextDim : (link ? Theme::Accent : Theme::Text));
        if (link) m_hits.push_back({DT_OPENURL, {box.left + 12 * k, box.top, right, box.bottom}});
        y += bh + 12 * k;
    };
    if (sc.user) block(sc.user, e->user, e->user.empty() ? 0 : DT_COPYUSER, L"Kopieren", 0, nullptr);
    if (sc.secret) {
        block(sc.secret, m_reveal ? e->secret : (e->secret.empty() ? L"" : L"●●●●●●●●●●●●"), e->secret.empty() ? 0 : DT_COPYSECRET, L"Kopieren",
              e->secret.empty() ? 0 : DT_SHOW, m_reveal ? L"Verbergen" : L"Anzeigen");
        if (!e->secret.empty() && e->cat == vault::Cat::Password) {
            y -= 6 * k;
            y += DrawStrength(x0, y, fw * 0.6f, e->secret);
        }
    }
    if (sc.url) block(sc.url, e->url, e->url.empty() ? 0 : DT_OPENURL, L"Öffnen", 0, nullptr);
    if (!e->tags.empty()) block(L"Tags", e->tags, 0, nullptr, 0, nullptr);
    // Notiz: mehrzeilig, bis vor die unteren Knoepfe
    const float footerH = 148 * k;
    if (!e->notes.empty() && y < rcDetail.bottom - footerH - 40 * k) {
        Txt(sc.notes, {x0, y, x0 + fw, y + 18 * k}, fSmall.Get(), Theme::TextDim);
        y += 20 * k;
        const D2D1_RECT_F box{x0, y, x0 + fw, std::max(y + 40 * k, rcDetail.bottom - footerH)};
        FillRR(box, 8 * k, Theme::Bg);
        StrokeRR(box, 8 * k, Theme::Border);
        Txt(e->notes, Inset(box, 12 * k, 8 * k), fWrap.Get(), Theme::Text);
    }
    // unten: Zeitstempel und Knoepfe
    const float by = rcDetail.bottom - 22 * k - 38 * k;
    Txt(L"Erstellt " + TimeText(e->created) + L"   ·   Geändert " + TimeText(e->modified), {x0, by - 28 * k, x0 + fw, by - 6 * k}, fSmall.Get(), Theme::TextDim);
    if ((sc.user && !e->user.empty()) || !e->secret.empty())   // Einloggen / Daten kopieren: Menue mit Kopieren, Website oeffnen und Auto-Type
        Button(DT_LOGIN, {x0, by - 28 * k - 10 * k - 44 * k, x0 + fw, by - 28 * k - 10 * k}, L"Einloggen / Daten kopieren  \u25BE", true);
    Button(DT_EDIT, {x0, by, x0 + (fw - 10 * k) / 2, by + 38 * k}, L"Bearbeiten", false);
    Button(DT_DELETE, {x0 + (fw + 10 * k) / 2, by, x0 + fw, by + 38 * k}, L"Löschen", false, true);
}

void App::DrawStatusBar() {
    Fill(rcStatus, Theme::Topbar);
    Fill({0, rcStatus.top, rcStatus.right, rcStatus.top + 1}, Theme::Border);
    const std::wstring left = !m_note.empty() ? m_note : (m_session.dpapi ? L"Tresor entsperrt (ohne Master-Passwort, an das Windows-Konto gebunden)  ·  " : L"Tresor entsperrt  ·  ") + std::to_wstring(m_entries.size()) + L" Einträge  ·  AES-256-GCM, nur lokal gespeichert";
    Txt(left, {12 * k, rcStatus.top, rcStatus.right * 0.62f, rcStatus.bottom}, fSmall.Get(), Theme::TextDim);
    std::wstring right;
    const ULONGLONG now = GetTickCount64();
    if (m_clipUntil > now) right = L"Zwischenablage wird in " + std::to_wstring((m_clipUntil - now + 999) / 1000) + L" s geleert";
    else if (m_settings.autoLock) {
        const ULONGLONG idle = now - m_lastInput, left2 = idle >= kAutoLockMs ? 0 : (kAutoLockMs - idle) / 1000;
        wchar_t buf[48];
        swprintf_s(buf, L"Automatische Sperre in %d:%02d", (int)(left2 / 60), (int)(left2 % 60));
        right = buf;
    }
    Txt(right, {rcStatus.right * 0.62f, rcStatus.top, rcStatus.right - 12 * k, rcStatus.bottom}, fRight.Get(), m_clipUntil > now ? Theme::Warn : Theme::TextDim);
}

// ============================================================================
// Dialoge
// ============================================================================

void App::DrawDialog() {
    Fill({0, 0, rcStatus.right, rcStatus.bottom}, 0x000000, 0.6f);
    if (m_dlg.kind == DlgKind::Entry) DrawEntryDialog();
    else DrawSimpleDialog();
}

// Hoehe des Eintragsdialogs (feste Teile plus Notizfeld, das bei kleinen Fenstern schrumpft)
float App::EntryDialogHeight(float fh, float, float availH) {
    const Dialog& d = m_dlg;
    const vault::Schema& sc = vault::SchemaOf(d.cat);
    const float pad = 24 * k;
    float h = pad + 36 * k + 38 * k;                      // Titel + Kategorie-Chips
    h += 24 * k + fh;                                     // Titel-Feld
    if (sc.user) h += 24 * k + fh;
    if (sc.secret) h += 24 * k + fh + 26 * k + (d.genOpen ? 54 * k : 0);
    if (sc.url) h += 24 * k + fh;
    h += 24 * k + fh;                                     // Tags
    h += 24 * k;                                          // Notiz-Beschriftung
    h += 28 * k + 38 * k + pad;                           // Fehler + Knoepfe
    const float wanted = (d.cat == vault::Cat::Note ? 9 : 4) * 20 * k + 20 * k;
    const float notes = std::clamp(availH - h, 64 * k, wanted);
    return h + notes;
}

void App::DrawEntryDialog() {
    Dialog& d = m_dlg;
    RECT cr;
    GetClientRect(m_hwnd, &cr);
    const float pad = 24 * k, fh = 36 * k, bw = std::min(680 * k, cr.right - 40 * k);
    const vault::Schema& sc = vault::SchemaOf(d.cat);
    const float height = std::min((float)cr.bottom - 20 * k, EntryDialogHeight(fh, 0, (float)cr.bottom - 20 * k));
    d.box = {(cr.right - bw) / 2, (cr.bottom - height) / 2, (cr.right + bw) / 2, (cr.bottom + height) / 2};
    FillRR(d.box, 14 * k, Theme::Panel);
    StrokeRR(d.box, 14 * k, Theme::Border);
    const float x0 = d.box.left + pad, fw = W(d.box) - 2 * pad;
    float y = d.box.top + pad;
    Txt(d.title, {x0, y, d.box.right - pad, y + 30 * k}, fTitle.Get(), Theme::Text);
    y += 36 * k;
    float cx = x0;   // Kategorie-Chips
    for (int i = 0; i < vault::kCatCount; ++i) {
        const std::wstring n = vault::CatName(static_cast<vault::Cat>(i));
        const float w = TextW(n, fBadge.Get()) + 30 * k;
        Chip(D_CAT0 + i, {cx, y, cx + w, y + 28 * k}, n, (int)d.cat == i, kCatColor[i]);
        cx += w + 8 * k;
    }
    y += 38 * k;
    auto place = [&](int id, const wchar_t* label, float w) {
        Field* f = FieldById(id);
        if (!f) return;
        f->label = label;
        f->hidden = false;
        LabeledField(*f, x0, y, w);
    };
    auto hide = [&](int id) { if (Field* f = FieldById(id)) { f->hidden = true; f->rc = {}; } };
    place(F_TITLE, L"Titel", fw);
    y += 24 * k + fh;
    if (sc.user) { place(F_USER, sc.user, fw); y += 24 * k + fh; } else hide(F_USER);
    if (sc.secret) {
        const float sideW = 200 * k;
        place(F_SECRET, sc.secret, fw - sideW - 8 * k);
        if (Field* f = FieldById(F_SECRET)) f->mask = !d.reveal;
        const float by = y + 20 * k;
        Button(D_REVEAL, {x0 + fw - sideW, by, x0 + fw - sideW + 96 * k, by + fh}, d.reveal ? L"Verbergen" : L"Anzeigen", false);
        Button(D_GENTOGGLE, {x0 + fw - 96 * k, by, x0 + fw, by + fh}, L"Generator", d.genOpen);
        y += 24 * k + fh;
        if (Field* f = FieldById(F_SECRET)) DrawStrength(x0, y - 2 * k, fw - sideW - 8 * k, f->ed.text);
        y += 26 * k;
        if (d.genOpen) {   // Generator-Zeile: Laenge, Zeichenklassen, Erzeugen
            const D2D1_RECT_F panel{x0, y - 2 * k, x0 + fw, y + 46 * k};
            FillRR(panel, 8 * k, Theme::Card);
            const float py = panel.top + 9 * k, ph = 28 * k;
            Txt(L"Länge", {panel.left + 12 * k, py, panel.left + 60 * k, py + ph}, fSmall.Get(), Theme::TextDim);
            Button(D_LENMINUS, {panel.left + 62 * k, py, panel.left + 90 * k, py + ph}, L"−", false);
            Txt(std::to_wstring(m_settings.gen.length), {panel.left + 92 * k, py, panel.left + 122 * k, py + ph}, fCenter.Get(), Theme::Text);
            Button(D_LENPLUS, {panel.left + 124 * k, py, panel.left + 152 * k, py + ph}, L"+", false);
            float px = panel.left + 168 * k;
            const struct { int id; const wchar_t* t; bool on; } cls[4] = {{D_GU, L"A–Z", m_settings.gen.upper}, {D_GL, L"a–z", m_settings.gen.lower},
                                                                         {D_GD, L"0–9", m_settings.gen.digits}, {D_GS, L"#$%", m_settings.gen.symbols}};
            for (const auto& c : cls) {
                Chip(c.id, {px, py, px + 58 * k, py + ph}, c.t, c.on, Theme::Accent);
                px += 66 * k;
            }
            Button(D_GENRUN, {panel.right - 112 * k, py, panel.right - 10 * k, py + ph}, L"Erzeugen", true);
            y += 54 * k;
        }
    } else { hide(F_SECRET); }
    if (sc.url) { place(F_URL, sc.url, fw); y += 24 * k + fh; } else hide(F_URL);
    place(F_TAGS, L"Tags (durch Leerzeichen getrennt)", fw);
    y += 24 * k + fh;
    {   // Notizfeld nimmt den Rest bis zu Fehlerzeile und Knoepfen
        Field* f = FieldById(F_NOTES);
        f->label = sc.notes;
        f->hidden = false;
        Txt(f->label, {x0, y, x0 + fw, y + 18 * k}, fSmall.Get(), Theme::TextDim);
        const float bottom = d.box.bottom - pad - 38 * k - 28 * k;
        f->rc = {x0, y + 22 * k, x0 + fw, std::max(y + 22 * k + 60 * k, bottom)};
    }
    const float bh = 38 * k, by2 = d.box.bottom - pad - bh;
    if (!d.error.empty()) Txt(d.error, {x0, by2 - 26 * k, x0 + fw, by2 - 4 * k}, fNormal.Get(), Theme::Danger);
    Button(D_OK, {d.box.right - pad - 150 * k, by2, d.box.right - pad, by2 + bh}, L"Speichern", true);
    Button(D_CANCEL, {d.box.right - pad - 150 * k - 10 * k - 130 * k, by2, d.box.right - pad - 160 * k, by2 + bh}, L"Abbrechen", false);
    for (auto& f : d.fields) DrawField(f, m_focusId == f.id);
}

void App::DrawSimpleDialog() {
    Dialog& d = m_dlg;
    RECT cr;
    GetClientRect(m_hwnd, &cr);
    const float pad = 24 * k, fh = 36 * k, bw = std::min(540 * k, cr.right - 40 * k);
    const bool confirm = d.kind == DlgKind::Confirm, imp = d.kind == DlgKind::Import;
    const bool strength = d.kind == DlgKind::ChangePw && d.mode != 2;
    const float msgH = confirm ? 70 : imp ? 96 : d.msgH;
    const float height = pad + 36 * k + msgH * k + (float)d.fields.size() * (24 * k + fh) + (strength ? 30 * k : 0) + 30 * k + 38 * k + pad;
    d.box = {(cr.right - bw) / 2, (cr.bottom - height) / 2, (cr.right + bw) / 2, (cr.bottom + height) / 2};
    FillRR(d.box, 14 * k, Theme::Panel);
    StrokeRR(d.box, 14 * k, Theme::Border);
    const float x0 = d.box.left + pad, fw = W(d.box) - 2 * pad;
    float y = d.box.top + pad;
    Txt(d.title, {x0, y, d.box.right - pad, y + 30 * k}, fTitle.Get(), Theme::Text);
    y += 36 * k;
    if (msgH > 0) {
        Txt(d.message, {x0, y, x0 + fw, y + (msgH - 4) * k}, fWrap.Get(), Theme::TextDim);
        y += msgH * k;
    }
    for (auto& f : d.fields) {
        LabeledField(f, x0, y, fw);
        y += 24 * k + fh;
    }
    if (strength) DrawStrength(x0, y - 2 * k, fw, FieldById(F_NEW) ? FieldById(F_NEW)->ed.text : L"");
    const float bh = 38 * k, by = d.box.bottom - pad - bh;
    if (!d.error.empty()) Txt(d.error, {x0, by - 26 * k, x0 + fw, by - 4 * k}, fNormal.Get(), Theme::Danger);
    const wchar_t* okLabel = confirm ? L"Löschen" : imp ? L"Importieren" : d.mode == 1 ? L"Festlegen" : d.mode == 2 ? L"Entfernen" : L"Ändern";
    Button(D_OK, {d.box.right - pad - 150 * k, by, d.box.right - pad, by + bh}, okLabel, !confirm, confirm || d.mode == 2);
    Button(D_CANCEL, {d.box.right - pad - 160 * k - 130 * k, by, d.box.right - pad - 160 * k, by + bh}, L"Abbrechen", false);
    for (auto& f : d.fields) DrawField(f, m_focusId == f.id);
}

// ============================================================================
// Tresor
// ============================================================================

void App::TryUnlock() {
    const ULONGLONG now = GetTickCount64();
    if (now < m_nextTry) {
        m_lockErr = L"Bitte noch " + std::to_wstring((m_nextTry - now + 999) / 1000) + L" s warten, dann erneut versuchen.";
        Invalidate();
        return;
    }
    if (!m_lockNoPw && m_lockPw.ed.text.empty()) { m_lockErr = L"Bitte das Master-Passwort eingeben."; Invalidate(); return; }
    std::wstring pw = m_lockPw.ed.text;
    std::string plain;
    vault::Session s;
    const vault::Result r = vault::OpenFile(VaultPath(), pw, s, plain);
    vault::Wipe(pw);
    if (r == vault::Result::Ok) {
        std::vector<vault::Entry> entries;
        if (vault::Parse(plain, entries)) {
            vault::Wipe(plain);
            m_entries = std::move(entries);
            m_session = s;
            WipeField(m_lockPw);
            m_lockPw.mask = true;
            m_lockErr.clear();
            m_fails = 0;
            m_screen = Screen::Main;
            m_cat = -1;
            m_selId.clear();
            m_search.ed.SetText(L"");
            m_scroll = 0;
            m_reveal = false;
            m_note.clear();
            m_focusId = kNoFocus;
            Rebuild();
            Touch();
        } else {
            vault::Wipe(plain);
            m_lockErr = L"Der Tresor-Inhalt konnte nicht gelesen werden.";
        }
    } else if (r == vault::Result::WrongPassword && m_lockNoPw) {
        m_lockErr = L"Dieser Tresor ist an ein anderes Windows-Konto gebunden und lässt sich hier nicht öffnen.";
    } else if (r == vault::Result::WrongPassword) {
        ++m_fails;
        m_nextTry = GetTickCount64() + std::min(m_fails, 6) * 1000ull;   // Bremse gegen schnelles Durchprobieren
        m_lockErr = L"Falsches Master-Passwort.";
        WipeField(m_lockPw);
    } else if (r == vault::Result::BadFormat) {
        m_lockErr = L"Die Tresor-Datei ist beschädigt oder keine Vault-Datei. Eine Sicherung kann als vault.dat.bak im Tresor-Ordner liegen (Zahnrad → Tresor-Ordner öffnen).";
    } else if (r == vault::Result::NotFound) {
        m_screen = Screen::Setup;
        m_focusId = F_SETPW;
    } else {
        m_lockErr = L"Der Tresor konnte nicht geöffnet werden.";
    }
    Invalidate();
}

void App::CreateVault() {
    const std::wstring& a = m_setPw.ed.text;
    if (a.size() < 8) { m_setErr = L"Das Master-Passwort braucht mindestens 8 Zeichen."; Invalidate(); return; }
    if (a != m_setPw2.ed.text) { m_setErr = L"Die beiden Passwörter stimmen nicht überein."; Invalidate(); return; }
    vault::Session s;
    if (!vault::NewSession(a, vault::kDefaultIterations, s)) { m_setErr = L"Die Verschlüsselung konnte nicht vorbereitet werden."; Invalidate(); return; }
    m_session = s;
    m_entries.clear();
    if (!Persist()) { m_setErr = L"Die Tresor-Datei konnte nicht geschrieben werden."; m_session.Close(); Invalidate(); return; }
    WipeField(m_setPw);
    WipeField(m_setPw2);
    m_setErr.clear();
    m_screen = Screen::Main;
    m_cat = -1;
    m_focusId = kNoFocus;
    Rebuild();
    Touch();
    SetNote(L"Tresor angelegt. Lege jetzt deinen ersten Eintrag an.");
    Invalidate();
}

void App::CreateVaultNoPw() {
    vault::Session s;
    if (!vault::NewDpapiSession(s)) { m_setErr = L"Der Schlüssel konnte nicht an das Windows-Konto gebunden werden."; Invalidate(); return; }
    m_session = s;
    m_entries.clear();
    if (!Persist()) { m_setErr = L"Die Tresor-Datei konnte nicht geschrieben werden."; m_session.Close(); Invalidate(); return; }
    WipeField(m_setPw);
    WipeField(m_setPw2);
    m_setErr.clear();
    m_screen = Screen::Main;
    m_cat = -1;
    m_focusId = kNoFocus;
    Rebuild();
    Touch();
    SetNote(L"Tresor ohne Master-Passwort angelegt. Ein Master-Passwort kannst du jederzeit im Zahnrad-Menü festlegen.", true);
    Invalidate();
}

bool App::Persist() {
    std::string plain = vault::Serialize(m_entries);
    const vault::Result r = vault::SaveFile(VaultPath(), m_session, plain);
    vault::Wipe(plain);
    if (r != vault::Result::Ok) SetNote(L"Achtung: Der Tresor konnte nicht gespeichert werden!", true);
    return r == vault::Result::Ok;
}

void App::Lock(bool automatic) {
    if (m_screen != Screen::Main) return;
    ClearClipboardIfOurs();
    for (auto& f : m_dlg.fields) vault::Wipe(f.ed.text);
    m_dlg = Dialog{};
    vault::Wipe(m_entries);
    m_view.clear();
    m_selId.clear();
    m_reveal = false;
    m_search.ed.SetText(L"");
    m_note.clear();
    m_lockNoPw = m_session.dpapi;
    m_session.Close();
    m_screen = Screen::Lock;
    WipeField(m_lockPw);
    m_lockPw.mask = true;
    m_lockErr = automatic ? L"Der Tresor wurde nach 5 Minuten Inaktivität automatisch gesperrt." : L"";
    m_focusId = m_lockNoPw ? kNoFocus : F_LOCKPW;
    Invalidate();
}

void App::Rebuild() {
    m_view.clear();
    const std::wstring q = m_search.ed.text;
    for (int i = 0; i < (int)m_entries.size(); ++i) {
        const vault::Entry& e = m_entries[i];
        if (m_cat >= 0 && (int)e.cat != m_cat) continue;
        if (!q.empty() && !vault::Matches(e, q)) continue;
        m_view.push_back(i);
    }
    std::sort(m_view.begin(), m_view.end(), [&](int a, int b) { return vault::Lower(m_entries[a].title) < vault::Lower(m_entries[b].title); });
    if (!m_selId.empty() && !FindEntry(m_selId)) m_selId.clear();
    ClampScroll();
}

const vault::Entry* App::Sel() const {
    for (const auto& e : m_entries)
        if (e.id == m_selId) return &e;
    return nullptr;
}
vault::Entry* App::FindEntry(const std::wstring& id) {
    for (auto& e : m_entries)
        if (e.id == id) return &e;
    return nullptr;
}

int App::RowAt(float x, float y) const {
    if (m_screen != Screen::Main || x < rcList.left || x >= rcList.right) return -1;
    const float top = rcList.top + 36 * k;
    if (y < top || y >= rcList.bottom) return -1;
    const int i = (int)std::floor((y - top + m_scroll) / (kRowH * k));
    return i >= 0 && i < (int)m_view.size() ? i : -1;
}

void App::ClampScroll() {
    const float content = (float)m_view.size() * kRowH * k, view = std::max(0.f, H(rcList) - 36 * k);
    m_scroll = std::clamp(m_scroll, 0.f, std::max(0.f, content - view));
}

void App::EnsureRowVisible(int vi) {
    const float rh = kRowH * k, view = H(rcList) - 36 * k;
    if (vi * rh < m_scroll) m_scroll = vi * rh;
    else if ((vi + 1) * rh > m_scroll + view) m_scroll = (vi + 1) * rh - view;
    ClampScroll();
}

// ============================================================================
// Aktionen
// ============================================================================

void App::CloseDialog() {
    for (auto& f : m_dlg.fields) vault::Wipe(f.ed.text);
    m_dlg = Dialog{};
    m_focusId = kNoFocus;
    Invalidate();
}

void App::BeginEntry(const vault::Entry* e) {
    m_dlg = Dialog{};
    Dialog& d = m_dlg;
    d.kind = DlgKind::Entry;
    d.title = e ? L"Eintrag bearbeiten" : L"Neuer Eintrag";
    d.cat = e ? e->cat : (m_cat >= 0 ? static_cast<vault::Cat>(m_cat) : vault::Cat::Password);
    d.editId = e ? e->id : L"";
    auto mk = [&](int id, const wchar_t* hint, const std::wstring& text, size_t maxLen) {
        Field f;
        f.id = id;
        f.hint = hint;
        f.ed.maxLen = maxLen;
        f.ed.SetText(text);
        d.fields.push_back(std::move(f));
    };
    mk(F_TITLE, L"z. B. GitHub, Windows 11 Pro, Heimnetz", e ? e->title : L"", 100);
    mk(F_USER, L"", e ? e->user : L"", 160);
    mk(F_SECRET, L"", e ? e->secret : L"", 512);
    d.fields.back().mask = true;
    mk(F_URL, L"https://…", e ? e->url : L"", 300);
    mk(F_TAGS, L"z. B. arbeit privat", e ? e->tags : L"", 120);
    mk(F_NOTES, L"", e ? e->notes : L"", 8000);
    d.fields.back().multiline = true;
    d.fields.back().ed.multiline = true;
    m_focusId = F_TITLE;
    Invalidate();
}

void App::ApplyEntry() {
    Dialog& d = m_dlg;
    auto txt = [&](int id) { const Field* f = FieldById(id); return f ? f->ed.text : std::wstring(); };
    std::wstring title = vault::Trim(txt(F_TITLE));
    if (title.empty()) { d.error = L"Bitte einen Titel eingeben."; Invalidate(); return; }
    vault::Entry* e = d.editId.empty() ? nullptr : FindEntry(d.editId);
    vault::Entry fresh;
    if (!e) {
        fresh.id = vault::NewId();
        fresh.created = (long long)time(nullptr);
        e = &fresh;
    }
    e->cat = d.cat;
    e->title = title;
    e->user = vault::Trim(txt(F_USER));
    e->secret = txt(F_SECRET);
    e->url = vault::Trim(txt(F_URL));
    e->tags = vault::Trim(txt(F_TAGS));
    e->notes = txt(F_NOTES);
    e->modified = (long long)time(nullptr);
    const std::wstring id = e->id;
    if (e == &fresh) m_entries.push_back(fresh);
    vault::Wipe(fresh);
    m_selId = id;
    m_reveal = false;
    if (Persist()) SetNote(L"Gespeichert");
    CloseDialog();
    Rebuild();
    for (int i = 0; i < (int)m_view.size(); ++i)
        if (m_entries[m_view[i]].id == id) { EnsureRowVisible(i); break; }
    Invalidate();
}

void App::BeginChangePw(int mode) {
    m_dlg = Dialog{};
    Dialog& d = m_dlg;
    d.kind = DlgKind::ChangePw;
    d.mode = mode;
    d.title = mode == 1 ? L"Master-Passwort festlegen" : mode == 2 ? L"Master-Passwort entfernen" : L"Master-Passwort ändern";
    if (mode == 1) {
        d.message = L"Danach fragt der Tresor beim Start dieses Passwort ab. Es lässt sich nicht wiederherstellen; wer es vergisst, verliert den Zugriff auf alle Einträge.";
        d.msgH = 70;
    } else if (mode == 2) {
        d.message = L"Der Tresor wird danach ohne Master-Passwort geöffnet und ist nur noch durch dein Windows-Konto geschützt: Wer bei diesem PC unter deinem Konto angemeldet ist, kann ihn öffnen.";
        d.msgH = 80;
    }
    auto mk = [&](int id, const wchar_t* label) {
        Field f;
        f.id = id;
        f.label = label;
        f.mask = true;
        f.ed.maxLen = 128;
        d.fields.push_back(std::move(f));
    };
    if (mode != 1) mk(F_OLD, L"Aktuelles Master-Passwort");
    if (mode != 2) {
        mk(F_NEW, L"Neues Master-Passwort (mindestens 8 Zeichen)");
        mk(F_NEW2, L"Neues Master-Passwort wiederholen");
    }
    m_focusId = d.fields.front().id;
    Invalidate();
}

void App::ApplyChangePw() {
    Dialog& d = m_dlg;
    auto txt = [&](int id) { const Field* f = FieldById(id); return f ? f->ed.text : std::wstring(); };
    if (d.mode != 1) {   // altes Passwort pruefen, indem die Datei damit geoeffnet wird
        vault::Session check;
        std::string plain;
        std::wstring oldPw = txt(F_OLD);
        const vault::Result r = vault::OpenFile(VaultPath(), oldPw, check, plain);
        vault::Wipe(oldPw);
        vault::Wipe(plain);
        check.Close();
        if (r != vault::Result::Ok) { d.error = L"Das aktuelle Master-Passwort stimmt nicht."; Invalidate(); return; }
    }
    vault::Session s;
    if (d.mode == 2) {   // Master-Passwort entfernen: Schluessel an das Windows-Konto binden
        if (!vault::NewDpapiSession(s)) { d.error = L"Der Schlüssel konnte nicht an das Windows-Konto gebunden werden."; Invalidate(); return; }
    } else {
        std::wstring np = txt(F_NEW), np2 = txt(F_NEW2);
        const bool tooShort = np.size() < 8, differ = np != np2;
        const bool derived = !tooShort && !differ && vault::NewSession(np, vault::kDefaultIterations, s);
        vault::Wipe(np);
        vault::Wipe(np2);
        if (tooShort) { d.error = L"Das neue Master-Passwort braucht mindestens 8 Zeichen."; Invalidate(); return; }
        if (differ) { d.error = L"Die neuen Passwörter stimmen nicht überein."; Invalidate(); return; }
        if (!derived) { d.error = L"Die Verschlüsselung konnte nicht vorbereitet werden."; Invalidate(); return; }
    }
    const vault::Session old = m_session;
    m_session = s;
    if (!Persist()) { m_session = old; d.error = L"Der Tresor konnte nicht gespeichert werden."; Invalidate(); return; }
    CloseDialog();
    SetNote(d.mode == 1 ? L"Master-Passwort festgelegt. Beim nächsten Start wird es abgefragt."
            : d.mode == 2 ? L"Master-Passwort entfernt. Der Tresor öffnet sich jetzt ohne Passwort (an dein Windows-Konto gebunden)."
                          : L"Master-Passwort geändert. Beim nächsten Entsperren gilt das neue Passwort.", true);
    Invalidate();
}

// Backup: die verschluesselte Tresor-Datei wird unveraendert als .vault kopiert (bleibt mit dem aktuellen Master-Passwort verschluesselt)
void App::ExportBackup() {
    if (!Persist()) return;   // sicherstellen, dass die Datei den aktuellen Stand enthaelt
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t name[MAX_PATH]{};
    swprintf_s(name, L"Vault-Backup-%04d-%02d-%02d.vault", st.wYear, st.wMonth, st.wDay);
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Vault-Backup (*.vault)\0*.vault\0Alle Dateien (*.*)\0*.*\0";
    ofn.lpstrFile = name;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"vault";
    ofn.lpstrTitle = L"Verschlüsseltes Backup speichern";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return;
    if (CopyFileW(VaultPath().c_str(), name, FALSE)) SetNote(L"Backup gespeichert: " + fs::path(name).filename().wstring() +
                                                  (m_session.dpapi ? L" (an dieses Windows-Konto gebunden – auf einem anderen PC nur nach Festlegen eines Master-Passworts importierbar)"
                                                                   : L" (verschlüsselt mit dem aktuellen Master-Passwort)"));
    else SetNote(L"Das Backup konnte nicht gespeichert werden.", true);
    Invalidate();
}

void App::BeginImport() {
    wchar_t name[MAX_PATH]{};
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Vault-Backup (*.vault)\0*.vault\0Alle Dateien (*.*)\0*.*\0";
    ofn.lpstrFile = name;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Backup importieren";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    const vault::FileMode bm = vault::ReadMode(name);
    if (bm != vault::FileMode::Password && bm != vault::FileMode::Dpapi) { SetNote(L"Das ist keine gültige Vault-Backup-Datei.", true); Invalidate(); return; }
    m_dlg = Dialog{};
    Dialog& d = m_dlg;
    d.kind = DlgKind::Import;
    d.title = L"Backup importieren";
    d.editId = name;   // Pfad der Backup-Datei
    if (bm == vault::FileMode::Dpapi) {   // Backup ohne Master-Passwort: kein Passwort noetig, es zaehlt das Windows-Konto
        d.noPw = true;
        ApplyImport();
        return;
    }
    d.message = L"Datei: " + fs::path(name).filename().wstring() + L"\nGib das Master-Passwort ein, mit dem dieses Backup verschlüsselt wurde. Neue Einträge werden hinzugefügt, neuere Fassungen vorhandener Einträge übernommen; nichts wird gelöscht.";
    Field f;
    f.id = F_IMPPW;
    f.label = L"Master-Passwort des Backups";
    f.mask = true;
    f.ed.maxLen = 128;
    d.fields.push_back(std::move(f));
    m_focusId = F_IMPPW;
    Invalidate();
}

void App::ApplyImport() {
    Dialog& d = m_dlg;
    const Field* f = FieldById(F_IMPPW);
    if (!d.noPw && (!f || f->ed.text.empty())) { d.error = L"Bitte das Master-Passwort des Backups eingeben."; Invalidate(); return; }
    vault::Session s;
    std::string plain;
    const vault::Result r = vault::OpenFile(fs::path(d.editId), d.noPw ? std::wstring() : f->ed.text, s, plain);
    s.Close();
    if (d.noPw && r != vault::Result::Ok) {
        CloseDialog();
        SetNote(L"Dieses Backup ist an ein anderes Windows-Konto gebunden und lässt sich hier nicht öffnen.", true);
        Invalidate();
        return;
    }
    if (r == vault::Result::WrongPassword) { d.error = L"Falsches Passwort oder beschädigte Backup-Datei."; Invalidate(); return; }
    if (r != vault::Result::Ok) { d.error = r == vault::Result::BadFormat ? L"Das ist keine gültige Vault-Backup-Datei." : L"Die Datei konnte nicht gelesen werden."; Invalidate(); return; }
    std::vector<vault::Entry> imp;
    const bool parsed = vault::Parse(plain, imp);
    vault::Wipe(plain);
    if (!parsed) { d.error = L"Der Inhalt des Backups konnte nicht gelesen werden."; Invalidate(); return; }
    int added = 0, updated = 0;
    vault::Merge(m_entries, imp, added, updated);
    vault::Wipe(imp);
    if (added || updated) Persist();
    CloseDialog();
    Rebuild();
    SetNote(L"Backup importiert: " + std::to_wstring(added) + L" neu, " + std::to_wstring(updated) + L" aktualisiert");
    Invalidate();
}

void App::BeginConfirmDelete() {
    const vault::Entry* e = Sel();
    if (!e) return;
    m_dlg = Dialog{};
    m_dlg.kind = DlgKind::Confirm;
    m_dlg.title = L"Eintrag löschen?";
    m_dlg.message = L"„" + e->title + L"“ wird endgültig aus dem Tresor entfernt. Das lässt sich nicht rückgängig machen.";
    m_dlg.editId = e->id;
    Invalidate();
}

void App::GenerateInto() {
    Field* f = FieldById(F_SECRET);
    if (!f) return;
    std::wstring pw = vault::Generate(m_settings.gen);
    f->ed.SetText(pw);
    vault::Wipe(pw);
    m_dlg.reveal = true;
    SaveSettings(m_settings);
    Invalidate();
}

void App::CopyText(const std::wstring& text, bool secret, const wchar_t* what) {
    SetClipboardText(m_hwnd, text, secret);
    if (secret) {
        m_clipText = text;
        m_clipUntil = GetTickCount64() + kClipClearMs;
        SetNote(std::wstring(what) + L" kopiert! Zwischenablage wird in " + std::to_wstring(kClipClearMs / 1000) + L" s geleert.");
    } else {
        SetNote(std::wstring(what) + L" kopiert");
    }
    Invalidate();
}

void App::ClearClipboardIfOurs() {
    if (!m_clipUntil) return;
    if (ClipboardText() == m_clipText && OpenClipboard(m_hwnd)) {
        EmptyClipboard();
        CloseClipboard();
    }
    vault::Wipe(m_clipText);
    m_clipUntil = 0;
    SetNote(L"Zwischenablage geleert");
}

// Nur Web-Adressen oeffnen (http/https); fehlt das Schema, wird https:// vorangestellt
void App::OpenUrl(const std::wstring& url) {
    std::wstring u = vault::Trim(url);
    if (u.empty()) return;
    if (u.compare(0, 7, L"http://") != 0 && u.compare(0, 8, L"https://") != 0) u = L"https://" + u;
    ShellExecuteW(m_hwnd, L"open", u.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void App::ShowLoginMenu() {
    const vault::Entry* e = Sel();
    if (!e) return;
    const vault::Schema& sc = vault::SchemaOf(e->cat);
    const bool hasUser = sc.user && !e->user.empty(), hasSecret = !e->secret.empty(), hasUrl = !e->url.empty();
    const std::wstring userWord = sc.user ? sc.user : L"Benutzername", secretWord = vault::SecretWord(e->cat);
    HMENU m = CreatePopupMenu();
    if (hasUrl) AppendMenuW(m, MF_STRING | (hasUser ? 0 : MF_GRAYED), 1, (L"Website öffnen und " + userWord + L" kopieren").c_str());
    AppendMenuW(m, MF_STRING | (hasUser ? 0 : MF_GRAYED), 2, (userWord + L" kopieren").c_str());
    AppendMenuW(m, MF_STRING | (hasSecret ? 0 : MF_GRAYED), 3, (secretWord + L" kopieren").c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    const UINT typeFlags = m_settings.autoType && (hasUser || hasSecret) ? 0 : MF_GRAYED;
    AppendMenuW(m, MF_STRING | typeFlags, 4, L"Auto-Type: Benutzername ⇥ Passwort eintippen");
    AppendMenuW(m, MF_STRING | typeFlags, 5, L"Auto-Type und mit Enter absenden");
    if (!m_settings.autoType) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L"(Auto-Type im Zahnrad-Menü einschalten)");
    POINT pt{};
    GetCursorPos(&pt);
    for (const auto& b : m_hits)
        if (b.id == DT_LOGIN) {
            pt = {(LONG)b.rc.left, (LONG)b.rc.top};
            ClientToScreen(m_hwnd, &pt);
        }
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(m);
    // Der Eintrag kann sich waehrend des Menues nicht aendern, die Zeiger bleiben gueltig
    switch (cmd) {
        case 1: OpenUrl(e->url); CopyText(e->user, true, userWord.c_str()); break;
        case 2: CopyText(e->user, true, userWord.c_str()); break;   // true: nicht in Verlauf/Cloud-Zwischenablage, wird automatisch geleert
        case 3: CopyText(e->secret, true, secretWord.c_str()); break;
        case 4: BeginAutoType(false); break;
        case 5: BeginAutoType(true); break;
    }
}

// Auto-Type: das Fenster des Tresors verschwindet, nach kAutoTypeDelayMs wird in das dann aktive Fenster getippt.
// In dieser Zeit klickt man das Benutzernamen-Feld an. Der Tresor selbst ist nie das Ziel.
void App::BeginAutoType(bool enter) {
    const vault::Entry* e = Sel();
    if (!e || m_screen != Screen::Main) return;
    if (!m_settings.autoType) { SetNote(L"Auto-Type ist ausgeschaltet (Zahnrad-Menü)."); return; }
    if (e->user.empty() && e->secret.empty()) { SetNote(L"Dieser Eintrag hat nichts zum Eintippen."); return; }
    m_autoId = e->id;
    m_autoEnter = enter;
    m_autoAt = GetTickCount64() + kAutoTypeDelayMs;
    SetNote(L"Auto-Type startet in " + std::to_wstring(kAutoTypeDelayMs / 1000) + L" s – jetzt das Benutzernamen-Feld anklicken …", true);
    ShowWindow(m_hwnd, SW_MINIMIZE);
}

void App::RunAutoType() {
    const std::wstring id = m_autoId;
    const bool enter = m_autoEnter;
    m_autoAt = 0;
    m_autoId.clear();
    const vault::Entry* e = m_screen == Screen::Main ? FindEntry(id) : nullptr;
    if (!e) { SetNote(L"Auto-Type abgebrochen: Der Tresor ist gesperrt."); return; }
    HWND fg = GetForegroundWindow();
    if (!fg || fg == m_hwnd || GetAncestor(fg, GA_ROOT) == m_hwnd) {   // kein anderes Fenster: nichts tippen
        ShowWindow(m_hwnd, SW_RESTORE);
        SetNote(L"Auto-Type abgebrochen: Es war kein anderes Fenster aktiv.");
        return;
    }
    wchar_t title[160]{};
    GetWindowTextW(fg, title, 160);
    bool ok = true;
    if (!e->user.empty()) ok = TypeText(e->user);
    if (ok && !e->user.empty() && !e->secret.empty()) { Sleep(60); ok = TypeKey(VK_TAB); Sleep(120); }
    if (ok && !e->secret.empty()) ok = TypeText(e->secret);
    if (ok && enter) { Sleep(60); ok = TypeKey(VK_RETURN); }
    SetNote(ok ? L"Eingetippt in: " + std::wstring(title) : L"Auto-Type wurde von Windows blockiert (Zielfenster mit Administratorrechten?).");
}

void App::ShowGearMenu() {
    HMENU m = CreatePopupMenu();
    if (m_session.dpapi) AppendMenuW(m, MF_STRING, 1, L"Master-Passwort festlegen …");
    else {
        AppendMenuW(m, MF_STRING, 1, L"Master-Passwort ändern …");
        AppendMenuW(m, MF_STRING, 7, L"Master-Passwort entfernen …");
    }
    AppendMenuW(m, MF_STRING | (m_settings.autoLock ? MF_CHECKED : 0), 2, L"Nach 5 Minuten Inaktivität automatisch sperren");
    AppendMenuW(m, MF_STRING | (m_settings.autoType ? MF_CHECKED : 0), 8, L"Auto-Type (Eintippen per Tastatursimulation) anbieten");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 5, L"Backup exportieren (.vault) …");
    AppendMenuW(m, MF_STRING, 6, L"Backup importieren (.vault) …");
    AppendMenuW(m, MF_STRING, 3, L"Tresor-Ordner im Explorer öffnen");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 4, L"Über Duncan's Vault");
    POINT pt{(LONG)rcTop.right - (LONG)(16 * k), (LONG)rcTop.bottom};
    ClientToScreen(m_hwnd, &pt);
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(m);
    switch (cmd) {
        case 1: BeginChangePw(m_session.dpapi ? 1 : 0); break;
        case 7: BeginChangePw(2); break;
        case 2: m_settings.autoLock = !m_settings.autoLock; SaveSettings(m_settings); Touch(); break;
        case 8: m_settings.autoType = !m_settings.autoType; SaveSettings(m_settings); break;
        case 5: ExportBackup(); break;
        case 6: BeginImport(); break;
        case 3: ShellExecuteW(m_hwnd, L"open", DataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
        case 4: {
            const std::wstring text = L"Duncan's Vault\nTeil von Duncan's Suite\n\nVersion " APP_VERSION
                                      L"\n\nLokaler Tresor für Passwörter, Software-Lizenzen, WLAN-Schlüssel und Notizen.\n"
                                      L"Verschlüsselung: AES-256-GCM, Schlüssel per PBKDF2-HMAC-SHA256 (600.000 Runden) aus dem Master-Passwort.\n\n"
                                      L"Datenschutz: Alle Daten bleiben zu 100 % auf diesem Gerät. Es gibt keine Cloud-Verbindung, keinen Sync und keine Telemetrie.\n\n"
                                      L"Tresor-Datei: %LOCALAPPDATA%\\DuncansVault\\vault.dat (eine Sicherung der vorherigen Fassung liegt als vault.dat.bak daneben).";
            MessageBoxW(m_hwnd, text.c_str(), L"Über", MB_OK | MB_ICONINFORMATION);
            break;
        }
    }
    Invalidate();
}

// ============================================================================
// Eingabe
// ============================================================================

std::vector<Field*> App::ActiveFields() {
    std::vector<Field*> v;
    if (m_screen == Screen::Lock) { if (!m_lockNoPw) v.push_back(&m_lockPw); }
    else if (m_screen == Screen::Setup) { v.push_back(&m_setPw); v.push_back(&m_setPw2); }
    else if (m_dlg.kind != DlgKind::None) for (auto& f : m_dlg.fields) v.push_back(&f);
    else v.push_back(&m_search);
    return v;
}

Field* App::FieldById(int id) {
    for (Field* f : ActiveFields())
        if (f->id == id) return f;
    return nullptr;
}

void App::FocusNext(bool back) {
    auto fs_ = ActiveFields();
    const int n = (int)fs_.size();
    if (!n) return;
    int idx = -1;
    for (int i = 0; i < n; ++i)
        if (fs_[i]->id == m_focusId) idx = i;
    idx = idx < 0 ? 0 : (idx + (back ? n - 1 : 1)) % n;
    for (int tries = 0; tries < n && fs_[idx]->hidden; ++tries) idx = (idx + (back ? n - 1 : 1)) % n;
    m_focusId = fs_[idx]->id;
    fs_[idx]->ed.SelectAll();
    ResetCaret();
    Invalidate();
}

void App::FieldKey(Field& f, WPARAM vk) {
    const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    docs::TextEdit& e = f.ed;
    switch (vk) {
        case VK_LEFT: e.Move(-1, shift, ctrl); break;
        case VK_RIGHT: e.Move(1, shift, ctrl); break;
        case VK_UP:
        case VK_DOWN: {
            if (!f.multiline) return;
            ComPtr<IDWriteTextLayout> layout = MakeLayout(f, e.text);
            if (!layout) return;
            float cx = 0, cy = 0;
            DWRITE_HIT_TEST_METRICS hm{};
            layout->HitTestTextPosition((UINT32)e.caret, FALSE, &cx, &cy, &hm);
            BOOL trailing = FALSE, inside = FALSE;
            DWRITE_HIT_TEST_METRICS m2{};
            layout->HitTestPoint(cx, cy + (vk == VK_UP ? -hm.height * 0.5f : hm.height * 1.5f), &trailing, &inside, &m2);
            e.SetCaret((size_t)m2.textPosition + (trailing ? 1 : 0), shift);
            break;
        }
        case VK_HOME: e.Home(shift); break;
        case VK_END: e.End(shift); break;
        case VK_BACK:
            if (ctrl && !e.HasSelection()) e.Move(-1, true, true);
            e.Backspace();
            break;
        case VK_DELETE: e.Delete(); break;
        case 'A': if (!ctrl) return; e.SelectAll(); break;
        case 'C': if (!ctrl) return; if (e.HasSelection() && !f.mask) SetClipboardText(m_hwnd, e.Selected(), false); break;   // Passwortfelder nie kopierbar
        case 'X':
            if (!ctrl) return;
            if (e.HasSelection() && !f.mask) { SetClipboardText(m_hwnd, e.Selected(), false); e.DeleteSelection(); }
            break;
        case 'V': if (!ctrl) return; e.Insert(ClipboardText()); break;
        default: return;
    }
    ResetCaret();
    if (f.id == F_SEARCH) { m_scroll = 0; Rebuild(); }
    Invalidate();
}

void App::FieldMouse(Field& f, float x, float y, bool extend) {
    const std::wstring shown = f.mask ? std::wstring(f.ed.text.size(), L'●') : f.ed.text;
    ComPtr<IDWriteTextLayout> layout;
    if (f.ed.text.empty() || !(layout = MakeLayout(f, shown))) {
        f.ed.SetCaret(0, extend);
        return;
    }
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS m{};
    if (f.multiline) layout->HitTestPoint(x - (f.rc.left + 11 * k), y - (f.rc.top + 8 * k) + f.vscroll, &trailing, &inside, &m);
    else layout->HitTestPoint(x - (f.rc.left + 11 * k) + f.scroll, H(f.rc) / 2, &trailing, &inside, &m);
    f.ed.SetCaret((size_t)m.textPosition + (trailing ? 1 : 0), extend);
    ResetCaret();
}

void App::KeyDown(WPARAM vk) {
    Touch();
    const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    // Entsperrmaske / Einrichten
    if (m_screen != Screen::Main) {
        if (vk == VK_TAB) { FocusNext(shift); return; }
        if (vk == VK_RETURN) {
            if (m_screen == Screen::Lock) TryUnlock();
            else if (m_focusId == F_SETPW) FocusNext(false);
            else CreateVault();
            return;
        }
        if (Field* f = FocusedField()) FieldKey(*f, vk);
        return;
    }
    // Dialoge
    if (m_dlg.kind != DlgKind::None) {
        if (vk == VK_ESCAPE) { CloseDialog(); return; }
        if (vk == VK_TAB) { FocusNext(shift); return; }
        if (vk == VK_RETURN) {
            Field* f = FocusedField();
            if (m_dlg.kind == DlgKind::Entry && f && f->multiline && !ctrl) {   // im Notizfeld ist Enter ein Zeilenumbruch
                f->ed.Insert(L"\n");
                ResetCaret();
                Invalidate();
                return;
            }
            if (m_dlg.kind == DlgKind::Entry) ApplyEntry();
            else if (m_dlg.kind == DlgKind::ChangePw) ApplyChangePw();
            else if (m_dlg.kind == DlgKind::Import) ApplyImport();
            else {   // Loeschen bestaetigen
                const std::wstring id = m_dlg.editId;
                m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [&](vault::Entry& x) { if (x.id == id) { vault::Wipe(x); return true; } return false; }), m_entries.end());
                m_selId.clear();
                Persist();
                CloseDialog();
                Rebuild();
                SetNote(L"Eintrag gelöscht");
                Invalidate();
            }
            return;
        }
        if (Field* f = FocusedField()) FieldKey(*f, vk);
        return;
    }
    // Suchfeld
    if (m_focusId == F_SEARCH) {
        if (vk == VK_ESCAPE) { m_search.ed.SetText(L""); Rebuild(); m_focusId = kNoFocus; Invalidate(); }
        else if (vk == VK_DOWN || vk == VK_RETURN) { m_focusId = kNoFocus; if (!m_view.empty() && m_selId.empty()) m_selId = m_entries[m_view[0]].id; Invalidate(); }
        else FieldKey(m_search, vk);
        return;
    }
    if (ctrl) {
        switch (vk) {
            case 'F': m_focusId = F_SEARCH; m_search.ed.SelectAll(); ResetCaret(); Invalidate(); return;
            case 'N': BeginEntry(nullptr); return;
            case 'L': Lock(false); return;
            case 'C':
                if (const vault::Entry* e = Sel(); e && !e->secret.empty()) CopyText(e->secret, true, vault::SecretWord(e->cat));
                return;
        }
    }
    int cur = -1;
    for (int i = 0; i < (int)m_view.size(); ++i)
        if (m_entries[m_view[i]].id == m_selId) cur = i;
    auto move = [&](int to) {
        if (m_view.empty()) return;
        to = std::clamp(to, 0, (int)m_view.size() - 1);
        m_selId = m_entries[m_view[to]].id;
        m_reveal = false;
        EnsureRowVisible(to);
        Invalidate();
    };
    switch (vk) {
        case VK_UP: move(cur < 0 ? 0 : cur - 1); break;
        case VK_DOWN: move(cur < 0 ? 0 : cur + 1); break;
        case VK_HOME: move(0); break;
        case VK_END: move((int)m_view.size() - 1); break;
        case VK_F2: case VK_RETURN: if (const vault::Entry* e = Sel()) BeginEntry(e); break;
        case VK_DELETE: BeginConfirmDelete(); break;
        case VK_ESCAPE: m_selId.clear(); m_note.clear(); Invalidate(); break;
    }
}

// ============================================================================
// Fensterprozedur
// ============================================================================

LRESULT CALLBACK App::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->m_hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

LRESULT App::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    auto hit = [&](float x, float y) {
        int id = 0;
        for (const auto& b : m_hits) {
            if (m_dlg.kind != DlgKind::None && b.id < D_OK) continue;   // Knoepfe unter dem Dialog sind gesperrt
            if (In(b.rc, x, y)) id = b.id;
        }
        return id;
    };
    auto command = [&](int id) {
        switch (id) {
            case B_NEW: BeginEntry(nullptr); break;
            case B_GEAR: ShowGearMenu(); break;
            case B_LOCK: Lock(false); break;
            case DT_COPYUSER: if (const vault::Entry* e = Sel()) CopyText(e->user, false, vault::SchemaOf(e->cat).user); break;
            case DT_COPYSECRET: if (const vault::Entry* e = Sel()) CopyText(e->secret, true, vault::SecretWord(e->cat)); break;
            case DT_SHOW: m_reveal = !m_reveal; break;
            case DT_OPENURL: if (const vault::Entry* e = Sel()) OpenUrl(e->url); break;
            case DT_LOGIN: ShowLoginMenu(); break;
            case DT_EDIT: if (const vault::Entry* e = Sel()) BeginEntry(e); break;
            case DT_DELETE: BeginConfirmDelete(); break;
            case L_UNLOCK: TryUnlock(); break;
            case L_REVEAL: m_lockPw.mask = !m_lockPw.mask; break;
            case S_CREATE: CreateVault(); break;
            case S_SKIP: CreateVaultNoPw(); break;
            case D_CANCEL: CloseDialog(); break;
            case D_OK: {
                if (m_dlg.kind == DlgKind::Entry) ApplyEntry();
                else if (m_dlg.kind == DlgKind::ChangePw) ApplyChangePw();
                else if (m_dlg.kind == DlgKind::Import) ApplyImport();
                else KeyDown(VK_RETURN);   // Loeschen bestaetigen: gleicher Weg wie Enter
                break;
            }
            case D_REVEAL: m_dlg.reveal = !m_dlg.reveal; break;
            case D_GENTOGGLE: m_dlg.genOpen = !m_dlg.genOpen; if (m_dlg.genOpen) GenerateInto(); break;
            case D_GENRUN: GenerateInto(); break;
            case D_LENMINUS: m_settings.gen.length = std::max(vault::kGenMin, m_settings.gen.length - 1); GenerateInto(); break;
            case D_LENPLUS: m_settings.gen.length = std::min(vault::kGenMax, m_settings.gen.length + 1); GenerateInto(); break;
            case D_GU: m_settings.gen.upper = !m_settings.gen.upper; GenerateInto(); break;
            case D_GL: m_settings.gen.lower = !m_settings.gen.lower; GenerateInto(); break;
            case D_GD: m_settings.gen.digits = !m_settings.gen.digits; GenerateInto(); break;
            case D_GS: m_settings.gen.symbols = !m_settings.gen.symbols; GenerateInto(); break;
            default:
                if (id >= C_CAT0 && id < C_CAT0 + 1 + vault::kCatCount) {
                    m_cat = id - C_CAT0 - 1;
                    m_scroll = 0;
                    Rebuild();
                } else if (id >= D_CAT0 && id < D_CAT0 + vault::kCatCount) {
                    m_dlg.cat = static_cast<vault::Cat>(id - D_CAT0);
                    if (const Field* f = FocusedField(); f && f->hidden) m_focusId = F_TITLE;
                }
        }
        Invalidate();
    };

    switch (msg) {
        case WM_SIZE:
            if (rt) rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
            Invalidate();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(m_hwnd, &ps);
            Paint();
            EndPaint(m_hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize = {(LONG)(1000 * k), (LONG)(600 * k)};
            return 0;
        }
        case WM_DPICHANGED: {
            k = HIWORD(wp) / 96.f;
            auto* r = reinterpret_cast<RECT*>(lp);
            SetWindowPos(m_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            CreateFormats();
            DiscardDevice();
            Invalidate();
            return 0;
        }
        case WM_TIMER:
            if (wp == TIMER_CARET && m_focusId != kNoFocus) { m_caretOn = !m_caretOn; Invalidate(); }
            else if (wp == TIMER_SEC) {
                const ULONGLONG now = GetTickCount64();
                if (m_clipUntil && now >= m_clipUntil) ClearClipboardIfOurs();
                if (m_autoAt && now >= m_autoAt) RunAutoType();
                if (m_noteUntil && now >= m_noteUntil) { m_note.clear(); m_noteUntil = 0; }
                if (m_screen == Screen::Main && m_settings.autoLock && now - m_lastInput >= kAutoLockMs) Lock(true);
                if (m_screen == Screen::Main) Invalidate();
            }
            return 0;
        case WM_SETCURSOR: {
            if (LOWORD(lp) != HTCLIENT) break;
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(m_hwnd, &pt);
            bool text = false;
            for (Field* f : ActiveFields())
                if (!f->hidden && In(f->rc, (float)pt.x, (float)pt.y)) text = true;
            const bool hand = hit((float)pt.x, (float)pt.y) != 0 || (m_dlg.kind == DlgKind::None && RowAt((float)pt.x, (float)pt.y) >= 0);
            SetCursor(LoadCursor(nullptr, text ? IDC_IBEAM : (hand ? IDC_HAND : IDC_ARROW)));
            return TRUE;
        }
        case WM_MOUSEMOVE: {
            Touch();
            const float x = (float)GET_X_LPARAM(lp), y = (float)GET_Y_LPARAM(lp);
            if (!m_tracking) {
                TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, m_hwnd, 0};
                TrackMouseEvent(&t);
                m_tracking = true;
            }
            if (m_dragField != kNoFocus) {
                if (Field* f = FieldById(m_dragField)) { FieldMouse(*f, x, y, true); Invalidate(); }
                return 0;
            }
            const int hb = hit(x, y);
            const int hr = m_dlg.kind == DlgKind::None ? RowAt(x, y) : -1;
            if (hb != m_hoverBtn || hr != m_hoverRow) {
                m_hoverBtn = hb;
                m_hoverRow = hr;
                Invalidate();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            m_tracking = false;
            m_hoverBtn = 0;
            m_hoverRow = -1;
            Invalidate();
            return 0;
        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONDOWN: {
            Touch();
            const float x = (float)GET_X_LPARAM(lp), y = (float)GET_Y_LPARAM(lp);
            const bool shift = GetKeyState(VK_SHIFT) < 0;
            SetFocus(m_hwnd);
            if (const int id = hit(x, y)) { command(id); return 0; }
            for (Field* f : ActiveFields())
                if (!f->hidden && In(f->rc, x, y)) {
                    m_focusId = f->id;
                    FieldMouse(*f, x, y, shift);
                    if (msg == WM_LBUTTONDBLCLK) f->ed.SelectAll();
                    m_dragField = f->id;
                    SetCapture(m_hwnd);
                    Invalidate();
                    return 0;
                }
            if (m_dlg.kind != DlgKind::None || m_screen != Screen::Main) return 0;
            m_focusId = kNoFocus;
            if (const int r = RowAt(x, y); r >= 0) {
                m_selId = m_entries[m_view[r]].id;
                m_reveal = false;
            }
            Invalidate();
            return 0;
        }
        case WM_LBUTTONUP:
            if (m_dragField != kNoFocus) { m_dragField = kNoFocus; ReleaseCapture(); }
            return 0;
        case WM_MOUSEWHEEL:
            Touch();
            if (m_screen == Screen::Main && m_dlg.kind == DlgKind::None) {
                m_scroll -= GET_WHEEL_DELTA_WPARAM(wp) / 120.f * 3 * kRowH * k;
                ClampScroll();
                Invalidate();
            }
            return 0;
        case WM_KEYDOWN:
            KeyDown(wp);
            return 0;
        case WM_CHAR: {
            Touch();
            Field* f = FocusedField();
            if (!f) return 0;
            wchar_t ch = (wchar_t)wp;
            if (ch < 0x20) return 0;
            std::wstring s(1, ch);
            if (ch >= 0xD800 && ch <= 0xDBFF) { m_hiSurrogate = ch; return 0; }
            if (ch >= 0xDC00 && ch <= 0xDFFF) { if (!m_hiSurrogate) return 0; s = std::wstring(1, m_hiSurrogate) + ch; m_hiSurrogate = 0; }
            f->ed.Insert(s);
            ResetCaret();
            if (f->id == F_SEARCH) { m_scroll = 0; Rebuild(); }
            Invalidate();
            return 0;
        }
        case WM_CLOSE:
            ClearClipboardIfOurs();
            vault::Wipe(m_entries);
            m_session.Close();
            DestroyWindow(m_hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(m_hwnd, TIMER_CARET);
            KillTimer(m_hwnd, TIMER_SEC);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // Dunkle Kontextmenues (undokumentiert: uxtheme-Ordinal 135 = SetPreferredAppMode, 136 = FlushMenuThemes)
    if (HMODULE ux = LoadLibraryW(L"uxtheme.dll")) {
        using SetMode = int(WINAPI*)(int);
        using Flush = void(WINAPI*)();
        if (auto f = reinterpret_cast<SetMode>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(135))))) f(2);
        if (auto f = reinterpret_cast<Flush>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(136))))) f();
    }
    int code = 0;
    {
        App app;
        if (app.Create(hi, show)) {
            MSG m;
            while (GetMessageW(&m, nullptr, 0, 0) > 0) {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
            code = (int)m.wParam;
        } else {
            code = 1;
        }
    }
    CoUninitialize();
    return code;
}
