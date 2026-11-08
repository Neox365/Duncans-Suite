#include "Card.h"

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace card {

using namespace Gdiplus;

Session::Session() {
    GdiplusStartupInput in;
    m_ok = GdiplusStartup(&m_token, &in, nullptr) == Gdiplus::Ok;
}

Session::~Session() {
    if (m_ok) GdiplusShutdown(m_token);
}

namespace {

const Paper kPapers[] = {
    {L"10 × 15 cm", 100, 150}, {L"13 × 18 cm", 130, 180}, {L"A6 (Postkarte)", 105, 148},
    {L"A5", 148, 210},         {L"A4", 210, 297},         {L"Quadrat 15 × 15 cm", 150, 150},
};

const Layout kLayouts[] = {
    {L"1 Foto", 1},
    {L"2 Fotos nebeneinander", 2},
    {L"2 Fotos untereinander", 2},
    {L"3 Fotos, eines groß links", 3},
    {L"3 Fotos, eines groß oben", 3},
    {L"4 Fotos (Raster)", 4},
    {L"5 Fotos, eines groß oben", 5},
    {L"6 Fotos (Raster)", 6},
};

}  // namespace

int PaperCount() { return static_cast<int>(sizeof(kPapers) / sizeof(kPapers[0])); }
const Paper& GetPaper(int i) { return kPapers[std::clamp(i, 0, PaperCount() - 1)]; }
int LayoutCount() { return static_cast<int>(sizeof(kLayouts) / sizeof(kLayouts[0])); }
const Layout& GetLayout(int i) { return kLayouts[std::clamp(i, 0, LayoutCount() - 1)]; }

std::vector<RectN> Cells(int layout, float aspect) {
    switch (std::clamp(layout, 0, LayoutCount() - 1)) {
        case 1: return {{0, 0, 0.5f, 1}, {0.5f, 0, 0.5f, 1}};
        case 2: return {{0, 0, 1, 0.5f}, {0, 0.5f, 1, 0.5f}};
        case 3: return {{0, 0, 0.62f, 1}, {0.62f, 0, 0.38f, 0.5f}, {0.62f, 0.5f, 0.38f, 0.5f}};
        case 4: return {{0, 0, 1, 0.62f}, {0, 0.62f, 0.5f, 0.38f}, {0.5f, 0.62f, 0.5f, 0.38f}};
        case 5: return {{0, 0, 0.5f, 0.5f}, {0.5f, 0, 0.5f, 0.5f}, {0, 0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f, 0.5f}};
        case 6: return {{0, 0, 1, 0.52f}, {0, 0.52f, 0.25f, 0.48f}, {0.25f, 0.52f, 0.25f, 0.48f}, {0.5f, 0.52f, 0.25f, 0.48f}, {0.75f, 0.52f, 0.25f, 0.48f}};
        case 7: {
            const int cols = aspect >= 1.f ? 3 : 2, rows = 6 / cols;
            std::vector<RectN> r;
            for (int i = 0; i < 6; ++i)
                r.push_back({static_cast<float>(i % cols) / cols, static_cast<float>(i / cols) / rows, 1.f / cols, 1.f / rows});
            return r;
        }
        default: return {{0, 0, 1, 1}};
    }
}

void CanvasSize(const Spec& s, UINT dpi, UINT& w, UINT& h) {
    const Paper& p = GetPaper(s.paper);
    const double a = s.landscape ? p.hMm : p.wMm, b = s.landscape ? p.wMm : p.hMm;
    w = static_cast<UINT>(std::lround(a / 25.4 * dpi));
    h = static_cast<UINT>(std::lround(b / 25.4 * dpi));
}

std::shared_ptr<Bitmap> LoadPhoto(const std::wstring& path, UINT maxEdge) {
    std::unique_ptr<Bitmap> src(Bitmap::FromFile(path.c_str(), FALSE));
    if (!src || src->GetLastStatus() != Ok || src->GetWidth() == 0 || src->GetHeight() == 0) return nullptr;
    UINT size = src->GetPropertyItemSize(PropertyTagOrientation);
    if (size > 0) {
        std::vector<BYTE> buf(size);
        auto* item = reinterpret_cast<PropertyItem*>(buf.data());
        if (src->GetPropertyItem(PropertyTagOrientation, size, item) == Ok && item->type == PropertyTagTypeShort && item->length >= 2) {
            static const RotateFlipType kMap[] = {RotateNoneFlipNone, RotateNoneFlipNone, RotateNoneFlipX, Rotate180FlipNone, Rotate180FlipX,
                                                  Rotate90FlipX,      Rotate90FlipNone,   Rotate270FlipX, Rotate270FlipNone};
            const unsigned o = *static_cast<unsigned short*>(item->value);
            if (o >= 2 && o <= 8) src->RotateFlip(kMap[o]);
        }
    }
    const UINT sw = src->GetWidth(), sh = src->GetHeight();
    double k = 1.0;
    if (maxEdge > 0 && std::max(sw, sh) > maxEdge) k = static_cast<double>(maxEdge) / std::max(sw, sh);
    const UINT dw = std::max<UINT>(1, static_cast<UINT>(std::lround(sw * k))), dh = std::max<UINT>(1, static_cast<UINT>(std::lround(sh * k)));
    auto dst = std::make_shared<Bitmap>(dw, dh, PixelFormat32bppPARGB);   // Kopie, damit die Datei nicht gesperrt bleibt
    Graphics g(dst.get());
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.DrawImage(src.get(), Rect(0, 0, static_cast<INT>(dw), static_cast<INT>(dh)), 0, 0, static_cast<INT>(sw), static_cast<INT>(sh), UnitPixel);
    return dst;
}

namespace {

void RoundedPath(GraphicsPath& p, const RectF& r, float rad) {
    rad = std::min(rad, std::min(r.Width, r.Height) / 2);
    if (rad < 0.5f) { p.AddRectangle(r); return; }
    const float d = rad * 2;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    p.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    p.CloseFigure();
}

// Quelle so beschneiden, dass sie das Ziel ausfuellt (Mitte bleibt)
void CoverSource(UINT sw, UINT sh, const RectF& dst, RectF& src) {
    const float da = dst.Width / dst.Height, sa = static_cast<float>(sw) / sh;
    if (sa > da) { src.Height = static_cast<float>(sh); src.Width = sh * da; src.Y = 0; src.X = (sw - src.Width) / 2; }
    else { src.Width = static_cast<float>(sw); src.Height = sw / da; src.X = 0; src.Y = (sh - src.Height) / 2; }
}

void DrawBackground(Graphics& g, const Spec& s, UINT w, UINT h, const PhotoLoader& load) {
    const RectF full(0, 0, static_cast<float>(w), static_cast<float>(h));
    const Color c1(255, GetRValue(s.bg1), GetGValue(s.bg1), GetBValue(s.bg1)), c2(255, GetRValue(s.bg2), GetGValue(s.bg2), GetBValue(s.bg2));
    if (s.bgMode == 2 && !s.photos.empty()) {
        if (auto img = load(s.photos[0])) {
            // unscharf: stark verkleinern und wieder aufziehen
            const UINT tw = std::max<UINT>(8, w / 24), th = std::max<UINT>(8, h / 24);
            Bitmap tiny(tw, th, PixelFormat32bppPARGB);
            {
                Graphics sg(&tiny);
                sg.SetInterpolationMode(InterpolationModeHighQualityBilinear);
                RectF src;
                CoverSource(img->GetWidth(), img->GetHeight(), RectF(0, 0, static_cast<float>(tw), static_cast<float>(th)), src);
                sg.DrawImage(img.get(), RectF(0, 0, static_cast<float>(tw), static_cast<float>(th)), src.X, src.Y, src.Width, src.Height, UnitPixel);
            }
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.DrawImage(&tiny, full, 0, 0, static_cast<float>(tw), static_cast<float>(th), UnitPixel);
            SolidBrush veil(Color(90, 255, 255, 255));   // hellt auf, damit Fotos und Text davor lesbar bleiben
            g.FillRectangle(&veil, full);
            return;
        }
    }
    if (s.bgMode >= 1) {
        LinearGradientBrush br(full, c1, c2, LinearGradientModeVertical);
        g.FillRectangle(&br, full);
    } else {
        SolidBrush br(c1);
        g.FillRectangle(&br, full);
    }
}

void DrawPhotoCell(Graphics& g, const Spec& s, const RectF& cell, float border, const std::shared_ptr<Bitmap>& img) {
    const float shortEdge = std::min(cell.Width, cell.Height);
    const float rad = shortEdge * s.radiusPct / 100.f;
    GraphicsPath outer;
    RoundedPath(outer, cell, rad);
    if (border > 0.5f) {
        SolidBrush white(Color(255, 255, 255, 255));
        g.FillPath(&white, &outer);
    }
    const float b = std::min(border, shortEdge / 4);
    RectF inner(cell.X + b, cell.Y + b, cell.Width - 2 * b, cell.Height - 2 * b);
    if (inner.Width < 2 || inner.Height < 2) return;
    GraphicsPath clip;
    RoundedPath(clip, inner, std::max(0.f, rad - b));
    if (!img) {
        SolidBrush grey(Color(255, 200, 203, 210));
        g.FillPath(&grey, &clip);
        return;
    }
    RectF src;
    CoverSource(img->GetWidth(), img->GetHeight(), inner, src);
    GraphicsState st = g.Save();
    g.SetClip(&clip, CombineModeIntersect);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.DrawImage(img.get(), inner, src.X, src.Y, src.Width, src.Height, UnitPixel);
    g.Restore(st);
}

void DrawText(Graphics& g, const Spec& s, UINT w, UINT h, const RectF& band) {
    if (s.text.empty()) return;
    std::wstring t = s.text;
    t.erase(std::remove(t.begin(), t.end(), L'\r'), t.end());
    auto fam = std::make_unique<FontFamily>(s.font.c_str());
    if (!fam->IsAvailable()) fam = std::make_unique<FontFamily>(L"Segoe UI");
    const int style = (s.bold ? FontStyleBold : 0) | (s.italic ? FontStyleItalic : 0);
    const float px = h * std::clamp(s.textSizePct, 2, 25) / 100.f;
    Font font(fam.get(), px, style, UnitPixel);
    StringFormat fmt(StringFormatFlagsNoClip);
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentNear);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    const float maxW = w * 0.92f;
    RectF m;
    g.MeasureString(t.c_str(), -1, &font, RectF(0, 0, maxW, 0), &fmt, &m);
    const float cx = std::clamp(s.textX, 0.f, 1.f) * w;
    const float cy = s.textY >= 0 ? std::clamp(s.textY, 0.f, 1.f) * h : (band.Height > 0 ? band.Y + band.Height / 2 : h * 0.88f);
    const RectF box(cx - maxW / 2, cy - m.Height / 2, maxW, m.Height);
    if (s.shadow) {
        SolidBrush sh(Color(110, 0, 0, 0));
        g.DrawString(t.c_str(), -1, &font, RectF(box.X + px * 0.05f, box.Y + px * 0.05f, box.Width, box.Height), &fmt, &sh);
    }
    SolidBrush br(Color(255, GetRValue(s.textColor), GetGValue(s.textColor), GetBValue(s.textColor)));
    g.DrawString(t.c_str(), -1, &font, box, &fmt, &br);
}

}  // namespace

