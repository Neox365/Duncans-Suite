// Das Fadenkreuz-Fenster: durchklickbar, ohne Rahmen, immer oben, nimmt nie den Fokus. Gezeichnet wird mit GDI+ in ein
// Bild mit Transparenz (UpdateLayeredWindow), daher keine Hintergrundfarbe und kein Flackern.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "Crosshair.h"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>

namespace boostmode {

namespace {
const wchar_t* const kClass = L"DuncansBoostCrosshair";

LRESULT CALLBACK OverlayProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCHITTEST) return HTTRANSPARENT;
    if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(h, m, w, l);
}

int Scale(int v, UINT dpi) { return MulDiv(v, static_cast<int>(dpi), 96); }
}  // namespace

CrosshairOverlay::~CrosshairOverlay() { Destroy(); }

void CrosshairOverlay::Destroy() {
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    shown_ = false;
    drawnDpi_ = 0;
}

bool CrosshairOverlay::Ensure() {
    if (hwnd_) return true;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = OverlayProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);   // schlaegt beim zweiten Mal fehl: egal
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, L"", WS_POPUP,
                            0, 0, 8, 8, nullptr, nullptr, wc.hInstance, nullptr);
    return hwnd_ != nullptr;
}

void CrosshairOverlay::Render(const CrosshairSpec& spec, UINT dpi) {
    side_ = Scale(2 * (CrosshairSizePx(2) + 6), dpi);          // Platz fuer die groesste Variante
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = side_;
    bi.bmiHeader.biHeight = -side_;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) { ReleaseDC(nullptr, screen); return; }
    ZeroMemory(bits, static_cast<size_t>(side_) * side_ * 4);
    {
        Gdiplus::Bitmap bmp(side_, side_, side_ * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const COLORREF c = CrosshairColorValue(spec.color);
        const float mid = side_ / 2.0f;
        const float arm = static_cast<float>(Scale(CrosshairSizePx(spec.size), dpi));
        const float gap = static_cast<float>(Scale(3, dpi));
        const float thick = std::max(2.0f, static_cast<float>(Scale(2, dpi)));
        Gdiplus::Pen outline(Gdiplus::Color(200, 0, 0, 0), thick + 2.0f);
        Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)), thick);
        Gdiplus::SolidBrush fill(Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)));
        Gdiplus::SolidBrush back(Gdiplus::Color(200, 0, 0, 0));
        auto cross = [&](bool withGap) {
            const float in = withGap ? gap : 0.0f;
            for (Gdiplus::Pen* p : { &outline, &pen }) {
                g.DrawLine(p, mid - arm, mid, mid - in, mid);
                g.DrawLine(p, mid + in, mid, mid + arm, mid);
                g.DrawLine(p, mid, mid - arm, mid, mid - in);
                g.DrawLine(p, mid, mid + in, mid, mid + arm);
            }
        };
        auto dot = [&](float r) {
            g.FillEllipse(&back, mid - r - 1.0f, mid - r - 1.0f, 2 * r + 2.0f, 2 * r + 2.0f);
            g.FillEllipse(&fill, mid - r, mid - r, 2 * r, 2 * r);
        };
        switch (spec.style) {
        case 0: cross(true); break;
        case 1: cross(false); break;
        case 2: dot(thick * 1.1f); break;
        case 3: cross(true); dot(thick * 0.9f); break;
        default:
            g.DrawEllipse(&outline, mid - arm, mid - arm, 2 * arm, 2 * arm);
            g.DrawEllipse(&pen, mid - arm, mid - arm, 2 * arm, 2 * arm);
            dot(thick * 0.8f);
            break;
        }
    }
    HDC mem = CreateCompatibleDC(screen);
    HGDIOBJ old = SelectObject(mem, dib);
    POINT src{ 0, 0 };
    SIZE sz{ side_, side_ };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(hwnd_, screen, nullptr, &sz, mem, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(dib);
    ReleaseDC(nullptr, screen);
}

void CrosshairOverlay::Update(bool visible, const CrosshairSpec& specIn, HMONITOR monitor, UINT dpi) {
    if (!visible) {
        if (hwnd_ && shown_) { ShowWindow(hwnd_, SW_HIDE); shown_ = false; }
        return;
    }
    if (!Ensure()) return;
    const CrosshairSpec spec = ClampCrosshair(specIn);
    if (dpi == 0) dpi = 96;
    if (drawnDpi_ != dpi || drawn_.style != spec.style || drawn_.color != spec.color || drawn_.size != spec.size) {
        Render(spec, dpi);
        drawn_ = spec;
        drawnDpi_ = dpi;
        cx_ = cy_ = -1;
    }
    MONITORINFO mi{ sizeof(mi) };
    RECT area{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    if (monitor && GetMonitorInfoW(monitor, &mi)) area = mi.rcMonitor;
    const int x = (area.left + area.right) / 2 - side_ / 2;
    const int y = (area.top + area.bottom) / 2 - side_ / 2;
    // Spiele holen sich gern die oberste Ebene: bei jedem Durchlauf wieder nach oben, ohne Fokus
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    cx_ = x; cy_ = y;
    shown_ = true;
}

}  // namespace boostmode
