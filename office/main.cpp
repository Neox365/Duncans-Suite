// Duncan's Office: Writer (Textverarbeitung auf Basis des Windows-Rich-Edit-Steuerelements) und Sheets (Tabellenkalkulation)
// in einer Anwendung. Reines Win32 + GDI, keine Fremdbibliotheken. Alles bleibt lokal, es gibt keine Netzwerkzugriffe.
//
// Aufbau: Das Hauptfenster zeichnet Tab-Leiste, Werkzeugleiste und Statusleiste selbst (Knoepfe als Rechtecke, Auswahl per
// WM_COMMAND). Die Kindfenster sind das Rich-Edit (Writer), das Raster (eigene Fensterklasse) und die Formelzeile (Edit).
// Die Rechenlogik der Tabelle steckt in Sheet.h, die Textkodierung in Codec.h (beide ohne Win32 und getestet).
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <winspool.h>

#include <algorithm>
#include <memory>
#include <set>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../docs/PdfWriter.h"
#include "Codec.h"
#include "Sheet.h"

#ifndef APP_VERSION
#define APP_VERSION L"0.0.0"
#endif

namespace {

// Farbschema: dunkles Flat-Design
constexpr COLORREF kBg = RGB(0x1E, 0x1E, 0x2E);
constexpr COLORREF kPanel = RGB(0x2D, 0x2D, 0x3D);
constexpr COLORREF kText = RGB(0xE0, 0xE0, 0xE0);
constexpr COLORREF kAccent = RGB(0x72, 0x89, 0xDA);
constexpr COLORREF kDim = RGB(0x9A, 0x9A, 0xB2);
constexpr COLORREF kLine = RGB(0x40, 0x40, 0x58);
constexpr COLORREF kHover = RGB(0x3B, 0x3B, 0x52);
constexpr COLORREF kError = RGB(0xF2, 0x8B, 0x82);
constexpr COLORREF kPaper = RGB(0xF7, 0xF7, 0xFA);       // Seite im Writer (damit gespeicherter Text auch in anderen Programmen lesbar bleibt)
constexpr COLORREF kPaperText = RGB(0x1E, 0x1E, 0x28);

constexpr wchar_t kWndClass[] = L"DuncansOfficeWindow";
constexpr wchar_t kGridClass[] = L"DuncansOfficeGrid";
constexpr int kSizes[] = { 8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48, 72 };

enum Cmd : int {
    C_WRITER = 101, C_SHEETS, C_NEW, C_OPEN, C_SAVE, C_SAVEAS, C_PDF, C_PRINT,
    C_BOLD, C_ITALIC, C_UNDER, C_SMALLER, C_BIGGER, C_SIZE, C_LEFT, C_CENTER, C_RIGHT,
    C_PICK_WRITER, C_PICK_SHEETS,
    C_STRIKE, C_FONT, C_COLOR, C_BULLET, C_FIND, C_FINDNEXT,
    ID_RICH = 201, ID_FORMULA = 202,
    C_SIZE_BASE = 300,
    C_PRINTER_BASE = 400,   // bis 400 + Zahl der Drucker
    C_FONT_BASE = 600,      // bis 600 + Zahl der Schriftarten
    C_COLOR_BASE = 700,     // bis 700 + Zahl der Farben
    C_COLOR_MORE = 730,
    // Dialog "Suchen und Ersetzen"
    ID_F_FIND = 501, ID_F_REPL, ID_F_CASE, ID_F_WORD, ID_F_NEXT, ID_F_REPLACE, ID_F_ALL, ID_F_CLOSE, ID_F_STATUS
};
constexpr wchar_t kFindClass[] = L"DuncansOfficeFind";
struct ColorPreset { const wchar_t* name; COLORREF c; };
const ColorPreset kColors[] = {
    { L"Standard", kPaperText },          { L"Schwarz", RGB(0, 0, 0) },          { L"Grau", RGB(0x66, 0x66, 0x66) },
    { L"Rot", RGB(0xC6, 0x28, 0x28) },    { L"Orange", RGB(0xEF, 0x6C, 0x00) },  { L"Grün", RGB(0x2E, 0x7D, 0x32) },
    { L"Blau", RGB(0x15, 0x65, 0xC0) },   { L"Violett", RGB(0x6A, 0x1B, 0x9A) }, { L"Rosa", RGB(0xAD, 0x14, 0x57) },
    { L"Braun", RGB(0x5D, 0x40, 0x37) } };
constexpr int kColorCount = static_cast<int>(sizeof kColors / sizeof kColors[0]);
const wchar_t* const kFontCandidates[] = { L"Segoe UI", L"Arial", L"Calibri", L"Cambria", L"Candara", L"Consolas", L"Courier New", L"Georgia",
                                           L"Palatino Linotype", L"Tahoma", L"Times New Roman", L"Trebuchet MS", L"Verdana", L"Comic Sans MS" };
int CALLBACK CollectFont(const LOGFONTW* lf, const TEXTMETRICW*, DWORD, LPARAM p)
{
    reinterpret_cast<std::set<std::wstring>*>(p)->insert(lf->lfFaceName);
    return 1;
}
constexpr UINT WM_PRINTERS = WM_APP + 21;   // Druckerliste im Hintergrund fertig gesucht
constexpr UINT WM_TITLE_REFRESH = WM_APP + 22;   // Fenstertitel neu setzen, nachdem das Rich-Edit sein "geaendert"-Kennzeichen gesetzt hat

enum class Mode { Start, Writer, Sheets };

COLORREF Mix(COLORREF a, COLORREF b, int pct)
{
    auto ch = [&](int x, int y) { return x + (y - x) * pct / 100; };
    return RGB(ch(GetRValue(a), GetRValue(b)), ch(GetGValue(a), GetGValue(b)), ch(GetBValue(a), GetBValue(b)));
}

std::wstring FileNameOf(const std::wstring& p)
{
    const size_t k = p.find_last_of(L"\\/");
    return k == std::wstring::npos ? p : p.substr(k + 1);
}

std::wstring ExtOf(const std::wstring& p)
{
    const size_t k = p.find_last_of(L'.');
    if (k == std::wstring::npos || p.find_first_of(L"\\/", k) != std::wstring::npos) return L"";
    std::wstring e = p.substr(k);
    for (auto& ch : e) ch = static_cast<wchar_t>(std::towlower(ch));
    return e;
}

bool ReadAll(const std::wstring& path, std::string& out)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > 256ll * 1024 * 1024) { CloseHandle(h); return false; }
    out.resize(static_cast<size_t>(sz.QuadPart));
    size_t pos = 0;
    bool ok = true;
    while (pos < out.size() && ok) {
        DWORD got = 0;
        ok = ReadFile(h, &out[pos], static_cast<DWORD>(std::min<size_t>(out.size() - pos, 1u << 20)), &got, nullptr) && got > 0;
        pos += got;
    }
    CloseHandle(h);
    return ok && pos == out.size();
}

// Erst in eine .tmp-Datei schreiben und dann ersetzen, damit ein Abbruch nie die alte Datei zerstoert
bool WriteAtomic(const std::wstring& path, const std::string& data)
{
    const std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    size_t pos = 0;
    bool ok = true;
    while (pos < data.size() && ok) {
        DWORD put = 0;
        ok = WriteFile(h, data.data() + pos, static_cast<DWORD>(std::min<size_t>(data.size() - pos, 1u << 20)), &put, nullptr) && put > 0;
        pos += put;
    }
    ok = ok && FlushFileBuffers(h);
    CloseHandle(h);
    if (ok) ok = MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(tmp.c_str());
    return ok;
}

struct StreamIn { const std::string* data; size_t pos; };
DWORD CALLBACK StreamInCb(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* got)
{
    auto* s = reinterpret_cast<StreamIn*>(cookie);
    const size_t n = std::min<size_t>(static_cast<size_t>(cb), s->data->size() - s->pos);
    std::memcpy(buf, s->data->data() + s->pos, n);
    s->pos += n;
    *got = static_cast<LONG>(n);
    return 0;
}
DWORD CALLBACK StreamOutCb(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* got)
{
    reinterpret_cast<std::string*>(cookie)->append(reinterpret_cast<const char*>(buf), static_cast<size_t>(cb));
    *got = cb;
    return 0;
}

