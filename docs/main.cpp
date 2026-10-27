// Oggys Docs-Files (Win32 + Direct2D + DirectWrite): Rechnungs-Manager und Rechnungs-Generator.
//
// Aufbau: Topbar (Suche, Hinzufuegen, Rechnung erstellen, Zahnrad) | Ordnerbaum Jahr/Glaeubiger | Rechnungsliste | Statusleiste.
// Belege liegen unter <Wurzel>\<Jahr>\<Glaeubiger>\, Betrag/Faelligkeit/Status/Schlagworte stehen im Index
// (.oggys-index.tsv in der Wurzel). PDFs lassen sich per Drag & Drop ins Fenster ziehen. Wie im Photo Commander wird alles selbst
// gezeichnet (kein Common Controls); die Textfelder (Suche, Formulare) sind eigene Felder auf Basis von TextEdit.h.
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Invoice.h"
#include "Store.h"
#include "TextEdit.h"
#include "Theme.h"
#include "../common/AboutCard.h"

#ifndef APP_VERSION
#define APP_VERSION L"0.4.1"   // normalerweise aus CMake (DOCS_VER)
#endif

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace {

constexpr UINT_PTR TIMER_CARET = 1;
constexpr int kSearchId = 1000;     // Feld-Id der Suchleiste
constexpr int kNoFocus = -1;        // kein Textfeld aktiv (Feld-Id 0 ist ein gueltiges Formularfeld)
constexpr int kMaxItemRows = 10;    // Positionen im Rechnungsformular

enum Id {
    B_ADD = 1, B_NEW, B_GEAR,
    T_ADD = 4, T_CLEAR = 5,         // Tag-Leiste: "+ Tags" und "Filter loeschen"
    T_TAG0 = 6,                     // 6..29 Tag-Chips (Filter); Ids unter D_OK sind bei offenem Dialog gesperrt
    D_OK = 30, D_CANCEL, D_SKIP, D_ADDROW, D_DELROW,
    D_STATUS0 = 40,                 // 40..43 Status-Auswahl im Formular
    D_CRED0 = 50,                   // 50..57 Glaeubiger-Vorschlaege
    D_SAVESENDER = 60,              // Rechnungsdialog: "Als Standard-Absender speichern"
    D_SMALLBIZ = 61,                // Rechnungsdialog: "Kleinunternehmer nach § 19 UStG"
    D_PAGE0 = 62, D_PAGE1 = 63,     // Rechnungsdialog: Seite "Rechnung" / "Absender & Konto"
    D_EXAMPLE = 64, D_CLEAR = 65,   // Rechnungsdialog: "Beispiel laden" / "Felder leeren"
    D_TAGSUG0 = 70                  // 70..77 Tag-Vorschlaege im Tag-Dialog
};
constexpr int kMaxTagChips = 24;
enum MenuId {
    M_ROOT = 100, M_OPENROOT, M_MOVE, M_RESCAN, M_ABOUT, M_PDFREADER = 105, M_AUTOIMPORT,
    C_OPEN = 120, C_SHOW, C_STATUS0 = 130, C_EDIT = 140, C_DELETE, C_INCOME, C_EXPENSE, C_TAGS
};

struct Settings {
    std::wstring root;                  // Rechnungsordner
    bool moveFiles = false;             // Hinzufuegen: Original verschieben statt kopieren
    int sortCol = 3;                    // 0 Name, 1 Glaeubiger, 2 Betrag, 3 Faelligkeit, 4 Status
    bool sortDesc = false;
    std::wstring issuer, issuerAddr, payNote;   // zuletzt benutzte Absenderdaten fuer den Rechnungs-Generator
    int vat = 19;
    std::wstring taxNo;                 // Steuernummer / USt-IdNr. des Absenders
    bool smallBiz = false;              // Kleinunternehmer nach § 19 UStG (keine USt auf der Rechnung)
    std::wstring owner, court, phone, email, web, bank, iban, bic;   // Absender-Profil fuer die 3-spaltige PDF-Fusszeile
    bool autoImport = true;             // Ablegen per Drag-and-Drop: Jahr/Glaeubiger automatisch bestimmen und einsortieren (Umschalt = Dialog)
    bool pdfReader = true;              // PDFs im Duncan's PDF Reader oeffnen (falls installiert), sonst Standardprogramm
};

struct Field {
    int id = 0;
    std::wstring label, hint;
    docs::TextEdit ed;
    D2D1_RECT_F rc{};
    float scroll = 0;
    bool dateMask = false;     // Datumsfeld: Punkte beim Tippen automatisch setzen (docs::MaskDate)
    bool hidden = false;       // nicht sichtbar (z. B. Ratenfeld bei anderem Status): kein Fokus, keine Anzeige
    bool disabled = false;     // ausgegraut (z. B. USt-Satz bei Kleinunternehmern): kein Fokus, nicht editierbar
    size_t prevLen = 0;        // Textlaenge vor der letzten Aenderung (unterscheidet Tippen von Loeschen)
};

struct HitBtn { int id; D2D1_RECT_F rc; };

struct Node {
    std::wstring label, year, creditor;   // year leer = alle; creditor leer = alle des Jahres
    int count = 0, depth = 0;
};

enum class DlgKind { None, Add, Edit, Invoice, Tags };

struct Dialog {
    DlgKind kind = DlgKind::None;
    std::wstring title, subtitle, error;
    std::vector<Field> fields;
    docs::Status status = docs::Status::Open;
    std::vector<std::wstring> queue;      // Add: Dateien, die der Reihe nach erfasst werden
    size_t queueIdx = 0;
    std::wstring editRel;                 // Edit: relativer Pfad
    int itemRows = 3;
    bool saveSender = true;               // Rechnung: Absender, Adresse, Steuernummer, Zahlungshinweis und USt-Satz als Standard merken
    bool smallBiz = false;                // Rechnung: Kleinunternehmer nach § 19 UStG
    int page = 0;                         // Rechnung: 0 = Rechnung, 1 = Absender & Konto (Fusszeile)
    bool exampleLoaded = false;           // Rechnung: Musterdaten geladen (nichts davon wird als Standard gespeichert)
    std::vector<std::wstring> creditorChips;
    D2D1_RECT_F box{}, totals{};
};

inline bool In(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
inline float W(const D2D1_RECT_F& r) { return r.right - r.left; }
inline float H(const D2D1_RECT_F& r) { return r.bottom - r.top; }
inline D2D1_RECT_F Inset(D2D1_RECT_F r, float dx, float dy) { return {r.left + dx, r.top + dy, r.right - dx, r.bottom - dy}; }

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring s;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) s = p;
    CoTaskMemFree(p);
    return s;
}

std::wstring TodayIso() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return docs::IsoFrom(st.wYear, st.wMonth, st.wDay);
}

std::wstring SettingsPath() {
    std::wstring dir = KnownFolder(FOLDERID_LocalAppData);
    if (dir.empty()) return L"";
    dir += L"\\OggysDocsFiles";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.ini";
}

std::wstring IniString(const std::wstring& ini, const wchar_t* key) {
    wchar_t buf[2048]{};
    GetPrivateProfileStringW(L"Allgemein", key, L"", buf, 2048, ini.c_str());
    return buf;
}

void LoadSettings(Settings& st) {
    const std::wstring ini = SettingsPath();
    if (ini.empty()) return;
    st.root = IniString(ini, L"Rechnungsordner");
    st.moveFiles = GetPrivateProfileIntW(L"Allgemein", L"Verschieben", 0, ini.c_str()) != 0;
    st.sortCol = std::clamp((int)GetPrivateProfileIntW(L"Allgemein", L"Sortierung", 3, ini.c_str()), 0, 4);
    st.sortDesc = GetPrivateProfileIntW(L"Allgemein", L"Absteigend", 0, ini.c_str()) != 0;
    st.issuer = IniString(ini, L"AbsenderName");
    st.issuerAddr = IniString(ini, L"AbsenderAdresse");
    st.payNote = IniString(ini, L"Zahlungshinweis");
    st.vat = std::clamp((int)GetPrivateProfileIntW(L"Allgemein", L"MwSt", 19, ini.c_str()), 0, 30);
    st.pdfReader = GetPrivateProfileIntW(L"Allgemein", L"PdfReader", 1, ini.c_str()) != 0;
    st.autoImport = GetPrivateProfileIntW(L"Allgemein", L"AutoImport", 1, ini.c_str()) != 0;
    st.taxNo = IniString(ini, L"Steuernummer");
    st.owner = IniString(ini, L"Inhaber");
    st.court = IniString(ini, L"Registergericht");
    st.phone = IniString(ini, L"Telefon");
    st.email = IniString(ini, L"EMail");
    st.web = IniString(ini, L"Webseite");
    st.bank = IniString(ini, L"Bank");
    st.iban = IniString(ini, L"IBAN");
    st.bic = IniString(ini, L"BIC");
    st.smallBiz = GetPrivateProfileIntW(L"Allgemein", L"Kleinunternehmer", 0, ini.c_str()) != 0;
}

// Fortlaufender Rechnungszaehler je Jahr (zuletzt vergebene Nummer); wird nie zurueckgesetzt, damit keine Nummer doppelt vergeben wird
int InvoiceCounter(int year) {
    const std::wstring ini = SettingsPath();
    if (ini.empty()) return 0;
    return (int)GetPrivateProfileIntW(L"Rechnungsnummern", (L"Jahr" + std::to_wstring(year)).c_str(), 0, ini.c_str());
}

void SetInvoiceCounter(int year, int n) {
    const std::wstring ini = SettingsPath();
    if (ini.empty()) return;
    WritePrivateProfileStringW(L"Rechnungsnummern", (L"Jahr" + std::to_wstring(year)).c_str(), std::to_wstring(n).c_str(), ini.c_str());
}

void SaveSettings(const Settings& st) {
    const std::wstring ini = SettingsPath();
    if (ini.empty()) return;
    auto put = [&](const wchar_t* k, const std::wstring& v) { WritePrivateProfileStringW(L"Allgemein", k, v.c_str(), ini.c_str()); };
    put(L"Rechnungsordner", st.root);
    put(L"Verschieben", st.moveFiles ? L"1" : L"0");
    put(L"Sortierung", std::to_wstring(st.sortCol));
    put(L"Absteigend", st.sortDesc ? L"1" : L"0");
    put(L"AbsenderName", st.issuer);
    put(L"AbsenderAdresse", st.issuerAddr);
    put(L"Zahlungshinweis", st.payNote);
    put(L"MwSt", std::to_wstring(st.vat));
    put(L"PdfReader", st.pdfReader ? L"1" : L"0");
    put(L"AutoImport", st.autoImport ? L"1" : L"0");
    put(L"Steuernummer", st.taxNo);
    put(L"Inhaber", st.owner);
    put(L"Registergericht", st.court);
    put(L"Telefon", st.phone);
    put(L"EMail", st.email);
    put(L"Webseite", st.web);
    put(L"Bank", st.bank);
    put(L"IBAN", st.iban);
    put(L"BIC", st.bic);
    put(L"Kleinunternehmer", st.smallBiz ? L"1" : L"0");
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

void SetClipboardText(HWND hwnd, const std::wstring& s) {
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
    CloseClipboard();
}

// Duncan's PDF Reader: zuerst neben dieser EXE (Installer-Ordner), sonst die per Suite geladene Kopie in %LOCALAPPDATA%\\DuncansPdf.
std::wstring FindPdfReader() {
    wchar_t self[MAX_PATH * 2]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH * 2);
    std::vector<fs::path> candidates;
    candidates.push_back(fs::path(self).parent_path() / L"DuncansPdf.exe");
    const std::wstring local = KnownFolder(FOLDERID_LocalAppData);
    if (!local.empty()) candidates.push_back(fs::path(local) / L"DuncansPdf" / L"DuncansPdf.exe");
    std::error_code ec;
    for (const auto& c : candidates)
        if (fs::is_regular_file(c, ec)) return c.wstring();
    return L"";
}

class App {
public:
    bool Create(HINSTANCE hi, int show, const std::vector<std::wstring>& startFiles);
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    // --- Geraet / Zeichnen
    void CreateFormats();
    void EnsureDevice();
    void DiscardDevice();
    void Layout();
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
    float DrawBadge(float x, float y, float h, docs::Status s);   // Rueckgabe: Breite
    void DrawScrollbar(const D2D1_RECT_F& area, float scroll, float content);
    void DrawTop();
    void DrawTree();
    void DrawList();
    void DrawStats();
    void DrawTags();
    void DrawStatusBar();
    void DrawField(Field& f, bool focused);
    void DrawDialog();
    void DrawAddEdit();
    void DrawInvoice();
    void DrawTagsDialog();
    float HeadOff() const;   // Hoehe von Statistik-Leiste, Tag-Leiste und Spaltenkopf

    // --- Daten
    void InitRoot();
    std::wstring IndexPath() const { return (fs::path(m_settings.root) / L".oggys-index.tsv").wstring(); }
    void LoadIndex();
    void SaveIndex();
    void Refresh();
    void BuildTree();
    void RebuildView();
    const docs::Doc* FindDoc(const std::wstring& rel) const;
    docs::Doc* FindDocMut(const std::wstring& rel);
    std::vector<const docs::Doc*> SelectedDocs() const;
    fs::path AbsPath(const std::wstring& rel) const { return fs::path(m_settings.root) / rel; }

    // --- Aktionen
    void AddFiles(std::vector<std::wstring> files, bool autoMode = false);
    void ImportAuto(const std::vector<std::wstring>& files);   // ohne Dialog: Jahr/Glaeubiger raten, kopieren/verschieben, indexieren
    void BeginTagsDialog();
    void ApplyTags();
    void SetKindOfSelected(bool income);
    void PickFilesToAdd();
    void BeginAddDialog();
    void ApplyAdd(bool skipOnly);
    void BeginEditDialog(const docs::Doc& d);
    void ApplyEdit();
    void BeginInvoiceDialog(int mode = 0);   // 0 = aus den Einstellungen, 1 = Beispieldaten, 2 = leere Maske
    void ApplyInvoice();
    void CloseDialog();
    void SetStatusOfSelected(docs::Status s);
    void TrashSelected();
    void OpenDoc(const docs::Doc& d);
    void OpenFile(const fs::path& file);        // PDFs im PDF Reader, alles andere (und Rueckfall) im Standardprogramm
    void ShowInExplorer(const docs::Doc& d);
    void ChangeRoot();
    void ShowGearMenu();
    void ShowContextMenu(int x, int y);
    void OnDropFiles(HDROP drop);
    void SetNote(const std::wstring& s) { m_note = s; }
    void Invalidate() { InvalidateRect(m_hwnd, nullptr, FALSE); }
    std::wstring ViewTitle() const;

    // --- Eingabe
    Field* FocusedField();
    Field* FieldById(int id);
    void FieldKey(Field& f, WPARAM vk);
    void FieldMouse(Field& f, float x, bool extend);
    void OnFieldChanged(Field& f);
    void OnKey(WPARAM vk);
    void DialogKey(WPARAM vk);
    void FocusNext(bool back);
    void MoveSelection(int delta, bool extend);
    void EnsureRowVisible(int viewIdx);
    void ClampScroll();
    int RowAt(float x, float y) const;
    int HeaderColAt(float x, float y) const;
    int NodeAt(float x, float y) const;
    void ResetCaret() { m_caretOn = true; }

    HWND m_hwnd = nullptr;
    float k = 1.f;
    ComPtr<ID2D1Factory> d2d;
    ComPtr<IDWriteFactory> dw;
    ComPtr<ID2D1HwndRenderTarget> rt;
    ComPtr<ID2D1SolidColorBrush> brush;
    aboutcard::State m_about;             // "Ueber"-Kachelansicht im Fenster (statt MessageBox)
    ComPtr<IDWriteTextFormat> fNormal, fSmall, fBold, fTitle, fTitleR, fLogo, fCenter, fRight, fBadge;

    D2D1_RECT_F rcTop{}, rcTree{}, rcList{}, rcStats{}, rcTags{}, rcHead{}, rcStatus{}, rcSearch{};
    std::vector<HitBtn> m_hits;
    int m_hoverBtn = 0, m_hoverRow = -1, m_hoverNode = -1;
    bool m_tracking = false;

