#include "Icons.h"

#include <algorithm>
#include <cmath>

// gdiplus.h erwartet min/max im globalen Namensraum (NOMINMAX ist gesetzt)
using std::max;
using std::min;
#include <objbase.h>
#include <propidl.h>   // PROPID (von gdiplus.h benoetigt, wegen WIN32_LEAN_AND_MEAN)
#include <gdiplus.h>

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#endif

using namespace Gdiplus;

namespace
{
    ULONG_PTR g_token = 0;

    Color ToColor(COLORREF c, BYTE alpha = 255)
    {
        return Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
    }

    void AddRoundRect(GraphicsPath& path, REAL x, REAL y, REAL w, REAL h, REAL r)
    {
        r = std::min(r, std::min(w, h) / 2);
        const REAL d = r * 2;
        path.AddArc(x, y, d, d, 180, 90);
        path.AddArc(x + w - d, y, d, d, 270, 90);
        path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
        path.AddArc(x, y + h - d, d, d, 90, 90);
        path.CloseFigure();
    }

    // Zeichnet das Glyph im Einheitsquadrat (0..1), abgebildet auf das Rechteck (ox, oy, size).
    void DrawGlyph(Graphics& g, CategoryIcon icon, REAL ox, REAL oy, REAL size, COLORREF color)
    {
        const REAL pw = std::max<REAL>(1.4f, size * 0.085f);
        Pen pen(ToColor(color), pw);
        pen.SetLineJoin(LineJoinRound);
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        SolidBrush fill(ToColor(color));

        auto X = [&](REAL u) { return ox + u * size; };
        auto Y = [&](REAL v) { return oy + v * size; };
        auto P = [&](REAL u, REAL v) { return PointF(X(u), Y(v)); };
        auto Rect = [&](REAL u, REAL v, REAL w, REAL h) { g.DrawRectangle(&pen, X(u), Y(v), w * size, h * size); };
        auto RRect = [&](REAL u, REAL v, REAL w, REAL h, REAL r)
        {
            GraphicsPath p;
            AddRoundRect(p, X(u), Y(v), w * size, h * size, r * size);
            g.DrawPath(&pen, &p);
        };
        auto Line = [&](REAL u1, REAL v1, REAL u2, REAL v2) { g.DrawLine(&pen, P(u1, v1), P(u2, v2)); };
        auto Circle = [&](REAL u, REAL v, REAL r) { g.DrawEllipse(&pen, X(u - r), Y(v - r), 2 * r * size, 2 * r * size); };
        auto Dot = [&](REAL u, REAL v, REAL r) { g.FillEllipse(&fill, X(u - r), Y(v - r), 2 * r * size, 2 * r * size); };
        auto FillRectU = [&](REAL u, REAL v, REAL w, REAL h) { g.FillRectangle(&fill, X(u), Y(v), w * size, h * size); };

        switch (icon)
        {
        case CategoryIcon::Gpu:
            RRect(0.02f, 0.22f, 0.96f, 0.5f, 0.08f);
            Circle(0.36f, 0.47f, 0.15f);
            Dot(0.36f, 0.47f, 0.04f);
            Line(0.66f, 0.38f, 0.86f, 0.38f);
            Line(0.66f, 0.56f, 0.86f, 0.56f);
            Line(0.1f, 0.86f, 0.62f, 0.86f);
            break;
        case CategoryIcon::Audio:
        {
            PointF poly[] = { P(0.08f, 0.38f), P(0.28f, 0.38f), P(0.52f, 0.14f), P(0.52f, 0.86f), P(0.28f, 0.62f), P(0.08f, 0.62f) };
            g.DrawPolygon(&pen, poly, 6);
            g.DrawArc(&pen, X(0.34f), Y(0.3f), 0.4f * size, 0.4f * size, -50, 100);
            g.DrawArc(&pen, X(0.24f), Y(0.12f), 0.76f * size, 0.76f * size, -50, 100);
            break;
        }
        case CategoryIcon::Network:
            Circle(0.5f, 0.5f, 0.42f);
            g.DrawEllipse(&pen, X(0.3f), Y(0.08f), 0.4f * size, 0.84f * size);
            Line(0.08f, 0.5f, 0.92f, 0.5f);
            break;
        case CategoryIcon::Usb:
        {
            Line(0.5f, 0.14f, 0.5f, 0.84f);
            Dot(0.5f, 0.86f, 0.1f);
            Line(0.5f, 0.62f, 0.24f, 0.46f);
            Line(0.24f, 0.46f, 0.24f, 0.36f);
            FillRectU(0.16f, 0.24f, 0.16f, 0.16f);
            Line(0.5f, 0.46f, 0.76f, 0.34f);
            Dot(0.78f, 0.32f, 0.08f);
            PointF tri[] = { P(0.5f, 0.0f), P(0.38f, 0.17f), P(0.62f, 0.17f) };
            g.FillPolygon(&fill, tri, 3);
            break;
        }
        case CategoryIcon::Storage:
            RRect(0.04f, 0.26f, 0.92f, 0.48f, 0.1f);
            Line(0.18f, 0.5f, 0.52f, 0.5f);
            Dot(0.78f, 0.5f, 0.05f);
            break;
        case CategoryIcon::Cpu:
            Rect(0.26f, 0.26f, 0.48f, 0.48f);
            FillRectU(0.4f, 0.4f, 0.2f, 0.2f);
            for (int i = 0; i < 3; ++i)
            {
                const REAL t = 0.36f + 0.14f * i;
                Line(t, 0.26f, t, 0.1f);
                Line(t, 0.74f, t, 0.9f);
                Line(0.26f, t, 0.1f, t);
                Line(0.74f, t, 0.9f, t);
            }
            break;
        case CategoryIcon::System:
            Circle(0.5f, 0.5f, 0.2f);
            for (int i = 0; i < 8; ++i)
            {
                const REAL a = static_cast<REAL>(i * 3.14159265 / 4);
                Line(0.5f + std::cos(a) * 0.3f, 0.5f + std::sin(a) * 0.3f,
                     0.5f + std::cos(a) * 0.43f, 0.5f + std::sin(a) * 0.43f);
            }
            break;
        case CategoryIcon::Input:
            RRect(0.03f, 0.24f, 0.94f, 0.52f, 0.1f);
            for (int i = 0; i < 4; ++i)
            {
                Dot(0.22f + 0.185f * i, 0.4f, 0.04f);
                Dot(0.22f + 0.185f * i, 0.52f, 0.04f);
            }
            Line(0.3f, 0.65f, 0.7f, 0.65f);
            break;
        case CategoryIcon::Monitor:
            RRect(0.06f, 0.14f, 0.88f, 0.54f, 0.06f);
            Line(0.5f, 0.68f, 0.5f, 0.82f);
            Line(0.3f, 0.86f, 0.7f, 0.86f);
            break;
        case CategoryIcon::Bluetooth:
        {
            PointF pts[] = { P(0.28f, 0.3f), P(0.72f, 0.7f), P(0.5f, 0.9f), P(0.5f, 0.1f), P(0.72f, 0.3f), P(0.28f, 0.7f) };
            g.DrawLines(&pen, pts, 6);
            break;
        }
        case CategoryIcon::Camera:
            RRect(0.05f, 0.26f, 0.9f, 0.56f, 0.1f);
            Line(0.34f, 0.26f, 0.4f, 0.14f);
            Line(0.4f, 0.14f, 0.6f, 0.14f);
            Line(0.6f, 0.14f, 0.66f, 0.26f);
            Circle(0.5f, 0.54f, 0.16f);
            break;
        case CategoryIcon::Printer:
            RRect(0.08f, 0.36f, 0.84f, 0.34f, 0.07f);
            Rect(0.28f, 0.12f, 0.44f, 0.24f);
            Rect(0.28f, 0.6f, 0.44f, 0.28f);
            break;
        case CategoryIcon::Battery:
            RRect(0.04f, 0.28f, 0.8f, 0.44f, 0.08f);
            FillRectU(0.86f, 0.42f, 0.09f, 0.16f);
            FillRectU(0.13f, 0.37f, 0.4f, 0.26f);
            break;
        case CategoryIcon::Port:
            RRect(0.08f, 0.26f, 0.84f, 0.48f, 0.08f);
            Dot(0.3f, 0.5f, 0.05f);
            Dot(0.5f, 0.5f, 0.05f);
            Dot(0.7f, 0.5f, 0.05f);
            break;
        case CategoryIcon::Broom:
        {
            // Besen: senkrecht gezeichnet, dann um 35 Grad gedreht (Stiel oben rechts, Borsten unten links)
            const GraphicsState state = g.Save();
            g.TranslateTransform(X(0.5f), Y(0.56f));
            g.RotateTransform(35.0f);
            g.DrawLine(&pen, PointF(0.0f, -0.46f * size), PointF(0.0f, -0.16f * size));   // Stiel
            g.DrawRectangle(&pen, -0.08f * size, -0.18f * size, 0.16f * size, 0.1f * size);   // Zwinge
            PointF head[4] = { PointF(-0.08f * size, -0.06f * size), PointF(0.08f * size, -0.06f * size),
                               PointF(0.2f * size, 0.34f * size), PointF(-0.2f * size, 0.34f * size) };
            SolidBrush headFill(Color(70, GetRValue(color), GetGValue(color), GetBValue(color)));
            g.FillPolygon(&headFill, head, 4);
            g.DrawPolygon(&pen, head, 4);
            for (int i = -1; i <= 1; ++i)
                g.DrawLine(&pen, PointF(i * 0.05f * size, -0.02f * size), PointF(i * 0.12f * size, 0.3f * size));
            g.Restore(state);
            break;
        }
        case CategoryIcon::Other:
        default:
            RRect(0.18f, 0.18f, 0.64f, 0.64f, 0.14f);
            Dot(0.5f, 0.5f, 0.09f);
            break;
        }
    }
}

