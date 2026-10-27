#pragma once
// "Ueber"-Ansicht fuer die Direct2D-Programme (Docs-Files, Vault, Photo Commander): kein Windows-Fenster und keine MessageBox,
// sondern eine Karte mit Kacheln direkt im Programmfenster (wie die Kacheln der Suite). Aufbau:
//   Kopf (Logo-Buchstabe, Name, Version) | Kacheln (zwei Spalten, breite Kacheln ueber beide) | Knoepfe "GitHub oeffnen" / "Schliessen".
// Einbau je Programm: aboutcard::State im Fensterobjekt halten, am Anfang der Fensterprozedur HandleMessage aufrufen,
// am Ende des Zeichnens (vor EndDraw) Draw aufrufen, zum Oeffnen state.open = true setzen und neu zeichnen.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <string>
#include <vector>

#include "Theme.h"

namespace aboutcard {

struct Tile {
    std::wstring label, value;
    bool wide = false;        // ueber beide Spalten
};

struct Spec {
    std::wstring name, version;
    wchar_t letter = L'V';    // Buchstabe im Logo-Quadrat
    unsigned accent = 0x409CFF;   // 0xRRGGBB
    std::vector<Tile> tiles;
};

struct Layout { D2D1_RECT_F card{}, github{}, close{}; };
struct State { bool open = false; int hover = 0; Layout lay; };   // hover: 1 GitHub, 2 Schliessen

inline void Open(State& st, HWND hwnd) { st.open = true; st.hover = 0; InvalidateRect(hwnd, nullptr, FALSE); }
inline void Close(State& st, HWND hwnd) { st.open = false; st.hover = 0; InvalidateRect(hwnd, nullptr, FALSE); }

namespace detail {
inline D2D1_COLOR_F Col(COLORREF c, float a = 1.f) { return D2D1::ColorF(GetRValue(c) / 255.f, GetGValue(c) / 255.f, GetBValue(c) / 255.f, a); }
inline D2D1_COLOR_F Hex(unsigned rgb, float a = 1.f) { return D2D1::ColorF(rgb, a); }
inline bool In(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

inline Microsoft::WRL::ComPtr<IDWriteTextFormat> Fmt(IDWriteFactory* dw, float size, DWRITE_FONT_WEIGHT w, DWRITE_TEXT_ALIGNMENT al, bool wrap) {
    Microsoft::WRL::ComPtr<IDWriteTextFormat> f;
    dw->CreateTextFormat(L"Segoe UI", nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"de-DE", &f);
    if (f) {
        f->SetTextAlignment(al);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        f->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    return f;
}

// Hoehe eines umbrochenen Textes bei gegebener Breite
inline float TextH(IDWriteFactory* dw, IDWriteTextFormat* f, const std::wstring& s, float w) {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> l;
    if (FAILED(dw->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()), f, w, 10000.f, &l)) || !l) return 20.f;
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    return m.height;
}
}  // namespace detail

// Zeichnet die Karte ueber das ganze Fenster (cr = Client-Rechteck, k = DPI-Faktor) und merkt sich die Knopf-Rechtecke in st.lay.
inline void Draw(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br, IDWriteFactory* dw, float kScale, const RECT& cr, const Spec& s, State& st) {
    using namespace detail;
    if (!rt || !br || !dw) return;
    float k = kScale;
    const float W = static_cast<float>(cr.right), H = static_cast<float>(cr.bottom);
    auto fill = [&](const D2D1_RECT_F& r, float rad, D2D1_COLOR_F c) { br->SetColor(c); rt->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), br); };
    auto stroke = [&](const D2D1_RECT_F& r, float rad, D2D1_COLOR_F c, float w = 1.f) { br->SetColor(c); rt->DrawRoundedRectangle(D2D1::RoundedRect(r, rad, rad), br, w); };
    auto text = [&](const std::wstring& t, const D2D1_RECT_F& r, IDWriteTextFormat* f, D2D1_COLOR_F c) {
        br->SetColor(c);
        rt->DrawText(t.c_str(), static_cast<UINT32>(t.size()), f, r, br, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    // Hintergrund abdunkeln
    br->SetColor(Hex(0x000000, 0.66f));
    rt->FillRectangle(D2D1::RectF(0, 0, W, H), br);

    // Passt die Karte nicht in die Hoehe, wird sie schrittweise kompakter (Abstaende und Schrift)
    struct Placed { const Tile* t; float x, y, w, h; };
    std::vector<Placed> placed;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fTitle, fSub, fLabel, fVal, fBtn, fLogo;
    float pad = 0, gap = 0, cardW = 0, inner = 0, colW = 0, y = 0, headH = 0, btnH = 0, cardH = 0;
    for (float sc = 1.f;; sc -= 0.08f) {
    k = kScale * sc;                    // ab hier gilt der verkleinerte Faktor
    fTitle = Fmt(dw, 22 * k, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    fSub = Fmt(dw, 13 * k, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    fLabel = Fmt(dw, 11 * k, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    fVal = Fmt(dw, 13.5f * k, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true);
    fBtn = Fmt(dw, 13 * k, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, false);
    fLogo = Fmt(dw, 24 * k, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, false);
    if (!fTitle || !fSub || !fLabel || !fVal || !fBtn || !fLogo) return;
    fLogo->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fBtn->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    pad = 28 * k; gap = 12 * k;
    cardW = std::min(660 * kScale, W - 40 * kScale); inner = cardW - 2 * pad; colW = (inner - gap) / 2;
    placed.clear();
    y = 0;
    for (size_t i = 0; i < s.tiles.size();) {
        const Tile& a = s.tiles[i];
        const float labelH = 22 * k;
        if (a.wide || i + 1 >= s.tiles.size() || s.tiles[i + 1].wide) {
            const float h = labelH + TextH(dw, fVal.Get(), a.value, inner - 28 * k) + 16 * k;
            placed.push_back({&a, 0, y, inner, std::max(h, 58 * k)});
            y += std::max(h, 58 * k) + gap;
            i += 1;
        } else {
            const Tile& b = s.tiles[i + 1];
            const float ha = labelH + TextH(dw, fVal.Get(), a.value, colW - 28 * k) + 16 * k, hb = labelH + TextH(dw, fVal.Get(), b.value, colW - 28 * k) + 16 * k;
            const float h = std::max({ha, hb, 58 * k});
            placed.push_back({&a, 0, y, colW, h});
            placed.push_back({&b, colW + gap, y, colW, h});
            y += h + gap;
            i += 2;
        }
    }
    headH = 76 * k; btnH = 40 * k;
    cardH = pad + headH + 14 * k + y + 6 * k + btnH + pad;
    if (cardH <= H - 20 * kScale || sc < 0.5f) break;
    }
    const float top = std::max(10.f, (H - cardH) / 2);
    const D2D1_RECT_F card = D2D1::RectF((W - cardW) / 2, top, (W + cardW) / 2, top + std::min(cardH, H - 20 * k));
    st.lay.card = card;
    fill(card, 16 * k, Col(RGB(22, 24, 29)));
    stroke(card, 16 * k, Col(theme::kBorder));

    const float x0 = card.left + pad;
    float cy = card.top + pad;
    // Kopf
    const D2D1_RECT_F logo = D2D1::RectF(x0, cy, x0 + 60 * k, cy + 60 * k);
    fill(logo, 14 * k, Hex(s.accent));
    const std::wstring letter(1, s.letter);
    text(letter, logo, fLogo.Get(), Hex(0xFFFFFF));
    text(s.name, D2D1::RectF(x0 + 76 * k, cy + 6 * k, card.right - pad, cy + 38 * k), fTitle.Get(), Col(theme::kText));
    text(L"Teil von Duncan's Suite", D2D1::RectF(x0 + 76 * k, cy + 38 * k, card.right - pad, cy + 62 * k), fSub.Get(), Col(theme::kTextDim));
    cy += headH + 14 * k;

    // Kacheln
    for (const Placed& p : placed) {
        const D2D1_RECT_F r = D2D1::RectF(x0 + p.x, cy + p.y, x0 + p.x + p.w, cy + p.y + p.h);
        fill(r, 12 * k, Col(theme::kCardRaised));
        stroke(r, 12 * k, Col(theme::kBorder));
        text(p.t->label, D2D1::RectF(r.left + 14 * k, r.top + 9 * k, r.right - 14 * k, r.top + 25 * k), fLabel.Get(), Col(theme::kTextDim));
        text(p.t->value, D2D1::RectF(r.left + 14 * k, r.top + 28 * k, r.right - 14 * k, r.bottom - 6 * k), fVal.Get(), Col(theme::kText));
    }

    // Knoepfe
    const float by = card.bottom - pad - btnH;
    st.lay.close = D2D1::RectF(card.right - pad - 130 * k, by, card.right - pad, by + btnH);
    st.lay.github = D2D1::RectF(st.lay.close.left - 10 * k - 160 * k, by, st.lay.close.left - 10 * k, by + btnH);
    fill(st.lay.github, 10 * k, Col(st.hover == 1 ? theme::kCardHover : theme::kCardRaised));
    stroke(st.lay.github, 10 * k, Col(st.hover == 1 ? theme::kBorderHover : theme::kBorder));
    text(L"GitHub öffnen", st.lay.github, fBtn.Get(), Col(theme::kText));
    fill(st.lay.close, 10 * k, st.hover == 2 ? Hex(0x60B0FF) : Col(theme::kAccent));
    text(L"Schließen", st.lay.close, fBtn.Get(), Hex(0x08141F));
}

// Verarbeitet Fensternachrichten, solange die Karte offen ist.
// Rueckgabe: 0 = nicht behandelt, 1 = behandelt (Prozedur gibt 0 zurueck), 2 = Cursor gesetzt (Prozedur gibt TRUE zurueck)
inline int HandleMessage(HWND hwnd, State& st, UINT msg, WPARAM wp, LPARAM lp, const wchar_t* url) {
    using namespace detail;
    auto hit = [&](float x, float y) { return In(st.lay.github, x, y) ? 1 : In(st.lay.close, x, y) ? 2 : In(st.lay.card, x, y) ? 3 : 0; };
    switch (msg) {
    case WM_MOUSEMOVE: {
        const int h = hit(static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp)));
        const int hv = h == 1 || h == 2 ? h : 0;
        if (hv != st.hover) { st.hover = hv; InvalidateRect(hwnd, nullptr, FALSE); }
        return 1;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        const int h = hit(static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp)));
        if (h == 1 && url) ShellExecuteW(hwnd, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
        else if (h == 2 || h == 0) Close(st, hwnd);       // Klick neben die Karte schliesst sie
        return 1;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE || wp == VK_RETURN || wp == VK_SPACE) Close(st, hwnd);
        return 1;
    case WM_LBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_MBUTTONDOWN: case WM_MOUSEWHEEL:
    case WM_KEYUP: case WM_CHAR:
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

}  // namespace aboutcard