    Settings m_settings;
    fs::path m_rootPath;
    docs::MetaMap m_meta;
    std::vector<docs::Doc> m_all;
    std::vector<int> m_view;                 // Indizes in m_all nach Filter und Sortierung
    std::vector<Node> m_nodes;
    int m_node = 0;
    float m_treeScroll = 0, m_scroll = 0;
    std::set<std::wstring> m_sel;            // markierte Dateien (relative Pfade)
    std::wstring m_focusRel, m_anchorRel;

    Field m_search;
    int m_focusId = kNoFocus;                // Id des aktiven Textfelds oder kNoFocus
    bool m_caretOn = true;
    int m_dragField = kNoFocus;              // Feld, in dem gerade mit der Maus markiert wird
    int m_dlgAdded = 0;                      // Hinzufuegen-Dialog: bisher uebernommene Dateien
    wchar_t m_hiSurrogate = 0;

    Dialog m_dlg;
    std::wstring m_note;
    long long m_openCents = 0;
    int m_overdue = 0;
    docs::Stats m_stats;                                  // Finanz-Uebersicht ueber alle Belege (unabhaengig vom Filter)
    std::vector<std::pair<std::wstring, int>> m_tags;     // alle Tags mit Anzahl
    std::set<std::wstring> m_tagFilter;                   // aktive Tag-Filter (klein geschrieben, alle muessen passen)
};

constexpr float kTopH = 60, kStatusH = 28, kTreeW = 270, kHeadH = 38, kRowH = 46, kNodeH = 32, kStatsH = 68, kTagsH = 42;

float App::HeadOff() const { return (kStatsH + kTagsH + kHeadH) * k; }

// ============================================================================
// Setup
// ============================================================================

bool App::Create(HINSTANCE hi, int show, const std::vector<std::wstring>& startFiles) {
    LoadSettings(m_settings);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"OggysDocsFiles";
    wc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(1));
    if (!wc.hIcon) wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);
    m_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Oggys Docs-Files", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1400, 860,
                             nullptr, nullptr, hi, this);
    if (!m_hwnd) return false;
    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hwnd, 20, &dark, sizeof(dark));
    k = GetDpiForWindow(m_hwnd) / 96.f;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf()))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dw.GetAddressOf()))))
        return false;
    CreateFormats();
    m_search.id = kSearchId;
    m_search.hint = L"Suchen: Gläubiger, Dateiname, Tags …";
    m_search.ed.maxLen = 80;
    InitRoot();
    SetWindowPos(m_hwnd, nullptr, 0, 0, (int)(1400 * k), (int)(860 * k), SWP_NOMOVE | SWP_NOZORDER);
    ShowWindow(m_hwnd, show);
    UpdateWindow(m_hwnd);
    DragAcceptFiles(m_hwnd, TRUE);
    SetTimer(m_hwnd, TIMER_CARET, 530, nullptr);
    if (!startFiles.empty()) AddFiles(startFiles);
    return true;
}

