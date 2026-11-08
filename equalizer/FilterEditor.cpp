#include "FilterEditor.h"

#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "../common/Theme.h"
#include "EqCurve.h"

namespace eq {
namespace {

enum Id { ID_LIST = 100, ID_NEW, ID_DEL, ID_LOAD, ID_CLEAR, ID_TYPE, ID_ON, ID_FC, ID_GAIN, ID_Q, ID_OK, ID_CANCEL };

const wchar_t kClass[] = L"DuncansEqFilterEditor";
const COLORREF kLine = RGB(125, 211, 252);   // Himmelblau wie im Hauptfenster

const char* const kTypes[] = {"PK", "LSC", "HSC", "HP", "LP", "NO"};

std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }   // nur ASCII (Filterzeilen)

std::wstring Num(double v, int decimals)
{
    wchar_t b[32];
    swprintf(b, 32, decimals == 0 ? L"%.0f" : decimals == 1 ? L"%.1f" : L"%.2f", v);
    std::wstring s = b;
    for (auto& c : s) if (c == L'.') c = L',';   // deutsche Schreibweise
    return s;
}

// Text einer Zeile in der Liste
std::wstring Describe(const std::string& line)
{
    Filter f;
    if (ParseFilter(line, f)) {
        std::wstring s = std::wstring(f.on ? L"" : L"(aus) ") + FilterTypeName(f.type) + L"  " + Num(f.fc, f.fc < 1000 ? 0 : 0) + L" Hz";
        if (FilterUsesGain(f.type)) s += L"  " + std::wstring(f.gain > 0 ? L"+" : L"") + Num(f.gain, 1) + L" dB";
        if (FilterUsesQ(f.type) && f.q > 0) s += L"  Q " + Num(f.q, 2);
        return s;
    }
    if (line.compare(0, 7, "Preamp:") == 0) return L"Vorverstärkung " + Wide(line.substr(8)) + L"  (aus Datei)";
    return Wide(line);
}

struct Editor {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    bool done = false, accepted = false, updating = false;
    Settings s;
    UINT dpi = 96;
    HFONT font = nullptr, fontBold = nullptr;
    HBRUSH brBg = nullptr, brCard = nullptr;
    RECT graph{};
    std::vector<double> freqs;
    HWND list = nullptr, bNew = nullptr, bDel = nullptr, bLoad = nullptr, bClear = nullptr, type = nullptr, on = nullptr, sFc = nullptr, sGain = nullptr, sQ = nullptr;
    HWND lFc = nullptr, lGain = nullptr, lQ = nullptr, lType = nullptr, lList = nullptr, hint = nullptr, bOk = nullptr, bCancel = nullptr;

    int S(int v) const { return MulDiv(v, static_cast<int>(dpi), 96); }
    int Sel() const { return static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0)); }

    HWND Make(const wchar_t* cls, const wchar_t* txt, DWORD style, int id)
    {
        HWND h = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
        return h;
    }
    HWND Label(const wchar_t* txt)
    {
        HWND h = CreateWindowExW(0, L"STATIC", txt, WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return h;
    }
    HWND Slider(int id, int lo, int hi)
    {
        HWND h = Make(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, id);
        SendMessageW(h, TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi));
        return h;
    }

    void Create();
    void Layout();
    void FillList(int keep);
    void ShowSelected();   // Steuerelemente aus der gewaehlten Zeile fuellen
    void PullFilter();     // Steuerelemente -> gewaehlte Zeile
    void LoadFile();
    void Paint();
    void Redraw() { InvalidateRect(hwnd, &graph, FALSE); }
};

double FcFromPos(int p) { return kMinFc * std::pow(kMaxFc / kMinFc, p / 1000.0); }
int PosFromFc(double f) { return static_cast<int>(std::lround(1000.0 * std::log(std::clamp(f, kMinFc, kMaxFc) / kMinFc) / std::log(kMaxFc / kMinFc))); }
double QFromPos(int p) { return kMinQ * std::pow(kMaxQ / kMinQ, p / 1000.0); }
int PosFromQ(double q) { return static_cast<int>(std::lround(1000.0 * std::log(std::clamp(q, kMinQ, kMaxQ) / kMinQ) / std::log(kMaxQ / kMinQ))); }