bool IconsInit()
{
    if (g_token)
        return true;
    GdiplusStartupInput input;
    return GdiplusStartup(&g_token, &input, nullptr) == Ok;
}

void IconsShutdown()
{
    if (g_token)
    {
        GdiplusShutdown(g_token);
        g_token = 0;
    }
}

void DrawCategoryTile(HDC dc, const RECT& tile, CategoryIcon icon, COLORREF color, COLORREF cardColor)
{
    if (!g_token)
        return;
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    const REAL w = static_cast<REAL>(tile.right - tile.left - 1);
    const REAL h = static_cast<REAL>(tile.bottom - tile.top - 1);

    // getoente Kachel mit feinem Rand
    GraphicsPath path;
    AddRoundRect(path, static_cast<REAL>(tile.left), static_cast<REAL>(tile.top), w, h, w * 0.26f);
    SolidBrush bg(ToColor(color, 38));
    g.FillPath(&bg, &path);
    Pen border(ToColor(color, 80), 1.0f);
    g.DrawPath(&border, &path);
    (void)cardColor;

    const REAL glyph = w * 0.56f;
    DrawGlyph(g, icon, tile.left + (w - glyph) / 2 + 0.5f, tile.top + (h - glyph) / 2 + 0.5f, glyph, color);
}