void App::CreateFormats() {
    auto make = [&](ComPtr<IDWriteTextFormat>& f, float size, DWRITE_FONT_WEIGHT w, DWRITE_TEXT_ALIGNMENT align) {
        f.Reset();
        dw->CreateTextFormat(L"Segoe UI", nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size * k, L"de-de", &f);
        f->SetTextAlignment(align);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        ComPtr<IDWriteInlineObject> ell;
        dw->CreateEllipsisTrimmingSign(f.Get(), &ell);
        f->SetTrimming(&trim, ell.Get());
    };
    make(fNormal, 13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING);
    make(fSmall, 11, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING);
    make(fBold, 13, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING);
    make(fTitle, 17, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING);
    make(fTitleR, 17, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_TRAILING);
    make(fLogo, 18, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    make(fCenter, 13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    make(fRight, 13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
    make(fBadge, 11, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
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

void App::DiscardDevice() {
    brush.Reset();
    rt.Reset();
}

void App::Layout() {
    RECT cr;
    GetClientRect(m_hwnd, &cr);
    const float Wd = (float)cr.right, Ht = (float)cr.bottom;
    rcTop = {0, 0, Wd, kTopH * k};
    rcStatus = {0, Ht - kStatusH * k, Wd, Ht};
    rcTree = {0, rcTop.bottom, kTreeW * k, rcStatus.top};
    rcList = {rcTree.right, rcTop.bottom, Wd, rcStatus.top};
    rcStats = {rcList.left, rcList.top, rcList.right, rcList.top + kStatsH * k};
    rcTags = {rcList.left, rcStats.bottom, rcList.right, rcStats.bottom + kTagsH * k};
    rcHead = {rcList.left, rcTags.bottom, rcList.right, rcTags.bottom + kHeadH * k};
    ClampScroll();
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
    Layout();
    m_hits.clear();
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(Theme::Bg));
    DrawList();
    DrawTree();
    DrawTop();
    DrawStatusBar();
    if (m_dlg.kind != DlgKind::None) DrawDialog();
    if (m_about.open) {
        RECT acr;
        GetClientRect(m_hwnd, &acr);
        aboutcard::Spec sp;
        sp.name = L"Oggys Docs-Files";
        sp.version = APP_VERSION;
        sp.letter = L'O';
        sp.accent = Theme::Accent;
        sp.tiles = {
            {L"Version", APP_VERSION}, {L"Entwickler", L"Duncan  ·  © 2026"},
            {L"Quellcode", L"github.com/Neox365/Duncans-Suite"}, {L"Teil von", L"Duncan's Suite"},
            {L"Rechnungsordner", m_settings.root, true},
            {L"Einstellungen", L"%LOCALAPPDATA%\\OggysDocsFiles\\settings.ini", true},
            {L"Datenschutz", L"Alle Daten werden zu 100 % lokal auf deinem Gerät gespeichert. Es erfolgt keine Übertragung personenbezogener Daten an externe Server.", true},
        };
        aboutcard::Draw(rt.Get(), brush.Get(), dw.Get(), k, acr, sp, m_about);
    }
    if (rt->EndDraw() == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) {
        DiscardDevice();
        Invalidate();
    }
}

void App::Button(int id, const D2D1_RECT_F& r, const std::wstring& label, bool accent, bool danger) {
    const bool hover = m_hoverBtn == id;
    const unsigned bg = accent ? Theme::Accent : (hover ? Theme::CardHover : Theme::Card);
    FillRR(r, 8 * k, bg);
    if (accent && hover) FillRR(r, 8 * k, 0xFFFFFF, 0.12f);
    Txt(label, r, fCenter.Get(), accent ? 0xFFFFFF : (danger ? Theme::Danger : Theme::Text));
    m_hits.push_back({id, r});
}

float App::DrawBadge(float x, float y, float h, docs::Status s) {
    unsigned c = Theme::Open;
    switch (s) {
        case docs::Status::Paid: c = Theme::Paid; break;
        case docs::Status::Installment: c = Theme::Installment; break;
        case docs::Status::Fixed: c = Theme::Fixed; break;
        default: break;
    }
    const std::wstring name = docs::StatusName(s);
    const float w = TextW(name, fBadge.Get()) + 30 * k;
    const D2D1_RECT_F r{x, y, x + w, y + h};
    FillRR(r, h / 2, c, 0.16f);
    StrokeRR(r, h / 2, c, 1.f);
    const float d = 6 * k;
    SetCol(c);
    rt->FillEllipse(D2D1::Ellipse({r.left + 12 * k, (r.top + r.bottom) / 2}, d / 2, d / 2), brush.Get());
    Txt(name, {r.left + 20 * k, r.top, r.right - 6 * k, r.bottom}, fBadge.Get(), c);
    return w;
}

void App::DrawScrollbar(const D2D1_RECT_F& area, float scroll, float content) {
    const float view = H(area);
    if (content <= view + 1) return;
    const float th = std::max(30 * k, view * view / content);
    const float ty = area.top + (view - th) * (scroll / (content - view));
    FillRR({area.right - 8 * k, ty, area.right - 3 * k, ty + th}, 3 * k, Theme::TextDim, 0.45f);
}

// ============================================================================
// Daten
// ============================================================================

void App::InitRoot() {
    if (m_settings.root.empty()) {
        const std::wstring docsDir = KnownFolder(FOLDERID_Documents);
        m_settings.root = (fs::path(docsDir.empty() ? L"." : docsDir) / L"Rechnungen").wstring();
    }
    m_rootPath = m_settings.root;
    std::error_code ec;
    fs::create_directories(m_rootPath / TodayIso().substr(0, 4), ec);   // Wurzel und aktuelles Jahr immer anlegen
    SaveSettings(m_settings);
    LoadIndex();
    Refresh();
}

void App::LoadIndex() {
    m_meta.clear();
    std::ifstream f(fs::path(IndexPath()), std::ios::binary);
    if (!f) return;
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    m_meta = docs::ParseIndex(text);
}

void App::SaveIndex() {
    // Schreiben ueber Temp-Datei, damit ein Absturz den Index nie halb geschrieben hinterlaesst
    const std::wstring path = IndexPath(), tmp = path + L".tmp";
    {
        std::ofstream o(fs::path(tmp), std::ios::binary | std::ios::trunc);
        if (!o) { SetNote(L"Index konnte nicht gespeichert werden."); return; }
        const std::string data = docs::SerializeIndex(m_meta);
        o.write(data.data(), (std::streamsize)data.size());
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        SetNote(L"Index konnte nicht gespeichert werden.");
    }
}

void App::Refresh() {
    m_all = docs::Scan(m_rootPath, m_meta);
    // Auswahl auf noch vorhandene Dateien beschraenken
    std::set<std::wstring> keep;
    for (const auto& d : m_all)
        if (m_sel.count(d.rel)) keep.insert(d.rel);
    m_sel.swap(keep);
    BuildTree();
    RebuildView();
}

void App::BuildTree() {
    const Node old = m_node >= 0 && m_node < (int)m_nodes.size() ? m_nodes[m_node] : Node{};
    std::map<std::wstring, std::map<std::wstring, int>, std::greater<std::wstring>> years;   // neueste Jahre zuerst
    for (const auto& d : m_all) years[d.year][d.creditor]++;
    for (const auto& f : docs::ListFolders(m_rootPath)) {   // leere Jahres-/Glaeubiger-Ordner erscheinen ebenfalls
        auto& y = years[f.first];
        if (!f.second.empty()) y[f.second] += 0;
    }
    m_nodes.clear();
    Node all;
    all.label = L"Alle Rechnungen";
    all.count = (int)m_all.size();
    m_nodes.push_back(all);
    for (const auto& y : years) {
        Node yn;
        yn.label = y.first.empty() ? L"Ohne Jahr" : y.first;
        yn.year = y.first.empty() ? L"?" : y.first;     // "?" = Belege direkt in der Wurzel
        yn.depth = 1;
        for (const auto& c : y.second) yn.count += c.second;
        m_nodes.push_back(yn);
        for (const auto& c : y.second) {
            Node cn;
            cn.label = c.first;
            cn.year = yn.year;
            cn.creditor = c.first;
            cn.count = c.second;
            cn.depth = 2;
            m_nodes.push_back(cn);
        }
    }
    m_node = 0;
    for (int i = 0; i < (int)m_nodes.size(); ++i)
        if (m_nodes[i].year == old.year && m_nodes[i].creditor == old.creditor) { m_node = i; break; }
}

void App::RebuildView() {
    const Node& nd = m_nodes[m_node];
    m_view.clear();
    // Tags und Finanz-Uebersicht sind live: jede Aenderung der Belege laeuft durch diese Funktion
    m_tags = docs::CollectTags(m_all);
    for (auto it = m_tagFilter.begin(); it != m_tagFilter.end();) {   // Filter auf Tags, die es nicht mehr gibt, entfernen
        bool found = false;
        for (const auto& t : m_tags) found = found || docs::Lower(t.first) == *it;
        it = found ? std::next(it) : m_tagFilter.erase(it);
    }
    m_stats = docs::ComputeStats(m_all, TodayIso().substr(0, 4), TodayIso());
    for (int i = 0; i < (int)m_all.size(); ++i) {
        const docs::Doc& d = m_all[i];
        const std::wstring y = d.year.empty() ? L"?" : d.year;
        if (!nd.year.empty() && y != nd.year) continue;
        if (!nd.creditor.empty() && d.creditor != nd.creditor) continue;
        if (!docs::Matches(d, m_search.ed.text)) continue;
        if (!docs::MatchesTags(d, m_tagFilter)) continue;
        m_view.push_back(i);
    }
    const int col = m_settings.sortCol;
    const bool desc = m_settings.sortDesc;
    std::stable_sort(m_view.begin(), m_view.end(), [&](int a, int b) {
        const docs::Doc &x = m_all[a], &y = m_all[b];
        int c = 0;
        switch (col) {
            case 0: c = StrCmpLogicalW(x.name.c_str(), y.name.c_str()); break;
            case 1: c = StrCmpLogicalW(x.creditor.c_str(), y.creditor.c_str()); break;
            case 2: {
                const long long ax = x.m.cents == docs::kUnknown ? INT64_MIN : x.m.cents, ay = y.m.cents == docs::kUnknown ? INT64_MIN : y.m.cents;
                c = ax < ay ? -1 : (ax > ay ? 1 : 0);
                break;
            }
            case 3: {   // ohne Faelligkeit immer ans Ende
                if (x.m.due.empty() != y.m.due.empty()) return y.m.due.empty();
                c = x.m.due.compare(y.m.due);
                break;
            }
            default: c = (int)x.m.status - (int)y.m.status; break;
        }
        if (c == 0) c = StrCmpLogicalW(x.name.c_str(), y.name.c_str());
        return desc ? c > 0 : c < 0;
    });
    // Summen der Ansicht: offene Betraege und ueberfaellige Rechnungen
    m_openCents = 0;
    m_overdue = 0;
    const std::wstring today = TodayIso();
    for (int i : m_view) {
        const docs::Doc& d = m_all[i];
        if (d.m.status != docs::Status::Open) continue;
        if (d.m.cents != docs::kUnknown) m_openCents += d.m.cents;
        if (!d.m.due.empty() && d.m.due < today) ++m_overdue;
    }
    ClampScroll();
}

const docs::Doc* App::FindDoc(const std::wstring& rel) const {
    for (const auto& d : m_all)
        if (_wcsicmp(d.rel.c_str(), rel.c_str()) == 0) return &d;
    return nullptr;
}
docs::Doc* App::FindDocMut(const std::wstring& rel) {
    return const_cast<docs::Doc*>(FindDoc(rel));
}

std::vector<const docs::Doc*> App::SelectedDocs() const {
    std::vector<const docs::Doc*> out;
    for (int i : m_view)
        if (m_sel.count(m_all[i].rel)) out.push_back(&m_all[i]);
    return out;
}

std::wstring App::ViewTitle() const {
    const Node& nd = m_nodes[m_node];
    if (nd.year.empty()) return L"Alle Rechnungen";
    return nd.creditor.empty() ? nd.label : nd.year + L" › " + nd.creditor;
}

// ============================================================================
// Zeichnen
// ============================================================================

void App::DrawTop() {
    Fill(rcTop, Theme::Topbar);
    Fill({0, rcTop.bottom - 1, rcTop.right, rcTop.bottom}, Theme::Border);
    FillRR({16 * k, 16 * k, 44 * k, 44 * k}, 8 * k, Theme::Accent);
    Txt(L"O", {16 * k, 16 * k, 44 * k, 44 * k}, fLogo.Get(), 0xFFFFFF);
    Txt(L"Oggys Docs-Files", {54 * k, 0, 250 * k, rcTop.bottom}, fTitle.Get(), Theme::Text);

    // rechte Seite: Zahnrad, "Rechnung erstellen", "Hinzufuegen"
    const float bh = 36 * k, by = (rcTop.bottom - bh) / 2;
    float x = rcTop.right - 16 * k;
    D2D1_RECT_F gear{x - 38 * k, by, x, by + bh};
    Button(B_GEAR, gear, L"⚙", false);
    x = gear.left - 10 * k;
    D2D1_RECT_F nw{x - 168 * k, by, x, by + bh};
    Button(B_NEW, nw, L"Rechnung erstellen", true);
    x = nw.left - 10 * k;
    D2D1_RECT_F add{x - 132 * k, by, x, by + bh};
    Button(B_ADD, add, L"+ Hinzufügen", false);

    // Suchleiste zwischen Titel und Knoepfen
    const float sl = 270 * k, sr = std::max(sl + 160 * k, add.left - 18 * k);
    rcSearch = {sl, by, sr, by + bh};
    m_search.rc = rcSearch;
    DrawField(m_search, m_focusId == kSearchId);
}

void App::DrawTree() {
    Fill(rcTree, Theme::Panel);
    Fill({rcTree.right - 1, rcTree.top, rcTree.right, rcTree.bottom}, Theme::Border);
    rt->PushAxisAlignedClip(rcTree, D2D1_ANTIALIAS_MODE_ALIASED);
    Txt(L"ORDNER", {16 * k, rcTree.top + 6 * k, rcTree.right - 16 * k, rcTree.top + 28 * k}, fSmall.Get(), Theme::TextDim);
    float y = rcTree.top + 32 * k - m_treeScroll;
    for (int i = 0; i < (int)m_nodes.size(); ++i) {
        const Node& n = m_nodes[i];
        const D2D1_RECT_F row{6 * k, y, rcTree.right - 8 * k, y + kNodeH * k};
        if (i == m_node) FillRR(row, 7 * k, Theme::Accent, 0.22f);
        else if (i == m_hoverNode) FillRR(row, 7 * k, Theme::CardHover);
        const float ix = 14 * k + n.depth * 16 * k;
        const unsigned tc = i == m_node ? Theme::Text : (n.depth == 0 ? Theme::Text : Theme::TextDim);
        {   // Ordner-Symbol: Reiter + Koerper (Jahr gelb, Glaeubiger gedaempft, "Alle" in der Akzentfarbe)
            const float cy = y + kNodeH * k / 2, w = (n.depth == 2 ? 13.f : 15.f) * k, h = (n.depth == 2 ? 9.f : 11.f) * k, top = cy - h / 2;
            const unsigned ic = n.depth == 0 ? Theme::Accent : Theme::Installment;
            const float a = n.depth == 2 ? 0.55f : 0.9f;
            FillRR({ix, top - 2 * k, ix + 6 * k, top + 3 * k}, 2 * k, ic, a);
            FillRR({ix, top, ix + w, top + h}, 2.5f * k, ic, a);
        }
        Txt(n.label, {ix + 22 * k, y, rcTree.right - 56 * k, y + kNodeH * k}, n.depth == 1 || n.depth == 0 ? fBold.Get() : fNormal.Get(), tc);
        Txt(std::to_wstring(n.count), {rcTree.right - 56 * k, y, rcTree.right - 16 * k, y + kNodeH * k}, fRight.Get(), Theme::TextDim);
        y += kNodeH * k;
    }
    rt->PopAxisAlignedClip();
}

void App::DrawStats() {
    Fill(rcStats, Theme::Bg);
    const std::wstring year = TodayIso().substr(0, 4);
    const float pad = 18 * k, gap = 10 * k, cw = (W(rcStats) - 2 * pad - 3 * gap) / 4;
    const long long saldo = m_stats.income - m_stats.expense;
    std::wstring openLabel = L"Offene Rechnungen  ·  " + std::to_wstring(m_stats.openCount);
    if (m_stats.overdue > 0) openLabel += L"  (" + std::to_wstring(m_stats.overdue) + L" überfällig)";
    struct Card { std::wstring label, value; unsigned color; };
    const Card cards[4] = {{L"Einnahmen " + year, docs::FormatAmount(m_stats.income), Theme::Paid},
                           {L"Ausgaben " + year, docs::FormatAmount(m_stats.expense), Theme::Open},
                           {L"Saldo " + year, docs::FormatAmount(saldo), saldo >= 0 ? Theme::Paid : Theme::Open},
                           {openLabel, docs::FormatAmount(m_stats.open), m_stats.overdue > 0 ? Theme::Open : Theme::Warn}};
    for (int i = 0; i < 4; ++i) {
        const float x = rcStats.left + pad + i * (cw + gap);
        const D2D1_RECT_F r{x, rcStats.top + 10 * k, x + cw, rcStats.bottom - 6 * k};
        FillRR(r, 10 * k, Theme::Card);
        FillRR({r.left, r.top + 10 * k, r.left + 3 * k, r.bottom - 10 * k}, 1.5f * k, cards[i].color);
        Txt(cards[i].label, {r.left + 16 * k, r.top + 6 * k, r.right - 8 * k, r.top + 26 * k}, fSmall.Get(), Theme::TextDim);
        Txt(cards[i].value, {r.left + 16 * k, r.top + 25 * k, r.right - 8 * k, r.bottom - 2 * k}, fBold.Get(), cards[i].color);
    }
}

void App::DrawTags() {
    Fill(rcTags, Theme::Bg);
    const float pad = 18 * k, h = 26 * k, y = rcTags.top + (H(rcTags) - h) / 2;
    Txt(L"TAGS", {rcList.left + pad, rcTags.top, rcList.left + pad + 48 * k, rcTags.bottom}, fSmall.Get(), Theme::TextDim);
    float right = rcTags.right - pad;
    const D2D1_RECT_F add{right - 92 * k, y, right, y + h};
    Button(T_ADD, add, L"+ Tags", false);
    right = add.left - 8 * k;
    if (!m_tagFilter.empty()) {
        const D2D1_RECT_F clr{right - 92 * k, y, right, y + h};
        Button(T_CLEAR, clr, L"✕ Filter", false, true);
        right = clr.left - 8 * k;
    }
    float x = rcList.left + pad + 52 * k;
    if (m_tags.empty()) {
        Txt(L"Noch keine Tags. Belege markieren und „+ Tags“ wählen.", {x, rcTags.top, right, rcTags.bottom}, fSmall.Get(), Theme::TextDim);
        return;
    }
    for (int i = 0; i < (int)m_tags.size() && i < kMaxTagChips; ++i) {
        const std::wstring label = m_tags[i].first + L"  " + std::to_wstring(m_tags[i].second);
        const float w = TextW(label, fBadge.Get()) + 24 * k;
        if (x + w > right) break;
        const D2D1_RECT_F r{x, y, x + w, y + h};
        const bool on = m_tagFilter.count(docs::Lower(m_tags[i].first)) > 0;
        const bool hover = m_hoverBtn == T_TAG0 + i;
        FillRR(r, h / 2, on ? Theme::Accent : Theme::Card, on ? 0.30f : (hover ? 1.f : 0.8f));
        if (on) StrokeRR(r, h / 2, Theme::Accent, 1.5f);
        Txt(label, r, fBadge.Get(), on ? Theme::Text : Theme::TextDim);
        m_hits.push_back({T_TAG0 + i, r});
        x += w + 8 * k;
    }
}

void App::DrawList() {
    Fill(rcList, Theme::Bg);
    DrawStats();
    DrawTags();
    // Spaltenbreiten: Anteile der Breite abzueglich Rand
    const float pad = 18 * k, total = W(rcList) - 2 * pad;
    const float wName = total * 0.33f, wCred = total * 0.21f, wAmt = total * 0.13f, wDue = total * 0.15f;
    const float xName = rcList.left + pad, xCred = xName + wName, xAmt = xCred + wCred, xDue = xAmt + wAmt, xStat = xDue + wDue;

    // Kopfzeile (sortierbar)
    Fill(rcHead, Theme::Panel);
    Fill({rcHead.left, rcHead.bottom - 1, rcHead.right, rcHead.bottom}, Theme::Border);
    const wchar_t* heads[5] = {L"Dateiname", L"Gläubiger", L"Betrag", L"Fälligkeit", L"Status"};
    const float xs[5] = {xName, xCred, xAmt, xDue, xStat};
    const float ws[5] = {wName, wCred, wAmt - 10 * k, wDue, rcList.right - xStat - pad};
    for (int c = 0; c < 5; ++c) {
        std::wstring h = heads[c];
        if (c == m_settings.sortCol) h += m_settings.sortDesc ? L"  ▾" : L"  ▴";
        Txt(h, {xs[c], rcHead.top, xs[c] + ws[c], rcHead.bottom}, c == 2 ? fRight.Get() : fBold.Get(),
            c == m_settings.sortCol ? Theme::Text : Theme::TextDim);
    }

    // Zeilen
    const D2D1_RECT_F body{rcList.left, rcHead.bottom, rcList.right, rcList.bottom};
    rt->PushAxisAlignedClip(body, D2D1_ANTIALIAS_MODE_ALIASED);
    const float rh = kRowH * k;
    const std::wstring today = TodayIso();
    const int first = std::max(0, (int)std::floor(m_scroll / rh));
    const int last = std::min((int)m_view.size() - 1, (int)std::floor((m_scroll + H(body)) / rh));
    for (int vi = first; vi <= last; ++vi) {
        const docs::Doc& d = m_all[m_view[vi]];
        const float y = body.top + vi * rh - m_scroll;
        const D2D1_RECT_F row{rcList.left + 6 * k, y + 2 * k, rcList.right - 12 * k, y + rh - 2 * k};
        const bool sel = m_sel.count(d.rel) > 0;
        if (sel) { FillRR(row, 8 * k, Theme::Accent, 0.20f); StrokeRR(row, 8 * k, Theme::Accent, 1.f); }
        else if (vi == m_hoverRow) FillRR(row, 8 * k, Theme::CardHover);
        else if (vi % 2 == 1) FillRR(row, 8 * k, Theme::Card, 0.35f);
        Txt(d.name, {xName, y, xName + wName - 8 * k, y + rh}, fNormal.Get(), Theme::Text);
        Txt(d.creditor + (d.year.empty() ? L"" : L"  ·  " + d.year), {xCred, y, xCred + wCred - 8 * k, y + rh}, fNormal.Get(), Theme::TextDim);
        Txt(docs::FormatAmount(d.m.cents), {xAmt, y, xAmt + wAmt - 10 * k, y + rh}, fRight.Get(),
            d.m.cents == docs::kUnknown ? Theme::TextDim : (d.m.income ? Theme::Paid : Theme::Text));
        const bool overdue = d.m.status == docs::Status::Open && !d.m.due.empty() && d.m.due < today;
        std::wstring due = d.m.due.empty() ? L"–" : docs::FormatDate(d.m.due);
        if (overdue) due += L"  überfällig";
        Txt(due, {xDue, y, xDue + wDue, y + rh}, fNormal.Get(), overdue ? Theme::Danger : (d.m.due.empty() ? Theme::TextDim : Theme::Text));
        const float bw = DrawBadge(xStat, y + (rh - 24 * k) / 2, 24 * k, d.m.status);
        if (d.m.status == docs::Status::Installment && !d.m.rate.empty() && xStat + bw + 8 * k < rcList.right - 16 * k)
            Txt(d.m.rate, {xStat + bw + 8 * k, y, rcList.right - 16 * k, y + rh}, fSmall.Get(), Theme::TextDim);
    }
    if (m_view.empty()) {
        const bool filtered = !m_search.ed.text.empty() || m_node != 0 || !m_tagFilter.empty();
        Txt(m_search.ed.text.empty() && m_tagFilter.empty() && m_node != 0 && !m_all.empty() ? L"In diesem Ordner liegen noch keine Rechnungen."
            : m_all.empty() ? L"Noch keine Rechnungen.\nPDFs hierher ziehen oder „+ Hinzufügen“ wählen."
                          : (filtered ? L"Keine Treffer für diesen Filter." : L""),
            body, fCenter.Get(), Theme::TextDim);
    }
    DrawScrollbar(body, m_scroll, (float)m_view.size() * rh);
    rt->PopAxisAlignedClip();
}

void App::DrawStatusBar() {
    Fill(rcStatus, Theme::Topbar);
    Fill({0, rcStatus.top, rcStatus.right, rcStatus.top + 1}, Theme::Border);
    const std::wstring left = !m_note.empty() ? m_note : (m_settings.root + L"   ·   " + ViewTitle());
    Txt(left, {12 * k, rcStatus.top, rcStatus.right * 0.55f, rcStatus.bottom}, fSmall.Get(), Theme::TextDim);
    std::wstring right = std::to_wstring(m_view.size()) + (m_view.size() == 1 ? L" Rechnung" : L" Rechnungen") + L"   ·   Offen: " +
                         docs::FormatAmount(m_openCents);
    if (m_overdue > 0) right += L"   ·   " + std::to_wstring(m_overdue) + L" überfällig";
    Txt(right, {rcStatus.right * 0.55f, rcStatus.top, rcStatus.right - 12 * k, rcStatus.bottom}, fRight.Get(), m_overdue > 0 ? Theme::Danger : Theme::TextDim);
}

// Textfeld: Rahmen, Hinweistext, Markierung, Cursor; scrollt waagerecht, damit der Cursor sichtbar bleibt
void App::DrawField(Field& f, bool focused) {
    if (f.hidden) return;
    if (f.disabled) {
        FillRR(f.rc, 8 * k, Theme::Panel);
        StrokeRR(f.rc, 8 * k, Theme::Border, 1.f);
        Txt(f.ed.text.empty() ? f.hint : f.ed.text, Inset(f.rc, 11 * k, 0), fNormal.Get(), Theme::TextDim, 0.45f);
        return;
    }
    FillRR(f.rc, 8 * k, Theme::Bg);
    StrokeRR(f.rc, 8 * k, focused ? Theme::Accent : Theme::Border, focused ? 1.5f : 1.f);
    const D2D1_RECT_F in = Inset(f.rc, 11 * k, 0);
    if (f.ed.text.empty()) {
        if (!f.hint.empty()) Txt(f.hint, in, fNormal.Get(), Theme::TextDim, 0.8f);
        if (focused && m_caretOn) Fill({in.left, f.rc.top + 9 * k, in.left + 1.5f * k, f.rc.bottom - 9 * k}, Theme::Text);
        return;
    }
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dw->CreateTextLayout(f.ed.text.c_str(), (UINT32)f.ed.text.size(), fNormal.Get(), 10000.f, H(f.rc), &layout))) return;
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    float cx = 0, cy = 0;
    DWRITE_HIT_TEST_METRICS hm{};
    layout->HitTestTextPosition((UINT32)f.ed.caret, FALSE, &cx, &cy, &hm);
    const float inner = W(in) - 2 * k;
    if (cx - f.scroll > inner) f.scroll = cx - inner;
    if (cx - f.scroll < 0) f.scroll = cx;
    f.scroll = std::max(0.f, f.scroll);
    rt->PushAxisAlignedClip(in, D2D1_ANTIALIAS_MODE_ALIASED);
    if (focused && f.ed.HasSelection()) {
        UINT32 n = 0;
        layout->HitTestTextRange((UINT32)f.ed.SelBegin(), (UINT32)(f.ed.SelEnd() - f.ed.SelBegin()), 0, 0, nullptr, 0, &n);
        std::vector<DWRITE_HIT_TEST_METRICS> ms(n);
        if (n && SUCCEEDED(layout->HitTestTextRange((UINT32)f.ed.SelBegin(), (UINT32)(f.ed.SelEnd() - f.ed.SelBegin()), in.left - f.scroll,
                                                    f.rc.top, ms.data(), n, &n)))
            for (UINT32 i = 0; i < n; ++i)
                Fill({ms[i].left, f.rc.top + 7 * k, ms[i].left + ms[i].width, f.rc.bottom - 7 * k}, Theme::Accent, 0.45f);
    }
    SetCol(Theme::Text);
    rt->DrawTextLayout({in.left - f.scroll, f.rc.top}, layout.Get(), brush.Get());
    if (focused && m_caretOn) Fill({in.left - f.scroll + cx, f.rc.top + 9 * k, in.left - f.scroll + cx + 1.5f * k, f.rc.bottom - 9 * k}, Theme::Text);
    rt->PopAxisAlignedClip();
}

// ============================================================================
// Dialoge
// ============================================================================

void App::DrawDialog() {
    Fill({0, 0, rcStatus.right, rcStatus.bottom}, 0x000000, 0.6f);
    if (m_dlg.kind == DlgKind::Invoice) DrawInvoice();
    else if (m_dlg.kind == DlgKind::Tags) DrawTagsDialog();
    else DrawAddEdit();
}

namespace {
const wchar_t* kStatusLabels[4] = {L"Offen", L"Bezahlt", L"Ratenzahlung", L"Fixkosten"};
const docs::Status kStatusOrder[4] = {docs::Status::Open, docs::Status::Paid, docs::Status::Installment, docs::Status::Fixed};
}  // namespace

void App::DrawAddEdit() {
    Dialog& d = m_dlg;
    const float bw = 600 * k, pad = 24 * k, fh = 38 * k;
    const bool edit = d.kind == DlgKind::Edit;
    float height = pad + 30 * k + 24 * k + (fh + 22 * k) /*Glaeubiger*/ + 36 * k /*Chips*/ + (fh + 22 * k) /*Jahr..*/ + (fh + 22 * k) /*Tags*/ +
                   (edit ? fh + 22 * k : 0) + (d.status == docs::Status::Installment ? fh + 22 * k : 0) /*Rate*/ + 64 * k /*Status*/ + 30 * k /*Fehler*/ + 44 * k + pad;
    RECT cr;
    GetClientRect(m_hwnd, &cr);
    d.box = {(cr.right - bw) / 2, std::max(20.f * k, (cr.bottom - height) / 2), (cr.right + bw) / 2, 0};
    d.box.bottom = d.box.top + height;
    FillRR(d.box, 14 * k, Theme::Panel);
    StrokeRR(d.box, 14 * k, Theme::Border);
    float y = d.box.top + pad;
    Txt(d.title, {d.box.left + pad, y, d.box.right - pad, y + 30 * k}, fTitle.Get(), Theme::Text);
    y += 30 * k;
    Txt(d.subtitle, {d.box.left + pad, y, d.box.right - pad, y + 24 * k}, fSmall.Get(), Theme::TextDim);
    y += 28 * k;
    const float x0 = d.box.left + pad, fw = W(d.box) - 2 * pad;
    auto place = [&](int id, float x, float w) {
        Field* f = FieldById(id);
        if (!f) return;
        Txt(f->label, {x, y, x + w, y + 18 * k}, fSmall.Get(), Theme::TextDim);
        f->rc = {x, y + 20 * k, x + w, y + 20 * k + fh};
    };
    place(0, x0, fw);
    y += 22 * k + fh;
    // Glaeubiger-Vorschlaege (vorhandene Ordner)
    float cx = x0;
    for (int i = 0; i < (int)d.creditorChips.size() && i < 8; ++i) {
        const float cw = TextW(d.creditorChips[i], fSmall.Get()) + 22 * k;
        if (cx + cw > x0 + fw) break;
        const D2D1_RECT_F r{cx, y + 2 * k, cx + cw, y + 26 * k};
        const bool hover = m_hoverBtn == D_CRED0 + i;
        FillRR(r, 12 * k, hover ? Theme::CardHover : Theme::Card);
        Txt(d.creditorChips[i], r, fBadge.Get(), Theme::TextDim);
        m_hits.push_back({D_CRED0 + i, r});
        cx += cw + 8 * k;
    }
    y += 36 * k;
    const float third = (fw - 2 * 12 * k) / 3;
    place(1, x0, third);
    place(2, x0 + third + 12 * k, third);
    place(3, x0 + 2 * (third + 12 * k), third);
    y += 22 * k + fh;
    place(4, x0, fw);
    y += 22 * k + fh;
    if (edit) { place(5, x0, fw); y += 22 * k + fh; }
    // Status
    Txt(L"Status", {x0, y, x0 + 200 * k, y + 18 * k}, fSmall.Get(), Theme::TextDim);
    float sx = x0;
    for (int i = 0; i < 4; ++i) {
        const float w = TextW(kStatusLabels[i], fBadge.Get()) + 36 * k;
        const D2D1_RECT_F r{sx, y + 22 * k, sx + w, y + 22 * k + 30 * k};
        const unsigned c = i == 0 ? Theme::Open : i == 1 ? Theme::Paid : i == 2 ? Theme::Installment : Theme::Fixed;
        const bool on = d.status == kStatusOrder[i];
        FillRR(r, 15 * k, c, on ? 0.30f : (m_hoverBtn == D_STATUS0 + i ? 0.14f : 0.07f));
        StrokeRR(r, 15 * k, c, on ? 2.f : 1.f);
        Txt(kStatusLabels[i], r, fBadge.Get(), on ? Theme::Text : c);
        m_hits.push_back({D_STATUS0 + i, r});
        sx += w + 8 * k;
    }
    y += 64 * k;
    if (Field* rf = FieldById(6)) {   // Ratenzahlung: Zusatzfeld "Rate / Fortschritt" unter den Status-Knoepfen
        rf->hidden = d.status != docs::Status::Installment;
        if (rf->hidden) rf->rc = {};
        else { place(6, x0, fw); y += 22 * k + fh; }
    }
    if (!d.error.empty()) Txt(d.error, {x0, y, x0 + fw, y + 26 * k}, fNormal.Get(), Theme::Danger);
    y += 30 * k;
    // Knoepfe
    const float bh = 38 * k;
    float bx = d.box.right - pad;
    D2D1_RECT_F ok{bx - 150 * k, y, bx, y + bh};
    Button(D_OK, ok, edit ? L"Speichern" : L"Hinzufügen", true);
    bx = ok.left - 10 * k;
    D2D1_RECT_F cancel{bx - 130 * k, y, bx, y + bh};
    Button(D_CANCEL, cancel, L"Abbrechen", false);
    if (!edit && d.queue.size() > 1) {
        D2D1_RECT_F skip{cancel.left - 10 * k - 130 * k, y, cancel.left - 10 * k, y + bh};
        Button(D_SKIP, skip, L"Überspringen", false);
    }
    for (auto& f : d.fields) DrawField(f, m_focusId == f.id);
}

void App::DrawInvoice() {
    Dialog& d = m_dlg;
    RECT cr;
    GetClientRect(m_hwnd, &cr);
    const float pad = 24 * k, fh = 36 * k, rowH = 42 * k;
    const float bw = std::min(940 * k, cr.right - 40 * k);
    // Titel + 3 Formularreihen + Positionskopf + Zeilen + Summenblock + Zahlungshinweis + Fehlerzeile + Knoepfe
    const float height = std::min((float)cr.bottom - 20 * k, pad + 36 * k + 3 * (24 * k + fh) + 20 * k + d.itemRows * rowH + 46 * k + (24 * k + fh) + 26 * k + 38 * k + pad);
    d.box = {(cr.right - bw) / 2, (cr.bottom - height) / 2, (cr.right + bw) / 2, (cr.bottom + height) / 2};
    FillRR(d.box, 14 * k, Theme::Panel);
    StrokeRR(d.box, 14 * k, Theme::Border);
    const float x0 = d.box.left + pad, fw = W(d.box) - 2 * pad, gap = 14 * k;
    float y = d.box.top + pad;
    Txt(d.title, {x0, y, x0 + 250 * k, y + 30 * k}, fTitle.Get(), Theme::Text);
    {   // Seiten-Reiter und Beispiel-/Leeren-Knoepfe in der Titelzeile
        const float th = 30 * k;
        Button(D_PAGE0, {x0 + 260 * k, y, x0 + 260 * k + 110 * k, y + th}, L"Rechnung", d.page == 0);
        Button(D_PAGE1, {x0 + 380 * k, y, x0 + 380 * k + 160 * k, y + th}, L"Absender & Konto", d.page == 1);
        const float rx = d.box.right - pad;
        Button(D_CLEAR, {rx - 120 * k, y, rx, y + th}, L"Felder leeren", false);
        Button(D_EXAMPLE, {rx - 120 * k - 8 * k - 130 * k, y, rx - 120 * k - 8 * k, y + th}, L"Beispiel laden", false);
    }
    y += 36 * k;
    for (auto& f : d.fields) {   // Felder der nicht angezeigten Seite ausblenden
        const bool footerField = f.id >= 9 && f.id <= 17 && f.id != 8;
        f.hidden = (d.page == 1) != footerField;
        if (f.hidden) f.rc = {};
    }
    auto place = [&](int id, float x, float w, float yy) {
        Field* f = FieldById(id);
        if (!f) return;
        Txt(f->label, {x, yy, x + w, yy + 18 * k}, fSmall.Get(), Theme::TextDim);
        f->rc = {x, yy + 20 * k, x + w, yy + 20 * k + fh};
    };
    const float half = (fw - gap) / 2;
    if (d.page == 1) {   // Absender & Konto: Steuer, Vertreter/Register, Kontakt, Bank (Inhalt der Fusszeile)
        place(9, x0, half, y);
        {
            const float bs = 22 * k, cy = y + 20 * k + (fh - bs) / 2;
            const D2D1_RECT_F box{x0 + half + gap, cy, x0 + half + gap + bs, cy + bs};
            const bool hover = m_hoverBtn == D_SMALLBIZ;
            FillRR(box, 6 * k, d.smallBiz ? Theme::Accent : Theme::Bg);
            StrokeRR(box, 6 * k, d.smallBiz ? Theme::Accent : (hover ? Theme::TextDim : Theme::Border), 1.5f);
            if (d.smallBiz) Txt(L"\u2713", box, fCenter.Get(), 0xFFFFFF);
            const D2D1_RECT_F label{box.right + 10 * k, y + 20 * k, x0 + fw, y + 20 * k + fh};
            Txt(L"Kleinunternehmer nach § 19 UStG", label, fNormal.Get(), hover ? Theme::Text : Theme::TextDim);
            m_hits.push_back({D_SMALLBIZ, {x0 + half + gap, y + 20 * k, x0 + fw, y + 20 * k + fh}});
        }
        y += 24 * k + fh;
        place(10, x0, half, y);  place(11, x0 + half + gap, half, y);
        y += 24 * k + fh;
        const float third = (fw - 2 * gap) / 3;
        place(12, x0, third, y);  place(13, x0 + third + gap, third, y);  place(14, x0 + 2 * (third + gap), third, y);
        y += 24 * k + fh;
        place(15, x0, third, y);  place(16, x0 + third + gap, third, y);  place(17, x0 + 2 * (third + gap), third, y);
        y += 24 * k + fh;
        Txt(L"Diese Angaben stehen in der dreispaltigen Fußzeile der PDF (links Firma und Register, Mitte Kontakt, rechts Bankverbindung).",
            {x0, y, x0 + fw, y + 22 * k}, fSmall.Get(), Theme::TextDim);
    } else {
    // Absender / Empfaenger nebeneinander
    place(0, x0, half, y);                 place(2, x0 + half + gap, half, y);
    y += 24 * k + fh;
    place(1, x0, half, y);                 place(3, x0 + half + gap, half, y);
    y += 24 * k + fh;
    const float quarter = (fw - 3 * gap) / 4;
    place(4, x0, quarter, y); place(5, x0 + (quarter + gap), quarter, y); place(6, x0 + 2 * (quarter + gap), quarter, y); place(7, x0 + 3 * (quarter + gap), quarter, y);
    if (Field* v = FieldById(7)) v->disabled = d.smallBiz;   // Kleinunternehmer: USt-Satz ausgegraut, nicht editierbar
    y += 24 * k + fh;

    // Positionen: Beschreibung | Menge | Einzelpreis | Summe
    const float cDesc = fw * 0.50f, cQty = fw * 0.12f, cUnit = fw * 0.18f;
    Txt(L"Beschreibung", {x0, y, x0 + cDesc, y + 18 * k}, fSmall.Get(), Theme::TextDim);
    Txt(L"Menge", {x0 + cDesc + gap, y, x0 + cDesc + cQty, y + 18 * k}, fSmall.Get(), Theme::TextDim);
    Txt(L"Einzelpreis (€)", {x0 + cDesc + cQty + gap, y, x0 + cDesc + cQty + cUnit, y + 18 * k}, fSmall.Get(), Theme::TextDim);
    Txt(L"Summe", {x0 + cDesc + cQty + cUnit + gap, y, x0 + fw, y + 18 * k}, fRight.Get(), Theme::TextDim);
    y += 20 * k;
    invoice::Data data;
    data.vatPercent = 19;
    data.smallBusiness = d.smallBiz;
    if (Field* v = FieldById(7)) {
        try { data.vatPercent = std::clamp(std::stoi(v->ed.text), 0, 100); } catch (...) {}
    }
    for (int i = 0; i < d.itemRows; ++i) {
        Field* fd = FieldById(100 + 3 * i);
        Field* fq = FieldById(101 + 3 * i);
        Field* fu = FieldById(102 + 3 * i);
        if (!fd || !fq || !fu) continue;
        fd->rc = {x0, y, x0 + cDesc, y + fh};
        fq->rc = {x0 + cDesc + gap, y, x0 + cDesc + cQty, y + fh};
        fu->rc = {x0 + cDesc + cQty + gap, y, x0 + cDesc + cQty + cUnit, y + fh};
        double q = 0;
        bool okU = false;
        const long long unit = docs::ParseAmountCents(fu->ed.text, okU);
        if (invoice::ParseQuantity(fq->ed.text, q) && okU && !fu->ed.text.empty()) {
            invoice::Item it;
            it.qty = q;
            it.unitCents = unit;
            data.items.push_back(it);
            Txt(docs::FormatAmount(invoice::LineCents(it)), {x0 + cDesc + cQty + cUnit + gap, y, x0 + fw, y + fh}, fRight.Get(), Theme::Text);
        }
        y += rowH;
    }
    const D2D1_RECT_F addRow{x0, y, x0 + 130 * k, y + 30 * k};
    Button(D_ADDROW, addRow, L"+ Position", false);
    if (d.itemRows > 1) Button(D_DELROW, {addRow.right + 8 * k, y, addRow.right + 8 * k + 130 * k, y + 30 * k}, L"− Position", false);
    // Summen live
    const invoice::Totals t = invoice::Compute(data);
    Txt(d.smallBiz ? std::wstring(L"Keine Umsatzsteuer (§ 19 UStG)")
                   : L"Netto " + docs::FormatAmount(t.net) + L"   ·   USt " + std::to_wstring(data.vatPercent) + L" %: " + docs::FormatAmount(t.vat),
        {x0 + fw * 0.4f, y, x0 + fw, y + 16 * k}, fRight.Get(), Theme::TextDim);
    Txt(L"Gesamt " + docs::FormatAmount(t.gross), {x0 + fw * 0.4f, y + 16 * k, x0 + fw, y + 38 * k}, fTitleR.Get(), Theme::Text);
    y += 46 * k;
    place(8, x0, fw, y);
    y += 24 * k + fh;
    }
    const float bh = 38 * k;
    y = d.box.bottom - pad - bh;   // Fehlerzeile und Knoepfe sitzen auf beiden Seiten an derselben Stelle
    if (!d.error.empty()) Txt(d.error, {x0, y - 28 * k, x0 + fw, y - 4 * k}, fNormal.Get(), Theme::Danger);
    float bx = d.box.right - pad;
    D2D1_RECT_F ok{bx - 170 * k, y, bx, y + bh};
    Button(D_OK, ok, L"PDF erzeugen", true);
    bx = ok.left - 10 * k;
    Button(D_CANCEL, {bx - 130 * k, y, bx, y + bh}, L"Abbrechen", false);
    {   // Checkbox unten links: Absender-Vorlage merken
        const float bs = 22 * k;
        const D2D1_RECT_F box{x0, y + (bh - bs) / 2, x0 + bs, y + (bh + bs) / 2};
        const bool hover = m_hoverBtn == D_SAVESENDER;
        FillRR(box, 6 * k, d.saveSender ? Theme::Accent : Theme::Bg);
        StrokeRR(box, 6 * k, d.saveSender ? Theme::Accent : (hover ? Theme::TextDim : Theme::Border), 1.5f);
        if (d.saveSender) Txt(L"\u2713", box, fCenter.Get(), 0xFFFFFF);
        const D2D1_RECT_F label{box.right + 10 * k, y, box.right + 10 * k + 330 * k, y + bh};
        Txt(L"Als Standard-Absender speichern", label, fNormal.Get(), hover ? Theme::Text : Theme::TextDim);
        m_hits.push_back({D_SAVESENDER, {x0, y, label.right, y + bh}});
    }
    for (auto& f : d.fields) DrawField(f, m_focusId == f.id);
}

// ============================================================================
// Hilfen
// ============================================================================

Field* App::FieldById(int id) {
    if (id == kSearchId) return &m_search;
    for (auto& f : m_dlg.fields)
        if (f.id == id) return &f;
    return nullptr;
}
Field* App::FocusedField() { return m_focusId != kNoFocus ? FieldById(m_focusId) : nullptr; }

void App::ClampScroll() {
    const float content = (float)m_view.size() * kRowH * k, view = std::max(0.f, H(rcList) - HeadOff());
    m_scroll = std::clamp(m_scroll, 0.f, std::max(0.f, content - view));
    const float tcontent = (float)m_nodes.size() * kNodeH * k + 32 * k, tview = H(rcTree);
    m_treeScroll = std::clamp(m_treeScroll, 0.f, std::max(0.f, tcontent - tview));
}

int App::RowAt(float x, float y) const {
    const D2D1_RECT_F body{rcList.left, rcHead.bottom, rcList.right, rcList.bottom};
    if (!In(body, x, y)) return -1;
    const int i = (int)std::floor((y - body.top + m_scroll) / (kRowH * k));
    return i >= 0 && i < (int)m_view.size() ? i : -1;
}

int App::HeaderColAt(float x, float y) const {
    if (!In(rcHead, x, y)) return -1;
    const float pad = 18 * k, total = W(rcList) - 2 * pad;
    const float edges[5] = {rcList.left + pad + total * 0.33f, rcList.left + pad + total * 0.54f, rcList.left + pad + total * 0.67f,
                            rcList.left + pad + total * 0.82f, rcList.right};
    for (int c = 0; c < 5; ++c)
        if (x < edges[c]) return c;
    return 4;
}

int App::NodeAt(float x, float y) const {
    if (!In(rcTree, x, y)) return -1;
    const int i = (int)std::floor((y - rcTree.top - 32 * k + m_treeScroll) / (kNodeH * k));
    return i >= 0 && i < (int)m_nodes.size() ? i : -1;
}

void App::EnsureRowVisible(int vi) {
    const float rh = kRowH * k, view = H(rcList) - HeadOff();
    if (vi * rh < m_scroll) m_scroll = vi * rh;
    else if ((vi + 1) * rh > m_scroll + view) m_scroll = (vi + 1) * rh - view;
    ClampScroll();
}

void App::MoveSelection(int delta, bool extend) {
    if (m_view.empty()) return;
    int cur = -1;
    for (int i = 0; i < (int)m_view.size(); ++i)
        if (m_all[m_view[i]].rel == m_focusRel) { cur = i; break; }
    int target = cur < 0 ? (delta > 0 ? 0 : (int)m_view.size() - 1) : std::clamp(cur + delta, 0, (int)m_view.size() - 1);
    if (cur < 0 && delta != 0 && std::abs(delta) == 1) target = delta > 0 ? 0 : (int)m_view.size() - 1;
    const std::wstring rel = m_all[m_view[target]].rel;
    if (extend && !m_anchorRel.empty()) {
        int a = -1;
        for (int i = 0; i < (int)m_view.size(); ++i)
            if (m_all[m_view[i]].rel == m_anchorRel) { a = i; break; }
        m_sel.clear();
        if (a < 0) a = target;
        for (int i = std::min(a, target); i <= std::max(a, target); ++i) m_sel.insert(m_all[m_view[i]].rel);
    } else {
        m_sel.clear();
        m_sel.insert(rel);
        m_anchorRel = rel;
    }
    m_focusRel = rel;
    EnsureRowVisible(target);
    Invalidate();
}

void App::OnFieldChanged(Field& f) {
    ResetCaret();
    if (f.dateMask) docs::MaskDate(f.ed, f.prevLen);
    f.prevLen = f.ed.text.size();
    if (f.id == kSearchId) {
        m_scroll = 0;
        RebuildView();
    }
    Invalidate();
}

void App::FieldKey(Field& f, WPARAM vk) {
    const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    docs::TextEdit& e = f.ed;
    switch (vk) {
        case VK_LEFT: e.Move(-1, shift, ctrl); break;
        case VK_RIGHT: e.Move(1, shift, ctrl); break;
        case VK_HOME: e.Home(shift); break;
        case VK_END: e.End(shift); break;
        case VK_BACK:
            if (ctrl && !e.HasSelection()) e.Move(-1, true, true);
            e.Backspace();
            break;
        case VK_DELETE: e.Delete(); break;
        case 'A': if (!ctrl) return; e.SelectAll(); break;
        case 'C': if (!ctrl) return; if (e.HasSelection()) SetClipboardText(m_hwnd, e.Selected()); break;
        case 'X':
            if (!ctrl) return;
            if (e.HasSelection()) { SetClipboardText(m_hwnd, e.Selected()); e.DeleteSelection(); }
            break;
        case 'V': if (!ctrl) return; e.Insert(ClipboardText()); break;
        default: return;
    }
    OnFieldChanged(f);
}

void App::FieldMouse(Field& f, float x, bool extend) {
    ComPtr<IDWriteTextLayout> layout;
    if (f.ed.text.empty() || FAILED(dw->CreateTextLayout(f.ed.text.c_str(), (UINT32)f.ed.text.size(), fNormal.Get(), 10000.f, H(f.rc), &layout))) {
        f.ed.SetCaret(0, extend);
        return;
    }
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS m{};
    layout->HitTestPoint(x - (f.rc.left + 11 * k) + f.scroll, H(f.rc) / 2, &trailing, &inside, &m);
    f.ed.SetCaret((size_t)m.textPosition + (trailing ? 1 : 0), extend);
    ResetCaret();
}

void App::FocusNext(bool back) {
    auto& fs_ = m_dlg.fields;
    if (fs_.empty()) return;
    int idx = -1;
    for (int i = 0; i < (int)fs_.size(); ++i)
        if (fs_[i].id == m_focusId) idx = i;
    const int n = (int)fs_.size();
    idx = idx < 0 ? 0 : (idx + (back ? n - 1 : 1)) % n;
    for (int tries = 0; tries < n && (fs_[idx].hidden || fs_[idx].disabled); ++tries) idx = (idx + (back ? n - 1 : 1)) % n;   // ausgeblendete/graue Felder ueberspringen
    m_focusId = fs_[idx].id;
    fs_[idx].ed.SelectAll();
    ResetCaret();
    Invalidate();
}

// ============================================================================
// Aktionen
// ============================================================================

void App::OpenFile(const fs::path& file) {
    const std::wstring p = file.wstring();
    if (m_settings.pdfReader && docs::Lower(file.extension().wstring()) == L".pdf") {
        const std::wstring reader = FindPdfReader();
        if (!reader.empty()) {
            const std::wstring arg = L"\"" + p + L"\"";
            if ((INT_PTR)ShellExecuteW(m_hwnd, L"open", reader.c_str(), arg.c_str(), nullptr, SW_SHOWNORMAL) > 32) return;
        }
    }
    if ((INT_PTR)ShellExecuteW(m_hwnd, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL) <= 32)
        SetNote(L"Die Datei konnte nicht ge\u00F6ffnet werden.");
}

void App::OpenDoc(const docs::Doc& d) {
    OpenFile(AbsPath(d.rel));
}

void App::ShowInExplorer(const docs::Doc& d) {
    const std::wstring args = L"/select,\"" + AbsPath(d.rel).wstring() + L"\"";
    ShellExecuteW(m_hwnd, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void App::CloseDialog() {
    m_dlg = Dialog{};
    m_focusId = kNoFocus;
    Invalidate();
}

void App::SetStatusOfSelected(docs::Status s) {
    int n = 0;
    for (const docs::Doc* d : SelectedDocs()) {
        docs::Meta m = d->m;
        m.status = s;
        m_meta[d->rel] = m;
        ++n;
    }
    if (!n) return;
    SaveIndex();
    Refresh();
    SetNote(std::to_wstring(n) + (n == 1 ? L" Rechnung" : L" Rechnungen") + L" als „" + docs::StatusName(s) + L"“ markiert");
    Invalidate();
}

void App::TrashSelected() {
    const auto sel = SelectedDocs();
    if (sel.empty()) return;
    const std::wstring msg = sel.size() == 1 ? L"„" + sel[0]->name + L"“ in den Papierkorb verschieben?"
                                             : std::to_wstring(sel.size()) + L" Rechnungen in den Papierkorb verschieben?";
    if (MessageBoxW(m_hwnd, msg.c_str(), L"Löschen", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    std::wstring from;
    for (const docs::Doc* d : sel) {
        from += AbsPath(d->rel).wstring();
        from.push_back(L'\0');
    }
    SHFILEOPSTRUCTW op{};
    op.hwnd = m_hwnd;
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    if (SHFileOperationW(&op) != 0 || op.fAnyOperationsAborted) {
        SetNote(L"Löschen fehlgeschlagen oder abgebrochen.");
    } else {
        for (const docs::Doc* d : sel) m_meta.erase(d->rel);
        m_sel.clear();
        SaveIndex();
        SetNote(std::to_wstring(sel.size()) + L" in den Papierkorb verschoben");
    }
    Refresh();
    Invalidate();
}

void App::ChangeRoot() {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(L"Rechnungsordner wählen");
    if (FAILED(dlg->Show(m_hwnd))) return;
    ComPtr<IShellItem> item;
    PWSTR p = nullptr;
    if (SUCCEEDED(dlg->GetResult(&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
        m_settings.root = p;
        CoTaskMemFree(p);
        m_sel.clear();
        m_node = 0;
        InitRoot();
        SetNote(L"Rechnungsordner: " + m_settings.root);
        Invalidate();
    }
}

void App::PickFilesToAdd() {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM);
    COMDLG_FILTERSPEC fl[2] = {{L"Belege (PDF, JPG, PNG)", L"*.pdf;*.jpg;*.jpeg;*.png"}, {L"Alle Dateien", L"*.*"}};
    dlg->SetFileTypes(2, fl);
    dlg->SetTitle(L"Rechnungen hinzufügen");
    if (FAILED(dlg->Show(m_hwnd))) return;
    ComPtr<IShellItemArray> arr;
    if (FAILED(dlg->GetResults(&arr))) return;
    DWORD n = 0;
    arr->GetCount(&n);
    std::vector<std::wstring> files;
    for (DWORD i = 0; i < n; ++i) {
        ComPtr<IShellItem> it;
        PWSTR p = nullptr;
        if (SUCCEEDED(arr->GetItemAt(i, &it)) && SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
            files.push_back(p);
            CoTaskMemFree(p);
        }
    }
    AddFiles(files);
}

void App::OnDropFiles(HDROP drop) {
    std::vector<std::wstring> files;
    const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < n; ++i) {
        const UINT len = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring p(len, L'\0');
        DragQueryFileW(drop, i, p.data(), len + 1);
        files.push_back(p);
    }
    DragFinish(drop);
    SetForegroundWindow(m_hwnd);
    // Standard laut Einstellung; mit gedrueckter Umschalttaste wird das Gegenteil gemacht (Dialog statt automatisch)
    AddFiles(files, m_settings.autoImport != (GetKeyState(VK_SHIFT) < 0));
}

void App::AddFiles(std::vector<std::wstring> in, bool autoMode) {
    std::vector<std::wstring> files;
    std::error_code ec;
    for (const auto& p : in) {
        if (fs::is_directory(p, ec)) {   // Ordner: alle Belege darin (begrenzt)
            int n = 0;
            for (fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end && n < 300; it.increment(ec))
                if (it->is_regular_file(ec) && docs::IsDocFile(it->path())) { files.push_back(it->path().wstring()); ++n; }
        } else if (docs::IsDocFile(p)) {
            files.push_back(p);
        }
    }
    if (files.empty()) {
        SetNote(L"Es wurden keine PDF-, JPG- oder PNG-Dateien gefunden.");
        Invalidate();
        return;
    }
    if (m_dlg.kind != DlgKind::None) {
        SetNote(L"Bitte erst den geöffneten Dialog abschließen.");
        Invalidate();
        return;
    }
    if (autoMode) {
        ImportAuto(files);
        return;
    }
    m_dlg = Dialog{};
    m_dlg.kind = DlgKind::Add;
    m_dlg.queue = files;
    BeginAddDialog();
}

namespace {
Field MakeField(int id, const wchar_t* label, const wchar_t* hint, const std::wstring& text = L"", size_t maxLen = 120) {
    Field f;
    f.id = id;
    f.label = label;
    f.hint = hint;
    f.ed.maxLen = maxLen;
    f.ed.SetText(text);
    f.prevLen = f.ed.text.size();
    return f;
}

std::wstring FileYear(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) {
        FILETIME lf;
        SYSTEMTIME st;
        if (FileTimeToLocalFileTime(&fa.ftLastWriteTime, &lf) && FileTimeToSystemTime(&lf, &st)) return std::to_wstring(st.wYear);
    }
    return TodayIso().substr(0, 4);
}
}  // namespace

void App::BeginAddDialog() {
    Dialog& d = m_dlg;
    const std::wstring file = d.queue[d.queueIdx];
    const std::wstring name = fs::path(file).filename().wstring();
    // Eingaben des vorigen Belegs (Glaeubiger/Jahr) als Vorschlag mitnehmen
    std::wstring cred, year = FileYear(file);
    if (Field* f = FieldById(0)) { cred = f->ed.text; }
    if (Field* f = FieldById(1); f && d.queueIdx > 0) year = f->ed.text;
    d.title = L"Rechnung hinzufügen";
    d.subtitle = name + (d.queue.size() > 1 ? L"   (" + std::to_wstring(d.queueIdx + 1) + L" von " + std::to_wstring(d.queue.size()) + L")" : L"");
    d.fields.clear();
    d.fields.push_back(MakeField(0, L"Gläubiger / Kategorie", L"z. B. Netzanbieter", cred, 60));
    d.fields.push_back(MakeField(1, L"Jahr", L"JJJJ", year, 4));
    d.fields.push_back(MakeField(2, L"Betrag (€)", L"z. B. 89,50", L"", 20));
    d.fields.push_back(MakeField(3, L"Fälligkeit", L"TT.MM.JJJJ", L"", 12));
    d.fields.back().dateMask = true;
    d.fields.push_back(MakeField(4, L"Tags (Stichwörter)", L"z. B. strom, abschlag", L"", 100));
    d.fields.push_back(MakeField(6, L"Rate / Fortschritt", L"z. B. Rate 3 von 12 oder 50 € / Monat", L"", 60));
    d.status = docs::Status::Open;
    d.error.clear();
    // Vorschlaege: die am haeufigsten benutzten Glaeubiger
    std::map<std::wstring, int> cnt;
    for (const auto& x : m_all) cnt[x.creditor]++;
    std::vector<std::pair<int, std::wstring>> order;
    for (const auto& c : cnt) order.push_back({-c.second, c.first});
    std::sort(order.begin(), order.end());
    d.creditorChips.clear();
    for (const auto& o : order)
        if (o.second != L"Sonstige") d.creditorChips.push_back(o.second);
    m_focusId = kNoFocus;
    FocusNext(false);
    Invalidate();
}

namespace {
bool ParseYear(const std::wstring& s, std::wstring& out) {
    const std::wstring t = docs::Trim(s);
    if (t.size() != 4) return false;
    for (wchar_t c : t)
        if (c < L'0' || c > L'9') return false;
    const int y = std::stoi(t);
    if (y < 1990 || y > 2100) return false;
    out = t;
    return true;
}

// Gemeinsame Pruefung von Gläubiger/Jahr/Betrag/Fälligkeit der Formulare Hinzufuegen und Bearbeiten.
bool ReadMeta(App*, const std::vector<Field>& fields, std::wstring& cred, std::wstring& year, docs::Meta& m, std::wstring& error) {
    auto get = [&](int id) -> const Field* {
        for (const auto& f : fields)
            if (f.id == id) return &f;
        return nullptr;
    };
    cred = docs::Trim(get(0)->ed.text);
    if (cred.empty()) { error = L"Bitte einen Gläubiger oder eine Kategorie eingeben."; return false; }
    if (!ParseYear(get(1)->ed.text, year)) { error = L"Das Jahr muss vierstellig sein (z. B. 2026)."; return false; }
    const std::wstring amount = docs::Trim(get(2)->ed.text);
    if (amount.empty()) m.cents = docs::kUnknown;
    else {
        bool ok = false;
        m.cents = docs::ParseAmountCents(amount, ok);
        if (!ok) { error = L"Der Betrag ist nicht lesbar (z. B. 1.234,56)."; return false; }
    }
    if (!docs::ParseDate(get(3)->ed.text, m.due)) { error = L"Die Fälligkeit ist kein gültiges Datum (TT.MM.JJJJ)."; return false; }
    m.tags = docs::CleanField(get(4)->ed.text);
    if (const Field* n = get(5)) m.note = docs::CleanField(n->ed.text);
    if (const Field* r = get(6)) m.rate = docs::CleanField(r->ed.text);
    return true;
}
}  // namespace

void App::ApplyAdd(bool skipOnly) {
    Dialog& d = m_dlg;
    const std::wstring src = d.queue[d.queueIdx];
    static std::wstring lastAdded;
    if (!skipOnly) {
        std::wstring cred, year;
        docs::Meta m;
        if (!ReadMeta(this, d.fields, cred, year, m, d.error)) { Invalidate(); return; }
        m.status = d.status;
        if (m.status != docs::Status::Installment) m.rate.clear();
        const fs::path target = docs::TargetPath(m_rootPath, year, cred, fs::path(src).filename().wstring());
        std::error_code ec;
        fs::create_directories(target.parent_path(), ec);
        const bool ok = m_settings.moveFiles ? MoveFileExW(src.c_str(), target.c_str(), MOVEFILE_COPY_ALLOWED) != 0
                                             : CopyFileW(src.c_str(), target.c_str(), TRUE) != 0;
        if (!ok) { d.error = L"Die Datei konnte nicht in den Rechnungsordner kopiert werden."; Invalidate(); return; }
        std::wstring rel = fs::relative(target, m_rootPath, ec).wstring();
        std::replace(rel.begin(), rel.end(), L'/', L'\\');
        m_meta[rel] = m;
        SaveIndex();
        lastAdded = rel;
        m_dlgAdded++;
    }
    d.error.clear();
    if (++d.queueIdx < d.queue.size()) {
        BeginAddDialog();
        return;
    }
    const int added = m_dlgAdded;
    m_dlgAdded = 0;
    CloseDialog();
    Refresh();
    if (!lastAdded.empty() && FindDoc(lastAdded)) {
        m_sel = {lastAdded};
        m_focusRel = m_anchorRel = lastAdded;
        for (int i = 0; i < (int)m_view.size(); ++i)
            if (m_all[m_view[i]].rel == lastAdded) { EnsureRowVisible(i); break; }
    }
    lastAdded.clear();
    SetNote(added == 0 ? L"Nichts hinzugefügt." : std::to_wstring(added) + (added == 1 ? L" Rechnung" : L" Rechnungen") + L" hinzugefügt");
    Invalidate();
}

void App::ImportAuto(const std::vector<std::wstring>& files) {
    std::set<std::wstring> knownSet;
    for (const auto& d : m_all) knownSet.insert(d.creditor);
    std::vector<std::wstring> known(knownSet.begin(), knownSet.end());
    std::set<std::wstring> imported;
    std::wstring last;
    int dup = 0, fail = 0;
    for (const auto& src : files) {
        const std::wstring name = fs::path(src).filename().wstring();
        const std::wstring year = docs::GuessYear(name, FileYear(src));
        const std::wstring cred = docs::SanitizeName(docs::GuessCreditor(name, known), L"Sonstige");
        std::error_code e1, e2;
        const fs::path same = m_rootPath / docs::SanitizeName(year, L"Unbekannt") / cred / docs::SanitizeFileName(name, L"Datei");
        if (fs::exists(same, e1) && fs::file_size(same, e1) == fs::file_size(src, e2) && !e1 && !e2) { ++dup; continue; }   // schon archiviert
        const fs::path target = docs::TargetPath(m_rootPath, year, cred, name);
        fs::create_directories(target.parent_path(), e1);
        const bool ok = m_settings.moveFiles ? MoveFileExW(src.c_str(), target.c_str(), MOVEFILE_COPY_ALLOWED) != 0
                                             : CopyFileW(src.c_str(), target.c_str(), TRUE) != 0;
        if (!ok) { ++fail; continue; }
        std::wstring rel = fs::relative(target, m_rootPath, e1).wstring();
        std::replace(rel.begin(), rel.end(), L'/', L'\\');
        m_meta[rel] = docs::Meta{};   // Status Offen; Betrag und Faelligkeit ergaenzt man mit F2
        imported.insert(rel);
        last = rel;
        known.push_back(cred);
    }
    if (!imported.empty()) SaveIndex();
    Refresh();
    if (!imported.empty()) {
        m_node = 0;   // "Alle Rechnungen": die neuen Belege sollen sichtbar sein
        RebuildView();
        m_search.ed.SetText(L"");
        m_tagFilter.clear();
        RebuildView();
        m_sel = imported;
        m_focusRel = m_anchorRel = last;
        for (int i = 0; i < (int)m_view.size(); ++i)
            if (m_all[m_view[i]].rel == last) { EnsureRowVisible(i); break; }
    }
    std::wstring note = std::to_wstring(imported.size()) + (imported.size() == 1 ? L" Beleg" : L" Belege") + L" automatisch nach Jahr und Gläubiger einsortiert";
    if (imported.empty()) note = L"Nichts importiert";
    else if (imported.size() == 1) note += L"  ·  F2: Betrag und Fälligkeit ergänzen, Doppelklick: im PDF Reader öffnen";
    if (dup) note += L"  ·  " + std::to_wstring(dup) + L" schon vorhanden";
    if (fail) note += L"  ·  " + std::to_wstring(fail) + L" fehlgeschlagen";
    SetNote(note);
    Invalidate();
}

void App::BeginTagsDialog() {
    if (m_dlg.kind != DlgKind::None) return;
    const auto sel = SelectedDocs();
    if (sel.empty()) {
        SetNote(L"Zuerst Belege markieren, dann Tags vergeben.");
        Invalidate();
        return;
    }
    m_dlg = Dialog{};
    Dialog& d = m_dlg;
    d.kind = DlgKind::Tags;
    d.title = L"Tags vergeben";
    d.subtitle = sel.size() == 1 ? sel[0]->name : std::to_wstring(sel.size()) + L" markierte Belege";
    d.fields.push_back(MakeField(0, L"Tags hinzufügen (mit Komma trennen)", L"z. B. Steuer, Büro, Software", L"", 100));
    d.fields.push_back(MakeField(1, L"Tags entfernen", L"leer lassen, wenn nichts entfernt werden soll", L"", 100));
    d.error.clear();
    for (const auto& t : m_tags)
        if (d.creditorChips.size() < 8) d.creditorChips.push_back(t.first);   // vorhandene Tags als Vorschlaege
    m_focusId = kNoFocus;
    FocusNext(false);
    Invalidate();
}

void App::ApplyTags() {
    Dialog& d = m_dlg;
    const Field* addF = FieldById(0);
    const Field* remF = FieldById(1);
    const std::wstring add = addF ? addF->ed.text : L"", rem = remF ? remF->ed.text : L"";
    if (docs::SplitTags(add).empty() && docs::SplitTags(rem).empty()) {
        d.error = L"Bitte mindestens einen Tag eingeben.";
        Invalidate();
        return;
    }
    int changed = 0;
    for (const docs::Doc* doc : SelectedDocs()) {
        docs::Meta m = doc->m;
        m.tags = docs::EditTags(m.tags, add, rem);
        if (m.tags == doc->m.tags) continue;
        m_meta[doc->rel] = m;
        ++changed;
    }
    if (changed) SaveIndex();
    CloseDialog();
    Refresh();
    SetNote(changed == 0 ? L"Keine Änderung an den Tags." : L"Tags bei " + std::to_wstring(changed) + (changed == 1 ? L" Beleg" : L" Belegen") + L" geändert");
    Invalidate();
}

void App::SetKindOfSelected(bool income) {
    int n = 0;
    for (const docs::Doc* doc : SelectedDocs()) {
        if (doc->m.income == income) continue;
        docs::Meta m = doc->m;
        m.income = income;
        m_meta[doc->rel] = m;
        ++n;
    }
    if (n) SaveIndex();
    Refresh();
    SetNote(std::to_wstring(n) + (n == 1 ? L" Beleg" : L" Belege") + (income ? L" als Einnahme markiert" : L" als Ausgabe markiert"));
    Invalidate();
}

void App::DrawTagsDialog() {
    Dialog& d = m_dlg;
    const float bw = 560 * k, pad = 24 * k, fh = 38 * k;
    const float height = pad + 30 * k + 28 * k + 2 * (fh + 22 * k) + 36 * k /*Vorschlaege*/ + 30 * k /*Fehler*/ + 44 * k + pad;
    RECT cr;
    GetClientRect(m_hwnd, &cr);
    d.box = {(cr.right - bw) / 2, std::max(20.f * k, (cr.bottom - height) / 2), (cr.right + bw) / 2, 0};
    d.box.bottom = d.box.top + height;
    FillRR(d.box, 14 * k, Theme::Panel);
    StrokeRR(d.box, 14 * k, Theme::Border);
    float y = d.box.top + pad;
    Txt(d.title, {d.box.left + pad, y, d.box.right - pad, y + 30 * k}, fTitle.Get(), Theme::Text);
    y += 30 * k;
    Txt(d.subtitle, {d.box.left + pad, y, d.box.right - pad, y + 24 * k}, fSmall.Get(), Theme::TextDim);
    y += 28 * k;
    const float x0 = d.box.left + pad, fw = W(d.box) - 2 * pad;
    auto place = [&](int id) {
        Field* f = FieldById(id);
        if (!f) return;
        Txt(f->label, {x0, y, x0 + fw, y + 18 * k}, fSmall.Get(), Theme::TextDim);
        f->rc = {x0, y + 20 * k, x0 + fw, y + 20 * k + fh};
        y += 22 * k + fh;
    };
    place(0);
    float cx = x0;   // vorhandene Tags zum Anklicken
    for (int i = 0; i < (int)d.creditorChips.size() && i < 8; ++i) {
        const float cw = TextW(d.creditorChips[i], fSmall.Get()) + 22 * k;
        if (cx + cw > x0 + fw) break;
        const D2D1_RECT_F r{cx, y + 2 * k, cx + cw, y + 26 * k};
        FillRR(r, 12 * k, m_hoverBtn == D_TAGSUG0 + i ? Theme::CardHover : Theme::Card);
        Txt(d.creditorChips[i], r, fBadge.Get(), Theme::TextDim);
        m_hits.push_back({D_TAGSUG0 + i, r});
        cx += cw + 8 * k;
    }
    y += 36 * k;
    place(1);
    if (!d.error.empty()) Txt(d.error, {x0, y, x0 + fw, y + 26 * k}, fNormal.Get(), Theme::Danger);
    y += 30 * k;
    const float bh = 38 * k;
    float bx = d.box.right - pad;
    D2D1_RECT_F ok{bx - 150 * k, y, bx, y + bh};
    Button(D_OK, ok, L"Übernehmen", true);
    bx = ok.left - 10 * k;
    Button(D_CANCEL, {bx - 130 * k, y, bx, y + bh}, L"Abbrechen", false);
    for (auto& f : d.fields) DrawField(f, m_focusId == f.id);
}

void App::BeginEditDialog(const docs::Doc& doc) {
    m_dlg = Dialog{};
    Dialog& d = m_dlg;
    d.kind = DlgKind::Edit;
    d.editRel = doc.rel;
    d.title = L"Rechnung bearbeiten";
    d.subtitle = doc.name;
    d.fields.push_back(MakeField(0, L"Gläubiger / Kategorie", L"z. B. Netzanbieter", doc.creditor, 60));
    d.fields.push_back(MakeField(1, L"Jahr", L"JJJJ", doc.year.empty() ? TodayIso().substr(0, 4) : doc.year, 4));
    d.fields.push_back(MakeField(2, L"Betrag (€)", L"z. B. 89,50", doc.m.cents == docs::kUnknown ? L"" : docs::FormatAmount(doc.m.cents, false), 20));
    d.fields.push_back(MakeField(3, L"Fälligkeit", L"TT.MM.JJJJ", docs::FormatDate(doc.m.due), 12));
    d.fields.back().dateMask = true;
    d.fields.push_back(MakeField(4, L"Tags (Stichwörter)", L"z. B. strom, abschlag", doc.m.tags, 100));
    d.fields.push_back(MakeField(5, L"Notiz", L"", doc.m.note, 200));
    d.fields.push_back(MakeField(6, L"Rate / Fortschritt", L"z. B. Rate 3 von 12 oder 50 € / Monat", doc.m.rate, 60));
    d.status = doc.m.status;
    std::map<std::wstring, int> cnt;
    for (const auto& x : m_all) cnt[x.creditor]++;
    std::vector<std::pair<int, std::wstring>> order;
    for (const auto& c : cnt) order.push_back({-c.second, c.first});
    std::sort(order.begin(), order.end());
    for (const auto& o : order)
        if (o.second != L"Sonstige") d.creditorChips.push_back(o.second);
    FocusNext(false);
    Invalidate();
}

void App::ApplyEdit() {
    Dialog& d = m_dlg;
    const docs::Doc* doc = FindDoc(d.editRel);
    if (!doc) { CloseDialog(); return; }
    std::wstring cred, year;
    docs::Meta m;
    if (!ReadMeta(this, d.fields, cred, year, m, d.error)) { Invalidate(); return; }
    m.status = d.status;
    if (m.status != docs::Status::Installment) m.rate.clear();
    std::wstring rel = doc->rel;
    const std::wstring sc = docs::SanitizeName(cred, L"Sonstige"), sy = docs::SanitizeName(year, L"Unbekannt");
    if (sc != doc->creditor || sy != doc->year) {   // Glaeubiger/Jahr geaendert: Datei in den passenden Ordner verschieben
        const fs::path target = docs::TargetPath(m_rootPath, year, cred, doc->name);
        std::error_code ec;
        fs::create_directories(target.parent_path(), ec);
        if (!MoveFileExW(AbsPath(doc->rel).c_str(), target.c_str(), MOVEFILE_COPY_ALLOWED)) {
            d.error = L"Die Datei konnte nicht verschoben werden.";
            Invalidate();
            return;
        }
        m_meta.erase(doc->rel);
        rel = fs::relative(target, m_rootPath, ec).wstring();
        std::replace(rel.begin(), rel.end(), L'/', L'\\');
    }
    m_meta[rel] = m;
    SaveIndex();
    CloseDialog();
    Refresh();
    m_sel = {rel};
    m_focusRel = m_anchorRel = rel;
    SetNote(L"Gespeichert");
    Invalidate();
}

void App::BeginInvoiceDialog(int mode) {
    if (mode == 0 && m_dlg.kind != DlgKind::None) return;
    if (mode != 0 && m_dlg.kind != DlgKind::Invoice) return;
    const int keepPage = mode ? m_dlg.page : 0;
    const bool keepSave = mode ? m_dlg.saveSender : true, keepSmall = mode ? m_dlg.smallBiz : m_settings.smallBiz;
    m_dlg = Dialog{};
    Dialog& d = m_dlg;
    d.kind = DlgKind::Invoice;
    d.title = L"Rechnung erstellen";
    d.page = keepPage;
    d.saveSender = keepSave;
    d.smallBiz = keepSmall;
    d.exampleLoaded = mode == 1;
    const std::wstring today = TodayIso();
    std::vector<std::wstring> names;
    for (const auto& x : m_all) names.push_back(x.name);
    const int thisYear = std::stoi(today.substr(0, 4));
    const std::wstring nextNo = docs::FormatInvoiceNo(thisYear, docs::NextInvoiceNo(InvoiceCounter(thisYear), names, thisYear));
    const std::wstring defNote = m_settings.payNote.empty() ? std::wstring(invoice::DefaultNote()) : m_settings.payNote;
    // Vorbelegung je Modus: 0 = gespeichertes Absender-Profil, 1 = Beispiel mit Musterdaten, 2 = leer
    struct Item3 { const wchar_t* desc; const wchar_t* qty; const wchar_t* unit; };
    std::wstring issuer, addr, tax, owner, court, phone, mail, web, bank, iban, bic, rcp, rcpAddr, number, note = defNote;
    std::wstring vat = std::to_wstring(m_settings.vat);
    std::vector<Item3> items;
    if (mode == 0) {
        issuer = m_settings.issuer; addr = m_settings.issuerAddr; tax = m_settings.taxNo; owner = m_settings.owner; court = m_settings.court;
        phone = m_settings.phone; mail = m_settings.email; web = m_settings.web; bank = m_settings.bank; iban = m_settings.iban; bic = m_settings.bic;
    } else if (mode == 1) {
        issuer = L"Mustermann Webdesign"; addr = L"Musterstraße 12 | 12345 Musterstadt"; tax = L"12/345/67890";
        owner = L"Inhaber: Max Mustermann"; court = L"Amtsgericht Musterstadt, HRB 12345";
        phone = L"+49 123 4567890"; mail = L"kontakt@mustermann.example"; web = L"www.mustermann.example";
        bank = L"Musterbank eG"; iban = L"DE02 1203 0000 0000 2020 51"; bic = L"BYLADEM1001";
        rcp = L"Beispiel GmbH"; rcpAddr = L"Hauptstraße 1 | 10115 Berlin"; number = L"BEISPIEL"; vat = L"19";
        items = {{L"Webdesign: Gestaltung und Umsetzung der Firmenwebsite", L"1", L"1.200,00"},
                 {L"Hosting und Wartung (Monate)", L"12", L"9,90"},
                 {L"Support (Stunden)", L"2", L"45,00"}};
        note = invoice::DefaultNote();
    }
    d.fields.push_back(MakeField(0, L"Gläubiger (Absender)", L"Name / Firma", issuer, 60));
    d.fields.push_back(MakeField(1, L"Absender-Adresse", L"Straße | PLZ Ort", addr, 140));
    d.fields.push_back(MakeField(2, L"Empfänger", L"Name / Firma", rcp, 60));
    d.fields.push_back(MakeField(3, L"Empfänger-Adresse", L"Straße | PLZ Ort", rcpAddr, 140));
    d.fields.push_back(MakeField(4, L"Rechnungsnummer", (L"leer = " + nextNo).c_str(), number, 30));
    d.fields.push_back(MakeField(5, L"Rechnungsdatum", L"TT.MM.JJJJ", docs::FormatDate(today), 12));
    d.fields.back().dateMask = true;
    d.fields.push_back(MakeField(6, L"Fällig am", L"TT.MM.JJJJ", docs::FormatDate(docs::AddDaysIso(today, 14)), 12));
    d.fields.back().dateMask = true;
    d.fields.push_back(MakeField(7, d.smallBiz ? L"USt-Satz (entfällt)" : L"USt-Satz (%)", L"19", vat, 3));
    d.itemRows = std::max<int>(3, (int)items.size());
    for (int i = 0; i < d.itemRows; ++i) {
        const bool have = i < (int)items.size();
        d.fields.push_back(MakeField(100 + 3 * i, L"", L"Leistung / Artikel", have ? items[i].desc : L"", 120));
        d.fields.push_back(MakeField(101 + 3 * i, L"", L"1", have ? items[i].qty : (items.empty() && i == 0 ? L"1" : L""), 8));
        d.fields.push_back(MakeField(102 + 3 * i, L"", L"0,00", have ? items[i].unit : L"", 14));
    }
    d.fields.push_back(MakeField(8, L"Schlusstext / Zahlungshinweis  ([Gesamtsumme] und [Fälligkeitsdatum] werden eingesetzt)", L"Standardtext", note, 600));
    d.fields.push_back(MakeField(9, L"Steuernummer / USt-IdNr.", L"z. B. 12/345/67890 oder DE123456789", tax, 30));
    d.fields.push_back(MakeField(10, L"Inhaber / gesetzlicher Vertreter", L"z. B. Inhaber: Max Mustermann", owner, 80));
    d.fields.push_back(MakeField(11, L"Amtsgericht / Handelsregister", L"z. B. Amtsgericht Musterstadt, HRB 12345", court, 80));
    d.fields.push_back(MakeField(12, L"Telefon", L"+49 …", phone, 40));
    d.fields.push_back(MakeField(13, L"E-Mail", L"name@beispiel.de", mail, 80));
    d.fields.push_back(MakeField(14, L"Webseite", L"www.beispiel.de", web, 80));
    d.fields.push_back(MakeField(15, L"Bank", L"Bankname", bank, 60));
    d.fields.push_back(MakeField(16, L"IBAN", L"DE00 0000 0000 0000 0000 00", iban, 40));
    d.fields.push_back(MakeField(17, L"BIC", L"BICXXXXX", bic, 14));
    for (auto& f : d.fields)
        if (f.id >= 9 && f.id <= 17) f.hidden = d.page == 0;
    m_focusId = kNoFocus;
    if (mode == 0) FocusNext(false);
    Invalidate();
}

void App::ApplyInvoice() {
    Dialog& d = m_dlg;
    auto txt = [&](int id) { const Field* f = FieldById(id); return f ? docs::Trim(f->ed.text) : std::wstring(); };
    invoice::Data data;
    data.issuer = txt(0);
    data.issuerAddress = txt(1);
    data.recipient = txt(2);
    data.recipientAddress = txt(3);
    data.note = txt(8);
    data.taxNo = txt(9);
    data.owner = txt(10); data.court = txt(11); data.phone = txt(12); data.email = txt(13); data.web = txt(14);
    data.bank = txt(15); data.iban = txt(16); data.bic = txt(17);
    data.smallBusiness = d.smallBiz;
    if (data.issuer.empty()) { d.error = L"Bitte den Gläubiger (Absender) eintragen."; Invalidate(); return; }
    if (data.recipient.empty()) { d.error = L"Bitte den Empfänger eintragen."; Invalidate(); return; }
    if (!docs::ParseDate(txt(5), data.dateIso) || data.dateIso.empty()) { d.error = L"Das Rechnungsdatum ist ungültig (TT.MM.JJJJ)."; Invalidate(); return; }
    if (!docs::ParseDate(txt(6), data.dueIso)) { d.error = L"Das Fälligkeitsdatum ist ungültig (TT.MM.JJJJ)."; Invalidate(); return; }
    try { data.vatPercent = std::stoi(txt(7).empty() ? L"0" : txt(7)); } catch (...) { data.vatPercent = -1; }
    if (d.smallBiz && (data.vatPercent < 0 || data.vatPercent > 100)) data.vatPercent = m_settings.vat;   // Satz entfaellt, Eingabe egal
    if (data.vatPercent < 0 || data.vatPercent > 100) { d.error = L"Der USt-Satz muss zwischen 0 und 100 liegen."; Invalidate(); return; }
    for (int i = 0; i < d.itemRows; ++i) {
        const std::wstring desc = txt(100 + 3 * i), qty = txt(101 + 3 * i), unit = txt(102 + 3 * i);
        if (desc.empty() && unit.empty()) continue;   // leere Zeile
        invoice::Item it;
        if (desc.empty()) { d.error = L"Position " + std::to_wstring(i + 1) + L": Bitte eine Beschreibung eintragen."; Invalidate(); return; }
        if (!qty.empty() && !invoice::ParseQuantity(qty, it.qty)) { d.error = L"Position " + std::to_wstring(i + 1) + L": Die Menge ist ungültig."; Invalidate(); return; }
        if (qty.empty()) it.qty = 1;
        bool ok = false;
        it.unitCents = docs::ParseAmountCents(unit, ok);
        if (!ok || unit.empty()) { d.error = L"Position " + std::to_wstring(i + 1) + L": Der Einzelpreis ist ungültig."; Invalidate(); return; }
        it.desc = desc;
        data.items.push_back(it);
    }
    if (data.items.empty()) { d.error = L"Bitte mindestens eine Position eintragen."; Invalidate(); return; }

    const std::wstring year = data.dateIso.substr(0, 4);
    data.number = txt(4);
    std::vector<std::wstring> names;
    for (const auto& x : m_all) names.push_back(x.name);
    const int yearNo = std::stoi(year);
    int usedNo = 0;   // vergebene laufende Nummer dieses Jahres (0 = freie Nummer, keine Zaehler-Aenderung)
    if (data.number.empty()) {   // RE-JJJJ-NNN: fortlaufend, Zaehler + vorhandene Dateien (GoBD: keine Nummer doppelt, keine Luecke durch Abbrechen)
        usedNo = docs::NextInvoiceNo(InvoiceCounter(yearNo), names, yearNo);
        data.number = docs::FormatInvoiceNo(yearNo, usedNo);
    } else {
        int y = 0, n = 0;
        if (!d.exampleLoaded && docs::InvoiceNoTaken(names, data.number)) { d.error = L"Die Rechnungsnummer " + data.number + L" ist schon vergeben."; Invalidate(); return; }
        if (docs::ParseInvoiceNo(data.number, y, n) && y == yearNo) usedNo = n;
    }
    const std::string pdf = invoice::BuildPdf(data);
    if (usedNo > InvoiceCounter(yearNo)) SetInvoiceCounter(yearNo, usedNo);
    const std::wstring fname = L"Rechnung_" + data.number + L"_" + data.recipient + L".pdf";
    const fs::path target = docs::TargetPath(m_rootPath, year, data.issuer, fname);
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    {
        std::ofstream o(target, std::ios::binary | std::ios::trunc);
        if (!o) { d.error = L"Die PDF-Datei konnte nicht geschrieben werden."; Invalidate(); return; }
        o.write(pdf.data(), (std::streamsize)pdf.size());
    }
    std::wstring rel = fs::relative(target, m_rootPath, ec).wstring();
    std::replace(rel.begin(), rel.end(), L'/', L'\\');
    docs::Meta m;
    m.cents = invoice::Compute(data).gross;
    m.due = data.dueIso;
    m.status = d.exampleLoaded ? docs::Status::Paid : docs::Status::Open;   // Beispiele sollen die offenen Betraege nicht verfaelschen
    m.income = !d.exampleLoaded;   // eigene Rechnung = Einnahme; Beispiele zaehlen nicht in die Finanz-Uebersicht
    m.tags = docs::EditTags(L"", std::wstring(d.exampleLoaded ? L"Beispiel, " : L"") + L"eigene Rechnung, " + data.recipient, L"");
    m_meta[rel] = m;
    SaveIndex();
    // Absenderdaten als Standard fuer die naechsten Rechnungen merken (nur wenn die Checkbox gesetzt ist)
    const bool savedSender = d.saveSender && !d.exampleLoaded;   // Musterdaten nie als Standard-Absender merken
    if (savedSender) {
        m_settings.issuer = data.issuer;
        m_settings.issuerAddr = txt(1);
        m_settings.payNote = data.note;
        if (!d.smallBiz) m_settings.vat = data.vatPercent;
        m_settings.taxNo = data.taxNo;
        m_settings.owner = data.owner; m_settings.court = data.court; m_settings.phone = data.phone; m_settings.email = data.email;
        m_settings.web = data.web; m_settings.bank = data.bank; m_settings.iban = data.iban; m_settings.bic = data.bic;
        m_settings.smallBiz = d.smallBiz;
        SaveSettings(m_settings);
    }
    CloseDialog();
    Refresh();
    m_sel = {rel};
    m_focusRel = m_anchorRel = rel;
    for (int i = 0; i < (int)m_view.size(); ++i)
        if (m_all[m_view[i]].rel == rel) { EnsureRowVisible(i); break; }
    SetNote(L"Rechnung " + data.number + L" erstellt: " + target.filename().wstring() + (savedSender ? L"  \u00B7  Absender als Standard gespeichert" : L""));
    OpenFile(target);
    Invalidate();
}

// ============================================================================
// Menues
// ============================================================================

void App::ShowGearMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, M_ROOT, L"Rechnungsordner ändern …");
    AppendMenuW(m, MF_STRING, M_OPENROOT, L"Rechnungsordner im Explorer öffnen");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (m_settings.moveFiles ? MF_CHECKED : 0), M_MOVE, L"Beim Hinzufügen verschieben statt kopieren");
    AppendMenuW(m, MF_STRING | (m_settings.autoImport ? MF_CHECKED : 0), M_AUTOIMPORT, L"Beim Ablegen automatisch einsortieren (Umschalt = Dialog)");
    const bool haveReader = !FindPdfReader().empty();
    AppendMenuW(m, MF_STRING | (m_settings.pdfReader && haveReader ? MF_CHECKED : 0) | (haveReader ? 0 : MF_GRAYED), M_PDFREADER,
                haveReader ? L"PDFs im Duncan's PDF Reader \u00F6ffnen" : L"PDFs im Duncan's PDF Reader \u00F6ffnen (nicht gefunden)");
    AppendMenuW(m, MF_STRING, M_RESCAN, L"Neu einlesen (F5)");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, M_ABOUT, L"Über Oggys Docs-Files");
    POINT pt{(LONG)rcTop.right - (LONG)(16 * k), (LONG)rcTop.bottom};
    ClientToScreen(m_hwnd, &pt);
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(m);
    switch (cmd) {
        case M_ROOT: ChangeRoot(); break;
        case M_OPENROOT: ShellExecuteW(m_hwnd, L"open", m_settings.root.c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
        case M_MOVE: m_settings.moveFiles = !m_settings.moveFiles; SaveSettings(m_settings); break;
        case M_AUTOIMPORT: m_settings.autoImport = !m_settings.autoImport; SaveSettings(m_settings); break;
        case M_PDFREADER: m_settings.pdfReader = !m_settings.pdfReader; SaveSettings(m_settings); break;
        case M_RESCAN: LoadIndex(); Refresh(); SetNote(L"Neu eingelesen"); break;
        case M_ABOUT: aboutcard::Open(m_about, m_hwnd); break;
    }
    Invalidate();
}

namespace {
// Farbiger, weich gezeichneter Punkt als Menue-Bitmap (32 Bit, vormultipliziertes Alpha)
HBITMAP MakeDotBitmap(unsigned rgb, int px) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = px;
    bi.bmiHeader.biHeight = -px;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP h = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!h || !bits) return h;
    auto* p = static_cast<uint32_t*>(bits);
    const float r = px / 2.f - 1.f;
    for (int y = 0; y < px; ++y)
        for (int x = 0; x < px; ++x) {
            const float dx = x + 0.5f - px / 2.f, dy = y + 0.5f - px / 2.f;
            const float cov = std::clamp(r + 0.5f - std::sqrt(dx * dx + dy * dy), 0.f, 1.f);
            const unsigned a = (unsigned)(cov * 255.f), R = (rgb >> 16) & 255, G = (rgb >> 8) & 255, B = rgb & 255;
            p[y * px + x] = (a << 24) | ((R * a / 255) << 16) | ((G * a / 255) << 8) | (B * a / 255);
        }
    return h;
}
}  // namespace

