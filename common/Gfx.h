#pragma once
// Gemeinsame kleine GDI-Helfer der Duncan's-Programme (vorher in jedem Programm einzeln kopiert).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>

#include <string>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace gfx {

// Mischt a mit b; percentB = Anteil von b in Prozent (0..100)
inline COLORREF Blend(COLORREF a, COLORREF b, int percentB)
{
    auto mix = [&](int ca, int cb) { return (ca * (100 - percentB) + cb * percentB) / 100; };
    return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

// Breite eines Textes in Pixeln bei der angegebenen Schrift
inline int TextWidth(HDC dc, HFONT font, const std::wstring& text)
{
    HGDIOBJ old = SelectObject(dc, font);
    SIZE size{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    SelectObject(dc, old);
    return size.cx;
}

// Abgerundetes Rechteck mit 1 px Rahmen (einfache GDI-Variante ohne Kantenglaettung)
inline void FillRound(HDC dc, const RECT& r, int radius, COLORREF fill, COLORREF border)
{
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

// Einzeiliger Text mit "..." am Ende, ohne &-Kuerzel
inline void DrawLabel(HDC dc, HFONT font, COLORREF color, RECT rc, const std::wstring& text, UINT flags = DT_LEFT)
{
    HGDIOBJ old = SelectObject(dc, font);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rc, flags | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, old);
}

// Dunkle Titelleiste (Windows 10 20H1 und neuer; aeltere Systeme ignorieren das Attribut)
inline void EnableDarkTitleBar(HWND hwnd)
{
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

}  // namespace gfx