void DrawSearchIcon(HDC dc, const RECT& box, COLORREF color)
{
    if (!g_token)
        return;
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    const REAL s = static_cast<REAL>(box.right - box.left);
    Pen pen(ToColor(color), std::max<REAL>(1.5f, s * 0.11f));
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    g.DrawEllipse(&pen, box.left + s * 0.06f, box.top + s * 0.06f, s * 0.58f, s * 0.58f);
    g.DrawLine(&pen, box.left + s * 0.58f, box.top + s * 0.58f, box.left + s * 0.92f, box.top + s * 0.92f);
}

void DrawGlowBar(HDC dc, const RECT& track, double fraction, bool indeterminate, int phase,
                 COLORREF from, COLORREF to, COLORREF trackColor, bool animate)
{
    if (!g_token)
        return;
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    const REAL x = static_cast<REAL>(track.left);
    const REAL y = static_cast<REAL>(track.top);
    const REAL w = static_cast<REAL>(track.right - track.left);
    const REAL h = static_cast<REAL>(track.bottom - track.top);

    GraphicsPath trackPath;
    AddRoundRect(trackPath, x, y, w, h, h / 2);
    SolidBrush trackBrush(ToColor(trackColor));
    g.FillPath(&trackBrush, &trackPath);

    // Bereich des gefuellten Balkens
    REAL fx = x, fw = 0;
    if (indeterminate)
    {
        fw = w * 0.3f;
        const REAL travel = w + fw;
        fx = x - fw + travel * static_cast<REAL>((phase % 90) / 90.0);
    }
    else
    {
        fw = static_cast<REAL>(w * std::clamp(fraction, 0.0, 1.0));
        if (fw > 0)
            fw = std::max(fw, h);
    }
    if (fw <= 0)
        return;

    // sichtbares Stueck (am Track beschnitten)
    const REAL left = std::max(fx, x);
    const REAL right = std::min(fx + fw, x + w);
    if (right <= left)
        return;

    // Glow: weiche, halbtransparente Huellen um den Balken
    for (int i = 4; i >= 1; --i)
    {
        GraphicsPath glow;
        const REAL grow = i * 1.6f;
        AddRoundRect(glow, left - grow, y - grow, (right - left) + 2 * grow, h + 2 * grow, (h + 2 * grow) / 2);
        SolidBrush gb(ToColor(to, static_cast<BYTE>(animate ? 20 : 8)));
        g.FillPath(&gb, &glow);
    }

    GraphicsPath barPath;
    AddRoundRect(barPath, left, y, right - left, h, h / 2);
    LinearGradientBrush grad(RectF(left, y, std::max<REAL>(1.0f, right - left), h), ToColor(from), ToColor(to),
                             LinearGradientModeHorizontal);
    g.FillPath(&grad, &barPath);

    // wanderndes Glanzlicht
    if (animate)
    {
        const REAL bandW = std::max<REAL>(h * 6, (right - left) * 0.25f);
        const REAL travel = (right - left) + bandW;
        const REAL bx = left - bandW + travel * static_cast<REAL>((phase % 60) / 60.0);
        LinearGradientBrush shine(RectF(bx, y, bandW, h), Color(0, 255, 255, 255), Color(0, 255, 255, 255),
                                  LinearGradientModeHorizontal);
        const Color cols[3] = { Color(0, 255, 255, 255), Color(120, 255, 255, 255), Color(0, 255, 255, 255) };
        const REAL pos[3] = { 0.0f, 0.5f, 1.0f };
        shine.SetInterpolationColors(cols, pos, 3);
        g.SetClip(&barPath);
        g.FillRectangle(&shine, bx, y, bandW, h);
        g.ResetClip();
    }
}

