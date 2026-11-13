#pragma once
// Meldungsfenster im dunklen Design der Duncan's-Programme als Ersatz fuer MessageBoxW: gleiche Aufrufform und gleiche Rueckgabewerte
// (IDOK, IDYES, IDNO, IDCANCEL), damit sich bestehender Code mit "MessageBoxW(" -> "themedbox::Show(" umstellen laesst. Der Aufruf blockiert
// wie MessageBox (eigene Nachrichtenschleife, Besitzerfenster gesperrt), ohne GDI+ oder Direct2D.
// Unterstuetzt: MB_OK, MB_OKCANCEL, MB_YESNO, MB_YESNOCANCEL, MB_ICONINFORMATION / WARNING / ERROR / QUESTION, MB_DEFBUTTON1..3.
// Tasten: Enter = markierte Schaltflaeche, Esc = Abbrechen (sonst Nein, sonst OK), J/Y = Ja, N = Nein, Tab / Pfeile wechseln die Schaltflaeche.
// Die Rundung der Fensterecken (Windows 11) uebernimmt DWM; unter Windows 10 sind die Ecken eckig.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

#include "Lang.h"
#include "Theme.h"

namespace themedbox {

namespace detail {

struct Ctx {
    std::wstring title, text;
    int icon = 0;                    // 0 keins, 1 Information, 2 Frage, 3 Warnung, 4 Fehler
    int n = 0;
    int ids[3] = {IDOK, 0, 0};
    std::wstring labels[3];
    RECT btn[3]{};
    int def = 0, hot = -1, down = -1;
    int result = 0;
    bool done = false;
    int dpi = 96;
    RECT textRc{}, iconRc{}, titleRc{};
    HFONT fTitle = nullptr, fText = nullptr, fBtn = nullptr, fIcon = nullptr;
};

inline int Px(const Ctx& c, int v) { return MulDiv(v, c.dpi, 96); }

inline HFONT Font(int dpi, int pt, int weight) {
    return CreateFontW(-MulDiv(pt, dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

inline int EscapeId(const Ctx& c) {
    for (int i = 0; i < c.n; ++i) if (c.ids[i] == IDCANCEL) return IDCANCEL;
    for (int i = 0; i < c.n; ++i) if (c.ids[i] == IDNO) return IDNO;
    return IDOK;
}

inline void Finish(Ctx& c, int id) { c.result = id; c.done = true; }

inline COLORREF IconColor(int icon) {
    return icon == 3 ? theme::kOrange : icon == 4 ? theme::kRed : theme::kAccent;
}

inline void Paint(HWND h, Ctx& c) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT cr;
    GetClientRect(h, &cr);
    HBRUSH bg = CreateSolidBrush(RGB(22, 24, 29));
    FillRect(dc, &cr, bg);
    DeleteObject(bg);
    HPEN pen = CreatePen(PS_SOLID, 1, theme::kBorder);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, 0, 0, cr.right, cr.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    SetBkMode(dc, TRANSPARENT);

    if (c.icon) {   // farbiger Kreis mit Zeichen
        HBRUSH ib = CreateSolidBrush(IconColor(c.icon));
        HPEN ip = CreatePen(PS_SOLID, 1, IconColor(c.icon));
        HGDIOBJ o1 = SelectObject(dc, ib), o2 = SelectObject(dc, ip);
        Ellipse(dc, c.iconRc.left, c.iconRc.top, c.iconRc.right, c.iconRc.bottom);
        SelectObject(dc, o1);
        SelectObject(dc, o2);
        DeleteObject(ib);
        DeleteObject(ip);
        const wchar_t* g = c.icon == 1 ? L"i" : c.icon == 2 ? L"?" : c.icon == 3 ? L"!" : L"×";
        SelectObject(dc, c.fIcon);
        SetTextColor(dc, RGB(255, 255, 255));
        RECT r = c.iconRc;
        DrawTextW(dc, g, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(dc, c.fTitle);
    SetTextColor(dc, theme::kText);
    RECT tr = c.titleRc;
    DrawTextW(dc, c.title.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, c.fText);
    SetTextColor(dc, theme::kTextDim);
    RECT xr = c.textRc;
    DrawTextW(dc, c.text.c_str(), -1, &xr, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

    SelectObject(dc, c.fBtn);
    for (int i = 0; i < c.n; ++i) {
        const bool isDef = i == c.def, hot = i == c.hot;
        const COLORREF fill = isDef ? (hot ? RGB(96, 176, 255) : theme::kAccent) : (hot ? theme::kCardHover : theme::kCardRaised);
        HBRUSH bb = CreateSolidBrush(fill);
        HPEN bp = CreatePen(PS_SOLID, 1, isDef ? fill : (hot ? theme::kBorderHover : theme::kBorder));
        HGDIOBJ o1 = SelectObject(dc, bb), o2 = SelectObject(dc, bp);
        const int rad = Px(c, 8);
        RoundRect(dc, c.btn[i].left, c.btn[i].top, c.btn[i].right, c.btn[i].bottom, rad, rad);
        SelectObject(dc, o1);
        SelectObject(dc, o2);
        DeleteObject(bb);
        DeleteObject(bp);
        SetTextColor(dc, isDef ? RGB(8, 20, 36) : theme::kText);
        RECT r = c.btn[i];
        DrawTextW(dc, c.labels[i].c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    EndPaint(h, &ps);
}

inline int HitButton(const Ctx& c, int x, int y) {
    const POINT p{x, y};
    for (int i = 0; i < c.n; ++i) if (PtInRect(&c.btn[i], p)) return i;
    return -1;
}

inline LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Ctx* c = reinterpret_cast<Ctx*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        c = static_cast<Ctx*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(c));
    }
    if (!c) return DefWindowProcW(h, msg, wp, lp);
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: Paint(h, *c); return 0;
    case WM_MOUSEMOVE: {
        const int i = HitButton(*c, static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp)));
        if (i != c->hot) {
            c->hot = i;
            InvalidateRect(h, nullptr, FALSE);
            TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
            TrackMouseEvent(&t);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        c->hot = -1;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        c->down = HitButton(*c, static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp)));
        SetCapture(h);
        return 0;
    case WM_LBUTTONUP: {
        ReleaseCapture();
        const int i = HitButton(*c, static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp)));
        if (i >= 0 && i == c->down) Finish(*c, c->ids[i]);
        c->down = -1;
        return 0;
    }
    case WM_KEYDOWN:
        switch (wp) {
        case VK_RETURN: Finish(*c, c->ids[c->def]); return 0;
        case VK_ESCAPE: Finish(*c, EscapeId(*c)); return 0;
        case VK_TAB: case VK_RIGHT: case VK_LEFT: {
            const bool back = wp == VK_LEFT || (wp == VK_TAB && GetKeyState(VK_SHIFT) < 0);
            c->def = (c->def + (back ? c->n - 1 : 1)) % c->n;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case 'J': case 'Y':
            for (int i = 0; i < c->n; ++i) if (c->ids[i] == IDYES) Finish(*c, IDYES);
            return 0;
        case 'N':
            for (int i = 0; i < c->n; ++i) if (c->ids[i] == IDNO) Finish(*c, IDNO);
            return 0;
        }
        return 0;
    case WM_CLOSE: Finish(*c, EscapeId(*c)); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

inline const wchar_t* ClassName() { return L"DuncansThemedBox"; }

inline void Register() {
    static bool done = false;
    if (done) return;
    done = true;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.lpszClassName = ClassName();
    RegisterClassExW(&wc);
}

}  // namespace detail

// Wie MessageBoxW. title darf leer sein; text beliebig lang (wird umbrochen).
inline int Show(HWND owner, const wchar_t* text, const wchar_t* title, UINT flags) {
    using namespace detail;
    Register();
    Ctx c;
    c.text = lang::Tr(text ? text : L"");
    c.title = lang::Tr(title ? title : L"");
    switch (flags & MB_ICONMASK) {
        case MB_ICONINFORMATION: c.icon = 1; break;
        case MB_ICONQUESTION: c.icon = 2; break;
        case MB_ICONWARNING: c.icon = 3; break;
        case MB_ICONERROR: c.icon = 4; break;
    }
    switch (flags & MB_TYPEMASK) {
        case MB_OKCANCEL: c.n = 2; c.ids[0] = IDOK; c.ids[1] = IDCANCEL; c.labels[0] = L"OK"; c.labels[1] = L"Abbrechen"; break;
        case MB_YESNO: c.n = 2; c.ids[0] = IDYES; c.ids[1] = IDNO; c.labels[0] = L"Ja"; c.labels[1] = L"Nein"; break;
        case MB_YESNOCANCEL: c.n = 3; c.ids[0] = IDYES; c.ids[1] = IDNO; c.ids[2] = IDCANCEL; c.labels[0] = L"Ja"; c.labels[1] = L"Nein"; c.labels[2] = L"Abbrechen"; break;
        default: c.n = 1; c.ids[0] = IDOK; c.labels[0] = L"OK"; break;
    }
    for (auto& l : c.labels) l = lang::Tr(l);
    const UINT d = flags & MB_DEFMASK;
    c.def = d == MB_DEFBUTTON2 && c.n > 1 ? 1 : d == MB_DEFBUTTON3 && c.n > 2 ? 2 : 0;
    HWND parent = owner && IsWindow(owner) ? owner : nullptr;
    c.dpi = parent ? static_cast<int>(GetDpiForWindow(parent)) : static_cast<int>(GetDpiForSystem());
    if (c.dpi < 96) c.dpi = 96;
    c.fTitle = Font(c.dpi, 12, FW_SEMIBOLD);
    c.fText = Font(c.dpi, 10, FW_NORMAL);
    c.fBtn = Font(c.dpi, 10, FW_SEMIBOLD);
    c.fIcon = Font(c.dpi, 16, FW_BOLD);

    // Layout
    const int pad = Px(c, 24), width = Px(c, 480), icon = c.icon ? Px(c, 44) : 0, gapIcon = c.icon ? Px(c, 16) : 0;
    const int textX = pad + icon + gapIcon, textW = width - textX - pad;
    const int titleH = c.title.empty() ? 0 : Px(c, 30), btnH = Px(c, 36), btnW = Px(c, 120), btnGap = Px(c, 10);
    HDC dc = GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, c.fText);
    RECT m{0, 0, textW, 0};
    DrawTextW(dc, c.text.c_str(), -1, &m, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    const int textH = std::max<int>(m.bottom, c.icon ? icon - titleH : 0);
    const int top = pad;
    c.iconRc = {pad, top, pad + icon, top + icon};
    c.titleRc = {textX, top, width - pad, top + titleH};
    c.textRc = {textX, top + titleH + (titleH ? Px(c, 4) : 0), width - pad, top + titleH + (titleH ? Px(c, 4) : 0) + textH};
    const int btnTop = c.textRc.bottom + Px(c, 24), height = btnTop + btnH + pad;
    int bx = width - pad - c.n * btnW - (c.n - 1) * btnGap;
    for (int i = 0; i < c.n; ++i, bx += btnW + btnGap) c.btn[i] = {bx, btnTop, bx + btnW, btnTop + btnH};

    // Position: mittig ueber dem Besitzerfenster, sonst mittig auf dem Bildschirm
    RECT area{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    if (parent) {
        GetWindowRect(parent, &area);
        if (IsIconic(parent)) area = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    }
    const int x = area.left + (area.right - area.left - width) / 2, y = area.top + (area.bottom - area.top - height) / 2;
    HWND h = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, ClassName(), c.title.c_str(), WS_POPUP, x, y, width, height, parent, nullptr, GetModuleHandleW(nullptr), &c);
    if (!h) {
        DeleteObject(c.fTitle); DeleteObject(c.fText); DeleteObject(c.fBtn); DeleteObject(c.fIcon);
        return MessageBoxW(owner, c.text.c_str(), c.title.c_str(), flags);   // Notausgang: Windows-Fenster
    }
    {   // runde Ecken (Windows 11): DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_ROUND = 2
        const HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        using Fn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        const Fn set = dwm ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(dwm, "DwmSetWindowAttribute"))) : nullptr;
        if (set) { const int round = 2; set(h, 33, &round, sizeof(round)); }
    }
    if (parent) EnableWindow(parent, FALSE);
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    SetFocus(h);
    if (!GetEnvironmentVariableW(L"DUNCANS_QUIET", nullptr, 0))   // Tests setzen DUNCANS_QUIET, damit es nicht piept
        MessageBeep(c.icon == 4 ? MB_ICONERROR : c.icon == 3 ? MB_ICONWARNING : MB_OK);
    MSG msg;
    while (!c.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (!c.done) PostQuitMessage(0);   // WM_QUIT weitergeben, falls das Programm waehrenddessen beendet wird
    if (parent) EnableWindow(parent, TRUE);
    DestroyWindow(h);
    if (parent) SetForegroundWindow(parent);
    DeleteObject(c.fTitle); DeleteObject(c.fText); DeleteObject(c.fBtn); DeleteObject(c.fIcon);
    return c.result ? c.result : IDCANCEL;
}

}  // namespace themedbox