void App::ShowContextMenu(int x, int y) {
    const auto sel = SelectedDocs();
    if (sel.empty()) return;
    HMENU m = CreatePopupMenu();
    std::vector<HBITMAP> dots;   // nach dem Menue freigeben
    AppendMenuW(m, MF_STRING, C_OPEN, L"\u00D6ffnen");
    AppendMenuW(m, MF_STRING, C_SHOW, L"Im Ordner anzeigen");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    bool same = true;
    for (const auto* d : sel) same = same && d->m.status == sel[0]->m.status;
    // Reihenfolge: Bezahlt, Offen, Ratenzahlung, Fixkosten; jeweils mit Punkt in der Badge-Farbe
    const docs::Status order[4] = {docs::Status::Paid, docs::Status::Open, docs::Status::Installment, docs::Status::Fixed};
    const unsigned colors[4] = {Theme::Paid, Theme::Open, Theme::Installment, Theme::Fixed};
    const int dotPx = std::max(12, (int)std::lround(14 * k));
    for (int i = 0; i < 4; ++i) {
        const std::wstring label = std::wstring(L"Status: ") + docs::StatusName(order[i]);
        MENUITEMINFOW mi{sizeof(mi)};
        mi.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE | MIIM_BITMAP;
        mi.wID = C_STATUS0 + (UINT)order[i];
        mi.dwTypeData = const_cast<LPWSTR>(label.c_str());
        mi.fState = same && sel[0]->m.status == order[i] ? MFS_CHECKED : MFS_UNCHECKED;
        HBITMAP dot = MakeDotBitmap(colors[i], dotPx);
        dots.push_back(dot);
        mi.hbmpItem = dot;
        InsertMenuItemW(m, GetMenuItemCount(m), TRUE, &mi);
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    bool allIncome = true;
    for (const auto* d : sel) allIncome = allIncome && d->m.income;
    AppendMenuW(m, MF_STRING | (allIncome ? MF_CHECKED : 0), C_INCOME, L"Als Einnahme z\u00E4hlen");
    AppendMenuW(m, MF_STRING | (!allIncome && std::none_of(sel.begin(), sel.end(), [](const docs::Doc* d) { return d->m.income; }) ? MF_CHECKED : 0), C_EXPENSE,
                L"Als Ausgabe z\u00E4hlen");
    AppendMenuW(m, MF_STRING, C_TAGS, L"Tags vergeben \u2026\tStrg+T");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (sel.size() == 1 ? 0 : MF_GRAYED), C_EDIT, L"Bearbeiten \u2026");
    AppendMenuW(m, MF_STRING, C_DELETE, L"L\u00F6schen");
    POINT pt{x, y};
    ClientToScreen(m_hwnd, &pt);
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(m);
    for (HBITMAP b : dots)
        if (b) DeleteObject(b);
    if (cmd == C_OPEN) OpenDoc(*sel[0]);
    else if (cmd == C_SHOW) ShowInExplorer(*sel[0]);
    else if (cmd >= C_STATUS0 && cmd < C_STATUS0 + 4) SetStatusOfSelected((docs::Status)(cmd - C_STATUS0));
    else if (cmd == C_EDIT && sel.size() == 1) BeginEditDialog(*sel[0]);
    else if (cmd == C_DELETE) TrashSelected();
    else if (cmd == C_INCOME) SetKindOfSelected(true);
    else if (cmd == C_EXPENSE) SetKindOfSelected(false);
    else if (cmd == C_TAGS) BeginTagsDialog();
}

