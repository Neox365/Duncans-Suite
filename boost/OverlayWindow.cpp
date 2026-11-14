// Das Overlay-Fenster: gezeichnet mit GDI+ in ein Bild mit Transparenz (UpdateLayeredWindow), daher ohne Hintergrundfarbe und ohne Flackern.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "Overlay.h"

#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <memory>

namespace boostmode {

namespace {

const wchar_t* const kClass = L"DuncansBoostOverlay";
const BYTE kCardA = 246;

int Sc(int v, UINT dpi) { return MulDiv(v, static_cast<int>(dpi), 96); }

Gdiplus::Color C(BYTE a, int r, int g, int b) { return Gdiplus::Color(a, static_cast<BYTE>(r), static_cast<BYTE>(g), static_cast<BYTE>(b)); }
const Gdiplus::Color kText = Gdiplus::Color(255, 238, 240, 245), kDim = Gdiplus::Color(255, 150, 156, 170);
const Gdiplus::Color kOrange = Gdiplus::Color(255, 255, 150, 50), kGreen = Gdiplus::Color(255, 74, 222, 128), kRed = Gdiplus::Color(255, 248, 90, 90), kAmber = Gdiplus::Color(255, 250, 190, 60);

Gdiplus::Color LevelColor(int level) { return level >= 2 ? kRed : (level == 1 ? kAmber : kText); }

void RoundRect(Gdiplus::GraphicsPath& p, const Gdiplus::RectF& r, float rad)
{
    const float d = rad * 2;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    p.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    p.CloseFigure();
}

void FillRound(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float rad, const Gdiplus::Color& fill, const Gdiplus::Color* border = nullptr)
{
    Gdiplus::GraphicsPath p;
    RoundRect(p, r, rad);
    Gdiplus::SolidBrush b(fill);
    g.FillPath(&b, &p);
    if (border) { Gdiplus::Pen pen(*border, 1.0f); g.DrawPath(&pen, &p); }
}

struct Fonts {
    Gdiplus::FontFamily fam{ L"Segoe UI" };
    std::unique_ptr<Gdiplus::Font> fsm, body, bold, head;
    Fonts(UINT dpi)
        : fsm(new Gdiplus::Font(&fam, static_cast<Gdiplus::REAL>(Sc(11, dpi)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel)),
          body(new Gdiplus::Font(&fam, static_cast<Gdiplus::REAL>(Sc(13, dpi)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel)),
          bold(new Gdiplus::Font(&fam, static_cast<Gdiplus::REAL>(Sc(13, dpi)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel)),
          head(new Gdiplus::Font(&fam, static_cast<Gdiplus::REAL>(Sc(15, dpi)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel)) {}
};

float TextW(Gdiplus::Graphics& g, const Gdiplus::Font& f, const std::wstring& t)
{
    Gdiplus::RectF r;
    Gdiplus::StringFormat sf(Gdiplus::StringFormat::GenericTypographic());
    sf.SetFormatFlags(Gdiplus::StringFormatFlagsMeasureTrailingSpaces | Gdiplus::StringFormatFlagsNoWrap);
    g.MeasureString(t.c_str(), -1, &f, Gdiplus::PointF(0, 0), &sf, &r);
    return r.Width;
}

void Text(Gdiplus::Graphics& g, const Gdiplus::Font& f, const Gdiplus::Color& c, const std::wstring& t, float x, float y, float h)
{
    Gdiplus::StringFormat sf(Gdiplus::StringFormat::GenericTypographic());
    sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::SolidBrush b(c);
    g.DrawString(t.c_str(), -1, &f, Gdiplus::RectF(x, y, 2000.0f, h), &sf, &b);
}

}  // namespace

GameOverlay::~GameOverlay() { Destroy(); }

void GameOverlay::Destroy()
{
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    shown_ = false;
    panel_ = false;
    lastKey_.clear();
}

LRESULT CALLBACK GameOverlay::Proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
    GameOverlay* self = reinterpret_cast<GameOverlay*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    return self ? self->OnMessage(h, m, w, l) : DefWindowProcW(h, m, w, l);
}

LRESULT GameOverlay::OnMessage(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_NCHITTEST: return panel_ ? HTCLIENT : HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr, hover_ ? IDC_HAND : IDC_ARROW)); return TRUE;
    case WM_MOUSEMOVE: {
        if (!panel_) return 0;
        if (!tracking_) { TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, h, 0 }; TrackMouseEvent(&t); tracking_ = true; }
        const POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        int hv = 0;
        for (int i = 1; i < 5; ++i) if (btn_[i].right > btn_[i].left && PtInRect(&btn_[i], p)) hv = i;
        if (hv != hover_) { hover_ = hv; lastKey_.clear(); Render(); }
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_ = false;
        if (hover_) { hover_ = 0; lastKey_.clear(); Render(); }
        return 0;
    case WM_LBUTTONUP: {
        if (!panel_ || !onAction_) return 0;
        const POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };       // Treffer an der Klickstelle bestimmen, nicht aus dem letzten Hover
        for (int i = 1; i < 5; ++i)
            if (btn_[i].right > btn_[i].left && PtInRect(&btn_[i], p)) { onAction_(i); break; }
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

bool GameOverlay::Ensure()
{
    if (hwnd_) return true;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, L"", WS_POPUP, 0, 0, 8, 8, nullptr, nullptr, wc.hInstance, this);
    return hwnd_ != nullptr;
}

void GameOverlay::Place(HMONITOR monitor, int w, int h)
{
    MONITORINFO mi{ sizeof(mi) };
    RECT r{ 0, 0, 1920, 1080 };
    if (monitor && GetMonitorInfoW(monitor, &mi)) r = mi.rcMonitor;
    const int mg = Sc(24, dpi_);
    const int c = ClampOverlayCorner(st_.corner);
    const int x = (c == 0 || c == 2) ? r.left + mg : r.right - mg - w;
    const int y = (c == 0 || c == 1) ? r.top + mg : r.bottom - mg - h;
    const RECT want{ x, y, x + w, y + h };
    RECT cur{};
    GetWindowRect(hwnd_, &cur);
    if (shown_ && cur.left == want.left && cur.top == want.top && cur.right == want.right && cur.bottom == want.bottom) return;   // unveraendert: nicht neu setzen
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void GameOverlay::Render()
{
    if (!hwnd_) return;
    const UINT dpi = dpi_;
    Fonts fo(dpi);
    // 1. Groesse bestimmen (auf einer kleinen Hilfs-Zeichenflaeche messen)
    Gdiplus::Bitmap probe(4, 4, PixelFormat32bppPARGB);
    Gdiplus::Graphics pg(&probe);
    pg.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

    int W = 0, H = 0;
    struct Seg { std::wstring text; Gdiplus::Color color; bool dot; };
    std::vector<Seg> segs;
    std::wstring statsText;
    if (!panel_) {
        if (st_.boostActive) segs.push_back({ L"Boost", kOrange, true });
        if (st_.recording) segs.push_back({ L"REC " + std::to_wstring(st_.clipSeconds) + L" s", kRed, true });
        if (st_.showStats) { statsText = StatsLine(st_.live); if (!statsText.empty()) segs.push_back({ statsText, kText, false }); }
        const float pad = static_cast<float>(Sc(12, dpi)), gap = static_cast<float>(Sc(14, dpi)), dotW = static_cast<float>(Sc(14, dpi));
        float w = pad * 2;
        for (size_t i = 0; i < segs.size(); ++i) { w += (segs[i].dot ? dotW : 0) + TextW(pg, *fo.bold, segs[i].text) + (i + 1 < segs.size() ? gap : 0); }
        const float toastW = st_.toast.empty() ? 0.0f : TextW(pg, *fo.bold, st_.toast) + pad * 2;
        W = static_cast<int>(std::max(segs.empty() ? 0.0f : w, toastW)) + 2;
        const int row = Sc(30, dpi);
        H = (segs.empty() ? 0 : row) + (st_.toast.empty() ? 0 : row + (segs.empty() ? 0 : Sc(6, dpi))) + 2;
    } else {
        W = Sc(360, dpi);
        H = Sc(318, dpi);
    }
    if (W < 8 || H < 8) { if (shown_) { ShowWindow(hwnd_, SW_HIDE); shown_ = false; } return; }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) { ReleaseDC(nullptr, screen); return; }
    ZeroMemory(bits, static_cast<size_t>(W) * H * 4);
    for (RECT& r : btn_) r = RECT{};
    {
        Gdiplus::Bitmap bmp(W, H, W * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        const Gdiplus::Color border = C(120, 255, 150, 50);
        if (!panel_) {
            float y = 1;
            const float pad = static_cast<float>(Sc(12, dpi)), gap = static_cast<float>(Sc(14, dpi)), dotW = static_cast<float>(Sc(14, dpi)), row = static_cast<float>(Sc(30, dpi));
            if (!segs.empty()) {
                float w = pad * 2;
                for (size_t i = 0; i < segs.size(); ++i) w += (segs[i].dot ? dotW : 0) + TextW(g, *fo.bold, segs[i].text) + (i + 1 < segs.size() ? gap : 0);
                const float x0 = (W - 2) - w + 1;                           // rechtsbuendig im Fenster (Ecke rechts) bzw. unten gleich
                FillRound(g, Gdiplus::RectF(x0, y, w, row), row / 2, C(kCardA, 22, 25, 33), &border);
                float x = x0 + pad;
                for (size_t i = 0; i < segs.size(); ++i) {
                    if (segs[i].dot) {
                        Gdiplus::SolidBrush b(segs[i].color);
                        const float d = static_cast<float>(Sc(8, dpi));
                        g.FillEllipse(&b, x, y + (row - d) / 2, d, d);
                        x += dotW;
                    }
                    Text(g, *fo.bold, segs[i].dot ? kText : segs[i].color, segs[i].text, x, y, row);
                    x += TextW(g, *fo.bold, segs[i].text) + gap;
                }
                y += row + Sc(6, dpi);
            }
            if (!st_.toast.empty()) {
                const float tw = TextW(g, *fo.bold, st_.toast) + pad * 2;
                const Gdiplus::Color tb = st_.toastGood ? C(150, 74, 222, 128) : C(150, 248, 90, 90);
                FillRound(g, Gdiplus::RectF((W - 2) - tw + 1, y, tw, row), row / 2, C(kCardA, 22, 25, 33), &tb);
                Text(g, *fo.bold, st_.toastGood ? kGreen : kRed, st_.toast, (W - 2) - tw + 1 + pad, y, row);
            }
        } else {
            const float p = static_cast<float>(Sc(16, dpi));
            FillRound(g, Gdiplus::RectF(1, 1, static_cast<float>(W - 2), static_cast<float>(H - 2)), static_cast<float>(Sc(16, dpi)), C(kCardA, 22, 25, 33), &border);
            float y = p;
            Text(g, *fo.head, kOrange, L"Duncan's Boost", p, y, static_cast<float>(Sc(24, dpi)));
            {   // Schliessen-Knopf
                const int s = Sc(26, dpi);
                RECT r{ W - static_cast<int>(p) - s, static_cast<int>(y) - 1, W - static_cast<int>(p), static_cast<int>(y) - 1 + s };
                btn_[OA_CLOSE] = r;
                FillRound(g, Gdiplus::RectF(static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(s), static_cast<float>(s)), static_cast<float>(s) / 3, hover_ == OA_CLOSE ? C(255, 60, 64, 78) : C(255, 40, 44, 56));
                Text(g, *fo.body, kText, L"×", static_cast<float>(r.left) + s * 0.34f, static_cast<float>(r.top), static_cast<float>(s));
            }
            if (!st_.toast.empty()) {                                      // Rueckmeldung (z. B. "Clip gespeichert") neben dem Titel
                const float tw = TextW(g, *fo.bold, st_.toast);
                Text(g, *fo.bold, st_.toastGood ? kGreen : kRed, st_.toast, static_cast<float>(W) - p - static_cast<float>(Sc(26, dpi)) - static_cast<float>(Sc(12, dpi)) - tw, y, static_cast<float>(Sc(24, dpi)));
            }
            y += Sc(36, dpi);
            auto label = [&](const wchar_t* head, const std::wstring& value, const Gdiplus::Color& vc) {
                Text(g, *fo.fsm, kDim, head, p, y, static_cast<float>(Sc(18, dpi)));
                y += Sc(18, dpi);
                Text(g, *fo.bold, vc, value, p, y, static_cast<float>(Sc(24, dpi)));
            };
            auto button = [&](int id, const std::wstring& t, float x, float w, bool primary) {
                const int bh = Sc(34, dpi);
                RECT r{ static_cast<int>(x), static_cast<int>(y), static_cast<int>(x + w), static_cast<int>(y) + bh };
                btn_[id] = r;
                const bool hot = hover_ == id;
                const Gdiplus::Color fill = primary ? (hot ? C(255, 255, 175, 85) : kOrange) : (hot ? C(255, 60, 64, 78) : C(255, 40, 44, 56));
                FillRound(g, Gdiplus::RectF(x, y, w, static_cast<float>(bh)), static_cast<float>(bh) / 3, fill);
                const float tw = TextW(g, *fo.bold, t);
                Text(g, *fo.bold, primary ? C(255, 24, 18, 10) : kText, t, x + (w - tw) / 2, y, static_cast<float>(bh));
            };
            const float full = static_cast<float>(W) - 2 * p, half = (full - Sc(8, dpi)) / 2;

            // Zock-Modus
            label(L"Zock-Modus", st_.boostActive ? L"läuft" : L"aus", st_.boostActive ? kGreen : kDim);
            if (st_.boostActive) {
                const int bh = Sc(30, dpi);
                RECT r{ W - static_cast<int>(p) - Sc(120, dpi), static_cast<int>(y) - Sc(30, dpi) + 4, W - static_cast<int>(p), static_cast<int>(y) - Sc(30, dpi) + 4 + bh };
                btn_[OA_END_BOOST] = r;
                FillRound(g, Gdiplus::RectF(static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right - r.left), static_cast<float>(bh)), static_cast<float>(bh) / 3, hover_ == OA_END_BOOST ? C(255, 60, 64, 78) : C(255, 40, 44, 56));
                const float tw = TextW(g, *fo.fsm, L"Beenden");
                Text(g, *fo.fsm, kText, L"Beenden", static_cast<float>(r.left) + (static_cast<float>(r.right - r.left) - tw) / 2, static_cast<float>(r.top), static_cast<float>(bh));
            }
            y += Sc(36, dpi);

            // Clips
            label(L"Video-Clips", st_.recording ? L"Aufnahme läuft, Rückblick " + std::to_wstring(st_.clipSeconds) + L" s" : (st_.clipEnabled ? L"an, wartet auf ein Spiel" : L"aus"),
                  st_.recording ? kRed : kDim);
            y += Sc(32, dpi);
            button(OA_SAVE_CLIP, L"Clip speichern", p, half, st_.recording);
            button(OA_TOGGLE_REC, st_.clipEnabled ? L"Aufnahme aus" : L"Aufnahme an", p + half + Sc(8, dpi), half, false);
            y += Sc(48, dpi);

            // Leistung
            Text(g, *fo.fsm, kDim, L"Leistung", p, y, static_cast<float>(Sc(18, dpi)));
            y += Sc(22, dpi);
            auto meter = [&](const wchar_t* name, int pct, const std::wstring& txt, int level) {
                const float lw = static_cast<float>(Sc(44, dpi)), bh = static_cast<float>(Sc(8, dpi)), tw = static_cast<float>(Sc(74, dpi));
                Text(g, *fo.fsm, kDim, name, p, y, static_cast<float>(Sc(20, dpi)));
                const float bx = p + lw, bw = full - lw - tw;
                FillRound(g, Gdiplus::RectF(bx, y + (Sc(20, dpi) - bh) / 2, bw, bh), bh / 2, C(255, 44, 48, 60));
                if (pct > 0) FillRound(g, Gdiplus::RectF(bx, y + (Sc(20, dpi) - bh) / 2, std::max(bh, bw * std::min(pct, 100) / 100.0f), bh), bh / 2, level >= 2 ? kRed : (level == 1 ? kAmber : kOrange));
                Text(g, *fo.fsm, LevelColor(level), txt, bx + bw + Sc(8, dpi), y, static_cast<float>(Sc(20, dpi)));
                y += Sc(22, dpi);
            };
            const Live& l = st_.live;
            meter(L"CPU", std::max(0, l.cpu), l.cpu >= 0 ? std::to_wstring(l.cpu) + L" %" : L"–", LoadLevel(l.cpu));
            meter(L"GPU", std::max(0, l.gpu), l.gpu >= 0 ? std::to_wstring(l.gpu) + L" %" + (l.gpuTemp >= 0 ? L"  " + std::to_wstring(l.gpuTemp) + L"°" : L"") : L"–", std::max(LoadLevel(l.gpu), TempLevel(l.gpuTemp)));
            meter(L"RAM", std::max(0, l.ram), l.ram >= 0 ? std::to_wstring(l.ram) + L" %" : L"–", LoadLevel(l.ram));
            y += Sc(4, dpi);
            Text(g, *fo.fsm, kDim, L"Strg+Alt+O schließt  ·  Strg+Alt+F7 speichert den Clip", p, y, static_cast<float>(Sc(18, dpi)));
        }
    }
    HDC mem = CreateCompatibleDC(screen);
    HGDIOBJ old = SelectObject(mem, dib);
    POINT src{ 0, 0 };
    SIZE sz{ W, H };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    Place(mon_, W, H);
    UpdateLayeredWindow(hwnd_, screen, nullptr, &sz, mem, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(dib);
    ReleaseDC(nullptr, screen);
    shown_ = true;
}

void GameOverlay::SetPanelOpen(bool open)
{
    if (open == panel_) return;
    panel_ = open;
    hover_ = 0;
    lastKey_.clear();
    if (!hwnd_ && !Ensure()) return;
    Render();
    if (!panel_ && !visible_) { ShowWindow(hwnd_, SW_HIDE); shown_ = false; }
}

void GameOverlay::Update(bool visible, const OverlayState& st, HMONITOR monitor, UINT dpi)
{
    visible_ = visible;
    if (!visible && !panel_) {
        if (hwnd_ && shown_) { ShowWindow(hwnd_, SW_HIDE); shown_ = false; }
        st_ = st;
        return;
    }
    if (!Ensure()) return;
    // Nur neu zeichnen, wenn sich etwas Sichtbares geaendert hat
    const Live& l = st.live;
    const std::wstring key = std::to_wstring(panel_) + L"|" + std::to_wstring(st.boostActive) + std::to_wstring(st.recording) + std::to_wstring(st.clipEnabled) + L"|" + std::to_wstring(st.clipSeconds) + L"|" +
                             std::to_wstring(st.showStats) + std::to_wstring(st.corner) + L"|" + std::to_wstring(l.cpu) + L"," + std::to_wstring(l.gpu) + L"," + std::to_wstring(l.gpuTemp) + L"," + std::to_wstring(l.ram) + L"|" +
                             st.toast + (st.toastGood ? L"g" : L"b") + L"|" + std::to_wstring(hover_) + L"|" + std::to_wstring(dpi) + L"|" + std::to_wstring(reinterpret_cast<uintptr_t>(monitor));
    st_ = st;
    mon_ = monitor;
    dpi_ = dpi;
    if (key == lastKey_ && shown_) return;
    lastKey_ = key;
    Render();
}

}  // namespace boostmode