void DrawShadow(HDC dc, const RECT& card, int radius, int scalePx)
{
    if (!g_token)
        return;
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    // mehrere, nach aussen groesser werdende und transparentere Huellen, leicht nach unten versetzt
    const REAL unit = std::max<REAL>(1.0f, scalePx / 96.0f);
    for (int i = 6; i >= 1; --i)
    {
        const REAL grow = i * unit * 1.1f;
        const REAL dy = unit * 3.0f;
        GraphicsPath path;
        AddRoundRect(path, card.left - grow, card.top - grow + dy, (card.right - card.left) + 2 * grow,
                     (card.bottom - card.top) + 2 * grow, radius + grow);
        SolidBrush brush(Color(static_cast<BYTE>(26), 0, 0, 0));
        g.FillPath(&brush, &path);
    }
}

void DrawChevron(HDC dc, const RECT& box, bool open, COLORREF color)
{
    if (!g_token)
        return;
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    const REAL s = static_cast<REAL>(box.right - box.left);
    Pen pen(ToColor(color), std::max<REAL>(1.6f, s * 0.13f));
    pen.SetLineJoin(LineJoinRound);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    auto P = [&](REAL u, REAL v) { return PointF(box.left + u * s, box.top + v * s); };
    PointF pts[3];
    if (open)
    {
        pts[0] = P(0.18f, 0.34f); pts[1] = P(0.5f, 0.66f); pts[2] = P(0.82f, 0.34f);
    }
    else
    {
        pts[0] = P(0.34f, 0.18f); pts[1] = P(0.66f, 0.5f); pts[2] = P(0.34f, 0.82f);
    }
    g.DrawLines(&pen, pts, 3);
}