// ============================================================================
// Eingabe
// ============================================================================

void App::DialogKey(WPARAM vk) {
    if (vk == VK_ESCAPE) { CloseDialog(); return; }
    if (vk == VK_TAB) { FocusNext(GetKeyState(VK_SHIFT) < 0); return; }
    if (vk == VK_RETURN) {
        const bool ctrl = GetKeyState(VK_CONTROL) < 0;
        if (m_dlg.kind == DlgKind::Invoice && !ctrl) FocusNext(false);   // im Rechnungsformular: Enter = naechstes Feld
        else if (m_dlg.kind == DlgKind::Add) ApplyAdd(false);
        else if (m_dlg.kind == DlgKind::Edit) ApplyEdit();
        else if (m_dlg.kind == DlgKind::Tags) ApplyTags();
        else ApplyInvoice();
        return;
    }
    if (Field* f = FocusedField()) FieldKey(*f, vk);
}

void App::OnKey(WPARAM vk) {
    const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    if (m_focusId == kSearchId) {
        if (vk == VK_ESCAPE) {
            if (!m_search.ed.text.empty()) { m_search.ed.SetText(L""); RebuildView(); }
            m_focusId = kNoFocus;
        } else if (vk == VK_DOWN || vk == VK_RETURN) {
            m_focusId = kNoFocus;
            if (!m_view.empty() && m_sel.empty()) MoveSelection(0, false);
        } else {
            FieldKey(m_search, vk);
            return;
        }
        Invalidate();
        return;
    }
    if (ctrl) {
        switch (vk) {
            case 'F': m_focusId = kSearchId; m_search.ed.SelectAll(); ResetCaret(); Invalidate(); return;
            case 'N': BeginInvoiceDialog(); return;
            case 'O': PickFilesToAdd(); return;
            case 'T': BeginTagsDialog(); return;
            case 'A':
                m_sel.clear();
                for (int i : m_view) m_sel.insert(m_all[i].rel);
                Invalidate();
                return;
        }
    }
    const int page = std::max(1, (int)((H(rcList) - HeadOff()) / (kRowH * k)) - 1);
    switch (vk) {
        case VK_UP: MoveSelection(-1, shift); break;
        case VK_DOWN: MoveSelection(1, shift); break;
        case VK_PRIOR: MoveSelection(-page, shift); break;
        case VK_NEXT: MoveSelection(page, shift); break;
        case VK_HOME: MoveSelection(-(int)m_view.size(), shift); break;
        case VK_END: MoveSelection((int)m_view.size(), shift); break;
        case VK_RETURN: if (const docs::Doc* d = FindDoc(m_focusRel)) OpenDoc(*d); break;
        case VK_DELETE: TrashSelected(); break;
        case VK_F5: LoadIndex(); Refresh(); SetNote(L"Neu eingelesen"); Invalidate(); break;
        case VK_F2: if (const docs::Doc* d = FindDoc(m_focusRel)) BeginEditDialog(*d); break;
        case VK_ESCAPE: m_sel.clear(); SetNote(L""); Invalidate(); break;
    }
}

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
    if (m_about.open) {
        const int r = aboutcard::HandleMessage(m_hwnd, m_about, msg, wp, lp, L"https://github.com/Neox365/Duncans-Suite");
        if (r == 2) return TRUE;
        if (r == 1) return 0;
    }
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
            case B_ADD: PickFilesToAdd(); break;
            case B_NEW: BeginInvoiceDialog(); break;
            case B_GEAR: ShowGearMenu(); break;
            case D_OK:
                if (m_dlg.kind == DlgKind::Add) ApplyAdd(false);
                else if (m_dlg.kind == DlgKind::Edit) ApplyEdit();
                else if (m_dlg.kind == DlgKind::Tags) ApplyTags();
                else ApplyInvoice();
                break;
            case T_ADD: BeginTagsDialog(); break;
            case T_CLEAR: m_tagFilter.clear(); m_scroll = 0; RebuildView(); break;
            case D_CANCEL: CloseDialog(); break;
            case D_SAVESENDER: m_dlg.saveSender = !m_dlg.saveSender; break;
            case D_PAGE0: case D_PAGE1:
                m_dlg.page = id == D_PAGE1 ? 1 : 0;
                m_focusId = kNoFocus;
                break;
            case D_EXAMPLE: BeginInvoiceDialog(1); break;
            case D_CLEAR: BeginInvoiceDialog(2); break;
            case D_SMALLBIZ:
                m_dlg.smallBiz = !m_dlg.smallBiz;
                if (Field* v = FieldById(7)) {
                    v->label = m_dlg.smallBiz ? L"USt-Satz (entfällt)" : L"USt-Satz (%)";
                    if (m_dlg.smallBiz && m_focusId == 7) m_focusId = kNoFocus;
                }
                break;
            case D_SKIP: ApplyAdd(true); break;
            case D_ADDROW:
                if (m_dlg.itemRows < kMaxItemRows) {
                    const int i = m_dlg.itemRows++;
                    // neue Zeilenfelder vor das Feld "Zahlungshinweis" (id 8) einsetzen: Reihenfolge = Tab-Reihenfolge
                    auto it = std::find_if(m_dlg.fields.begin(), m_dlg.fields.end(), [](const Field& f) { return f.id == 8; });
                    std::vector<Field> add = {MakeField(100 + 3 * i, L"", L"Leistung / Artikel", L"", 120), MakeField(101 + 3 * i, L"", L"1", L"", 8),
                                              MakeField(102 + 3 * i, L"", L"0,00", L"", 14)};
                    m_dlg.fields.insert(it, add.begin(), add.end());
                }
                break;
            case D_DELROW:
                if (m_dlg.itemRows > 1) {
                    const int i = --m_dlg.itemRows;
                    m_dlg.fields.erase(std::remove_if(m_dlg.fields.begin(), m_dlg.fields.end(), [&](const Field& f) { return f.id >= 100 + 3 * i && f.id < 103 + 3 * i; }),
                                       m_dlg.fields.end());
                    if (FieldById(m_focusId) == nullptr) m_focusId = kNoFocus;
                }
                break;
            default:
                if (id >= T_TAG0 && id < T_TAG0 + kMaxTagChips) {   // Tag-Filter ein-/ausschalten
                    if (const int i = id - T_TAG0; i < (int)m_tags.size()) {
                        const std::wstring key = docs::Lower(m_tags[i].first);
                        if (!m_tagFilter.erase(key)) m_tagFilter.insert(key);
                        m_scroll = 0;
                        RebuildView();
                    }
                }
                else if (id >= D_TAGSUG0 && id < D_TAGSUG0 + 8) {   // Vorschlag in das Feld "Tags hinzufuegen" uebernehmen
                    if (Field* f = FieldById(0); f && id - D_TAGSUG0 < (int)m_dlg.creditorChips.size())
                        f->ed.SetText(docs::EditTags(f->ed.text, m_dlg.creditorChips[id - D_TAGSUG0], L""));
                }
                else if (id >= D_STATUS0 && id < D_STATUS0 + 4) {
                    m_dlg.status = kStatusOrder[id - D_STATUS0];
                    if (m_focusId == 6 && m_dlg.status != docs::Status::Installment) m_focusId = kNoFocus;   // Ratenfeld wird ausgeblendet
                }
                else if (id >= D_CRED0 && id < D_CRED0 + 8) {
                    if (Field* f = FieldById(0); f && id - D_CRED0 < (int)m_dlg.creditorChips.size()) {
                        f->ed.SetText(m_dlg.creditorChips[id - D_CRED0]);
                        m_focusId = kNoFocus;
                        FocusNext(false);
                    }
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
            mmi->ptMinTrackSize = {(LONG)(1000 * k), (LONG)(620 * k)};
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
        case WM_ACTIVATE:   // zurueck im Fenster: von aussen geaenderte Dateien erkennen
            if (LOWORD(wp) != WA_INACTIVE && m_dlg.kind == DlgKind::None && !m_rootPath.empty()) { Refresh(); Invalidate(); }
            return 0;
        case WM_DROPFILES: OnDropFiles(reinterpret_cast<HDROP>(wp)); return 0;
        case WM_TIMER:
            if (wp == TIMER_CARET && m_focusId != kNoFocus && GetForegroundWindow() == m_hwnd && !IsIconic(m_hwnd)) { m_caretOn = !m_caretOn; Invalidate(); }   // nur blinken, wenn das Fenster aktiv ist
            return 0;
        case WM_SETCURSOR: {
            if (LOWORD(lp) != HTCLIENT) break;
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(m_hwnd, &pt);
            bool text = In(rcSearch, (float)pt.x, (float)pt.y);
            if (m_dlg.kind != DlgKind::None) {
                text = false;
                for (const auto& f : m_dlg.fields)
                    if (In(f.rc, (float)pt.x, (float)pt.y)) text = true;
            }
            const bool hand = hit((float)pt.x, (float)pt.y) != 0 || (m_dlg.kind == DlgKind::None && (RowAt((float)pt.x, (float)pt.y) >= 0 || NodeAt((float)pt.x, (float)pt.y) >= 0 || HeaderColAt((float)pt.x, (float)pt.y) >= 0));
            SetCursor(LoadCursor(nullptr, text ? IDC_IBEAM : (hand ? IDC_HAND : IDC_ARROW)));
            return TRUE;
        }
        case WM_MOUSEMOVE: {
            const float x = (float)GET_X_LPARAM(lp), y = (float)GET_Y_LPARAM(lp);
            if (!m_tracking) {
                TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, m_hwnd, 0};
                TrackMouseEvent(&t);
                m_tracking = true;
            }
            if (m_dragField != kNoFocus) {
                if (Field* f = FieldById(m_dragField)) { FieldMouse(*f, x, true); Invalidate(); }
                return 0;
            }
            const int hb = hit(x, y);
            int hr = -1, hn = -1;
            if (m_dlg.kind == DlgKind::None) {
                hr = RowAt(x, y);
                hn = NodeAt(x, y);
            }
            if (hb != m_hoverBtn || hr != m_hoverRow || hn != m_hoverNode) {
                m_hoverBtn = hb; m_hoverRow = hr; m_hoverNode = hn;
                Invalidate();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            m_tracking = false;
            m_hoverBtn = 0; m_hoverRow = m_hoverNode = -1;
            Invalidate();
            return 0;
        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONDOWN: {
            const float x = (float)GET_X_LPARAM(lp), y = (float)GET_Y_LPARAM(lp);
            const bool shift = GetKeyState(VK_SHIFT) < 0, ctrl = GetKeyState(VK_CONTROL) < 0;
            SetFocus(m_hwnd);
            if (const int id = hit(x, y)) { command(id); return 0; }
            // Textfelder (Dialog oder Suche)
            std::vector<Field*> fields;
            if (m_dlg.kind != DlgKind::None) for (auto& f : m_dlg.fields) fields.push_back(&f);
            else fields.push_back(&m_search);
            for (Field* f : fields)
                if (!f->hidden && !f->disabled && In(f->rc, x, y)) {
                    m_focusId = f->id;
                    FieldMouse(*f, x, shift);
                    if (msg == WM_LBUTTONDBLCLK) f->ed.SelectAll();
                    m_dragField = f->id;
                    SetCapture(m_hwnd);
                    Invalidate();
                    return 0;
                }
            if (m_dlg.kind != DlgKind::None) return 0;
            m_focusId = kNoFocus;
            if (const int c = HeaderColAt(x, y); c >= 0) {
                if (m_settings.sortCol == c) m_settings.sortDesc = !m_settings.sortDesc;
                else { m_settings.sortCol = c; m_settings.sortDesc = false; }
                SaveSettings(m_settings);
                RebuildView();
            } else if (const int n = NodeAt(x, y); n >= 0) {
                m_node = n;
                m_scroll = 0;
                RebuildView();
            } else if (const int r = RowAt(x, y); r >= 0) {
                const docs::Doc& d = m_all[m_view[r]];
                if (msg == WM_LBUTTONDBLCLK) {
                    OpenDoc(d);
                } else if (ctrl) {
                    if (!m_sel.erase(d.rel)) m_sel.insert(d.rel);
                    m_focusRel = m_anchorRel = d.rel;
                } else if (shift && !m_anchorRel.empty()) {
                    int a = r;
                    for (int i = 0; i < (int)m_view.size(); ++i)
                        if (m_all[m_view[i]].rel == m_anchorRel) { a = i; break; }
                    m_sel.clear();
                    for (int i = std::min(a, r); i <= std::max(a, r); ++i) m_sel.insert(m_all[m_view[i]].rel);
                    m_focusRel = d.rel;
                } else {
                    m_sel = {d.rel};
                    m_focusRel = m_anchorRel = d.rel;
                }
            } else if (In(rcList, x, y) && y >= rcHead.bottom) {   // Klick auf die leere Liste hebt die Auswahl auf (nicht in Statistik/Tag-Leiste)
                m_sel.clear();
            }
            Invalidate();
            return 0;
        }
        case WM_LBUTTONUP:
            if (m_dragField != kNoFocus) { m_dragField = kNoFocus; ReleaseCapture(); }
            return 0;
        case WM_RBUTTONDOWN: {
            if (m_dlg.kind != DlgKind::None) return 0;
            const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            const int r = RowAt((float)x, (float)y);
            if (r < 0) return 0;
            const docs::Doc& d = m_all[m_view[r]];
            if (!m_sel.count(d.rel)) { m_sel = {d.rel}; m_focusRel = m_anchorRel = d.rel; }
            Invalidate();
            UpdateWindow(m_hwnd);
            ShowContextMenu(x, y);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(m_hwnd, &pt);
            const float notches = GET_WHEEL_DELTA_WPARAM(wp) / 120.f;
            if (m_dlg.kind == DlgKind::None) {
                if (In(rcTree, (float)pt.x, (float)pt.y)) m_treeScroll -= notches * 3 * kNodeH * k;
                else m_scroll -= notches * 3 * kRowH * k;
                ClampScroll();
                Invalidate();
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (m_dlg.kind != DlgKind::None) DialogKey(wp);
            else OnKey(wp);
            return 0;
        case WM_CHAR: {
            Field* f = FocusedField();
            if (!f) return 0;
            wchar_t ch = (wchar_t)wp;
            if (ch < 0x20) return 0;   // Steuerzeichen (Backspace, Enter, Tab, Strg+Taste) laufen ueber WM_KEYDOWN
            std::wstring s(1, ch);
            if (ch >= 0xD800 && ch <= 0xDBFF) { m_hiSurrogate = ch; return 0; }   // Ersatzpaar: erst das zweite Zeichen abwarten
            if (ch >= 0xDC00 && ch <= 0xDFFF) { if (!m_hiSurrogate) return 0; s = std::wstring(1, m_hiSurrogate) + ch; m_hiSurrogate = 0; }
            f->ed.Insert(s);
            OnFieldChanged(*f);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(m_hwnd, TIMER_CARET);
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
    // Argumente: Dateien/Ordner, die direkt hinzugefuegt werden ("Senden an", Drag auf die EXE)
    std::vector<std::wstring> startFiles;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) startFiles.push_back(argv[i]);
        LocalFree(argv);
    }
    int code = 0;
    {
        App app;
        if (app.Create(hi, show, startFiles)) {
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
