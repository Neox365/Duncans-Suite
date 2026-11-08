#include "CardDesigner.h"

#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cwctype>
#include <map>
#include <memory>

#include "../common/Theme.h"
#include "Card.h"

namespace card {
namespace {

enum Id {
    ID_LIST = 100, ID_ADD, ID_REMOVE, ID_UP, ID_DOWN, ID_PAPER, ID_LAND, ID_LAYOUT, ID_MARGIN, ID_GAP, ID_RADIUS, ID_BORDER,
    ID_BG, ID_COL1, ID_COL2, ID_TEXT, ID_FONT, ID_SIZE, ID_BOLD, ID_ITALIC, ID_SHADOW, ID_COLT, ID_BAND, ID_SAVE, ID_CLOSE,
};

const wchar_t kClass[] = L"DuncansCardDesigner";

const wchar_t* const kFonts[] = {L"Georgia", L"Segoe UI", L"Times New Roman", L"Palatino Linotype", L"Arial Black", L"Impact",
                                 L"Segoe Script", L"Brush Script MT", L"Lucida Handwriting", L"Comic Sans MS", L"Candara"};

std::wstring FileName(const std::wstring& p) {
    const size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? p : p.substr(s + 1);
}

bool Usable(const std::wstring& p) {   // GDI+ kann kein WebP/HEIC lesen
    const size_t d = p.find_last_of(L'.');
    if (d == std::wstring::npos) return false;
    std::wstring e = p.substr(d + 1);
    for (auto& c : e) c = static_cast<wchar_t>(std::towlower(c));
    return e == L"jpg" || e == L"jpeg" || e == L"png" || e == L"bmp" || e == L"tif" || e == L"tiff" || e == L"gif";
}

struct Designer {
    HWND hwnd = nullptr;
    bool closed = false;
    Spec spec;
    std::map<std::wstring, std::shared_ptr<Gdiplus::Bitmap>> cache;
    std::unique_ptr<Gdiplus::Bitmap> prev;
    UINT prevW = 0, prevH = 0;
    bool dirty = true, dragging = false;
    RECT canvas{};   // Kartenflaeche im Fenster (Pixel)
    UINT dpi = 96;
    HFONT font = nullptr;
    HBRUSH brBg = nullptr, brCard = nullptr;
    HWND list = nullptr, paper = nullptr, land = nullptr, layout = nullptr, bg = nullptr, text = nullptr, fontCb = nullptr, status = nullptr;
    HWND sMargin = nullptr, sGap = nullptr, sRadius = nullptr, sBorder = nullptr, sSize = nullptr;
    HWND bold = nullptr, italic = nullptr, shadow = nullptr, band = nullptr;
    std::vector<HWND> all;
    std::vector<HWND> labels;

    int S(int v) const { return MulDiv(v, static_cast<int>(dpi), 96); }

    HWND Make(const wchar_t* cls, const wchar_t* txt, DWORD style, int id, DWORD ex = 0) {
        HWND h = CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 GetModuleHandleW(nullptr), nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
        all.push_back(h);
        return h;
    }
    HWND Label(const wchar_t* txt) {
        HWND h = CreateWindowExW(0, L"STATIC", txt, WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        labels.push_back(h);
        return h;
    }
    HWND Slider(int id, int lo, int hi) {
        HWND h = Make(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, id);
        SendMessageW(h, TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi));
        return h;
    }

    void Create();
    void Layout();
    void Push();   // Spec -> Steuerelemente
    void Pull();   // Steuerelemente -> Spec
    void Redraw() { dirty = true; InvalidateRect(hwnd, &area, FALSE); }
    void Paint();
    void FillList();
    void AddPhotos();
    void MovePhoto(int delta);
    void SaveCard();
    void PickColor(COLORREF& c);
    void SetText(float mx, float my);
    std::shared_ptr<Gdiplus::Bitmap> Load(const std::wstring& p) {
        auto it = cache.find(p);
        if (it != cache.end()) return it->second;
        auto b = LoadPhoto(p, 1600);
        cache[p] = b;
        return b;
    }

