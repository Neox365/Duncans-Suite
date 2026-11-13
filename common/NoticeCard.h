#pragma once
// Hinweis- und Rueckfrage-Karte fuer die Direct2D-Programme: ersetzt die Windows-MessageBox durch eine Karte im Design des Programms,
// direkt im Fenster (wie die "Ueber"-Karte). Nicht blockierend: Der Aufrufer zeigt sie mit Open an und reagiert, wenn HandleMessage
// kYes/kNo meldet (die Rueckgabe-Logik "if (MessageBox(...) == IDYES)" wird dadurch zu einer Rueckruf-Funktion).
// Einbau: noticecard::State im Fensterobjekt halten, am Anfang der Fensterprozedur HandleMessage aufrufen, am Ende des Zeichnens (vor EndDraw) Draw.
#include "AboutCard.h"

namespace noticecard {

enum Result { kNone = 0, kHandled = 1, kCursor = 2, kYes = 3, kNo = 4 };

struct Spec {
    std::wstring title, text;
    std::wstring yes = L"OK";       // Beschriftung des Hauptknopfs
    std::wstring no;                // leer = nur ein Knopf (reiner Hinweis); sonst der Gegenknopf ("Abbrechen")
    bool danger = false;            // Hauptknopf in Warnfarbe (zum Beispiel Loeschen)
    bool defaultYes = true;         // Enter bestaetigt den Hauptknopf; false = Enter waehlt den Gegenknopf (bei heiklen Fragen)
};

struct State {
    bool open = false;
    int hover = 0;                  // 1 Hauptknopf, 2 Gegenknopf
    D2D1_RECT_F card{}, yes{}, no{};
};

inline void Open(State& st, HWND hwnd) { st.open = true; st.hover = 0; InvalidateRect(hwnd, nullptr, FALSE); }
inline void Close(State& st, HWND hwnd) { st.open = false; st.hover = 0; InvalidateRect(hwnd, nullptr, FALSE); }

inline void Draw(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br, IDWriteFactory* dw, float k, const RECT& cr, const Spec& sIn, State& st) {
    using namespace aboutcard::detail;
    if (!rt || !br || !dw) return;
    Spec s = sIn;   // englische Fassung, falls eingestellt
    s.title = lang::Tr(s.title); s.text = lang::Tr(s.text); s.yes = lang::Tr(s.yes); s.no = lang::Tr(s.no);
    const float W = static_cast<float>(cr.right), H = static_cast<float>(cr.bottom);
    auto fill = [&](const D2D1_RECT_F& r, float rad, D2D1_COLOR_F c) { br->SetColor(c); rt->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), br); };
    auto stroke = [&](const D2D1_RECT_F& r, float rad, D2D1_COLOR_F c) { br->SetColor(c); rt->DrawRoundedRectangle(D2D1::RoundedRect(r, rad, rad), br, 1.f); };
    auto text = [&](const std::wstring& t, const D2D1_RECT_F& r, IDWriteTextFormat* f, D2D1_COLOR_F c) {
        br->SetColor(c);
        rt->DrawText(t.c_str(), static_cast<UINT32>(t.size()), f, r, br, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    br->SetColor(Hex(0x000000, 0.66f));
    rt->FillRectangle(D2D1::RectF(0, 0, W, H), br);

    auto fTitle = Fmt(dw, 19 * k, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    auto fText = Fmt(dw, 13.5f * k, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true);
    auto fBtn = Fmt(dw, 13 * k, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, false);
    if (!fTitle || !fText || !fBtn) return;
    fBtn->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    const float pad = 26 * k, cardW = std::min(560 * k, W - 40 * k), inner = cardW - 2 * pad, btnH = 40 * k;
    const float titleH = 30 * k, textH = TextH(dw, fText.Get(), s.text, inner);
    const float cardH = std::min(pad + titleH + 10 * k + textH + 24 * k + btnH + pad, H - 20 * k);
    const float left = (W - cardW) / 2, top = std::max(10.f, (H - cardH) / 2);
    st.card = D2D1::RectF(left, top, left + cardW, top + cardH);
    fill(st.card, 16 * k, Col(RGB(22, 24, 29)));
    stroke(st.card, 16 * k, Col(theme::kBorder));

    float cy = top + pad;
    text(s.title, D2D1::RectF(left + pad, cy, left + cardW - pad, cy + titleH), fTitle.Get(), Col(theme::kText));
    cy += titleH + 10 * k;
    text(s.text, D2D1::RectF(left + pad, cy, left + cardW - pad, cy + textH + 4 * k), fText.Get(), Col(theme::kTextDim));

    const float by = st.card.bottom - pad - btnH, bw = 150 * k;
    st.yes = D2D1::RectF(st.card.right - pad - bw, by, st.card.right - pad, by + btnH);
    st.no = s.no.empty() ? D2D1::RectF(0, 0, 0, 0) : D2D1::RectF(st.yes.left - 10 * k - bw, by, st.yes.left - 10 * k, by + btnH);
    if (!s.no.empty()) {
        fill(st.no, 10 * k, Col(st.hover == 2 ? theme::kCardHover : theme::kCardRaised));
        stroke(st.no, 10 * k, Col(st.hover == 2 ? theme::kBorderHover : theme::kBorder));
        text(s.no, st.no, fBtn.Get(), Col(theme::kText));
    }
    const unsigned yesCol = s.danger ? (st.hover == 1 ? 0xF0707E : 0xE5535F) : (st.hover == 1 ? 0x60B0FF : 0x409CFF);
    fill(st.yes, 10 * k, Hex(yesCol));
    text(s.yes, st.yes, fBtn.Get(), s.danger ? Hex(0xFFFFFF) : Hex(0x08141F));
}

// Rueckgabe: kNone = nicht behandelt (Karte zu), kHandled = verbraucht, kCursor = Cursor gesetzt (Prozedur gibt TRUE zurueck), kYes / kNo = Antwort
// (die Karte ist dann noch offen: der Aufrufer schliesst sie mit Close und fuehrt die Aktion aus)
inline int HandleMessage(HWND hwnd, State& st, const Spec& s, UINT msg, WPARAM wp, LPARAM lp) {
    using namespace aboutcard::detail;
    const bool hasNo = !s.no.empty();
    auto hit = [&](float x, float y) { return In(st.yes, x, y) ? 1 : (hasNo && In(st.no, x, y)) ? 2 : 0; };
    switch (msg) {
    case WM_MOUSEMOVE: {
        const int h = hit(static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp)));
        if (h != st.hover) { st.hover = h; InvalidateRect(hwnd, nullptr, FALSE); }
        return kHandled;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        const int h = hit(static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp)));
        return h == 1 ? kYes : h == 2 ? kNo : kHandled;      // Klick neben die Karte tut nichts (die Frage muss beantwortet werden)
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) return hasNo ? kNo : kYes;
        if (wp == VK_RETURN) return (s.defaultYes || !hasNo) ? kYes : kNo;
        return kHandled;
    case WM_LBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_MBUTTONDOWN: case WM_MOUSEWHEEL: case WM_KEYUP: case WM_CHAR:
        return kHandled;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursor(nullptr, st.hover ? IDC_HAND : IDC_ARROW));
            return kCursor;
        }
        return kNone;
    }
    return kNone;
}

}  // namespace noticecard
