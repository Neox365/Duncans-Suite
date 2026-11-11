#pragma once
// Docs-Files: Text aus gescannten PDFs und Fotos von Rechnungen mit der Windows-Texterkennung (common/Ocr.h) lesen.
// Wird nur benutzt, wenn eine PDF keinen eigenen Text enthaelt (Scan) oder die Datei ein Bild ist; der Text geht danach durch dieselbe
// Betrags- und Faelligkeitssuche wie bei normalen PDFs.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "../common/Ocr.h"
#include "InvoiceText.h"
#include "PdfText.h"

namespace docs {

// Hat der Text zu wenig sichtbare Zeichen, um eine echte Textebene zu sein? (Scans liefern nichts oder nur Rauschen)
inline bool TextTooShort(const std::wstring& t)
{
    size_t n = 0;
    for (wchar_t c : t) if (c > L' ' && ++n >= 40) return false;
    return true;
}

inline bool IsScanImageExt(const std::wstring& lowerExt)
{
    return lowerExt == L".jpg" || lowerExt == L".jpeg" || lowerExt == L".png" || lowerExt == L".bmp" || lowerExt == L".tif" || lowerExt == L".tiff";
}

namespace ocrdetail {
template <class T> struct Rel { T* p = nullptr; ~Rel() { if (p) p->Release(); } T** operator&() { return &p; } T* operator->() const { return p; } };
}

// Bild laden (EXIF-Drehung angewendet, laengste Kante hoechstens maxEdge) als BGRA
inline bool LoadImageBgra(const std::wstring& path, int maxEdge, std::vector<uint8_t>& out, int& w, int& h)
{
    using ocrdetail::Rel;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // schon initialisiert (egal wie) ist in Ordnung
    Rel<IWICImagingFactory> fac;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac)))) return false;
    Rel<IWICBitmapDecoder> dec;
    if (FAILED(fac->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) return false;
    Rel<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) return false;
    Rel<IWICBitmapSource> src;
    src.p = frame.p;
    src.p->AddRef();
    UINT orient = 1;
    {
        Rel<IWICMetadataQueryReader> q;
        if (SUCCEEDED(frame->GetMetadataQueryReader(&q))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(q->GetMetadataByName(L"/app1/ifd/{ushort=274}", &v)) && v.vt == VT_UI2) orient = v.uiVal;
            PropVariantClear(&v);
        }
    }
    auto replace = [&](IWICBitmapSource* next) { src.p->Release(); src.p = next; };
    UINT sw = 0, sh = 0;
    src->GetSize(&sw, &sh);
    if (sw == 0 || sh == 0) return false;
    if (static_cast<int>(std::max(sw, sh)) > maxEdge) {
        const double f = static_cast<double>(maxEdge) / std::max(sw, sh);
        Rel<IWICBitmapScaler> sc;
        if (FAILED(fac->CreateBitmapScaler(&sc)) || FAILED(sc->Initialize(src.p, std::max<UINT>(1, static_cast<UINT>(sw * f)), std::max<UINT>(1, static_cast<UINT>(sh * f)), WICBitmapInterpolationModeFant))) return false;
        sc.p->AddRef();
        replace(sc.p);
    }
    if (orient >= 2 && orient <= 8) {
        static const WICBitmapTransformOptions tf[9] = {WICBitmapTransformRotate0, WICBitmapTransformRotate0, WICBitmapTransformFlipHorizontal, WICBitmapTransformRotate180,
                                                        WICBitmapTransformFlipVertical, static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal),
                                                        WICBitmapTransformRotate90, static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal),
                                                        WICBitmapTransformRotate270};
        Rel<IWICBitmapFlipRotator> fr;
        if (SUCCEEDED(fac->CreateBitmapFlipRotator(&fr)) && SUCCEEDED(fr->Initialize(src.p, tf[orient]))) { fr.p->AddRef(); replace(fr.p); }
    }
    Rel<IWICFormatConverter> conv;
    if (FAILED(fac->CreateFormatConverter(&conv)) || FAILED(conv->Initialize(src.p, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    UINT cw = 0, ch = 0;
    conv->GetSize(&cw, &ch);
    out.assign(static_cast<size_t>(cw) * ch * 4, 0);
    if (FAILED(conv->CopyPixels(nullptr, cw * 4, static_cast<UINT>(out.size()), out.data()))) return false;
    w = static_cast<int>(cw);
    h = static_cast<int>(ch);
    // Transparenz auf Weiss (PNG-Scans mit Alphakanal wuerden sonst schwarz erkannt)
    for (size_t i = 0; i < out.size(); i += 4) {
        const unsigned a = out[i + 3];
        if (a == 255) continue;
        for (int c = 0; c < 3; ++c) out[i + c] = static_cast<uint8_t>((out[i + c] * a + 255 * (255 - a) + 127) / 255);
        out[i + 3] = 255;
    }
    return true;
}

// Text einer Datei per Texterkennung: PDF (die ersten maxPages Seiten, gerendert mit PDFium) oder Bild. Leer bei Fehler; error erklaert dann warum.
// pdf darf nullptr sein, dann gibt es fuer PDFs keinen Text.
inline std::wstring OcrFileText(const std::wstring& path, const PdfText* pdf, bool isPdf, int maxPages, std::wstring& error)
{
    std::wstring all;
    auto add = [&](const std::vector<uint8_t>& px, int w, int h) {
        const ocr::Result r = ocr::Recognize(px.data(), w, h);
        if (!r.ok) { if (error.empty()) error = r.error; return false; }
        if (!r.text.empty()) { if (!all.empty()) all += L"\n"; all += r.text; }
        return true;
    };
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    if (!isPdf) {
        if (!LoadImageBgra(path, 4000, px, w, h)) { error = L"Das Bild konnte nicht gelesen werden."; return L""; }
        add(px, w, h);
        return CleanOcrText(all);
    }
    if (!pdf || !pdf->Ready()) { error = L"Für die Texterkennung in PDFs wird die pdfium.dll der Suite gebraucht."; return L""; }
    for (int p = 0; p < maxPages; ++p) {
        if (!pdf->RenderPageBgra(path, p, 2800, px, w, h)) break;   // keine weitere Seite
        if (!add(px, w, h)) break;
    }
    return CleanOcrText(all);
}

}  // namespace docs