void Editor::Create()
{
    dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) dpi = 96;
    font = CreateFontW(-S(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    fontBold = CreateFontW(-S(13), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    brBg = CreateSolidBrush(theme::kBackground);
    brCard = CreateSolidBrush(theme::kCardRaised);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    freqs = LogFreqs(300);

    lList = Label(L"Filter und Korrektur (Reihenfolge wie in der Datei)");
    list = Make(L"LISTBOX", L"", LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_BORDER | WS_TABSTOP, ID_LIST);
    bNew = Make(L"BUTTON", L"Neu", BS_PUSHBUTTON | WS_TABSTOP, ID_NEW);
    bDel = Make(L"BUTTON", L"Entfernen", BS_PUSHBUTTON | WS_TABSTOP, ID_DEL);
    bLoad = Make(L"BUTTON", L"Datei laden …", BS_PUSHBUTTON | WS_TABSTOP, ID_LOAD);
    bClear = Make(L"BUTTON", L"Alle entfernen", BS_PUSHBUTTON | WS_TABSTOP, ID_CLEAR);
    lType = Label(L"Art");
    type = Make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_TYPE);
    for (const char* t : kTypes) SendMessageW(type, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(FilterTypeName(t)));
    on = Make(L"BUTTON", L"Aktiv", BS_AUTOCHECKBOX | WS_TABSTOP, ID_ON);
    lFc = Label(L"");
    sFc = Slider(ID_FC, 0, 1000);
    lGain = Label(L"");
    sGain = Slider(ID_GAIN, -48, 48);
    lQ = Label(L"");
    sQ = Slider(ID_Q, 0, 1000);
    hint = Label(L"Die Kurve zeigt alles zusammen: Regler, Bass, Sprache und diese Filter (ohne Vorverstärkung).");
    bOk = Make(L"BUTTON", L"Übernehmen", BS_DEFPUSHBUTTON | WS_TABSTOP, ID_OK);
    bCancel = Make(L"BUTTON", L"Abbrechen", BS_PUSHBUTTON | WS_TABSTOP, ID_CANCEL);
    int first = s.extra.empty() ? -1 : 0;   // erste bearbeitbare Zeile vorwaehlen (nicht die Vorverstaerkung aus einer Datei)
    for (size_t i = 0; i < s.extra.size(); ++i) { Filter f; if (ParseFilter(s.extra[i], f)) { first = static_cast<int>(i); break; } }
    FillList(first);
}

void Editor::Layout()
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int pad = S(14), pw = S(350), x = rc.right - pw - pad, w = pw;
    graph = {pad, pad, x - pad, rc.bottom - pad};
    int y = pad;
    auto put = [&](HWND h, int px, int py, int pw_, int ph) { SetWindowPos(h, nullptr, px, py, pw_, ph, SWP_NOZORDER); };
    put(lList, x, y, w, S(18));
    y += S(20);
    put(list, x, y, w, S(150));
    y += S(156);
    {
        const int g = S(6), bw = (w - g) / 2;
        put(bNew, x, y, bw, S(28));
        put(bDel, x + bw + g, y, bw, S(28));
        y += S(34);
        put(bLoad, x, y, bw, S(28));
        put(bClear, x + bw + g, y, bw, S(28));
        y += S(42);
    }
    put(lType, x, y + S(4), S(60), S(18));
    put(type, x + S(60), y, S(150), S(220));
    put(on, x + S(224), y + S(1), w - S(224), S(26));
    y += S(36);
    put(lFc, x, y, w, S(18));
    y += S(20);
    put(sFc, x, y, w, S(28));
    y += S(34);
    put(lGain, x, y, w, S(18));
    y += S(20);
    put(sGain, x, y, w, S(28));
    y += S(34);
    put(lQ, x, y, w, S(18));
    y += S(20);
    put(sQ, x, y, w, S(28));
    y += S(40);
    put(hint, x, y, w, S(60));
    const int by = rc.bottom - pad - S(36);
    put(bOk, x, by, w * 2 / 3 - S(4), S(36));
    put(bCancel, x + w * 2 / 3 + S(4), by, w / 3 - S(4), S(36));
    InvalidateRect(hwnd, nullptr, TRUE);
}

void Editor::FillList(int keep)
{
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const std::string& l : s.extra) SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Describe(l).c_str()));
    if (keep >= 0 && keep < static_cast<int>(s.extra.size())) SendMessageW(list, LB_SETCURSEL, keep, 0);
    ShowSelected();
}