void FillAlphaRect(HDC dc, const RECT& rc, COLORREF color, unsigned char alpha)
{
    if (!g_token)
        return;
    Graphics g(dc);
    SolidBrush brush(ToColor(color, alpha));
    g.FillRectangle(&brush, static_cast<INT>(rc.left), static_cast<INT>(rc.top),
                    static_cast<INT>(rc.right - rc.left), static_cast<INT>(rc.bottom - rc.top));
}

void DrawPanelAA(HDC dc, const RECT& rc, int radius, COLORREF fill, COLORREF border)
{
    if (!g_token)
        return;
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    GraphicsPath path;
    AddRoundRect(path, static_cast<REAL>(rc.left), static_cast<REAL>(rc.top),
                 static_cast<REAL>(rc.right - rc.left - 1), static_cast<REAL>(rc.bottom - rc.top - 1),
                 static_cast<REAL>(radius));
    SolidBrush brush(ToColor(fill));
    g.FillPath(&brush, &path);
    Pen pen(ToColor(border), 1.0f);
    g.DrawPath(&pen, &path);
}

void DrawBannerPanel(HDC dc, const RECT& rc, int radius, int scalePx)
{
    if (!g_token)
        return;
    DrawShadow(dc, rc, radius, scalePx);

    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    const REAL u = std::max<REAL>(1.0f, scalePx / 96.0f);
    const REAL w = static_cast<REAL>(rc.right - rc.left - 1);
    const REAL h = static_cast<REAL>(rc.bottom - rc.top - 1);

    GraphicsPath path;
    AddRoundRect(path, static_cast<REAL>(rc.left), static_cast<REAL>(rc.top), w, h, static_cast<REAL>(radius));

    // Verlauf: links leicht bläulich, rechts dunkler
    LinearGradientBrush grad(PointF(static_cast<REAL>(rc.left), 0), PointF(static_cast<REAL>(rc.right), 0),
                             Color(255, 30, 38, 58), Color(255, 24, 27, 36));
    g.FillPath(&grad, &path);

    // feines Punktraster (Platinen-Optik), am Panel beschnitten
    g.SetClip(&path);
    SolidBrush dot(Color(16, 150, 190, 255));
    const REAL step = 18.0f * u;
    for (REAL y = rc.top + step * 0.5f; y < rc.bottom; y += step)
        for (REAL x = rc.left + step * 0.5f; x < rc.right; x += step)
            g.FillEllipse(&dot, x - u * 0.8f, y - u * 0.8f, u * 1.6f, u * 1.6f);
    // weicher Lichtfleck oben links
    GraphicsPath glowPath;
    glowPath.AddEllipse(static_cast<REAL>(rc.left) - 120 * u, static_cast<REAL>(rc.top) - 140 * u, 420 * u, 280 * u);
    PathGradientBrush glow(&glowPath);
    glow.SetCenterColor(Color(46, 64, 156, 255));
    const Color edge(0, 64, 156, 255);
    INT count = 1;
    glow.SetSurroundColors(&edge, &count);
    g.FillPath(&glow, &glowPath);
    g.ResetClip();

    Pen border(Color(255, 45, 49, 61), 1.0f);
    g.DrawPath(&border, &path);
}

