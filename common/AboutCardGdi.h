#pragma once
// "Ueber"-Ansicht fuer die GDI-Programme (PDF Reader, Tidy): eine Karte mit Kacheln direkt im Programmfenster, kein eigenes Fenster.
// Gleiche Gestaltung wie common/AboutCard.h (Direct2D-Variante). Voraussetzung: GDI+ ist gestartet; Rundungen zeichnet detail::Round.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <gdiplus.h>

#include <algorithm>
#include <string>
#include <vector>

#include "Theme.h"

namespace aboutgdi {

struct Tile {
    std::wstring label, value;
    bool wide = false;        // ueber beide Spalten
};

struct Spec {
    std::wstring name;
    wchar_t letter = L'D';
    COLORREF accent = theme::kAccent;
    std::vector<Tile> tiles;
};

struct State {
    bool open = false;
    int hover = 0;            // 1 GitHub, 2 Schliessen
    RECT card{}, github{}, close{};
};

inline void Open(State& st, HWND hwnd) { st.open = true; st.hover = 0; InvalidateRect(hwnd, nullptr, FALSE); }
inline void Close(State& st, HWND hwnd) { st.open = false; st.hover = 0; InvalidateRect(hwnd, nullptr, FALSE); }

namespace detail {
inline HFONT MakeFont(int dpi, int pt, int weight) {
    return CreateFontW(-MulDiv(pt, dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}
inline int WrapH(HDC dc, HFONT f, const std::wstring& s, int w) {
    HGDIOBJ old = SelectObject(dc, f);
    RECT r{0, 0, std::max(1, w), 0};
    DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    return r.bottom;
}
inline void Text(HDC dc, HFONT f, COLORREF c, RECT r, const std::wstring& s, UINT flags) {
    HGDIOBJ old = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &r, flags | DT_NOPREFIX);
    SelectObject(dc, old);
}
// Abgerundetes Rechteck mit Kantenglaettung (GDI+) und 1 px Rahmen
inline void Round(HDC dc, const RECT& r, int rad, COLORREF fill, COLORREF border) {
    using namespace Gdiplus;
    Graphics gr(dc);
    gr.SetSmoothingMode(SmoothingModeAntiAlias);
    const REAL x = REAL(r.left) + 0.5f, y = REAL(r.top) + 0.5f, w = REAL(r.right - r.left) - 1, h = REAL(r.bottom - r.top) - 1, d = REAL(rad) * 2;
    GraphicsPath p;
    p.AddArc(x, y, d, d, 180, 90); p.AddArc(x + w - d, y, d, d, 270, 90); p.AddArc(x + w - d, y + h - d, d, d, 0, 90); p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
    SolidBrush b(Color(255, GetRValue(fill), GetGValue(fill), GetBValue(fill)));
    gr.FillPath(&b, &p);
    Pen pen(Color(255, GetRValue(border), GetGValue(border), GetBValue(border)), 1.0f);
    gr.DrawPath(&pen, &p);
}
}  // namespace detail

// Zeichnet die Karte ueber area (z. B. das Client-Rechteck); der vorhandene Inhalt wird abgedunkelt.
inline void Draw(HDC dc, const RECT& area, int dpi, const Spec& s, State& st) {
    using namespace detail;
    const int W = area.right - area.left, H = area.bottom - area.top;
    double sc = 1.0;                    // Passt die Karte nicht in die Hoehe, wird sie schrittweise kompakter (Abstaende und Schrift)
    auto S = [&](int v) { return std::max(1, static_cast<int>(MulDiv(v, dpi, 96) * sc + 0.5)); };
    {
        Gdiplus::Graphics gr(dc);
        Gdiplus::SolidBrush dim(Gdiplus::Color(170, 0, 0, 0));
        gr.FillRectangle(&dim, area.left, area.top, W, H);
    }
    HFONT fTitle = nullptr, fSub = nullptr, fLabel = nullptr, fVal = nullptr, fBtn = nullptr, fLogo = nullptr;
    struct Placed { const Tile* t; int x, y, w, h; };
    std::vector<Placed> placed;
    int pad = 0, gap = 0, cardW = 0, inner = 0, colW = 0, headH = 0, btnH = 0, cardH = 0, y = 0;
    for (;; sc -= 0.08) {
    for (HFONT* f : {&fTitle, &fSub, &fLabel, &fVal, &fBtn, &fLogo}) if (*f) { DeleteObject(*f); *f = nullptr; }
    auto PT = [&](int pt) { return std::max(6, static_cast<int>(pt * sc + 0.5)); };
    fTitle = MakeFont(dpi, PT(17), FW_SEMIBOLD); fSub = MakeFont(dpi, PT(10), FW_NORMAL); fLabel = MakeFont(dpi, PT(8), FW_SEMIBOLD);
    fVal = MakeFont(dpi, PT(10), FW_NORMAL); fBtn = MakeFont(dpi, PT(10), FW_SEMIBOLD); fLogo = MakeFont(dpi, PT(18), FW_BOLD);
    pad = S(28); gap = S(12);
    cardW = std::min(S(660), W - S(40)); inner = cardW - 2 * pad; colW = (inner - gap) / 2;
    placed.clear();
    y = 0;
    const int labelH = S(24), vpad = S(14);
    for (size_t i = 0; i < s.tiles.size();) {
        const Tile& a = s.tiles[i];
        if (a.wide || i + 1 >= s.tiles.size() || s.tiles[i + 1].wide) {
            const int h = std::max(S(58), labelH + WrapH(dc, fVal, a.value, inner - 2 * S(14)) + vpad);
            placed.push_back({&a, 0, y, inner, h});
            y += h + gap;
            i += 1;
        } else {
            const Tile& b = s.tiles[i + 1];
            const int h = std::max({S(58), labelH + WrapH(dc, fVal, a.value, colW - 2 * S(14)) + vpad, labelH + WrapH(dc, fVal, b.value, colW - 2 * S(14)) + vpad});
            placed.push_back({&a, 0, y, colW, h});
            placed.push_back({&b, colW + gap, y, colW, h});
            y += h + gap;
            i += 2;
        }
    }
    headH = S(76); btnH = S(40);
    cardH = pad + headH + S(14) + y + S(6) + btnH + pad;
    if (cardH <= H - S(20) || sc < 0.5) break;
    }
    cardH = std::min(cardH, H - S(20));
    const int left = area.left + (W - cardW) / 2, top = area.top + std::max(S(10), (H - cardH) / 2);
    st.card = RECT{left, top, left + cardW, top + cardH};
    detail::Round(dc, st.card, S(16), RGB(22, 24, 29), theme::kBorder);

    const int x0 = left + pad;
    int cy = top + pad;
    const RECT logo{x0, cy, x0 + S(60), cy + S(60)};
    detail::Round(dc, logo, S(14), s.accent, s.accent);
    Text(dc, fLogo, RGB(255, 255, 255), logo, std::wstring(1, s.letter), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    Text(dc, fTitle, theme::kText, RECT{x0 + S(76), cy + S(4), st.card.right - pad, cy + S(38)}, s.name, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    Text(dc, fSub, theme::kTextDim, RECT{x0 + S(76), cy + S(38), st.card.right - pad, cy + S(62)}, L"Teil von Duncan's Suite", DT_LEFT | DT_TOP | DT_SINGLELINE);
    cy += headH + S(14);

    for (const Placed& p : placed) {
        const RECT r{x0 + p.x, cy + p.y, x0 + p.x + p.w, cy + p.y + p.h};
        detail::Round(dc, r, S(12), theme::kCardRaised, theme::kBorder);
        Text(dc, fLabel, theme::kTextDim, RECT{r.left + S(14), r.top + S(9), r.right - S(14), r.top + S(26)}, p.t->label, DT_LEFT | DT_TOP | DT_SINGLELINE);
        Text(dc, fVal, theme::kText, RECT{r.left + S(14), r.top + S(28), r.right - S(14), r.bottom - S(4)}, p.t->value, DT_LEFT | DT_TOP | DT_WORDBREAK);
    }

    const int by = st.card.bottom - pad - btnH;
    st.close = RECT{st.card.right - pad - S(130), by, st.card.right - pad, by + btnH};
    st.github = RECT{st.close.left - S(10) - S(160), by, st.close.left - S(10), by + btnH};
    detail::Round(dc, st.github, S(10), st.hover == 1 ? theme::kCardHover : theme::kCardRaised, st.hover == 1 ? theme::kBorderHover : theme::kBorder);
    Text(dc, fBtn, theme::kText, st.github, L"GitHub öffnen", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    const COLORREF acc = st.hover == 2 ? RGB(96, 176, 255) : theme::kAccent;
    detail::Round(dc, st.close, S(10), acc, acc);
    Text(dc, fBtn, RGB(8, 20, 36), st.close, L"Schließen", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    DeleteObject(fTitle); DeleteObject(fSub); DeleteObject(fLabel); DeleteObject(fVal); DeleteObject(fBtn); DeleteObject(fLogo);
}

// Nachrichten behandeln, solange die Karte offen ist. 0 = nicht behandelt, 1 = behandelt (return 0), 2 = Cursor gesetzt (return TRUE)
inline int HandleMessage(HWND hwnd, State& st, UINT msg, WPARAM wp, LPARAM lp, const wchar_t* url) {
    auto hit = [&](int x, int y) {
        const POINT p{x, y};
        return PtInRect(&st.github, p) ? 1 : PtInRect(&st.close, p) ? 2 : PtInRect(&st.card, p) ? 3 : 0;
    };
    switch (msg) {
    case WM_MOUSEMOVE: {
        const int h = hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)), hv = h == 1 || h == 2 ? h : 0;
        if (hv != st.hover) { st.hover = hv; InvalidateRect(hwnd, nullptr, FALSE); }
        return 1;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        const int h = hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (h == 1 && url) ShellExecuteW(hwnd, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
        else if (h == 2 || h == 0) Close(st, hwnd);
        return 1;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE || wp == VK_RETURN || wp == VK_SPACE) Close(st, hwnd);
        return 1;
    case WM_LBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_MBUTTONDOWN: case WM_MOUSEWHEEL: case WM_KEYUP: case WM_CHAR:
        return 1;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursor(nullptr, st.hover ? IDC_HAND : IDC_ARROW));
            return 2;
        }
        return 0;
    }
    return 0;
}

}  // namespace aboutgdi