std::unique_ptr<Bitmap> Render(const Spec& s, UINT w, UINT h, const PhotoLoader& load) {
    if (w == 0 || h == 0) return nullptr;
    auto bmp = std::make_unique<Bitmap>(w, h, PixelFormat32bppARGB);
    Graphics g(bmp.get());
    g.SetSmoothingMode(SmoothingModeHighQuality);
    DrawBackground(g, s, w, h, load);

    const float shortSide = static_cast<float>(std::min(w, h));
    const float margin = shortSide * std::clamp(s.marginPct, 0, 15) / 100.f;
    const float gap = shortSide * std::clamp(s.gapPct, 0, 10) / 100.f;
    const float border = shortSide * std::clamp(s.borderPct, 0, 10) / 100.f;
    RectF area(margin, margin, w - 2 * margin, h - 2 * margin);
    RectF band;
    if (s.textBand && !s.text.empty()) {
        const float bandH = area.Height * 0.2f;
        band = RectF(area.X, area.GetBottom() - bandH, area.Width, bandH);
        area.Height -= bandH;
    }
    if (area.Width < 4 || area.Height < 4) return bmp;

    const RectF grown(area.X - gap / 2, area.Y - gap / 2, area.Width + gap, area.Height + gap);
    const auto cells = Cells(s.layout, area.Width / area.Height);
    for (size_t i = 0; i < cells.size(); ++i) {
        const RectN& c = cells[i];
        const RectF cell(grown.X + c.x * grown.Width + gap / 2, grown.Y + c.y * grown.Height + gap / 2, c.w * grown.Width - gap, c.h * grown.Height - gap);
        std::shared_ptr<Bitmap> img;
        if (!s.photos.empty()) img = load(s.photos[i % s.photos.size()]);
        DrawPhotoCell(g, s, cell, border, img);
    }
    DrawText(g, s, w, h, band);
    return bmp;
}