bool PutClipboard(HWND owner, const std::wstring& text)
{
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    bool ok = false;
    if (mem) {
        if (void* p = GlobalLock(mem)) {
            std::memcpy(p, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(mem);
            ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
        }
        if (!ok) GlobalFree(mem);
    }
    CloseClipboard();
    return ok;
}

std::wstring GetClipboard(HWND owner)
{
    std::wstring out;
    if (!OpenClipboard(owner)) return out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            out = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

struct Btn {
    RECT rc{};
    int id = 0;
    std::wstring text;
};

class App;
App* g_app = nullptr;

class App {
public:
    bool Create(HINSTANCE inst, int show, const std::wstring& openPath);
    int Run();

    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    LRESULT GridHandle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT FormulaKey(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref);
    LRESULT FindHandle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:
    // ---- Fenster und Darstellung ----
    int S(int v) const { return MulDiv(v, static_cast<int>(m_dpi), 96); }
    void MakeFonts();
    void Layout();
    void Paint(HDC dc, const RECT& rc);
    void DrawBtn(HDC dc, int index);
    void DrawStart(HDC dc);
    void Text(HDC dc, HFONT f, COLORREF col, RECT rc, const std::wstring& t, UINT flags = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    void RoundBox(HDC dc, const RECT& rc, int radius, COLORREF fill, COLORREF border);
    int TextW(HFONT f, const std::wstring& t);
    bool IsActive(int id) const;
    int HitBtn(POINT pt) const;
    void SwitchMode(Mode m);
    void UpdateTitle();
    void Command(int id);
    bool Ask(const std::wstring& what, const std::wstring& name, bool& save);   // false = Abbrechen

    // ---- Writer ----
    void InitRich();
    void SetDefaultFormat();
    void RefreshFmt();
    void ToggleFmt(DWORD mask, DWORD effect);
    void SetSizePt(int pt);
    void StepSize(int dir);
    void SetAlign(WORD a);
    void ShowSizeMenu();
    void ShowFontMenu();
    void ShowColorMenu();
    void SetFontName(const std::wstring& name);
    void SetTextColorRef(COLORREF c);
    void ToggleBullet();
    void ShowFindDialog();
    void ReadFindFields(HWND dlg);
    void SetFindStatus(const std::wstring& t);
    bool FindNext();
    void ReplaceOne();
    void ReplaceAll();
    bool SelectionMatches();
    void UpdateCounts();
    std::wstring RichText() const;
    bool WriterDirty() const { return SendMessageW(m_rich, EM_GETMODIFY, 0, 0) != 0; }
    bool ConfirmWriter();
    bool WriterNew();
    bool WriterOpen(const std::wstring& path);
    bool WriterSave(bool saveAs);
    void WriterPdf();
    void WriterPrint();
    void StartPrinterScan();
    void ShowPrinterMenu();
    void PrintTo(const std::wstring& printer);

    // ---- Sheets ----
    void RegisterGrid();
    int HdrW() const { return S(48); }
    int HdrH() const { return S(26); }
    int ColW() const { return S(104); }
    int RowH() const { return S(26); }
    RECT CellRect(int r, int c) const;
    void PaintGrid(HDC dc, const RECT& rc);
    void UpdateScroll();
    void ScrollTo(int x, int y);
    void OnScroll(int bar, WPARAM wp);
    void EnsureVisible(int r, int c);
    void Select(int r, int c);
    void Move(int dr, int dc);
    void LoadFormula();
    void CommitFormula();
    void BeginEdit(int firstChar);
    void ClearCell();
    void CopyCell(bool cut);
    void PasteCells();
    void UpdateSheetStatus();
    bool ConfirmSheets();
    bool SheetsNew();
    bool SheetsOpen();
    bool SheetsSave(bool saveAs);

    // ---- Dateidialoge ----
    std::wstring PickOpen(const wchar_t* filter);
    std::wstring PickSave(const wchar_t* filter, const wchar_t* defExt, const std::wstring& suggest);

    HINSTANCE m_inst = nullptr;
    HWND m_hwnd = nullptr, m_rich = nullptr, m_grid = nullptr, m_formula = nullptr;
    HACCEL m_accel = nullptr;
    UINT m_dpi = 96;
    Mode m_mode = Mode::Start;
    HFONT m_fUi = nullptr, m_fBold = nullptr, m_fTitle = nullptr, m_fHero = nullptr, m_fSmall = nullptr, m_fB = nullptr, m_fI = nullptr, m_fU = nullptr;
    HBRUSH m_brEdit = nullptr;
    RECT m_tabRc{}, m_barRc{}, m_bodyRc{}, m_statusRc{}, m_addrRc{}, m_fxRc{}, m_formulaBox{};
    std::vector<Btn> m_btns;
    int m_hot = -1, m_press = -1;
    bool m_tracking = false;
    std::wstring m_status;

    // Writer
    std::wstring m_wPath;
    bool m_bold = false, m_italic = false, m_under = false, m_strike = false, m_bullet = false;
    std::wstring m_fontName;
    std::vector<std::wstring> m_fontList;
    COLORREF m_color = kPaperText;
    std::wstring m_findText, m_replText;
    bool m_findCase = false, m_findWord = false;
    HWND m_findDlg = nullptr;
    HBRUSH m_brPanel = nullptr;
    HFONT m_fS = nullptr;   // durchgestrichenes "S" fuer den Knopf
    int m_sizePt = 0;
    WORD m_align = 0;
    bool m_wasDirty = false;
    // Drucker: die Suche (auch Netzwerkdrucker) laeuft im Hintergrund, damit das Fenster nie darauf wartet
    struct PrinterList { std::mutex mx; std::vector<std::wstring> names; std::wstring def; bool done = false; };
    std::shared_ptr<PrinterList> m_printers = std::make_shared<PrinterList>();
    bool m_printerScanning = false, m_printPending = false, m_printing = false;

    // Sheets
    sheet::Sheet m_sheet;
    std::wstring m_sPath;
    bool m_sDirty = false;
    int m_selR = 0, m_selC = 0;
    int m_scrollX = 0, m_scrollY = 0;
    bool m_editDirty = false, m_ignoreChange = false;
};

// ======================= Fenster, Schriften, Layout =======================

bool App::Create(HINSTANCE inst, int show, const std::wstring& openPath)
{
    m_inst = inst;
    g_app = this;
    if (!LoadLibraryW(L"Msftedit.dll")) {
        MessageBoxW(nullptr, L"Das Rich-Edit-Steuerelement (Msftedit.dll) konnte nicht geladen werden.", L"Duncan's Office", MB_ICONERROR);
        return false;
    }
    WNDCLASSEXW wc{ sizeof wc };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = kWndClass;
    wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
        if (m == WM_NCCREATE && g_app) g_app->m_hwnd = h;
        return g_app ? g_app->Handle(m, w, l) : DefWindowProcW(h, m, w, l);
    };
    if (!RegisterClassExW(&wc)) return false;
    RegisterGrid();

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int ww = std::min<int>(1180, work.right - work.left - 40), wh = std::min<int>(780, work.bottom - work.top - 40);
    m_hwnd = CreateWindowExW(0, kWndClass, L"Duncan's Office", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             work.left + (work.right - work.left - ww) / 2, work.top + (work.bottom - work.top - wh) / 2, ww, wh,
                             nullptr, nullptr, inst, nullptr);
    if (!m_hwnd) return false;
    // Startgroesse in Pixeln des Monitors (WM_CREATE kennt die DPI erst jetzt)
    SetWindowPos(m_hwnd, nullptr, work.left + (work.right - work.left - std::min<int>(S(1180), work.right - work.left - 40)) / 2,
                 work.top + (work.bottom - work.top - std::min<int>(S(780), work.bottom - work.top - 40)) / 2,
                 std::min<int>(S(1180), work.right - work.left - 40), std::min<int>(S(780), work.bottom - work.top - 40), SWP_NOZORDER | SWP_NOACTIVATE);

    ACCEL keys[] = {
        { FVIRTKEY | FCONTROL, 'N', C_NEW },           { FVIRTKEY | FCONTROL, 'O', C_OPEN },
        { FVIRTKEY | FCONTROL, 'S', C_SAVE },          { FVIRTKEY | FCONTROL | FSHIFT, 'S', C_SAVEAS },
        { FVIRTKEY | FCONTROL, 'P', C_PRINT },         { FVIRTKEY | FCONTROL, '1', C_WRITER },
        { FVIRTKEY | FCONTROL, '2', C_SHEETS },
        { FVIRTKEY | FCONTROL, 'B', C_BOLD },            { FVIRTKEY | FCONTROL, 'I', C_ITALIC },
        { FVIRTKEY | FCONTROL, 'U', C_UNDER },
        { FVIRTKEY | FCONTROL, 'F', C_FIND },            { FVIRTKEY | FCONTROL, 'H', C_FIND },
        { FVIRTKEY, VK_F3, C_FINDNEXT },
    };
    m_accel = CreateAcceleratorTableW(keys, static_cast<int>(sizeof keys / sizeof keys[0]));

    ShowWindow(m_hwnd, show);
    UpdateWindow(m_hwnd);
    if (!openPath.empty()) {
        if (ExtOf(openPath) == L".csv") {
            SwitchMode(Mode::Sheets);
            m_sPath = openPath;
            std::string bytes;
            if (ReadAll(openPath, bytes)) {
                sheet::FromCsv(m_sheet, codec::DecodeAuto(bytes));
                Select(0, 0);
            }
        }
        else {
            SwitchMode(Mode::Writer);
            WriterOpen(openPath);
        }
    }
    return true;
}

int App::Run()
{
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (m_accel && TranslateAcceleratorW(m_hwnd, m_accel, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void App::MakeFonts()
{
    for (HFONT* f : { &m_fUi, &m_fBold, &m_fTitle, &m_fHero, &m_fSmall, &m_fB, &m_fI, &m_fU, &m_fS })
        if (*f) { DeleteObject(*f); *f = nullptr; }
    auto mk = [&](int pt, int weight, bool italic, bool underline, bool strike = false) {
        return CreateFontW(-MulDiv(pt, static_cast<int>(m_dpi), 72), 0, 0, 0, weight, italic, underline, strike, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    };
    m_fUi = mk(10, FW_NORMAL, false, false);
    m_fBold = mk(10, FW_SEMIBOLD, false, false);
    m_fTitle = mk(13, FW_SEMIBOLD, false, false);
    m_fHero = mk(24, FW_SEMIBOLD, false, false);
    m_fSmall = mk(9, FW_NORMAL, false, false);
    m_fB = mk(11, FW_BOLD, false, false);
    m_fI = mk(11, FW_NORMAL, true, false);
    m_fU = mk(11, FW_NORMAL, false, true);
    m_fS = mk(11, FW_NORMAL, false, false, true);
    if (m_formula) SendMessageW(m_formula, WM_SETFONT, reinterpret_cast<WPARAM>(m_fUi), TRUE);
}

int App::TextW(HFONT f, const std::wstring& t)
{
    HDC dc = GetDC(m_hwnd);
    HGDIOBJ old = SelectObject(dc, f);
    SIZE sz{};
    GetTextExtentPoint32W(dc, t.c_str(), static_cast<int>(t.size()), &sz);
    SelectObject(dc, old);
    ReleaseDC(m_hwnd, dc);
    return sz.cx;
}

void App::Layout()
{
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    const int W = rc.right, H = rc.bottom;
    const int tabH = S(48), barH = S(46), statusH = S(28);
    m_btns.clear();
    m_tabRc = { 0, 0, W, tabH };
    const bool start = m_mode == Mode::Start;
    m_barRc = start ? RECT{ 0, tabH, W, tabH } : RECT{ 0, tabH, W, tabH + barH };
    m_statusRc = start ? RECT{ 0, H, W, H } : RECT{ 0, H - statusH, W, H };
    m_bodyRc = { 0, m_barRc.bottom, W, m_statusRc.top };

    auto add = [&](int id, RECT r, const std::wstring& t) { m_btns.push_back({ r, id, t }); };

    // Tab-Leiste: Marke, Reiter, Dateiknoepfe rechts
    int x = S(20) + S(26) + TextW(m_fTitle, L"Duncan's Office") + S(28);
    const int tabW = S(104);
    add(C_WRITER, { x, 0, x + tabW, tabH }, L"Writer");
    add(C_SHEETS, { x + tabW, 0, x + 2 * tabW, tabH }, L"Sheets");
    if (!start) {
        std::vector<std::pair<int, std::wstring>> files = { { C_NEW, L"Neu" }, { C_OPEN, L"Öffnen" }, { C_SAVE, L"Speichern" }, { C_SAVEAS, L"Speichern unter" } };
        if (m_mode == Mode::Writer) {
            files.push_back({ C_FIND, L"Suchen" });
            files.push_back({ C_PDF, L"PDF" });
            files.push_back({ C_PRINT, L"Drucken" });
        }
        int right = W - S(16);
        for (auto it = files.rbegin(); it != files.rend(); ++it) {
            const int bw = TextW(m_fUi, it->second) + S(28);
            add(it->first, { right - bw, (tabH - S(32)) / 2, right, (tabH + S(32)) / 2 }, it->second);
            right -= bw + S(4);
        }
    }

    // Werkzeugleiste
    const int by = m_barRc.top + (barH - S(32)) / 2, bh = S(32);
    if (m_mode == Mode::Writer) {
        int bx = S(16);
        for (int id : { C_BOLD, C_ITALIC, C_UNDER, C_STRIKE }) { add(id, { bx, by, bx + S(36), by + bh }, L""); bx += S(38); }
        bx += S(14);
        add(C_FONT, { bx, by, bx + S(170), by + bh }, L"");
        bx += S(176);
        add(C_SMALLER, { bx, by, bx + S(36), by + bh }, L"A−");
        bx += S(38);
        add(C_SIZE, { bx, by, bx + S(66), by + bh }, L"");
        bx += S(68);
        add(C_BIGGER, { bx, by, bx + S(36), by + bh }, L"A+");
        bx += S(38) + S(14);
        add(C_COLOR, { bx, by, bx + S(48), by + bh }, L"");
        bx += S(50);
        add(C_BULLET, { bx, by, bx + S(40), by + bh }, L"");
        bx += S(42) + S(14);
        for (int id : { C_LEFT, C_CENTER, C_RIGHT }) { add(id, { bx, by, bx + S(40), by + bh }, L""); bx += S(42); }
    }
    else if (m_mode == Mode::Sheets) {
        m_addrRc = { S(16), by, S(16) + S(76), by + bh };
        m_fxRc = { m_addrRc.right + S(10), by, m_addrRc.right + S(40), by + bh };
        m_formulaBox = { m_fxRc.right + S(4), by, W - S(16), by + bh };
    }
    else {
        const int cw = S(300), chh = S(210), gap = S(28);
        const int total = 2 * cw + gap;
        const int x0 = (W - total) / 2, y0 = m_bodyRc.top + S(150);
        add(C_PICK_WRITER, { x0, y0, x0 + cw, y0 + chh }, L"");
        add(C_PICK_SHEETS, { x0 + cw + gap, y0, x0 + 2 * cw + gap, y0 + chh }, L"");
    }

    // Kindfenster
    const bool writer = m_mode == Mode::Writer, sheets = m_mode == Mode::Sheets;
    if (m_rich) {
        const int pageW = std::min(W - S(40), S(940));
        const int px = (W - pageW) / 2;
        MoveWindow(m_rich, px, m_bodyRc.top + S(14), pageW, std::max(10, static_cast<int>(m_bodyRc.bottom - m_bodyRc.top) - S(14)), TRUE);
        const int pageH = std::max(10, static_cast<int>(m_bodyRc.bottom - m_bodyRc.top) - S(14));
        RECT fr{ S(44), S(36), pageW - S(44), pageH - S(8) };   // Seitenrand im Textfeld
        SendMessageW(m_rich, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&fr));
        ShowWindow(m_rich, writer ? SW_SHOW : SW_HIDE);
    }
    if (m_grid) {
        MoveWindow(m_grid, 0, m_bodyRc.top, W, std::max(10, static_cast<int>(m_bodyRc.bottom - m_bodyRc.top)), TRUE);
        ShowWindow(m_grid, sheets ? SW_SHOW : SW_HIDE);
        UpdateScroll();
    }
    if (m_formula) {
        const int eh = S(22);
        MoveWindow(m_formula, m_formulaBox.left + S(12), m_formulaBox.top + (bh - eh) / 2, std::max(10, static_cast<int>(m_formulaBox.right - m_formulaBox.left) - S(24)), eh, TRUE);
        ShowWindow(m_formula, sheets ? SW_SHOW : SW_HIDE);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void App::SwitchMode(Mode m)
{
    if (m == m_mode) return;
    m_mode = m;
    m_hot = m_press = -1;
    Layout();
    if (m == Mode::Writer) {
        SetFocus(m_rich);
        RefreshFmt();
        UpdateCounts();
        StartPrinterScan();
    }
    else if (m == Mode::Sheets) {
        SetFocus(m_grid);
        LoadFormula();
        UpdateSheetStatus();
    }
    UpdateTitle();
}

void App::UpdateTitle()
{
    std::wstring t;
    if (m_mode == Mode::Writer)
        t = (m_wPath.empty() ? L"Unbenannt" : FileNameOf(m_wPath)) + (WriterDirty() ? L"*" : L"") + L" – Writer – Duncan's Office";
    else if (m_mode == Mode::Sheets)
        t = (m_sPath.empty() ? L"Unbenannt" : FileNameOf(m_sPath)) + (m_sDirty ? L"*" : L"") + L" – Sheets – Duncan's Office";
    else
        t = L"Duncan's Office";
    SetWindowTextW(m_hwnd, t.c_str());
}

// ======================= Zeichnen =======================

void App::Text(HDC dc, HFONT f, COLORREF col, RECT rc, const std::wstring& t, UINT flags)
{
    HGDIOBJ old = SelectObject(dc, f);
    SetTextColor(dc, col);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, t.c_str(), static_cast<int>(t.size()), &rc, flags);
    SelectObject(dc, old);
}

void App::RoundBox(HDC dc, const RECT& rc, int radius, COLORREF fill, COLORREF border)
{
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius * 2, radius * 2);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

bool App::IsActive(int id) const
{
    switch (id) {
        case C_WRITER: return m_mode == Mode::Writer;
        case C_SHEETS: return m_mode == Mode::Sheets;
        case C_BOLD: return m_bold;
        case C_ITALIC: return m_italic;
        case C_UNDER: return m_under;
        case C_STRIKE: return m_strike;
        case C_BULLET: return m_bullet;
        case C_LEFT: return m_align == PFA_LEFT;
        case C_CENTER: return m_align == PFA_CENTER;
        case C_RIGHT: return m_align == PFA_RIGHT;
        default: return false;
    }
}

void App::DrawBtn(HDC dc, int index)
{
    const Btn& b = m_btns[static_cast<size_t>(index)];
    if (b.id == C_PICK_WRITER || b.id == C_PICK_SHEETS) return;   // Startkarten zeichnet DrawStart
    const bool hot = m_hot == index, down = m_press == index && hot, active = IsActive(b.id);
    const bool tab = b.id == C_WRITER || b.id == C_SHEETS;
    if (tab) {
        if (hot) { HBRUSH h = CreateSolidBrush(kHover); FillRect(dc, &b.rc, h); DeleteObject(h); }
        Text(dc, m_fBold, active ? kText : kDim, b.rc, b.text, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
        if (active) {
            RECT u{ b.rc.left + S(14), b.rc.bottom - S(3), b.rc.right - S(14), b.rc.bottom };
            HBRUSH a = CreateSolidBrush(kAccent);
            FillRect(dc, &u, a);
            DeleteObject(a);
        }
        return;
    }
    COLORREF fill = kPanel, border = kPanel;
    if (active) { fill = Mix(kPanel, kAccent, 40); border = fill; }
    if (hot) { fill = active ? Mix(kPanel, kAccent, 55) : kHover; border = fill; }
    if (down) { fill = Mix(fill, kBg, 35); border = fill; }
    RoundBox(dc, b.rc, S(7), fill, border);
    const UINT center = DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX;
    switch (b.id) {
        case C_BOLD: Text(dc, m_fB, kText, b.rc, L"B", center); break;
        case C_ITALIC: Text(dc, m_fI, kText, b.rc, L"I", center); break;
        case C_UNDER: Text(dc, m_fU, kText, b.rc, L"U", center); break;
        case C_STRIKE: Text(dc, m_fS, kText, b.rc, L"S", center); break;
        case C_FONT: {
            RECT tr{ b.rc.left + S(10), b.rc.top, b.rc.right - S(24), b.rc.bottom };
            Text(dc, m_fUi, kText, tr, m_fontName.empty() ? std::wstring(L"–") : m_fontName, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
            RECT ar{ b.rc.right - S(24), b.rc.top, b.rc.right - S(6), b.rc.bottom };
            Text(dc, m_fUi, kDim, ar, L"▾", center);
            break;
        }
        case C_COLOR: {   // "A" mit einem Streifen in der aktuellen Textfarbe
            RECT ar{ b.rc.left, b.rc.top + S(3), b.rc.right, b.rc.bottom - S(9) };
            Text(dc, m_fB, kText, ar, L"A", center);
            RECT bar{ b.rc.left + S(12), b.rc.bottom - S(10), b.rc.right - S(12), b.rc.bottom - S(6) };
            HBRUSH cb = CreateSolidBrush(m_color);
            FillRect(dc, &bar, cb);
            DeleteObject(cb);
            break;
        }
        case C_BULLET: {   // drei Zeilen mit Aufzaehlungspunkt
            const int x0 = b.rc.left + S(9), y0 = b.rc.top + (b.rc.bottom - b.rc.top - S(16)) / 2;
            HBRUSH br = CreateSolidBrush(kText);
            for (int i = 0; i < 3; ++i) {
                RECT dot{ x0, y0 + i * S(7), x0 + S(3), y0 + i * S(7) + S(3) };
                RECT line{ x0 + S(7), y0 + i * S(7) + S(1), b.rc.right - S(9), y0 + i * S(7) + S(3) };
                FillRect(dc, &dot, br);
                FillRect(dc, &line, br);
            }
            DeleteObject(br);
            break;
        }
        case C_SIZE: {
            const std::wstring t = (m_sizePt > 0 ? std::to_wstring(m_sizePt) : std::wstring(L"–")) + L"  ▾";
            Text(dc, m_fUi, kText, b.rc, t, center);
            break;
        }
        case C_LEFT: case C_CENTER: case C_RIGHT: {
            const int w = S(18), x0 = (b.rc.left + b.rc.right - w) / 2, y0 = b.rc.top + (b.rc.bottom - b.rc.top - S(14)) / 2;
            const int widths[4] = { w, w * 6 / 10, w, w * 7 / 10 };
            HBRUSH br = CreateSolidBrush(kText);
            for (int i = 0; i < 4; ++i) {
                const int lw = widths[i];
                const int lx = b.id == C_LEFT ? x0 : (b.id == C_CENTER ? x0 + (w - lw) / 2 : x0 + w - lw);
                RECT l{ lx, y0 + i * S(4), lx + lw, y0 + i * S(4) + std::max(1, S(2)) };
                FillRect(dc, &l, br);
            }
            DeleteObject(br);
            break;
        }
        default: Text(dc, m_fUi, kText, b.rc, b.text, center); break;
    }
}

void App::DrawStart(HDC dc)
{
    RECT hero{ 0, m_bodyRc.top + S(40), m_bodyRc.right, m_bodyRc.top + S(90) };
    Text(dc, m_fHero, kText, hero, L"Womit möchtest du anfangen?", DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    RECT sub{ 0, m_bodyRc.top + S(92), m_bodyRc.right, m_bodyRc.top + S(124) };
    Text(dc, m_fUi, kDim, sub, L"Alles bleibt auf deinem Rechner. Du kannst jederzeit oben zwischen Writer und Sheets wechseln.", DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);

    for (size_t i = 0; i < m_btns.size(); ++i) {
        const Btn& b = m_btns[i];
        if (b.id != C_PICK_WRITER && b.id != C_PICK_SHEETS) continue;
        const bool hot = m_hot == static_cast<int>(i), down = m_press == static_cast<int>(i) && hot;
        RoundBox(dc, b.rc, S(16), down ? Mix(kPanel, kBg, 30) : (hot ? kHover : kPanel), hot ? kAccent : kLine);
        const bool writer = b.id == C_PICK_WRITER;
        // Symbol: Seite mit Textzeilen bzw. Tabellenraster
        const int is = S(64);
        const int ix = b.rc.left + S(28), iy = b.rc.top + S(26);
        HPEN pen = CreatePen(PS_SOLID, std::max(2, S(2)), kAccent);
        HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        if (writer) {
            RoundRect(dc, ix + S(8), iy, ix + is - S(8), iy + is, S(8), S(8));
            for (int k = 0; k < 4; ++k) {
                const int ly = iy + S(14) + k * S(10);
                MoveToEx(dc, ix + S(18), ly, nullptr);
                LineTo(dc, ix + is - S(18) - (k == 3 ? S(10) : 0), ly);
            }
        }
        else {
            RoundRect(dc, ix, iy + S(6), ix + is, iy + is - S(6), S(8), S(8));
            for (int k = 1; k < 3; ++k) {
                MoveToEx(dc, ix + k * is / 3, iy + S(6), nullptr);
                LineTo(dc, ix + k * is / 3, iy + is - S(6));
                MoveToEx(dc, ix, iy + S(6) + k * (is - S(12)) / 3, nullptr);
                LineTo(dc, ix + is, iy + S(6) + k * (is - S(12)) / 3);
            }
        }
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(pen);
        RECT t1{ b.rc.left + S(28), b.rc.top + S(104), b.rc.right - S(20), b.rc.top + S(138) };
        Text(dc, m_fTitle, kText, t1, writer ? L"Duncan's Writer" : L"Duncan's Sheets");
        RECT t2{ b.rc.left + S(28), b.rc.top + S(140), b.rc.right - S(20), b.rc.bottom - S(16) };
        Text(dc, m_fUi, kDim, t2, writer ? L"Texte schreiben und formatieren. Öffnen und Speichern als .txt oder .rtf, Export als PDF, Drucken." : L"Tabellen mit Formeln (SUMME, MITTELWERT, MIN, MAX). Öffnen und Speichern als .csv.",
             DT_WORDBREAK | DT_NOPREFIX);
    }
}

void App::Paint(HDC dc, const RECT& rc)
{
    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    // Tab-Leiste
    HBRUSH pn = CreateSolidBrush(kPanel);
    FillRect(dc, &m_tabRc, pn);
    if (m_mode != Mode::Start) FillRect(dc, &m_barRc, pn);
    DeleteObject(pn);
    HBRUSH ln = CreateSolidBrush(kLine);
    RECT l1{ 0, m_tabRc.bottom - 1, rc.right, m_tabRc.bottom };
    FillRect(dc, &l1, ln);
    if (m_mode != Mode::Start) {
        RECT l2{ 0, m_barRc.bottom - 1, rc.right, m_barRc.bottom };
        FillRect(dc, &l2, ln);
    }
    DeleteObject(ln);

    // Marke
    RECT logo{ S(20), (m_tabRc.bottom - S(22)) / 2, S(20) + S(22), (m_tabRc.bottom + S(22)) / 2 };
    RoundBox(dc, logo, S(6), kAccent, kAccent);
    Text(dc, m_fSmall, RGB(0x1E, 0x1E, 0x2E), logo, L"O", DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    RECT brand{ S(20) + S(32), 0, S(20) + S(32) + TextW(m_fTitle, L"Duncan's Office") + S(8), m_tabRc.bottom };
    Text(dc, m_fTitle, kText, brand, L"Duncan's Office");

    if (m_mode == Mode::Start) DrawStart(dc);
    for (size_t i = 0; i < m_btns.size(); ++i) DrawBtn(dc, static_cast<int>(i));

    if (m_mode == Mode::Sheets) {
        RoundBox(dc, m_addrRc, S(7), kBg, kLine);
        Text(dc, m_fBold, kAccent, m_addrRc, sheet::CellName(m_selR, m_selC), DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
        Text(dc, m_fI, kDim, m_fxRc, L"fx", DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
        RoundBox(dc, m_formulaBox, S(7), kBg, GetFocus() == m_formula ? kAccent : kLine);
    }
    if (m_mode != Mode::Start) {
        HBRUSH sb = CreateSolidBrush(kPanel);
        FillRect(dc, &m_statusRc, sb);
        DeleteObject(sb);
        RECT sl{ 0, m_statusRc.top, rc.right, m_statusRc.top + 1 };
        HBRUSH lb = CreateSolidBrush(kLine);
        FillRect(dc, &sl, lb);
        DeleteObject(lb);
        RECT left{ S(16), m_statusRc.top, rc.right - S(200), m_statusRc.bottom };
        Text(dc, m_fSmall, kDim, left, m_status, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        RECT right{ rc.right - S(200), m_statusRc.top, rc.right - S(16), m_statusRc.bottom };
        Text(dc, m_fSmall, kDim, right, L"Duncan's Office v" APP_VERSION, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
    }
}

int App::HitBtn(POINT pt) const
{
    for (size_t i = 0; i < m_btns.size(); ++i)
        if (PtInRect(&m_btns[i].rc, pt)) return static_cast<int>(i);
    return -1;
}

// ======================= Befehle =======================

bool App::Ask(const std::wstring& what, const std::wstring& name, bool& save)
{
    const std::wstring msg = L"Änderungen an " + what + L" „" + name + L"“ speichern?";
    const int r = MessageBoxW(m_hwnd, msg.c_str(), L"Duncan's Office", MB_YESNOCANCEL | MB_ICONQUESTION);
    save = r == IDYES;
    return r != IDCANCEL;
}

void App::Command(int id)
{
    if (id >= C_SIZE_BASE && id < C_SIZE_BASE + static_cast<int>(sizeof kSizes / sizeof kSizes[0])) {
        SetSizePt(kSizes[id - C_SIZE_BASE]);
        return;
    }
    if (id >= C_FONT_BASE && id < C_FONT_BASE + static_cast<int>(m_fontList.size())) {
        SetFontName(m_fontList[static_cast<size_t>(id - C_FONT_BASE)]);
        return;
    }
    if (id >= C_COLOR_BASE && id < C_COLOR_BASE + kColorCount) {
        SetTextColorRef(kColors[id - C_COLOR_BASE].c);
        return;
    }
    if (id == C_COLOR_MORE) {
        static COLORREF custom[16];
        CHOOSECOLORW cc{ sizeof cc };
        cc.hwndOwner = m_hwnd;
        cc.rgbResult = m_color;
        cc.lpCustColors = custom;
        cc.Flags = CC_FULLOPEN | CC_RGBINIT;
        if (ChooseColorW(&cc)) SetTextColorRef(cc.rgbResult);
        return;
    }
    const bool writer = m_mode == Mode::Writer, sheets = m_mode == Mode::Sheets;
    switch (id) {
        case C_PICK_WRITER:
        case C_WRITER: SwitchMode(Mode::Writer); break;
        case C_PICK_SHEETS:
        case C_SHEETS: SwitchMode(Mode::Sheets); break;
        case C_NEW: if (writer) WriterNew(); else if (sheets) SheetsNew(); break;
        case C_OPEN:
            if (writer) { if (ConfirmWriter()) { const std::wstring p = PickOpen(L"Textdokumente (*.rtf;*.txt)\0*.rtf;*.txt\0Alle Dateien (*.*)\0*.*\0"); if (!p.empty()) WriterOpen(p); } }
            else if (sheets) SheetsOpen();
            break;
        case C_SAVE: if (writer) WriterSave(false); else if (sheets) SheetsSave(false); break;
        case C_SAVEAS: if (writer) WriterSave(true); else if (sheets) SheetsSave(true); break;
        case C_PDF: if (writer) WriterPdf(); break;
        case C_PRINT: if (writer) WriterPrint(); break;
        case C_BOLD: if (writer) ToggleFmt(CFM_BOLD, CFE_BOLD); break;
        case C_ITALIC: if (writer) ToggleFmt(CFM_ITALIC, CFE_ITALIC); break;
        case C_UNDER: if (writer) ToggleFmt(CFM_UNDERLINE, CFE_UNDERLINE); break;
        case C_STRIKE: if (writer) ToggleFmt(CFM_STRIKEOUT, CFE_STRIKEOUT); break;
        case C_FONT: if (writer) ShowFontMenu(); break;
        case C_COLOR: if (writer) ShowColorMenu(); break;
        case C_BULLET: if (writer) ToggleBullet(); break;
        case C_FIND: if (writer) ShowFindDialog(); break;
        case C_FINDNEXT: if (writer) { if (m_findText.empty()) ShowFindDialog(); else FindNext(); } break;
        case C_SMALLER: if (writer) StepSize(-1); break;
        case C_BIGGER: if (writer) StepSize(1); break;
        case C_SIZE: if (writer) ShowSizeMenu(); break;
        case C_LEFT: if (writer) SetAlign(PFA_LEFT); break;
        case C_CENTER: if (writer) SetAlign(PFA_CENTER); break;
        case C_RIGHT: if (writer) SetAlign(PFA_RIGHT); break;
        default: break;
    }
}

LRESULT App::Handle(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_CREATE: {
            m_dpi = GetDpiForWindow(m_hwnd);
            m_brEdit = CreateSolidBrush(kBg);
            m_brPanel = CreateSolidBrush(kPanel);
            m_rich = CreateWindowExW(0, L"RICHEDIT50W", L"", WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL | ES_WANTRETURN,
                                     0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_RICH)), m_inst, nullptr);
            m_grid = CreateWindowExW(0, kGridClass, L"", WS_CHILD | WS_HSCROLL | WS_VSCROLL | WS_TABSTOP, 0, 0, 10, 10, m_hwnd, nullptr, m_inst, nullptr);
            m_formula = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, m_hwnd,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FORMULA)), m_inst, nullptr);
            SetWindowSubclass(m_formula, [](HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) -> LRESULT {
                return g_app->FormulaKey(h, m, w, l, id, ref);
            }, 1, 0);
            const BOOL dark = TRUE;   // dunkle Titelleiste und dunkle Bildlaufleisten (ab Windows 10 20H1)
            DwmSetWindowAttribute(m_hwnd, 20, &dark, sizeof dark);
            SetWindowTheme(m_grid, L"DarkMode_Explorer", nullptr);
            SetWindowTheme(m_rich, L"DarkMode_Explorer", nullptr);
            MakeFonts();
            InitRich();
            Layout();
            UpdateScroll();
            return 0;
        }
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED && m_rich) Layout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize.x = S(980);
            mm->ptMinTrackSize.y = S(560);
            return 0;
        }
        case WM_DPICHANGED: {
            m_dpi = HIWORD(wp);
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(m_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            MakeFonts();
            Layout();
            return 0;
        }
        case WM_TITLE_REFRESH:
            if (WriterDirty() != m_wasDirty) { m_wasDirty = WriterDirty(); UpdateTitle(); }
            return 0;
        case WM_PRINTERS:
            m_printerScanning = false;
            if (m_printPending)
            {
                m_printPending = false;
                if (m_mode == Mode::Writer) ShowPrinterMenu();
            }
            if (m_mode == Mode::Writer) UpdateCounts();   // Hinweis "Drucker werden gesucht" aus der Statusleiste nehmen
            return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(m_hwnd, &ps);
            RECT rc{};
            GetClientRect(m_hwnd, &rc);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ old = SelectObject(mem, bmp);
            Paint(mem, rc);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(m_hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            const int h = HitBtn({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
            if (h != m_hot) { m_hot = h; InvalidateRect(m_hwnd, nullptr, FALSE); }
            if (!m_tracking) {
                TRACKMOUSEEVENT t{ sizeof t, TME_LEAVE, m_hwnd, 0 };
                TrackMouseEvent(&t);
                m_tracking = true;
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            m_tracking = false;
            if (m_hot != -1) { m_hot = -1; InvalidateRect(m_hwnd, nullptr, FALSE); }
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && m_hot >= 0) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
            break;
        case WM_LBUTTONDOWN:
            m_press = HitBtn({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
            SetCapture(m_hwnd);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP: {
            const int press = m_press;
            m_press = -1;
            ReleaseCapture();
            const int up = HitBtn({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
            InvalidateRect(m_hwnd, nullptr, FALSE);
            if (press >= 0 && press == up) SendMessageW(m_hwnd, WM_COMMAND, MAKEWPARAM(m_btns[static_cast<size_t>(up)].id, 0), 0);
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp), code = HIWORD(wp);
            if (id == ID_RICH) {
                if (code == EN_CHANGE) {
                    UpdateCounts();
                    RefreshFmt();
                    PostMessageW(m_hwnd, WM_TITLE_REFRESH, 0, 0);   // EN_CHANGE kommt, bevor das Kennzeichen gesetzt ist: Titel erst danach pruefen
                }
                return 0;
            }
            if (id == ID_FORMULA) {
                if (code == EN_CHANGE && !m_ignoreChange) m_editDirty = true;
                else if (code == EN_KILLFOCUS) { CommitFormula(); InvalidateRect(m_hwnd, &m_barRc, FALSE); }
                else if (code == EN_SETFOCUS) InvalidateRect(m_hwnd, &m_barRc, FALSE);
                return 0;
            }
            Command(id);
            return 0;
        }
        case WM_NOTIFY: {
            const NMHDR* nh = reinterpret_cast<const NMHDR*>(lp);
            if (nh->idFrom == ID_RICH && nh->code == EN_SELCHANGE) RefreshFmt();
            return 0;
        }
        case WM_MEASUREITEM: {
            auto* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
            if (mi->CtlType == ODT_MENU) {
                mi->itemWidth = static_cast<UINT>(S(180));
                mi->itemHeight = static_cast<UINT>(S(26));
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM: {
            const auto* di = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
            if (di->CtlType == ODT_MENU && di->itemData < static_cast<ULONG_PTR>(kColorCount)) {
                const ColorPreset& cp = kColors[di->itemData];
                HBRUSH bg = CreateSolidBrush((di->itemState & ODS_SELECTED) ? kHover : kPanel);
                FillRect(di->hDC, &di->rcItem, bg);
                DeleteObject(bg);
                RECT sw{ di->rcItem.left + S(10), di->rcItem.top + S(5), di->rcItem.left + S(26), di->rcItem.top + S(21) };
                RoundBox(di->hDC, sw, S(3), cp.c, kLine);
                RECT tr{ di->rcItem.left + S(36), di->rcItem.top, di->rcItem.right - S(8), di->rcItem.bottom };
                Text(di->hDC, m_fUi, kText, tr, cp.name);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLOREDIT:
            if (reinterpret_cast<HWND>(lp) == m_formula) {
                SetTextColor(reinterpret_cast<HDC>(wp), kText);
                SetBkColor(reinterpret_cast<HDC>(wp), kBg);
                return reinterpret_cast<LRESULT>(m_brEdit);
            }
            break;
        case WM_SETFOCUS:
            if (m_mode == Mode::Writer) SetFocus(m_rich);
            else if (m_mode == Mode::Sheets) SetFocus(m_grid);
            return 0;
        case WM_CLOSE: {
            bool save = false;
            if (WriterDirty()) {
                if (!Ask(L"dem Writer-Dokument", m_wPath.empty() ? L"Unbenannt" : FileNameOf(m_wPath), save)) return 0;
                if (save && !WriterSave(false)) return 0;
            }
            CommitFormula();
            if (m_sDirty) {
                if (!Ask(L"der Tabelle", m_sPath.empty() ? L"Unbenannt" : FileNameOf(m_sPath), save)) return 0;
                if (save && !SheetsSave(false)) return 0;
            }
            DestroyWindow(m_hwnd);
            return 0;
        }
        case WM_DESTROY:
            for (HFONT f : { m_fUi, m_fBold, m_fTitle, m_fHero, m_fSmall, m_fB, m_fI, m_fU, m_fS })
                if (f) DeleteObject(f);
            if (m_brEdit) DeleteObject(m_brEdit);
            if (m_brPanel) DeleteObject(m_brPanel);
            if (m_accel) DestroyAcceleratorTable(m_accel);
            PostQuitMessage(0);
            return 0;
        default: break;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

// ======================= Dateidialoge =======================

std::wstring App::PickOpen(const wchar_t* filter)
{
    wchar_t buf[MAX_PATH * 4] = L"";
    OPENFILENAMEW ofn{ sizeof ofn };
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = static_cast<DWORD>(sizeof buf / sizeof buf[0]);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_HIDEREADONLY;
    return GetOpenFileNameW(&ofn) ? std::wstring(buf) : std::wstring();
}

std::wstring App::PickSave(const wchar_t* filter, const wchar_t* defExt, const std::wstring& suggest)
{
    wchar_t buf[MAX_PATH * 4] = L"";
    wcsncpy_s(buf, suggest.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{ sizeof ofn };
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = static_cast<DWORD>(sizeof buf / sizeof buf[0]);
    ofn.lpstrDefExt = defExt;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_HIDEREADONLY;
    return GetSaveFileNameW(&ofn) ? std::wstring(buf) : std::wstring();
}

// ======================= Writer =======================

void App::InitRich()
{
    SendMessageW(m_rich, EM_SETTEXTMODE, TM_RICHTEXT | TM_MULTILEVELUNDO, 0);
    SendMessageW(m_rich, EM_EXLIMITTEXT, 0, 0x7FFFFFF);
    SendMessageW(m_rich, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(kPaper));
    SendMessageW(m_rich, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);
    SetDefaultFormat();
    SendMessageW(m_rich, EM_SETMODIFY, FALSE, 0);   // neues Dokument gilt als unveraendert
}

void App::SetDefaultFormat()
{
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE;
    cf.yHeight = 11 * 20;
    cf.crTextColor = kPaperText;
    wcscpy_s(cf.szFaceName, L"Segoe UI");
    SendMessageW(m_rich, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(m_rich, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
    PARAFORMAT2 pf{};
    pf.cbSize = sizeof pf;
    pf.dwMask = PFM_ALIGNMENT;
    pf.wAlignment = PFA_LEFT;
    SendMessageW(m_rich, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));
}

void App::RefreshFmt()
{
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof cf;
    SendMessageW(m_rich, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    m_bold = (cf.dwMask & CFM_BOLD) && (cf.dwEffects & CFE_BOLD);
    m_italic = (cf.dwMask & CFM_ITALIC) && (cf.dwEffects & CFE_ITALIC);
    m_under = (cf.dwMask & CFM_UNDERLINE) && (cf.dwEffects & CFE_UNDERLINE);
    m_sizePt = (cf.dwMask & CFM_SIZE) ? static_cast<int>((cf.yHeight + 10) / 20) : 0;
    m_strike = (cf.dwMask & CFM_STRIKEOUT) && (cf.dwEffects & CFE_STRIKEOUT);
    m_fontName = (cf.dwMask & CFM_FACE) ? std::wstring(cf.szFaceName) : std::wstring();
    if (cf.dwMask & CFM_COLOR) m_color = (cf.dwEffects & CFE_AUTOCOLOR) ? kPaperText : cf.crTextColor;
    PARAFORMAT2 pf{};
    pf.cbSize = sizeof pf;
    SendMessageW(m_rich, EM_GETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));
    m_align = (pf.dwMask & PFM_ALIGNMENT) ? pf.wAlignment : 0;
    m_bullet = (pf.dwMask & PFM_NUMBERING) && pf.wNumbering == PFN_BULLET;
    if (m_mode == Mode::Writer) InvalidateRect(m_hwnd, &m_barRc, FALSE);
}

void App::ToggleFmt(DWORD mask, DWORD effect)
{
    CHARFORMAT2W cur{};
    cur.cbSize = sizeof cur;
    SendMessageW(m_rich, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cur));
    const bool on = (cur.dwMask & mask) && (cur.dwEffects & effect);
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof cf;
    cf.dwMask = mask;
    cf.dwEffects = on ? 0 : effect;
    SendMessageW(m_rich, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    RefreshFmt();
    SetFocus(m_rich);
}

void App::SetSizePt(int pt)
{
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_SIZE;
    cf.yHeight = pt * 20;
    SendMessageW(m_rich, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    RefreshFmt();
    SetFocus(m_rich);
}

void App::StepSize(int dir)
{
    const int cur = m_sizePt > 0 ? m_sizePt : 11;
    int next = cur;
    if (dir > 0) {
        for (int s : kSizes) if (s > cur) { next = s; break; }
        if (next == cur) next = std::min(cur + 4, 200);
    }
    else {
        for (auto it = std::rbegin(kSizes); it != std::rend(kSizes); ++it) if (*it < cur) { next = *it; break; }
        if (next == cur) next = std::max(cur - 1, 4);
    }
    SetSizePt(next);
}

void App::SetAlign(WORD a)
{
    PARAFORMAT2 pf{};
    pf.cbSize = sizeof pf;
    pf.dwMask = PFM_ALIGNMENT;
    pf.wAlignment = a;
    SendMessageW(m_rich, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));
    RefreshFmt();
    SetFocus(m_rich);
}

void App::ShowSizeMenu()
{
    HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < sizeof kSizes / sizeof kSizes[0]; ++i)
        AppendMenuW(menu, MF_STRING | (kSizes[i] == m_sizePt ? MF_CHECKED : 0), static_cast<UINT_PTR>(C_SIZE_BASE) + i, std::to_wstring(kSizes[i]).c_str());
    for (const Btn& b : m_btns)
        if (b.id == C_SIZE) {
            POINT pt{ b.rc.left, b.rc.bottom };
            ClientToScreen(m_hwnd, &pt);
            TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);
        }
    DestroyMenu(menu);
}

std::wstring App::RichText() const
{
    GETTEXTLENGTHEX gl{ GTL_NUMCHARS, 1200 };
    const LRESULT n = SendMessageW(m_rich, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&gl), 0);
    if (n <= 0) return std::wstring();
    std::wstring w(static_cast<size_t>(n) + 1, L'\0');
    GETTEXTEX gt{};
    gt.cb = static_cast<DWORD>(w.size() * sizeof(wchar_t));
    gt.flags = GT_DEFAULT;
    gt.codepage = 1200;
    const LRESULT got = SendMessageW(m_rich, EM_GETTEXTEX, reinterpret_cast<WPARAM>(&gt), reinterpret_cast<LPARAM>(w.data()));
    w.resize(static_cast<size_t>(std::max<LRESULT>(0, got)));
    return w;
}

void App::UpdateCounts()
{
    const std::wstring t = RichText();
    size_t words = 0, chars = 0, noSpace = 0;
    bool inWord = false;
    for (wchar_t ch : t) {
        const bool space = std::iswspace(ch) != 0;
        if (ch != L'\r' && ch != L'\n' && ch != L'\v') {
            ++chars;
            if (!space) ++noSpace;
        }
        if (!space && !inWord) ++words;
        inWord = !space;
    }
    m_status = L"Wörter: " + std::to_wstring(words) + L"     Zeichen: " + std::to_wstring(chars) + L"     (ohne Leerzeichen: " + std::to_wstring(noSpace) + L")";
    if (m_mode == Mode::Writer) InvalidateRect(m_hwnd, &m_statusRc, FALSE);
}

bool App::ConfirmWriter()
{
    if (!WriterDirty()) return true;
    bool save = false;
    if (!Ask(L"dem Writer-Dokument", m_wPath.empty() ? L"Unbenannt" : FileNameOf(m_wPath), save)) return false;
    return save ? WriterSave(false) : true;
}

bool App::WriterNew()
{
    if (!ConfirmWriter()) return false;
    SendMessageW(m_rich, EM_SETSEL, 0, -1);
    SendMessageW(m_rich, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
    SetDefaultFormat();
    SendMessageW(m_rich, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(m_rich, EM_SETMODIFY, FALSE, 0);
    m_wPath.clear();
    m_wasDirty = false;
    RefreshFmt();
    UpdateCounts();
    UpdateTitle();
    SetFocus(m_rich);
    return true;
}

bool App::WriterOpen(const std::wstring& path)
{
    std::string bytes;
    if (!ReadAll(path, bytes)) {
        MessageBoxW(m_hwnd, (L"Die Datei konnte nicht gelesen werden:\n" + path).c_str(), L"Duncan's Office", MB_ICONWARNING);
        return false;
    }
    const bool rtf = ExtOf(path) == L".rtf" || bytes.compare(0, 5, "{\\rtf") == 0;
    if (rtf) {
        StreamIn ctx{ &bytes, 0 };
        EDITSTREAM es{};
        es.dwCookie = reinterpret_cast<DWORD_PTR>(&ctx);
        es.pfnCallback = StreamInCb;
        SendMessageW(m_rich, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&es));
        if (es.dwError) {
            MessageBoxW(m_hwnd, L"Das RTF-Dokument konnte nicht gelesen werden.", L"Duncan's Office", MB_ICONWARNING);
            return false;
        }
    }
    else {
        const std::wstring text = codec::DecodeAuto(bytes);
        SETTEXTEX st{ ST_DEFAULT, 1200 };
        SendMessageW(m_rich, EM_SETTEXTEX, reinterpret_cast<WPARAM>(&st), reinterpret_cast<LPARAM>(text.c_str()));
    }
    SendMessageW(m_rich, EM_SETSEL, 0, 0);
    SendMessageW(m_rich, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(m_rich, EM_SETMODIFY, FALSE, 0);
    m_wPath = path;
    m_wasDirty = false;
    RefreshFmt();
    UpdateCounts();
    UpdateTitle();
    SetFocus(m_rich);
    return true;
}

bool App::WriterSave(bool saveAs)
{
    std::wstring path = m_wPath;
    if (saveAs || path.empty()) {
        path = PickSave(L"Rich-Text-Dokument (*.rtf)\0*.rtf\0Nur Text (*.txt, ohne Formatierung)\0*.txt\0", L"rtf", m_wPath.empty() ? L"Unbenannt" : FileNameOf(m_wPath));
        if (path.empty()) return false;
    }
    std::string data;
    if (ExtOf(path) == L".txt") {
        std::wstring t = RichText(), out;
        out.reserve(t.size() + t.size() / 16);
        for (wchar_t ch : t) {
            if (ch == L'\r' || ch == L'\v') out += L"\r\n";
            else if (ch != L'\n') out.push_back(ch);
        }
        data = codec::EncodeUtf8Bom(out);
    }
    else {
        EDITSTREAM es{};
        es.dwCookie = reinterpret_cast<DWORD_PTR>(&data);
        es.pfnCallback = StreamOutCb;
        SendMessageW(m_rich, EM_STREAMOUT, SF_RTF, reinterpret_cast<LPARAM>(&es));
        if (es.dwError || data.empty()) {
            MessageBoxW(m_hwnd, L"Das Dokument konnte nicht als RTF erzeugt werden.", L"Duncan's Office", MB_ICONWARNING);
            return false;
        }
    }
    if (!WriteAtomic(path, data)) {
        MessageBoxW(m_hwnd, (L"Die Datei konnte nicht gespeichert werden:\n" + path).c_str(), L"Duncan's Office", MB_ICONWARNING);
        return false;
    }
    m_wPath = path;
    SendMessageW(m_rich, EM_SETMODIFY, FALSE, 0);
    m_wasDirty = false;
    UpdateTitle();
    return true;
}

// Einfacher PDF-Export: der reine Text (ohne Formatierung) in Helvetica 11 pt auf A4-Seiten mit Zeilenumbruch
void App::WriterPdf()
{
    std::wstring base = m_wPath.empty() ? L"Unbenannt" : FileNameOf(m_wPath);
    const size_t dot = base.find_last_of(L'.');
    if (dot != std::wstring::npos) base.resize(dot);
    const std::wstring path = PickSave(L"PDF-Dokument (*.pdf)\0*.pdf\0", L"pdf", base);
    if (path.empty()) return;

    constexpr double kMargin = 56, kSize = 11, kLead = 15;
    const double maxW = pdfw::kPageW - 2 * kMargin;
    std::vector<std::wstring> lines;
    std::wstring text = RichText(), para;
    auto flushPara = [&]() {
        if (para.empty()) { lines.emplace_back(); return; }
        std::wstring cur;
        size_t i = 0;
        while (i <= para.size()) {
            size_t e = para.find(L' ', i);
            if (e == std::wstring::npos) e = para.size();
            std::wstring word = para.substr(i, e - i);
            i = e + 1;
            const std::wstring tryLine = cur.empty() ? word : cur + L" " + word;
            if (pdfw::Document::Width(tryLine, kSize) <= maxW) { cur = tryLine; continue; }
            if (!cur.empty()) lines.push_back(cur);
            cur.clear();
            while (pdfw::Document::Width(word, kSize) > maxW && word.size() > 1) {   // sehr langes Wort Zeichen fuer Zeichen umbrechen
                size_t n = 1;
                while (n < word.size() && pdfw::Document::Width(word.substr(0, n + 1), kSize) <= maxW) ++n;
                lines.push_back(word.substr(0, n));
                word.erase(0, n);
            }
            cur = word;
        }
        lines.push_back(cur);
        para.clear();
    };
    for (wchar_t ch : text) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\v') { flushPara(); para.clear(); }
        else if (ch == L'\t') para += L"    ";
        else para.push_back(ch);
    }
    if (!para.empty()) flushPara();

    pdfw::Document doc;
    double y = pdfw::kPageH - kMargin - kSize;
    for (const std::wstring& line : lines) {
        if (y < kMargin) { doc.NewPage(); y = pdfw::kPageH - kMargin - kSize; }
        if (!line.empty()) doc.Text(kMargin, y, kSize, false, line);
        y -= kLead;
    }
    doc.Footer(L"Duncan's Office");
    if (!WriteAtomic(path, doc.Build(base))) MessageBoxW(m_hwnd, L"Die PDF-Datei konnte nicht gespeichert werden.", L"Duncan's Office", MB_ICONWARNING);
}

void App::StartPrinterScan()
{
    if (m_printerScanning) return;
    m_printerScanning = true;
    const std::shared_ptr<PrinterList> list = m_printers;
    const HWND wnd = m_hwnd;
    {
        std::lock_guard<std::mutex> lk(list->mx);
        list->done = false;
    }
    std::thread([list, wnd]() {
        std::vector<std::wstring> names;
        DWORD need = 0, count = 0;
        // Stufe 4 liefert nur die Namen (schnell); Netzwerkdrucker werden nicht einzeln abgefragt
        EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, nullptr, 4, nullptr, 0, &need, &count);
        if (need > 0) {
            std::vector<BYTE> buf(need);
            if (EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, nullptr, 4, buf.data(), need, &need, &count)) {
                const auto* info = reinterpret_cast<const PRINTER_INFO_4W*>(buf.data());
                for (DWORD i = 0; i < count; ++i)
                    if (info[i].pPrinterName) names.emplace_back(info[i].pPrinterName);
            }
        }
        wchar_t def[512] = L"";
        DWORD n = 512;
        GetDefaultPrinterW(def, &n);
        std::sort(names.begin(), names.end());
        {
            std::lock_guard<std::mutex> lk(list->mx);
            list->names = std::move(names);
            list->def = def;
            list->done = true;
        }
        if (IsWindow(wnd)) PostMessageW(wnd, WM_PRINTERS, 0, 0);
    }).detach();
}

// Drucken: Klick zeigt die Druckerliste (im Hintergrund gesucht, ein Klick waehrend der Suche merkt sich nur den Wunsch)
void App::WriterPrint()
{
    if (m_printing) return;   // ein Druckauftrag laeuft schon: weitere Klicks ignorieren
    bool done;
    {
        std::lock_guard<std::mutex> lk(m_printers->mx);
        done = m_printers->done;
    }
    if (done) { ShowPrinterMenu(); return; }
    m_printPending = true;
    m_status = L"Drucker werden gesucht …";
    InvalidateRect(m_hwnd, &m_statusRc, FALSE);
    StartPrinterScan();
}

void App::ShowPrinterMenu()
{
    std::vector<std::wstring> names;
    std::wstring def;
    {
        std::lock_guard<std::mutex> lk(m_printers->mx);
        names = m_printers->names;
        def = m_printers->def;
    }
    if (names.empty()) {
        MessageBoxW(m_hwnd, L"Es wurde kein Drucker gefunden. Bitte in Windows einen Drucker einrichten.", L"Duncan's Office", MB_ICONINFORMATION);
        return;
    }
    HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < names.size() && i < 90; ++i)
        AppendMenuW(menu, MF_STRING | (names[i] == def ? MF_CHECKED : 0), static_cast<UINT_PTR>(C_PRINTER_BASE) + i, names[i].c_str());
    POINT pt{ 0, 0 };
    for (const Btn& b : m_btns)
        if (b.id == C_PRINT) pt = { b.rc.left, b.rc.bottom };
    ClientToScreen(m_hwnd, &pt);
    SetForegroundWindow(m_hwnd);
    const int pick = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
    if (pick >= C_PRINTER_BASE && pick < C_PRINTER_BASE + static_cast<int>(names.size()))
        PrintTo(names[static_cast<size_t>(pick - C_PRINTER_BASE)]);
}

void App::PrintTo(const std::wstring& printer)
{
    if (m_printing) return;
    m_printing = true;
    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    HDC hdc = CreateDCW(L"WINSPOOL", printer.c_str(), nullptr, nullptr);   // Standardeinstellungen des Druckers
    bool ok = hdc != nullptr;
    if (ok) {
        const std::wstring name = m_wPath.empty() ? L"Unbenannt" : FileNameOf(m_wPath);
        DOCINFOW di{ sizeof di, name.c_str(), nullptr, nullptr, 0 };
        ok = StartDocW(hdc, &di) > 0;
        if (ok) {
            const int lx = GetDeviceCaps(hdc, LOGPIXELSX), ly = GetDeviceCaps(hdc, LOGPIXELSY);
            auto twx = [&](int px) { return MulDiv(px, 1440, lx); };
            auto twy = [&](int px) { return MulDiv(px, 1440, ly); };
            const int pageW = twx(GetDeviceCaps(hdc, HORZRES)), pageH = twy(GetDeviceCaps(hdc, VERTRES));
            const int offX = twx(GetDeviceCaps(hdc, PHYSICALOFFSETX)), offY = twy(GetDeviceCaps(hdc, PHYSICALOFFSETY));
            const int physW = twx(GetDeviceCaps(hdc, PHYSICALWIDTH)), physH = twy(GetDeviceCaps(hdc, PHYSICALHEIGHT));
            constexpr int margin = 1080;   // 0,75 Zoll
            FORMATRANGE fr{};
            fr.hdc = fr.hdcTarget = hdc;
            fr.rcPage = { 0, 0, pageW, pageH };
            fr.rc = { std::max(0, margin - offX), std::max(0, margin - offY), pageW - std::max(0, margin - (physW - offX - pageW)),
                      pageH - std::max(0, margin - (physH - offY - pageH)) };
            GETTEXTLENGTHEX gl{ GTL_NUMCHARS, 1200 };
            const LONG total = static_cast<LONG>(SendMessageW(m_rich, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&gl), 0));
            LONG pos = 0;
            while (pos < total && ok) {
                ok = StartPage(hdc) > 0;
                if (!ok) break;
                fr.chrg.cpMin = pos;
                fr.chrg.cpMax = -1;
                const LONG next = static_cast<LONG>(SendMessageW(m_rich, EM_FORMATRANGE, TRUE, reinterpret_cast<LPARAM>(&fr)));
                ok = EndPage(hdc) > 0;
                if (next <= pos) break;   // kein Fortschritt: Endlosschleife vermeiden
                pos = next;
            }
            SendMessageW(m_rich, EM_FORMATRANGE, FALSE, 0);
            if (ok) EndDoc(hdc); else AbortDoc(hdc);
        }
        DeleteDC(hdc);
    }
    m_printing = false;
    if (!ok) MessageBoxW(m_hwnd, L"Der Druckauftrag ist fehlgeschlagen.", L"Duncan's Office", MB_ICONWARNING);
}

// ======================= Sheets: Raster =======================

void App::RegisterGrid()
{
    WNDCLASSEXW wc{ sizeof wc };
    wc.style = CS_DBLCLKS;
    wc.hInstance = m_inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kGridClass;
    wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT { return g_app->GridHandle(h, m, w, l); };
    RegisterClassExW(&wc);
}

RECT App::CellRect(int r, int c) const
{
    const int x = HdrW() + c * ColW() - m_scrollX, y = HdrH() + r * RowH() - m_scrollY;
    return { x, y, x + ColW(), y + RowH() };
}

void App::UpdateScroll()
{
    if (!m_grid) return;
    RECT rc{};
    GetClientRect(m_grid, &rc);
    const int viewW = std::max(1, static_cast<int>(rc.right) - HdrW()), viewH = std::max(1, static_cast<int>(rc.bottom) - HdrH());
    const int maxX = std::max(0, sheet::kCols * ColW() - viewW), maxY = std::max(0, sheet::kRows * RowH() - viewH);
    m_scrollX = std::clamp(m_scrollX, 0, maxX);
    m_scrollY = std::clamp(m_scrollY, 0, maxY);
    SCROLLINFO si{ sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, 0, sheet::kCols * ColW() - 1, static_cast<UINT>(viewW), m_scrollX, 0 };
    SetScrollInfo(m_grid, SB_HORZ, &si, TRUE);
    si = { sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, 0, sheet::kRows * RowH() - 1, static_cast<UINT>(viewH), m_scrollY, 0 };
    SetScrollInfo(m_grid, SB_VERT, &si, TRUE);
}

void App::ScrollTo(int x, int y)
{
    m_scrollX = x;
    m_scrollY = y;
    UpdateScroll();
    InvalidateRect(m_grid, nullptr, FALSE);
}

void App::OnScroll(int bar, WPARAM wp)
{
    SCROLLINFO si{ sizeof si, SIF_ALL, 0, 0, 0, 0, 0 };
    GetScrollInfo(m_grid, bar, &si);
    const int line = bar == SB_HORZ ? ColW() / 2 : RowH();
    int pos = si.nPos;
    switch (LOWORD(wp)) {
        case SB_LINEUP: pos -= line; break;
        case SB_LINEDOWN: pos += line; break;
        case SB_PAGEUP: pos -= static_cast<int>(si.nPage); break;
        case SB_PAGEDOWN: pos += static_cast<int>(si.nPage); break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        case SB_TOP: pos = 0; break;
        case SB_BOTTOM: pos = si.nMax; break;
        default: return;
    }
    if (bar == SB_HORZ) ScrollTo(pos, m_scrollY);
    else ScrollTo(m_scrollX, pos);
}

void App::EnsureVisible(int r, int c)
{
    RECT rc{};
    GetClientRect(m_grid, &rc);
    const int viewW = rc.right - HdrW(), viewH = rc.bottom - HdrH();
    int x = m_scrollX, y = m_scrollY;
    const int x0 = c * ColW(), x1 = x0 + ColW(), y0 = r * RowH(), y1 = y0 + RowH();
    if (x0 < x) x = x0; else if (x1 > x + viewW) x = x1 - viewW;
    if (y0 < y) y = y0; else if (y1 > y + viewH) y = y1 - viewH;
    if (x != m_scrollX || y != m_scrollY) ScrollTo(x, y);
}

void App::PaintGrid(HDC dc, const RECT& rc)
{
    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    const int hw = HdrW(), hh = HdrH(), cw = ColW(), rh = RowH();
    const int firstC = m_scrollX / cw, lastC = std::min<int>(sheet::kCols - 1, (m_scrollX + rc.right) / cw);
    const int firstR = m_scrollY / rh, lastR = std::min<int>(sheet::kRows - 1, (m_scrollY + rc.bottom) / rh);

    SaveDC(dc);
    IntersectClipRect(dc, hw, hh, rc.right, rc.bottom);
    HPEN grid = CreatePen(PS_SOLID, 1, kLine);
    HGDIOBJ oldPen = SelectObject(dc, grid);
    HGDIOBJ oldFont = SelectObject(dc, m_fUi);
    for (int r = firstR; r <= lastR; ++r) {
        for (int c = firstC; c <= lastC; ++c) {
            const RECT cell = CellRect(r, c);
            MoveToEx(dc, cell.right - 1, cell.top, nullptr);
            LineTo(dc, cell.right - 1, cell.bottom - 1);
            LineTo(dc, cell.left - 1, cell.bottom - 1);
            const sheet::Value& v = m_sheet.Get(r, c);
            if (v.kind == sheet::Value::Empty) continue;
            const std::wstring t = m_sheet.Display(r, c);
            RECT tr{ cell.left + S(6), cell.top, cell.right - S(7), cell.bottom - 1 };
            SetTextColor(dc, v.kind == sheet::Value::Error ? kError : kText);
            DrawTextW(dc, t.c_str(), static_cast<int>(t.size()), &tr,
                      DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS | (v.kind == sheet::Value::Number ? DT_RIGHT : DT_LEFT));
        }
    }
    // Auswahlrahmen
    {
        const RECT s = CellRect(m_selR, m_selC);
        HPEN sel = CreatePen(PS_INSIDEFRAME, std::max(2, S(2)), kAccent);
        HGDIOBJ op = SelectObject(dc, sel), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, s.left - 1, s.top - 1, s.right, s.bottom);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(sel);
    }
    RestoreDC(dc, -1);

    // Kopfzeile (A..Z) und Zeilennummern, die aktive Spalte/Zeile hervorgehoben
    SelectObject(dc, m_fUi);
    SaveDC(dc);
    IntersectClipRect(dc, hw, 0, rc.right, hh);
    for (int c = firstC; c <= lastC; ++c) {
        RECT h{ hw + c * cw - m_scrollX, 0, hw + (c + 1) * cw - m_scrollX, hh };
        HBRUSH b = CreateSolidBrush(c == m_selC ? kHover : kPanel);
        FillRect(dc, &h, b);
        DeleteObject(b);
        MoveToEx(dc, h.right - 1, 0, nullptr);
        LineTo(dc, h.right - 1, hh);
        SetTextColor(dc, c == m_selC ? kAccent : kDim);
        const std::wstring t = sheet::ColName(c);
        DrawTextW(dc, t.c_str(), static_cast<int>(t.size()), &h, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    }
    RestoreDC(dc, -1);
    SaveDC(dc);
    IntersectClipRect(dc, 0, hh, hw, rc.bottom);
    for (int r = firstR; r <= lastR; ++r) {
        RECT h{ 0, hh + r * rh - m_scrollY, hw, hh + (r + 1) * rh - m_scrollY };
        HBRUSH b = CreateSolidBrush(r == m_selR ? kHover : kPanel);
        FillRect(dc, &h, b);
        DeleteObject(b);
        MoveToEx(dc, 0, h.bottom - 1, nullptr);
        LineTo(dc, hw, h.bottom - 1);
        SetTextColor(dc, r == m_selR ? kAccent : kDim);
        const std::wstring t = std::to_wstring(r + 1);
        DrawTextW(dc, t.c_str(), static_cast<int>(t.size()), &h, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    }
    RestoreDC(dc, -1);
    RECT corner{ 0, 0, hw, hh };
    HBRUSH cb = CreateSolidBrush(kPanel);
    FillRect(dc, &corner, cb);
    DeleteObject(cb);
    MoveToEx(dc, hw - 1, 0, nullptr);
    LineTo(dc, hw - 1, hh);
    MoveToEx(dc, 0, hh - 1, nullptr);
    LineTo(dc, hw, hh - 1);
    SelectObject(dc, oldFont);
    SelectObject(dc, oldPen);
    DeleteObject(grid);
}

void App::Select(int r, int c)
{
    CommitFormula();
    m_selR = std::clamp(r, 0, sheet::kRows - 1);
    m_selC = std::clamp(c, 0, sheet::kCols - 1);
    EnsureVisible(m_selR, m_selC);
    LoadFormula();
    UpdateSheetStatus();
    InvalidateRect(m_grid, nullptr, FALSE);
    InvalidateRect(m_hwnd, &m_barRc, FALSE);
}

void App::Move(int dr, int dc) { Select(m_selR + dr, m_selC + dc); }

void App::LoadFormula()
{
    m_ignoreChange = true;
    SetWindowTextW(m_formula, m_sheet.raw[m_selR][m_selC].c_str());
    m_ignoreChange = false;
    m_editDirty = false;
}

void App::CommitFormula()
{
    if (!m_editDirty) return;
    m_editDirty = false;
    const int n = GetWindowTextLengthW(m_formula);
    std::wstring t(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(m_formula, t.data(), n + 1);
    t.resize(static_cast<size_t>(n));
    if (t == m_sheet.raw[m_selR][m_selC]) return;
    m_sheet.Set(m_selR, m_selC, t);
    m_sDirty = true;
    InvalidateRect(m_grid, nullptr, FALSE);
    UpdateSheetStatus();
    UpdateTitle();
}

void App::BeginEdit(int firstChar)
{
    if (firstChar > 0) {
        m_ignoreChange = true;
        SetWindowTextW(m_formula, std::wstring(1, static_cast<wchar_t>(firstChar)).c_str());
        m_ignoreChange = false;
        m_editDirty = true;
    }
    SetFocus(m_formula);
    SendMessageW(m_formula, EM_SETSEL, 0x7FFFFFFF, 0x7FFFFFFF);
}

void App::ClearCell()
{
    if (m_sheet.raw[m_selR][m_selC].empty()) return;
    m_sheet.Set(m_selR, m_selC, L"");
    m_sDirty = true;
    LoadFormula();
    UpdateSheetStatus();
    UpdateTitle();
    InvalidateRect(m_grid, nullptr, FALSE);
}

void App::CopyCell(bool cut)
{
    PutClipboard(m_hwnd, m_sheet.raw[m_selR][m_selC]);
    if (cut) ClearCell();
}

void App::PasteCells()
{
    const std::wstring t = GetClipboard(m_hwnd);
    if (t.empty()) return;
    int r = m_selR, c0 = m_selC, c = c0;
    std::wstring field;
    bool changed = false;
    auto put = [&]() {
        if (r < sheet::kRows && c < sheet::kCols) { m_sheet.raw[r][c] = field; changed = true; }
        field.clear();
    };
    for (size_t i = 0; i < t.size(); ++i) {
        const wchar_t ch = t[i];
        if (ch == L'\t') { put(); ++c; }
        else if (ch == L'\r') continue;
        else if (ch == L'\n') {
            put();
            ++r;
            c = c0;
            if (i + 1 == t.size()) { field.clear(); r = sheet::kRows; }   // abschliessender Zeilenumbruch erzeugt keine leere Zeile
        }
        else field.push_back(ch);
    }
    if (!field.empty() || (!t.empty() && t.back() != L'\n')) put();
    if (!changed) return;
    m_sheet.Invalidate();
    m_sDirty = true;
    LoadFormula();
    UpdateSheetStatus();
    UpdateTitle();
    InvalidateRect(m_grid, nullptr, FALSE);
}

void App::UpdateSheetStatus()
{
    const sheet::Value& v = m_sheet.Get(m_selR, m_selC);
    m_status = L"Zelle " + sheet::CellName(m_selR, m_selC);
    if (v.kind != sheet::Value::Empty) m_status += L"     Wert: " + m_sheet.Display(m_selR, m_selC);
    else m_status += L"     Tipp: Formeln beginnen mit =, zum Beispiel =SUMME(A1:A5) oder =MITTELWERT(B1:B9)";
    if (m_mode == Mode::Sheets) InvalidateRect(m_hwnd, &m_statusRc, FALSE);
}

LRESULT App::GridHandle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ old = SelectObject(mem, bmp);
            PaintGrid(mem, rc);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SIZE: UpdateScroll(); return 0;
        case WM_HSCROLL: OnScroll(SB_HORZ, wp); return 0;
        case WM_VSCROLL: OnScroll(SB_VERT, wp); return 0;
        case WM_MOUSEWHEEL: {
            const int steps = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
            if (LOWORD(wp) & MK_SHIFT) ScrollTo(m_scrollX - steps * ColW(), m_scrollY);
            else ScrollTo(m_scrollX, m_scrollY - steps * 3 * RowH());
            return 0;
        }
        case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS | DLGC_WANTALLKEYS;
        case WM_SETFOCUS:
        case WM_KILLFOCUS: InvalidateRect(hwnd, nullptr, FALSE); return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            SetFocus(hwnd);
            const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (x >= HdrW() && y >= HdrH()) {
                Select((y - HdrH() + m_scrollY) / RowH(), (x - HdrW() + m_scrollX) / ColW());
                if (msg == WM_LBUTTONDBLCLK) BeginEdit(0);
            }
            return 0;
        }
        case WM_KEYDOWN: {
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            switch (wp) {
                case VK_LEFT: Move(0, -1); return 0;
                case VK_RIGHT: Move(0, 1); return 0;
                case VK_UP: Move(-1, 0); return 0;
                case VK_DOWN: Move(1, 0); return 0;
                case VK_TAB: Move(0, shift ? -1 : 1); return 0;
                case VK_RETURN: Move(shift ? -1 : 1, 0); return 0;
                case VK_PRIOR: Move(-10, 0); return 0;
                case VK_NEXT: Move(10, 0); return 0;
                case VK_HOME: if (ctrl) Select(0, 0); else Select(m_selR, 0); return 0;
                case VK_END: {
                    int rows = 0, cols = 0;
                    if (m_sheet.Used(rows, cols)) { if (ctrl) Select(rows - 1, cols - 1); else Select(m_selR, cols - 1); }
                    return 0;
                }
                case VK_DELETE:
                case VK_BACK: ClearCell(); return 0;
                case VK_F2: BeginEdit(0); return 0;
                case 'C': if (ctrl) { CopyCell(false); return 0; } break;
                case 'X': if (ctrl) { CopyCell(true); return 0; } break;
                case 'V': if (ctrl) { PasteCells(); return 0; } break;
                default: break;
            }
            break;
        }
        case WM_CHAR:
            if (wp >= 32 && wp != 127) { BeginEdit(static_cast<int>(wp)); return 0; }
            return 0;
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Formelzeile: Enter/Tab/Pfeil hoch/runter uebernehmen die Eingabe, Esc verwirft sie
LRESULT App::FormulaKey(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    if (msg == WM_GETDLGCODE) return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;
    if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_TAB || wp == VK_ESCAPE)) return 0;   // kein Signalton
    if (msg == WM_KEYDOWN) {
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        switch (wp) {
            case VK_RETURN: Move(shift ? -1 : 1, 0); SetFocus(m_grid); return 0;
            case VK_TAB: Move(0, shift ? -1 : 1); SetFocus(m_grid); return 0;
            case VK_DOWN: Move(1, 0); SetFocus(m_grid); return 0;
            case VK_UP: Move(-1, 0); SetFocus(m_grid); return 0;
            case VK_ESCAPE: LoadFormula(); SetFocus(m_grid); return 0;
            default: break;
        }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// ======================= Sheets: Dateien =======================

bool App::ConfirmSheets()
{
    CommitFormula();
    if (!m_sDirty) return true;
    bool save = false;
    if (!Ask(L"der Tabelle", m_sPath.empty() ? L"Unbenannt" : FileNameOf(m_sPath), save)) return false;
    return save ? SheetsSave(false) : true;
}

bool App::SheetsNew()
{
    if (!ConfirmSheets()) return false;
    m_sheet.Clear();
    m_sPath.clear();
    m_sDirty = false;
    m_scrollX = m_scrollY = 0;
    Select(0, 0);
    UpdateScroll();
    UpdateTitle();
    return true;
}

bool App::SheetsOpen()
{
    if (!ConfirmSheets()) return false;
    const std::wstring path = PickOpen(L"CSV-Dateien (*.csv)\0*.csv\0Textdateien (*.txt)\0*.txt\0Alle Dateien (*.*)\0*.*\0");
    if (path.empty()) return false;
    std::string bytes;
    if (!ReadAll(path, bytes)) {
        MessageBoxW(m_hwnd, (L"Die Datei konnte nicht gelesen werden:\n" + path).c_str(), L"Duncan's Office", MB_ICONWARNING);
        return false;
    }
    const int dropped = sheet::FromCsv(m_sheet, codec::DecodeAuto(bytes));
    m_sPath = path;
    m_sDirty = false;
    m_scrollX = m_scrollY = 0;
    m_selR = m_selC = 0;
    LoadFormula();
    Select(0, 0);
    UpdateScroll();
    UpdateTitle();
    InvalidateRect(m_grid, nullptr, FALSE);
    if (dropped > 0)
        MessageBoxW(m_hwnd, (std::to_wstring(dropped) + L" Zellen lagen außerhalb des Rasters (A–Z, Zeilen 1–100) und wurden nicht übernommen.").c_str(),
                    L"Duncan's Office", MB_ICONINFORMATION);
    return true;
}

bool App::SheetsSave(bool saveAs)
{
    CommitFormula();
    std::wstring path = m_sPath;
    if (saveAs || path.empty()) {
        path = PickSave(L"CSV-Dateien (*.csv)\0*.csv\0", L"csv", m_sPath.empty() ? L"Tabelle" : FileNameOf(m_sPath));
        if (path.empty()) return false;
    }
    if (!WriteAtomic(path, codec::EncodeUtf8Bom(sheet::ToCsv(m_sheet)))) {
        MessageBoxW(m_hwnd, (L"Die Datei konnte nicht gespeichert werden:\n" + path).c_str(), L"Duncan's Office", MB_ICONWARNING);
        return false;
    }
    m_sPath = path;
    m_sDirty = false;
    UpdateTitle();
    return true;
}

// ======================= Writer: Schriftart, Farbe, Aufzaehlung =======================

void App::ShowFontMenu()
{
    if (m_fontList.empty()) {   // nur Schriften aus der Auswahlliste, die auf diesem Rechner wirklich installiert sind
        std::set<std::wstring> installed;
        LOGFONTW lf{};
        lf.lfCharSet = DEFAULT_CHARSET;
        HDC dc = GetDC(m_hwnd);
        EnumFontFamiliesExW(dc, &lf, reinterpret_cast<FONTENUMPROCW>(CollectFont), reinterpret_cast<LPARAM>(&installed), 0);
        ReleaseDC(m_hwnd, dc);
        for (const wchar_t* f : kFontCandidates)
            if (installed.count(f)) m_fontList.emplace_back(f);
        if (m_fontList.empty()) m_fontList.emplace_back(L"Segoe UI");
    }
    HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < m_fontList.size(); ++i)
        AppendMenuW(menu, MF_STRING | (m_fontList[i] == m_fontName ? MF_CHECKED : 0), static_cast<UINT_PTR>(C_FONT_BASE) + i, m_fontList[i].c_str());
    for (const Btn& b : m_btns)
        if (b.id == C_FONT) {
            POINT pt{ b.rc.left, b.rc.bottom };
            ClientToScreen(m_hwnd, &pt);
            TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);
        }
    DestroyMenu(menu);
}

void App::ShowColorMenu()
{
    HMENU menu = CreatePopupMenu();
    for (int i = 0; i < kColorCount; ++i)
        AppendMenuW(menu, MF_OWNERDRAW, static_cast<UINT_PTR>(C_COLOR_BASE + i), reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(i)));   // itemData = Index, gezeichnet in WM_DRAWITEM
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, C_COLOR_MORE, L"Weitere Farben …");
    for (const Btn& b : m_btns)
        if (b.id == C_COLOR) {
            POINT pt{ b.rc.left, b.rc.bottom };
            ClientToScreen(m_hwnd, &pt);
            TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);
        }
    DestroyMenu(menu);
}

void App::SetFontName(const std::wstring& name)
{
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_FACE | CFM_CHARSET;
    cf.bCharSet = DEFAULT_CHARSET;
    wcsncpy_s(cf.szFaceName, name.c_str(), _TRUNCATE);
    SendMessageW(m_rich, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    RefreshFmt();
    SetFocus(m_rich);
}

void App::SetTextColorRef(COLORREF c)
{
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = c;
    cf.dwEffects = 0;   // nicht "automatisch", sonst bliebe die Farbe schwarz
    SendMessageW(m_rich, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    RefreshFmt();
    SetFocus(m_rich);
}

void App::ToggleBullet()
{
    PARAFORMAT2 pf{};
    pf.cbSize = sizeof pf;
    pf.dwMask = PFM_NUMBERING | PFM_OFFSET;
    pf.wNumbering = m_bullet ? 0 : PFN_BULLET;
    pf.dxOffset = m_bullet ? 0 : 360;   // haengender Einzug von 0,25 Zoll
    SendMessageW(m_rich, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));
    RefreshFmt();
    SetFocus(m_rich);
}

// ======================= Writer: Suchen und Ersetzen =======================

void App::SetFindStatus(const std::wstring& t)
{
    if (m_findDlg && IsWindow(m_findDlg)) SetDlgItemTextW(m_findDlg, ID_F_STATUS, t.c_str());
}

void App::ReadFindFields(HWND dlg)
{
    wchar_t buf[512];
    GetDlgItemTextW(dlg, ID_F_FIND, buf, 512);
    m_findText = buf;
    GetDlgItemTextW(dlg, ID_F_REPL, buf, 512);
    m_replText = buf;
    m_findCase = IsDlgButtonChecked(dlg, ID_F_CASE) == BST_CHECKED;
    m_findWord = IsDlgButtonChecked(dlg, ID_F_WORD) == BST_CHECKED;
}

// Sucht ab dem Ende der Auswahl; am Dokumentende geht es von vorn weiter
bool App::FindNext()
{
    if (m_findText.empty()) return false;
    CHARRANGE sel{};
    SendMessageW(m_rich, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));
    FINDTEXTEXW ft{};
    ft.lpstrText = m_findText.c_str();
    const WPARAM flags = FR_DOWN | (m_findCase ? FR_MATCHCASE : 0) | (m_findWord ? FR_WHOLEWORD : 0);
    ft.chrg = { sel.cpMax, -1 };
    LRESULT r = SendMessageW(m_rich, EM_FINDTEXTEXW, flags, reinterpret_cast<LPARAM>(&ft));
    bool wrapped = false;
    if (r < 0) {
        ft.chrg = { 0, -1 };
        r = SendMessageW(m_rich, EM_FINDTEXTEXW, flags, reinterpret_cast<LPARAM>(&ft));
        wrapped = true;
    }
    if (r < 0) {
        SetFindStatus(L"Nicht gefunden.");
        return false;
    }
    SendMessageW(m_rich, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&ft.chrgText));
    SendMessageW(m_rich, EM_SCROLLCARET, 0, 0);
    SetFindStatus(wrapped ? L"Am Ende angekommen, von vorn weitergesucht." : L"");
    return true;
}

bool App::SelectionMatches()
{
    CHARRANGE sel{};
    SendMessageW(m_rich, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));
    if (sel.cpMax - sel.cpMin != static_cast<LONG>(m_findText.size()) || m_findText.empty()) return false;
    std::wstring cur(m_findText.size() + 1, L'\0');
    SendMessageW(m_rich, EM_GETSELTEXT, 0, reinterpret_cast<LPARAM>(cur.data()));
    cur.resize(m_findText.size());
    return m_findCase ? cur == m_findText : _wcsicmp(cur.c_str(), m_findText.c_str()) == 0;
}

void App::ReplaceOne()
{
    if (m_findText.empty()) return;
    if (!SelectionMatches() && !FindNext()) return;   // erst die passende Stelle markieren
    if (!SelectionMatches()) return;
    SendMessageW(m_rich, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(m_replText.c_str()));
    FindNext();
}

void App::ReplaceAll()
{
    if (m_findText.empty()) return;
    const WPARAM flags = FR_DOWN | (m_findCase ? FR_MATCHCASE : 0) | (m_findWord ? FR_WHOLEWORD : 0);
    LONG pos = 0;
    int count = 0;
    SendMessageW(m_rich, WM_SETREDRAW, FALSE, 0);
    while (count < 100000) {
        FINDTEXTEXW ft{};
        ft.lpstrText = m_findText.c_str();
        ft.chrg = { pos, -1 };
        if (SendMessageW(m_rich, EM_FINDTEXTEXW, flags, reinterpret_cast<LPARAM>(&ft)) < 0) break;
        SendMessageW(m_rich, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&ft.chrgText));
        SendMessageW(m_rich, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(m_replText.c_str()));
        pos = ft.chrgText.cpMin + static_cast<LONG>(m_replText.size());   // hinter dem Ersetzten weiter, auch wenn es den Suchtext enthaelt
        ++count;
    }
    SendMessageW(m_rich, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(m_rich, nullptr, TRUE);
    SetFindStatus(count == 0 ? std::wstring(L"Nicht gefunden.") : std::to_wstring(count) + (count == 1 ? L" Stelle ersetzt." : L" Stellen ersetzt."));
}

void App::ShowFindDialog()
{
    if (m_mode != Mode::Writer || (m_findDlg && IsWindow(m_findDlg))) return;
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{ sizeof wc };
        wc.hInstance = m_inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = m_brPanel;
        wc.lpszClassName = kFindClass;
        wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT { return g_app->FindHandle(h, m, w, l); };
        RegisterClassExW(&wc);
        registered = true;
    }
    {   // markierter Text (eine Zeile) wird zum Suchtext
        CHARRANGE sel{};
        SendMessageW(m_rich, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));
        if (sel.cpMax > sel.cpMin && sel.cpMax - sel.cpMin < 200) {
            wchar_t buf[256]{};
            SendMessageW(m_rich, EM_GETSELTEXT, 0, reinterpret_cast<LPARAM>(buf));
            if (!wcschr(buf, L'\r') && !wcschr(buf, L'\n') && !wcschr(buf, L'\v')) m_findText = buf;
        }
    }
    RECT client{ 0, 0, S(460), S(272) };
    AdjustWindowRectEx(&client, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    RECT own{};
    GetWindowRect(m_hwnd, &own);
    const int w = client.right - client.left, h = client.bottom - client.top;
    m_findDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kFindClass, L"Suchen und Ersetzen", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                                own.left + (own.right - own.left - w) / 2, own.top + S(120), w, h, m_hwnd, nullptr, m_inst, nullptr);
    if (!m_findDlg) return;
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(m_findDlg, 20, &dark, sizeof dark);
    SendMessageW(m_findDlg, DM_SETDEFID, ID_F_NEXT, 0);
    ShowWindow(m_findDlg, SW_SHOW);
    HWND edit = GetDlgItem(m_findDlg, ID_F_FIND);
    SetFocus(edit);
    SendMessageW(edit, EM_SETSEL, 0, -1);

    EnableWindow(m_hwnd, FALSE);   // modal: das Hauptfenster ruht, bis der Dialog zu ist
    MSG msg;
    while (IsWindow(m_findDlg)) {
        const BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage(static_cast<int>(msg.wParam));   // Programmende waehrend der Dialog offen ist
            break;
        }
        if (!IsDialogMessageW(m_findDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(m_hwnd, TRUE);
    if (IsWindow(m_findDlg)) DestroyWindow(m_findDlg);
    m_findDlg = nullptr;
    SetActiveWindow(m_hwnd);
    SetFocus(m_rich);
}

LRESULT App::FindHandle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_CREATE: {
            auto label = [&](const wchar_t* t, int x, int y, int w, int h, int id) {
                HWND c = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE, S(x), S(y), S(w), S(h), hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), m_inst, nullptr);
                SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(m_fUi), TRUE);
                return c;
            };
            auto edit = [&](int y, int id, const std::wstring& text) {
                HWND c = CreateWindowExW(0, L"EDIT", text.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, S(20), S(y), S(420), S(26), hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), m_inst, nullptr);
                SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(m_fUi), TRUE);
                return c;
            };
            auto check = [&](const wchar_t* t, int y, int id, bool on) {
                HWND c = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, S(20), S(y), S(420), S(22), hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), m_inst, nullptr);
                SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(m_fUi), TRUE);
                SetWindowTheme(c, L"", L"");   // ohne Windows-Design folgt die Schriftfarbe WM_CTLCOLORSTATIC (dunkles Design)
                CheckDlgButton(hwnd, id, on ? BST_CHECKED : BST_UNCHECKED);
                return c;
            };
            auto button = [&](const wchar_t* t, int x, int w, int id, bool def) {
                HWND c = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | (def ? BS_DEFPUSHBUTTON : 0), S(x), S(226), S(w), S(34), hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), m_inst, nullptr);
                SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(m_fUi), TRUE);
                return c;
            };
            label(L"Suchen nach:", 20, 14, 420, 20, 0);
            edit(36, ID_F_FIND, m_findText);
            label(L"Ersetzen durch:", 20, 72, 420, 20, 0);
            edit(94, ID_F_REPL, m_replText);
            check(L"Groß-/Kleinschreibung beachten", 134, ID_F_CASE, m_findCase);
            check(L"Nur ganze Wörter", 158, ID_F_WORD, m_findWord);
            label(L"", 20, 190, 420, 22, ID_F_STATUS);
            button(L"Weitersuchen", 20, 122, ID_F_NEXT, true);
            button(L"Ersetzen", 150, 96, ID_F_REPLACE, false);
            button(L"Alle ersetzen", 254, 112, ID_F_ALL, false);
            button(L"Schließen", 374, 66, ID_F_CLOSE, false);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDOK:   // Enter (IsDialogMessage meldet IDOK, da das Fenster keine eigene Dialogklasse ist)
                case ID_F_NEXT: ReadFindFields(hwnd); FindNext(); return 0;
                case ID_F_REPLACE: ReadFindFields(hwnd); ReplaceOne(); return 0;
                case ID_F_ALL: ReadFindFields(hwnd); ReplaceAll(); return 0;
                case ID_F_CLOSE:
                case IDCANCEL: ReadFindFields(hwnd); DestroyWindow(hwnd); return 0;
                default: break;
            }
            break;
        case WM_CLOSE:
            ReadFindFields(hwnd);
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostMessageW(m_hwnd, WM_NULL, 0, 0);   // weckt die Nachrichtenschleife des modalen Dialogs
            return 0;
        case WM_CTLCOLOREDIT:
            SetTextColor(reinterpret_cast<HDC>(wp), kText);
            SetBkColor(reinterpret_cast<HDC>(wp), kBg);
            return reinterpret_cast<LRESULT>(m_brEdit);
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            SetTextColor(reinterpret_cast<HDC>(wp), GetDlgCtrlID(reinterpret_cast<HWND>(lp)) == ID_F_STATUS ? kAccent : kText);
            SetBkColor(reinterpret_cast<HDC>(wp), kPanel);
            return reinterpret_cast<LRESULT>(m_brPanel);
        case WM_DRAWITEM: {
            const auto* di = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
            if (di->CtlType != ODT_BUTTON) break;
            HBRUSH back = CreateSolidBrush(kPanel);
            FillRect(di->hDC, &di->rcItem, back);
            DeleteObject(back);
            const bool down = (di->itemState & ODS_SELECTED) != 0, def = di->CtlID == ID_F_NEXT, focus = (di->itemState & ODS_FOCUS) != 0;
            COLORREF fill = def ? kAccent : kHover;
            if (down) fill = Mix(fill, kBg, 30);
            RoundBox(di->hDC, di->rcItem, S(7), fill, focus ? kText : fill);
            wchar_t t[64];
            GetWindowTextW(di->hwndItem, t, 64);
            Text(di->hDC, m_fUi, def ? RGB(0x1E, 0x1E, 0x2E) : kText, di->rcItem, t, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
            return TRUE;
        }
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show)
{
    std::wstring open;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        if (argc > 1) open = argv[1];
        LocalFree(argv);
    }
    static App app;
    if (!app.Create(inst, show, open)) return 1;
    return app.Run();
}