    RECT area{};   // Vorschaubereich

    // Beschriftungen und ihre Steuerelemente in Zeilen, siehe Layout()
    HWND lPhotos = nullptr, lPaper = nullptr, lLayout = nullptr, lMargin = nullptr, lGap = nullptr, lRadius = nullptr, lBorder = nullptr, lBg = nullptr,
         lText = nullptr, lFont = nullptr, lSize = nullptr, lHint = nullptr;
    HWND bAdd = nullptr, bRemove = nullptr, bUp = nullptr, bDown = nullptr, bCol1 = nullptr, bCol2 = nullptr, bColT = nullptr, bSave = nullptr, bClose = nullptr;
};

void Designer::Create() {
    dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) dpi = 96;
    font = CreateFontW(-S(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    brBg = CreateSolidBrush(theme::kBackground);
    brCard = CreateSolidBrush(theme::kCardRaised);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));

    lPhotos = Label(L"Fotos (die Reihenfolge entspricht den Feldern)");
    list = Make(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_BORDER | WS_TABSTOP, ID_LIST);
    bAdd = Make(L"BUTTON", L"Hinzufügen …", BS_PUSHBUTTON | WS_TABSTOP, ID_ADD);
    bRemove = Make(L"BUTTON", L"Entfernen", BS_PUSHBUTTON | WS_TABSTOP, ID_REMOVE);
    bUp = Make(L"BUTTON", L"▲", BS_PUSHBUTTON | WS_TABSTOP, ID_UP);
    bDown = Make(L"BUTTON", L"▼", BS_PUSHBUTTON | WS_TABSTOP, ID_DOWN);

    lPaper = Label(L"Papier");
    paper = Make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_PAPER);
    for (int i = 0; i < PaperCount(); ++i) SendMessageW(paper, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(GetPaper(i).name));
    land = Make(L"BUTTON", L"Querformat", BS_AUTOCHECKBOX | WS_TABSTOP, ID_LAND);
    lLayout = Label(L"Layout");
    layout = Make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_LAYOUT);
    for (int i = 0; i < LayoutCount(); ++i) SendMessageW(layout, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(GetLayout(i).name));

    lMargin = Label(L"Rand");
    sMargin = Slider(ID_MARGIN, 0, 15);
    lGap = Label(L"Abstand");
    sGap = Slider(ID_GAP, 0, 10);
    lRadius = Label(L"Ecken");
    sRadius = Slider(ID_RADIUS, 0, 50);
    lBorder = Label(L"Fotorahmen");
    sBorder = Slider(ID_BORDER, 0, 10);

    lBg = Label(L"Hintergrund");
    bg = Make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_BG);
    for (const wchar_t* n : {L"Farbe", L"Farbverlauf", L"Unscharfes Foto"}) SendMessageW(bg, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n));
    bCol1 = Make(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, ID_COL1);
    bCol2 = Make(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, ID_COL2);

    lText = Label(L"Text auf der Karte (im Vorschaubild verschiebbar)");
    text = Make(L"EDIT", L"", ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_BORDER | WS_TABSTOP, ID_TEXT);
    lFont = Label(L"Schrift");
    fontCb = Make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_FONT);
    for (const wchar_t* n : kFonts) {
        Gdiplus::FontFamily f(n);
        if (f.IsAvailable()) SendMessageW(fontCb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n));
    }
    if (SendMessageW(fontCb, CB_GETCOUNT, 0, 0) == 0) SendMessageW(fontCb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Segoe UI"));
    lSize = Label(L"Größe");
    sSize = Slider(ID_SIZE, 2, 25);
    bold = Make(L"BUTTON", L"Fett", BS_AUTOCHECKBOX | WS_TABSTOP, ID_BOLD);
    italic = Make(L"BUTTON", L"Kursiv", BS_AUTOCHECKBOX | WS_TABSTOP, ID_ITALIC);
    shadow = Make(L"BUTTON", L"Schatten", BS_AUTOCHECKBOX | WS_TABSTOP, ID_SHADOW);
    bColT = Make(L"BUTTON", L"Textfarbe", BS_OWNERDRAW | WS_TABSTOP, ID_COLT);
    band = Make(L"BUTTON", L"Schreibfeld unten", BS_AUTOCHECKBOX | WS_TABSTOP, ID_BAND);
    lHint = Label(L"Doppelklick auf die Karte setzt den Text zurück.");
    bSave = Make(L"BUTTON", L"Als Bild speichern …", BS_DEFPUSHBUTTON | WS_TABSTOP, ID_SAVE);
    bClose = Make(L"BUTTON", L"Schließen", BS_PUSHBUTTON | WS_TABSTOP, ID_CLOSE);
    status = Label(L"");
    Push();
    FillList();
}