void Editor::ShowSelected()
{
    updating = true;
    const int i = Sel();
    Filter f;
    const bool ok = i >= 0 && i < static_cast<int>(s.extra.size()) && ParseFilter(s.extra[static_cast<size_t>(i)], f);
    for (HWND h : {type, on, sFc, sGain, sQ}) EnableWindow(h, ok);
    EnableWindow(bDel, i >= 0);
    if (ok) {
        int ti = 0;
        for (int k = 0; k < 6; ++k) if (f.type == kTypes[k]) ti = k;
        SendMessageW(type, CB_SETCURSEL, ti, 0);
        SendMessageW(on, BM_SETCHECK, f.on ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(sFc, TBM_SETPOS, TRUE, PosFromFc(f.fc));
        SendMessageW(sGain, TBM_SETPOS, TRUE, static_cast<int>(std::lround(f.gain * 2)));
        SendMessageW(sQ, TBM_SETPOS, TRUE, PosFromQ(f.q > 0 ? f.q : DefaultQ(f.type)));
        SetWindowTextW(lFc, (L"Frequenz: " + Num(f.fc, 0) + L" Hz").c_str());
        SetWindowTextW(lGain, FilterUsesGain(f.type) ? (L"Verstärkung: " + std::wstring(f.gain > 0 ? L"+" : L"") + Num(f.gain, 1) + L" dB").c_str() : L"Verstärkung: bei dieser Art nicht einstellbar");
        SetWindowTextW(lQ, FilterUsesQ(f.type) ? (L"Güte (Q): " + Num(f.q > 0 ? f.q : DefaultQ(f.type), 2) + L"  (höher = schmaler)").c_str() : L"Güte: bei dieser Art nicht einstellbar");
        EnableWindow(sGain, FilterUsesGain(f.type));
        EnableWindow(sQ, FilterUsesQ(f.type));
    } else {
        SetWindowTextW(lFc, i >= 0 ? L"Diese Zeile lässt sich hier nicht bearbeiten (aus einer Datei)." : L"Wähle eine Zeile oder lege mit „Neu“ einen Filter an.");
        SetWindowTextW(lGain, L"");
        SetWindowTextW(lQ, L"");
    }
    updating = false;
}

void Editor::PullFilter()
{
    if (updating) return;
    const int i = Sel();
    if (i < 0 || i >= static_cast<int>(s.extra.size())) return;
    Filter f;
    if (!ParseFilter(s.extra[static_cast<size_t>(i)], f)) return;
    const int ti = static_cast<int>(SendMessageW(type, CB_GETCURSEL, 0, 0));
    if (ti >= 0 && ti < 6) {
        const std::string old = f.type;
        f.type = kTypes[ti];
        if (old != f.type && !FilterUsesGain(f.type)) f.gain = 0;
        if (old != f.type) f.q = FilterUsesQ(f.type) ? DefaultQ(f.type) : 0;
    }
    f.on = SendMessageW(on, BM_GETCHECK, 0, 0) == BST_CHECKED;
    f.fc = FcFromPos(static_cast<int>(SendMessageW(sFc, TBM_GETPOS, 0, 0)));
    if (FilterUsesGain(f.type)) f.gain = static_cast<int>(SendMessageW(sGain, TBM_GETPOS, 0, 0)) * 0.5;
    if (FilterUsesQ(f.type)) f.q = QFromPos(static_cast<int>(SendMessageW(sQ, TBM_GETPOS, 0, 0)));
    s.extra[static_cast<size_t>(i)] = FormatFilter(f);
    updating = true;
    SendMessageW(list, LB_DELETESTRING, i, 0);
    SendMessageW(list, LB_INSERTSTRING, i, reinterpret_cast<LPARAM>(Describe(s.extra[static_cast<size_t>(i)]).c_str()));
    SendMessageW(list, LB_SETCURSEL, i, 0);
    updating = false;
    ShowSelected();
    Redraw();
}

void Editor::LoadFile()
{
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Textdatei (*.txt)\0*.txt\0Alle Dateien\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    HANDLE h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { SetWindowTextW(hint, L"Die Datei konnte nicht gelesen werden."); return; }
    std::string text;
    char buf[8192];
    DWORD n = 0;
    while (text.size() < 300000 && ReadFile(h, buf, sizeof(buf), &n, nullptr) && n) text.append(buf, n);
    CloseHandle(h);
    int ignored = 0;
    std::vector<std::string> lines = ParseFilterFile(text, ignored);
    if (lines.empty()) { SetWindowTextW(hint, L"In der Datei stehen keine nutzbaren Filterzeilen (erwartet: „Filter: ON PK Fc … Hz Gain … dB Q …“)."); return; }
    size_t added = 0;
    for (const std::string& l : lines) {
        if (s.extra.size() >= kMaxExtraLines) break;
        s.extra.push_back(l);
        ++added;
    }
    FillList(static_cast<int>(s.extra.size()) - static_cast<int>(added));
    std::wstring msg = std::to_wstring(added) + L" Zeilen übernommen";
    if (ignored) msg += L", " + std::to_wstring(ignored) + L" ignoriert";
    if (added < lines.size()) msg += L" (mehr als " + std::to_wstring(kMaxExtraLines) + L" Zeilen passen nicht)";
    SetWindowTextW(hint, (msg + L".").c_str());
    Redraw();
}

void Editor::Paint()
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    const int gw = graph.right - graph.left, gh = graph.bottom - graph.top;
    if (gw > 40 && gh > 40) {
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, gw, gh);
        HGDIOBJ old = SelectObject(mem, bmp);
        {
            Gdiplus::Graphics g(mem);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
            g.Clear(Gdiplus::Color(255, GetRValue(theme::kCardRaised), GetGValue(theme::kCardRaised), GetBValue(theme::kCardRaised)));
            const std::vector<double> db = ResponseDb(s, freqs);
            double peak = 0;
            for (double v : db) peak = std::max(peak, std::fabs(v));
            const double range = std::max(15.0, std::ceil(peak / 5.0) * 5.0);   // +- 15 dB, bei stark verbogenen Kurven mehr
            const float L = static_cast<float>(S(46)), R = static_cast<float>(gw - S(16)), T = static_cast<float>(S(16)), B = static_cast<float>(gh - S(30));
            auto X = [&](double f) { return L + static_cast<float>(std::log(f / kMinFc) / std::log(kMaxFc / kMinFc)) * (R - L); };
            auto Y = [&](double d) { return T + static_cast<float>((range - std::clamp(d, -range, range)) / (2 * range)) * (B - T); };
            Gdiplus::FontFamily fam(L"Segoe UI");
            Gdiplus::Font fnt(&fam, static_cast<Gdiplus::REAL>(S(11)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush dim(Gdiplus::Color(255, GetRValue(theme::kTextDim), GetGValue(theme::kTextDim), GetBValue(theme::kTextDim)));
            Gdiplus::Pen grid(Gdiplus::Color(60, 255, 255, 255), 1.f);
            Gdiplus::Pen zero(Gdiplus::Color(130, 255, 255, 255), 1.f);
            Gdiplus::StringFormat center;
            center.SetAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::StringFormat right;
            right.SetAlignment(Gdiplus::StringAlignmentFar);
            for (double f : {20., 50., 100., 200., 500., 1000., 2000., 5000., 10000., 20000.}) {
                g.DrawLine(&grid, X(f), T, X(f), B);
                const std::wstring t = f >= 1000 ? Num(f / 1000, 0) + L"k" : Num(f, 0);
                g.DrawString(t.c_str(), -1, &fnt, Gdiplus::PointF(X(f), B + S(6)), &center, &dim);
            }
            for (int d = -static_cast<int>(range); d <= static_cast<int>(range); d += 5) {
                g.DrawLine(d == 0 ? &zero : &grid, L, Y(d), R, Y(d));
                const std::wstring t = (d > 0 ? L"+" : L"") + std::to_wstring(d);
                g.DrawString(t.c_str(), -1, &fnt, Gdiplus::PointF(L - S(6), Y(d) - S(7)), &right, &dim);
            }
            std::vector<Gdiplus::PointF> pts;
            for (size_t i = 0; i < freqs.size(); ++i) pts.push_back({X(freqs[i]), Y(db[i])});
            Gdiplus::Pen glow(Gdiplus::Color(60, GetRValue(kLine), GetGValue(kLine), GetBValue(kLine)), static_cast<float>(S(7)));
            Gdiplus::Pen line(Gdiplus::Color(255, GetRValue(kLine), GetGValue(kLine), GetBValue(kLine)), static_cast<float>(S(2)));
            glow.SetLineJoin(Gdiplus::LineJoinRound);
            line.SetLineJoin(Gdiplus::LineJoinRound);
            g.DrawLines(&glow, pts.data(), static_cast<INT>(pts.size()));
            g.DrawLines(&line, pts.data(), static_cast<INT>(pts.size()));
            // markierter Filter: senkrechte Linie bei seiner Frequenz
            const int i = Sel();
            Filter f;
            if (i >= 0 && i < static_cast<int>(s.extra.size()) && ParseFilter(s.extra[static_cast<size_t>(i)], f)) {
                Gdiplus::Pen mark(Gdiplus::Color(200, 255, 150, 50), 1.5f);
                mark.SetDashStyle(Gdiplus::DashStyleDash);
                g.DrawLine(&mark, X(f.fc), T, X(f.fc), B);
            }
        }
        BitBlt(dc, graph.left, graph.top, gw, gh, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
    }
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    auto* e = reinterpret_cast<Editor*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        e = static_cast<Editor*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        e->hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(e));
    }
    if (!e) return DefWindowProcW(h, m, wp, lp);
    switch (m) {
        case WM_CREATE: e->Create(); return 0;
        case WM_SIZE: if (e->font) e->Layout(); return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize = {e->S(900), e->S(620)};
            return 0;
        }
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(h, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc, e->brBg);
            return 1;
        }
        case WM_PAINT: e->Paint(); return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, theme::kText);
            SetBkColor(dc, theme::kBackground);
            return reinterpret_cast<LRESULT>(e->brBg);
        }
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, theme::kText);
            SetBkColor(dc, theme::kCardRaised);
            return reinterpret_cast<LRESULT>(e->brCard);
        }
        case WM_HSCROLL: e->PullFilter(); return 0;
        case WM_COMMAND: {
            const int id = LOWORD(wp), code = HIWORD(wp);
            switch (id) {
                case ID_LIST: if (code == LBN_SELCHANGE) { e->ShowSelected(); e->Redraw(); } break;
                case ID_TYPE: if (code == CBN_SELCHANGE) e->PullFilter(); break;
                case ID_ON: e->PullFilter(); break;
                case ID_NEW:
                    if (e->s.extra.size() >= kMaxExtraLines) { SetWindowTextW(e->hint, L"Mehr Zeilen sind nicht möglich."); break; }
                    {
                        Filter f;
                        e->s.extra.push_back(FormatFilter(f));
                        e->FillList(static_cast<int>(e->s.extra.size()) - 1);
                        e->Redraw();
                    }
                    break;
                case ID_DEL: {
                    const int i = e->Sel();
                    if (i >= 0 && i < static_cast<int>(e->s.extra.size())) {
                        e->s.extra.erase(e->s.extra.begin() + i);
                        e->FillList(std::min(i, static_cast<int>(e->s.extra.size()) - 1));
                        e->Redraw();
                    }
                    break;
                }
                case ID_CLEAR:
                    e->s.extra.clear();
                    e->FillList(-1);
                    e->Redraw();
                    break;
                case ID_LOAD: e->LoadFile(); break;
                case ID_OK: e->accepted = true; e->done = true; DestroyWindow(h); break;
                case ID_CANCEL:
                case IDCANCEL: e->done = true; DestroyWindow(h); break;
            }
            return 0;
        }
        case WM_CLOSE: e->done = true; DestroyWindow(h); return 0;
        case WM_DESTROY: e->done = true; return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

}  // namespace