void DrawScanScene(HDC dc, const RECT& area, float probe, int phase, int scalePx, bool busy)
{
    if (!g_token)
        return;
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    const REAL u = std::max<REAL>(1.0f, scalePx / 96.0f);

    const REAL x0 = area.left + 26 * u, x1 = area.right - 26 * u;
    const REAL base = static_cast<REAL>(area.bottom) - 16 * u;
    if (x1 - x0 < 120 * u)
        return;
    probe = std::clamp(probe, 0.0f, 1.0f);
    const REAL px = x0 + (x1 - x0) * probe;

    const Color green(255, 74, 222, 128), accent(255, 64, 156, 255);

    // Leiterbahn: geprueft (gruen, laeuft aus) / ausstehend (gestrichelt)
    if (px - x0 > 4 * u)
    {
        LinearGradientBrush fade(PointF(x0, 0), PointF(px, 0), Color(0, 74, 222, 128), green);
        Pen done(&fade, 3.0f * u);
        done.SetStartCap(LineCapRound);
        done.SetEndCap(LineCapRound);
        g.DrawLine(&done, x0, base, px, base);
    }
    Pen todo(Color(110, 120, 140, 180), 2.0f * u);
    todo.SetDashStyle(DashStyleCustom);
    const REAL dashes[2] = { 2.0f, 3.5f };
    todo.SetDashPattern(dashes, 2);
    g.DrawLine(&todo, px, base, x1, base);

    // Knoten entlang der Bahn
    const REAL spacing = 46 * u;
    for (REAL x = x0 + spacing; x < x1 - 4 * u; x += spacing)
    {
        const bool checked = x < px - 6 * u;
        if (checked)
        {
            SolidBrush halo(Color(40, 74, 222, 128));
            g.FillEllipse(&halo, x - 7 * u, base - 7 * u, 14 * u, 14 * u);
            SolidBrush fill(green);
            g.FillEllipse(&fill, x - 3.6f * u, base - 3.6f * u, 7.2f * u, 7.2f * u);
        }
        else
        {
            Pen ring(Color(140, 120, 140, 180), 1.4f * u);
            g.DrawEllipse(&ring, x - 3.4f * u, base - 3.4f * u, 6.8f * u, 6.8f * u);
        }
    }

    // Strahl von der Sonde zur Bahn
    const REAL chip = 26 * u;
    const REAL cy = base - 30 * u;   // Sonden-Mitte
    {
        GraphicsPath beam;
        PointF pts[4] = { PointF(px - 7 * u, cy + chip * 0.5f), PointF(px + 7 * u, cy + chip * 0.5f),
                          PointF(px + 26 * u, base), PointF(px - 26 * u, base) };
        beam.AddPolygon(pts, 4);
        LinearGradientBrush bb(PointF(0, cy + chip * 0.5f), PointF(0, base),
                               Color(busy ? 120 : 70, 64, 156, 255), Color(0, 64, 156, 255));
        g.FillPath(&bb, &beam);
    }

    // Pulsring (nur beim Scan)
    if (busy)
    {
        const REAL t = static_cast<REAL>(phase % 40) / 40.0f;
        Pen pulse(Color(static_cast<BYTE>(150 * (1.0f - t)), 64, 156, 255), 1.6f * u);
        const REAL r = chip * 0.7f + t * 22 * u;
        g.DrawEllipse(&pulse, px - r, cy - r, 2 * r, 2 * r);
    }

    // Sonde: Chip mit Pins und Pfeil (wie das App-Icon)
    GraphicsPath body;
    AddRoundRect(body, px - chip / 2, cy - chip / 2, chip, chip, 6 * u);
    SolidBrush bodyFill(Color(255, 20, 26, 40));
    g.FillPath(&bodyFill, &body);
    Pen outline(accent, 2.2f * u);
    outline.SetLineJoin(LineJoinRound);
    g.DrawPath(&outline, &body);
    Pen pin(accent, 2.0f * u);
    pin.SetStartCap(LineCapRound);
    pin.SetEndCap(LineCapRound);
    for (int i = -1; i <= 1; ++i)
    {
        const REAL o = i * 8.5f * u;
        g.DrawLine(&pin, px + o, cy - chip / 2, px + o, cy - chip / 2 - 5 * u);
        g.DrawLine(&pin, px + o, cy + chip / 2, px + o, cy + chip / 2 + 5 * u);
        g.DrawLine(&pin, px - chip / 2, cy + o, px - chip / 2 - 5 * u, cy + o);
        g.DrawLine(&pin, px + chip / 2, cy + o, px + chip / 2 + 5 * u, cy + o);
    }
    SolidBrush arrow(green);
    PointF tri[3] = { PointF(px, cy - 7 * u), PointF(px - 7 * u, cy + 5 * u), PointF(px + 7 * u, cy + 5 * u) };
    g.FillPolygon(&arrow, tri, 3);
}