void Designer::Layout() {
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int W = rc.right, H = rc.bottom, pad = S(14), pw = S(340), x = W - pw - pad, w = pw;
    area = {pad, pad, x - pad, H - pad};
    int y = pad;
    auto put = [&](HWND h, int px, int py, int pw_, int ph) { SetWindowPos(h, nullptr, px, py, pw_, ph, SWP_NOZORDER); };
    const int rowH = S(26), lw = S(86);
    put(lPhotos, x, y, w, S(18));
    y += S(20);
    put(list, x, y, w, S(92));
    y += S(98);
    {
        const int g = S(6), bw = (w - 3 * g - 2 * S(34)) / 2;
        put(bAdd, x, y, bw, S(28));
        put(bRemove, x + bw + g, y, bw, S(28));
        put(bUp, x + 2 * (bw + g), y, S(34), S(28));
        put(bDown, x + 2 * (bw + g) + S(34) + g, y, S(34), S(28));
    }
    y += S(38);
    put(lPaper, x, y + S(4), lw, S(18));
    put(paper, x + lw, y, S(130), S(200));
    put(land, x + lw + S(138), y + S(1), w - lw - S(138), rowH);
    y += rowH + S(6);
    put(lLayout, x, y + S(4), lw, S(18));
    put(layout, x + lw, y, w - lw, S(240));
    y += rowH + S(8);
    const std::pair<HWND, HWND> sliders[] = {{lMargin, sMargin}, {lGap, sGap}, {lRadius, sRadius}, {lBorder, sBorder}};
    for (const auto& [l, s] : sliders) {
        put(l, x, y + S(4), lw, S(18));
        put(s, x + lw, y, w - lw, S(26));
        y += S(28);
    }
    y += S(6);
    put(lBg, x, y + S(4), lw, S(18));
    put(bg, x + lw, y, S(130), S(200));
    put(bCol1, x + lw + S(138), y, S(36), rowH);
    put(bCol2, x + lw + S(180), y, S(36), rowH);
    y += rowH + S(10);
    put(lText, x, y, w, S(18));
    y += S(20);
    put(text, x, y, w, S(52));
    y += S(58);
    put(lFont, x, y + S(4), lw, S(18));
    put(fontCb, x + lw, y, w - lw, S(240));
    y += rowH + S(4);
    put(lSize, x, y + S(4), lw, S(18));
    put(sSize, x + lw, y, w - lw, S(26));
    y += S(30);
    {
        const int cw = w / 3;
        put(bold, x, y, cw, S(24));
        put(italic, x + cw, y, cw, S(24));
        put(shadow, x + 2 * cw, y, cw, S(24));
    }
    y += S(28);
    put(bColT, x, y, S(110), S(26));
    put(band, x + S(120), y + S(1), w - S(120), S(24));
    y += S(32);
    put(lHint, x, y, w, S(18));
    y += S(26);
    const int by = std::max(y, H - pad - S(40) - S(22));
    put(status, x, by, w, S(18));
    put(bSave, x, by + S(22), w * 2 / 3 - S(4), S(36));
    put(bClose, x + w * 2 / 3 + S(4), by + S(22), w / 3 - S(4), S(36));
    InvalidateRect(hwnd, nullptr, TRUE);
}