namespace {

bool EncoderFor(const wchar_t* mime, CLSID& out) {
    UINT n = 0, size = 0;
    if (GetImageEncodersSize(&n, &size) != Ok || size == 0) return false;
    std::vector<BYTE> buf(size);
    auto* codecs = reinterpret_cast<ImageCodecInfo*>(buf.data());
    if (GetImageEncoders(n, size, codecs) != Ok) return false;
    for (UINT i = 0; i < n; ++i)
        if (wcscmp(codecs[i].MimeType, mime) == 0) { out = codecs[i].Clsid; return true; }
    return false;
}

}  // namespace

bool Save(const Spec& s, const std::wstring& path, UINT dpi, std::wstring* err) {
    auto fail = [&](const wchar_t* t) { if (err) *err = t; return false; };
    UINT w = 0, h = 0;
    CanvasSize(s, dpi, w, h);
    if (w == 0 || h == 0) return fail(L"Ungültige Kartengröße");
    const UINT maxEdge = std::max(w, h);
    PhotoLoader load = [&](const std::wstring& p) { return LoadPhoto(p, maxEdge); };
    auto bmp = Render(s, w, h, load);
    if (!bmp) return fail(L"Die Karte konnte nicht gezeichnet werden");
    bmp->SetResolution(static_cast<REAL>(dpi), static_cast<REAL>(dpi));
    const size_t dot = path.find_last_of(L'.');
    std::wstring ext = dot == std::wstring::npos ? L"" : path.substr(dot + 1);
    for (auto& c : ext) c = static_cast<wchar_t>(std::towlower(c));
    const bool png = ext == L"png";
    CLSID clsid{};
    if (!EncoderFor(png ? L"image/png" : L"image/jpeg", clsid)) return fail(L"Kein Bild-Encoder verfügbar");
    Status st;
    if (png) {
        st = bmp->Save(path.c_str(), &clsid, nullptr);
    } else {
        ULONG q = 95;
        EncoderParameters ep;
        ep.Count = 1;
        ep.Parameter[0].Guid = EncoderQuality;
        ep.Parameter[0].Type = EncoderParameterValueTypeLong;
        ep.Parameter[0].NumberOfValues = 1;
        ep.Parameter[0].Value = &q;
        st = bmp->Save(path.c_str(), &clsid, &ep);
    }
    if (st != Ok) return fail(L"Die Datei konnte nicht geschrieben werden");
    return true;
}

}  // namespace card