bool ShowFilterEditor(HWND owner, Settings& cfg)
{
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = Proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        wc.hIcon = owner ? reinterpret_cast<HICON>(SendMessageW(owner, WM_GETICON, ICON_BIG, 0)) : nullptr;
        RegisterClassExW(&wc);
        registered = true;
    }
    Editor e;
    e.s = cfg;
    e.owner = owner;
    const UINT sys = owner ? GetDpiForWindow(owner) : GetDpiForSystem();
    const int w = MulDiv(1100, static_cast<int>(sys), 96), h = MulDiv(700, static_cast<int>(sys), 96);
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const int ww = std::min<int>(w, wa.right - wa.left), hh = std::min<int>(h, wa.bottom - wa.top);
    const int x = wa.left + (wa.right - wa.left - ww) / 2, y = wa.top + (wa.bottom - wa.top - hh) / 2;
    if (owner) EnableWindow(owner, FALSE);
    HWND hw = CreateWindowExW(WS_EX_CONTROLPARENT, kClass, L"Filter & Korrektur", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE, x, y, ww, hh, owner, nullptr,
                              GetModuleHandleW(nullptr), &e);
    if (!hw) {
        if (owner) EnableWindow(owner, TRUE);
        return false;
    }
    e.Layout();
    MSG msg;
    while (!e.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(hw, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    if (e.font) DeleteObject(e.font);
    if (e.fontBold) DeleteObject(e.fontBold);
    if (e.brBg) DeleteObject(e.brBg);
    if (e.brCard) DeleteObject(e.brCard);
    if (!e.accepted || e.s.extra == cfg.extra) return false;
    cfg.extra = e.s.extra;
    return true;
}

}  // namespace eq