void Designer::Push() {
    SendMessageW(paper, CB_SETCURSEL, spec.paper, 0);
    SendMessageW(land, BM_SETCHECK, spec.landscape ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(layout, CB_SETCURSEL, spec.layout, 0);
    SendMessageW(sMargin, TBM_SETPOS, TRUE, spec.marginPct);
    SendMessageW(sGap, TBM_SETPOS, TRUE, spec.gapPct);
    SendMessageW(sRadius, TBM_SETPOS, TRUE, spec.radiusPct);
    SendMessageW(sBorder, TBM_SETPOS, TRUE, spec.borderPct);
    SendMessageW(bg, CB_SETCURSEL, spec.bgMode, 0);
    int fi = static_cast<int>(SendMessageW(fontCb, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(spec.font.c_str())));
    SendMessageW(fontCb, CB_SETCURSEL, fi < 0 ? 0 : fi, 0);
    SendMessageW(sSize, TBM_SETPOS, TRUE, spec.textSizePct);
    SendMessageW(bold, BM_SETCHECK, spec.bold ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(italic, BM_SETCHECK, spec.italic ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(shadow, BM_SETCHECK, spec.shadow ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(band, BM_SETCHECK, spec.textBand ? BST_CHECKED : BST_UNCHECKED, 0);
}

void Designer::Pull() {
    auto sel = [](HWND h) { return static_cast<int>(SendMessageW(h, CB_GETCURSEL, 0, 0)); };
    auto pos = [](HWND h) { return static_cast<int>(SendMessageW(h, TBM_GETPOS, 0, 0)); };
    auto chk = [](HWND h) { return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; };
    spec.paper = std::max(0, sel(paper));
    spec.landscape = chk(land);
    spec.layout = std::max(0, sel(layout));
    spec.marginPct = pos(sMargin);
    spec.gapPct = pos(sGap);
    spec.radiusPct = pos(sRadius);
    spec.borderPct = pos(sBorder);
    spec.bgMode = std::max(0, sel(bg));
    spec.textSizePct = pos(sSize);
    spec.bold = chk(bold);
    spec.italic = chk(italic);
    spec.shadow = chk(shadow);
    spec.textBand = chk(band);
    const int fi = sel(fontCb);
    if (fi >= 0) {
        wchar_t f[64]{};
        SendMessageW(fontCb, CB_GETLBTEXT, fi, reinterpret_cast<LPARAM>(f));
        spec.font = f;
    }
    const int n = GetWindowTextLengthW(text);
    std::wstring t(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(text, t.data(), n + 1);
    t.resize(static_cast<size_t>(n));
    spec.text = t;
}

void Designer::FillList() {
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const auto& p : spec.photos) SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(FileName(p).c_str()));
}

void Designer::AddPhotos() {
    std::vector<wchar_t> buf(64 * 1024, L'\0');
    OPENFILENAMEW o{};
    o.lStructSize = sizeof(o);
    o.hwndOwner = hwnd;
    o.lpstrFilter = L"Bilder (JPG, PNG, BMP, TIF, GIF)\0*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff;*.gif\0";
    o.lpstrFile = buf.data();
    o.nMaxFile = static_cast<DWORD>(buf.size());
    o.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&o)) return;
    const std::wstring first = buf.data();
    const wchar_t* p = buf.data() + first.size() + 1;
    if (*p == L'\0') {
        spec.photos.push_back(first);
    } else {
        while (*p) {
            spec.photos.push_back(first + L"\\" + p);
            p += wcslen(p) + 1;
        }
    }
    FillList();
    Redraw();
}

void Designer::MovePhoto(int delta) {
    const int i = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
    const int j = i + delta;
    if (i < 0 || j < 0 || j >= static_cast<int>(spec.photos.size())) return;
    std::swap(spec.photos[i], spec.photos[j]);
    FillList();
    SendMessageW(list, LB_SETCURSEL, j, 0);
    Redraw();
}

void Designer::PickColor(COLORREF& c) {
    static COLORREF custom[16];
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hwnd;
    cc.rgbResult = c;
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&cc)) {
        c = cc.rgbResult;
        InvalidateRect(bCol1, nullptr, TRUE);
        InvalidateRect(bCol2, nullptr, TRUE);
        InvalidateRect(bColT, nullptr, TRUE);
        Redraw();
    }
}

void Designer::SaveCard() {
    Pull();
    if (spec.photos.empty() && MessageBoxW(hwnd, L"Es sind noch keine Fotos gewählt. Trotzdem speichern?", L"Karte gestalten", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    wchar_t file[MAX_PATH] = L"Karte.jpg";
    OPENFILENAMEW o{};
    o.lStructSize = sizeof(o);
    o.hwndOwner = hwnd;
    o.lpstrFilter = L"JPEG-Bild (*.jpg)\0*.jpg\0PNG-Bild (*.png)\0*.png\0";
    o.lpstrFile = file;
    o.nMaxFile = MAX_PATH;
    o.lpstrDefExt = L"jpg";
    o.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&o)) return;
    std::wstring path = file;
    const bool wantPng = o.nFilterIndex == 2;
    const size_t d = path.find_last_of(L'.');
    std::wstring ext = d == std::wstring::npos ? L"" : path.substr(d + 1);
    for (auto& c : ext) c = static_cast<wchar_t>(std::towlower(c));
    if (ext != L"jpg" && ext != L"jpeg" && ext != L"png") path += wantPng ? L".png" : L".jpg";
    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    std::wstring err;
    const bool ok = Save(spec, path, 300, &err);
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    if (ok) {
        UINT w = 0, h = 0;
        CanvasSize(spec, 300, w, h);
        SetWindowTextW(status, (L"Gespeichert: " + FileName(path) + L" (" + std::to_wstring(w) + L" × " + std::to_wstring(h) + L" px, 300 dpi)").c_str());
    } else {
        MessageBoxW(hwnd, err.c_str(), L"Karte gestalten", MB_OK | MB_ICONWARNING);
    }
}

void Designer::SetText(float mx, float my) {
    const float cw = static_cast<float>(canvas.right - canvas.left), ch = static_cast<float>(canvas.bottom - canvas.top);
    if (cw < 1 || ch < 1) return;
    spec.textX = std::clamp((mx - canvas.left) / cw, 0.f, 1.f);
    spec.textY = std::clamp((my - canvas.top) / ch, 0.f, 1.f);
    Redraw();
}

void Designer::Paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    const int aw = area.right - area.left, ah = area.bottom - area.top;
    if (aw > 10 && ah > 10) {
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, aw, ah);
        HGDIOBJ old = SelectObject(mem, bmp);
        RECT r{0, 0, aw, ah};
        FillRect(mem, &r, brBg);
        UINT cw = 0, ch = 0;
        CanvasSize(spec, 300, cw, ch);
        if (cw > 0 && ch > 0) {
            const double k = std::min(static_cast<double>(aw - S(20)) / cw, static_cast<double>(ah - S(20)) / ch);
            const UINT pw = std::max<UINT>(16, static_cast<UINT>(cw * k)), ph = std::max<UINT>(16, static_cast<UINT>(ch * k));
            if (dirty || !prev || pw != prevW || ph != prevH) {
                PhotoLoader load = [this](const std::wstring& p) { return Load(p); };
                prev = Render(spec, pw, ph, load);
                prevW = pw;
                prevH = ph;
                dirty = false;
            }
            const int x = (aw - static_cast<int>(pw)) / 2, y = (ah - static_cast<int>(ph)) / 2;
            RECT shadowR{x + S(4), y + S(5), x + static_cast<int>(pw) + S(4), y + static_cast<int>(ph) + S(5)};
            HBRUSH sh = CreateSolidBrush(RGB(8, 9, 11));
            FillRect(mem, &shadowR, sh);
            DeleteObject(sh);
            if (prev) {
                Gdiplus::Graphics g(mem);
                g.DrawImage(prev.get(), Gdiplus::Rect(x, y, static_cast<INT>(pw), static_cast<INT>(ph)), 0, 0, static_cast<INT>(pw), static_cast<INT>(ph), Gdiplus::UnitPixel);
            }
            canvas = {area.left + x, area.top + y, area.left + x + static_cast<int>(pw), area.top + y + static_cast<int>(ph)};
        }
        BitBlt(dc, area.left, area.top, aw, ah, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
    }
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    auto* d = reinterpret_cast<Designer*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        d = static_cast<Designer*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        d->hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));
    }
    if (!d) return DefWindowProcW(h, m, wp, lp);
    switch (m) {
        case WM_CREATE: d->Create(); return 0;
        case WM_SIZE: if (d->font) d->Layout(); return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize = {d->S(980), d->S(700)};
            return 0;
        }
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(h, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc, d->brBg);
            return 1;
        }
        case WM_PAINT: d->Paint(); return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, reinterpret_cast<HWND>(lp) == d->status ? theme::kGreen : theme::kText);
            SetBkColor(dc, theme::kBackground);
            return reinterpret_cast<LRESULT>(d->brBg);
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, theme::kText);
            SetBkColor(dc, theme::kCardRaised);
            return reinterpret_cast<LRESULT>(d->brCard);
        }
        case WM_DRAWITEM: {
            auto* di = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            COLORREF c = di->CtlID == ID_COL1 ? d->spec.bg1 : di->CtlID == ID_COL2 ? d->spec.bg2 : d->spec.textColor;
            HBRUSH br = CreateSolidBrush(c);
            HBRUSH border = CreateSolidBrush(theme::kBorderHover);
            FillRect(di->hDC, &di->rcItem, border);
            RECT in = di->rcItem;
            InflateRect(&in, -1, -1);
            FillRect(di->hDC, &in, br);
            DeleteObject(br);
            DeleteObject(border);
            if (di->CtlID == ID_COLT) {
                const int lum = (GetRValue(c) * 3 + GetGValue(c) * 6 + GetBValue(c)) / 10;
                SetTextColor(di->hDC, lum > 140 ? RGB(0, 0, 0) : RGB(255, 255, 255));
                SetBkMode(di->hDC, TRANSPARENT);
                SelectObject(di->hDC, d->font);
                DrawTextW(di->hDC, L"Textfarbe", -1, &in, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            if (di->itemState & ODS_FOCUS) DrawFocusRect(di->hDC, &in);
            return TRUE;
        }
        case WM_HSCROLL:
            d->Pull();
            d->Redraw();
            return 0;
        case WM_COMMAND: {
            const int id = LOWORD(wp), code = HIWORD(wp);
            switch (id) {
                case ID_ADD: d->AddPhotos(); break;
                case ID_REMOVE: {
                    const int i = static_cast<int>(SendMessageW(d->list, LB_GETCURSEL, 0, 0));
                    if (i >= 0 && i < static_cast<int>(d->spec.photos.size())) {
                        d->spec.photos.erase(d->spec.photos.begin() + i);
                        d->FillList();
                        d->Redraw();
                    }
                    break;
                }
                case ID_UP: d->MovePhoto(-1); break;
                case ID_DOWN: d->MovePhoto(1); break;
                case ID_COL1: d->PickColor(d->spec.bg1); break;
                case ID_COL2: d->PickColor(d->spec.bg2); break;
                case ID_COLT: d->PickColor(d->spec.textColor); break;
                case ID_SAVE: d->SaveCard(); break;
                case ID_CLOSE:
                case IDCANCEL: d->closed = true; DestroyWindow(h); break;
                case ID_TEXT: if (code == EN_CHANGE) { d->Pull(); d->Redraw(); } break;
                default:
                    if (id == ID_PAPER || id == ID_LAYOUT || id == ID_BG || id == ID_FONT) { if (code == CBN_SELCHANGE) { d->Pull(); d->Redraw(); } }
                    else if (id == ID_LAND || id == ID_BOLD || id == ID_ITALIC || id == ID_SHADOW || id == ID_BAND) { d->Pull(); d->Redraw(); }
                    break;
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            const POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (!d->spec.text.empty() && PtInRect(&d->canvas, p)) {
                d->dragging = true;
                SetCapture(h);
                d->SetText(static_cast<float>(p.x), static_cast<float>(p.y));
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            if (d->dragging) d->SetText(static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp)));
            return 0;
        case WM_LBUTTONUP:
            if (d->dragging) { d->dragging = false; ReleaseCapture(); }
            return 0;
        case WM_LBUTTONDBLCLK: {
            const POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (PtInRect(&d->canvas, p)) { d->spec.textX = 0.5f; d->spec.textY = -1.f; d->Redraw(); }
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && !d->spec.text.empty()) {
                POINT p;
                GetCursorPos(&p);
                ScreenToClient(h, &p);
                if (PtInRect(&d->canvas, p)) { SetCursor(LoadCursorW(nullptr, IDC_SIZEALL)); return TRUE; }
            }
            break;
        case WM_CLOSE: d->closed = true; DestroyWindow(h); return 0;
        case WM_DESTROY:
            d->closed = true;
            return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

}  // namespace

void ShowDesigner(HWND owner, const std::vector<std::wstring>& photos) {
    Session gdi;
    if (!gdi.Ok()) return;
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = Proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        wc.hIcon = reinterpret_cast<HICON>(SendMessageW(owner, WM_GETICON, ICON_BIG, 0));
        RegisterClassExW(&wc);
        registered = true;
    }

    Designer d;
    for (const auto& p : photos)
        if (Usable(p) && d.spec.photos.size() < 24) d.spec.photos.push_back(p);
    if (!photos.empty() && d.spec.photos.empty())
        MessageBoxW(owner, L"Diese Bildformate kann die Kartenfunktion noch nicht laden (JPG, PNG, BMP, TIF und GIF gehen). Fotos lassen sich im Fenster hinzufügen.", L"Karte gestalten", MB_OK | MB_ICONINFORMATION);
    static const int kBest[] = {0, 0, 1, 3, 5, 6, 7};
    d.spec.layout = kBest[std::min<size_t>(d.spec.photos.size(), 6)];
    if (!d.spec.photos.empty()) {
        if (auto first = LoadPhoto(d.spec.photos[0], 400)) d.spec.landscape = first->GetWidth() >= first->GetHeight();
    }

    const UINT sysDpi = owner ? GetDpiForWindow(owner) : GetDpiForSystem();
    const int w = MulDiv(1260, static_cast<int>(sysDpi), 96), h = MulDiv(800, static_cast<int>(sysDpi), 96);
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const int ww = std::min<int>(w, wa.right - wa.left), hh = std::min<int>(h, wa.bottom - wa.top);
    const int x = wa.left + (wa.right - wa.left - ww) / 2, y = wa.top + (wa.bottom - wa.top - hh) / 2;
    EnableWindow(owner, FALSE);
    HWND hw = CreateWindowExW(WS_EX_CONTROLPARENT, kClass, L"Karte gestalten", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE, x, y, ww, hh, owner,
                              nullptr, GetModuleHandleW(nullptr), &d);
    if (!hw) {
        EnableWindow(owner, TRUE);
        return;
    }
    d.Layout();
    MSG msg;
    while (!d.closed && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(hw, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (d.font) DeleteObject(d.font);
    if (d.brBg) DeleteObject(d.brBg);
    if (d.brCard) DeleteObject(d.brCard);
}

}  // namespace card
