#include "ImageOps.h"
#include "ByteStrip.h"

#include <wrl/client.h>
#include <wincodecsdk.h>
#include <propvarutil.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

using Microsoft::WRL::ComPtr;

namespace imgops {

namespace {

ComPtr<IWICImagingFactory> MakeFactory() {
    ComPtr<IWICImagingFactory> f;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
    return f;
}

std::wstring LowerExt(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return L"";
    std::wstring e = path.substr(dot + 1);
    for (auto& c : e) c = (wchar_t)towlower(c);
    return e;
}

bool IsJpegExt(const std::wstring& e) { return e == L"jpg" || e == L"jpeg" || e == L"jpe"; }

void SetErr(std::wstring* err, const std::wstring& msg) {
    if (err) *err = msg;
}

// ---- Metadaten-Abfrage ------------------------------------------------------

// Probiert den JPEG-Pfad ("/app1/...") und danach den TIFF-/generischen Pfad.
bool Query(IWICMetadataQueryReader* r, const std::wstring& suffix, PROPVARIANT* pv) {
    if (!r) return false;
    PropVariantInit(pv);
    if (SUCCEEDED(r->GetMetadataByName((L"/app1" + suffix).c_str(), pv))) return true;
    PropVariantClear(pv);
    PropVariantInit(pv);
    if (SUCCEEDED(r->GetMetadataByName(suffix.c_str(), pv))) return true;
    PropVariantClear(pv);
    return false;
}

std::wstring Widen(const char* s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n <= 1) return L"";
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}

bool AsString(const PROPVARIANT& pv, std::wstring& out) {
    switch (pv.vt) {
        case VT_LPSTR:  out = pv.pszVal ? Widen(pv.pszVal) : L""; break;
        case VT_LPWSTR: out = pv.pwszVal ? pv.pwszVal : L""; break;
        case VT_BSTR:   out = pv.bstrVal ? pv.bstrVal : L""; break;
        default: return false;
    }
    while (!out.empty() && (out.back() == L' ' || out.back() == L'\0')) out.pop_back();
    return !out.empty();
}

bool AsUInt(const PROPVARIANT& pv, unsigned long long& v) {
    switch (pv.vt) {
        case VT_UI1: v = pv.bVal; return true;
        case VT_UI2: v = pv.uiVal; return true;
        case VT_UI4: v = pv.ulVal; return true;
        case VT_UI8: v = pv.uhVal.QuadPart; return true;
        default: return false;
    }
}

// EXIF-RATIONAL: Zaehler in den unteren, Nenner in den oberen 32 Bit.
bool Rational(ULARGE_INTEGER u, double& v) {
    if (u.HighPart == 0) return false;
    v = (double)u.LowPart / (double)u.HighPart;
    return true;
}

bool AsRational(const PROPVARIANT& pv, double& v) {
    return pv.vt == VT_UI8 && Rational(pv.uhVal, v);
}

std::wstring Fmt(const wchar_t* f, double a) {
    wchar_t buf[64];
    swprintf_s(buf, f, a);
    return buf;
}

// GPS: drei Rationals (Grad, Minuten, Sekunden) + Referenzbuchstabe.
bool ReadGpsCoord(IWICMetadataQueryReader* r, int valueTag, int refTag, double& deg) {
    PROPVARIANT v;
    if (!Query(r, L"/ifd/gps/{ushort=" + std::to_wstring(valueTag) + L"}", &v)) return false;
    bool ok = false;
    if (v.vt == (VT_VECTOR | VT_UI8) && v.cauh.cElems == 3) {
        double d, m, s;
        if (Rational(v.cauh.pElems[0], d) && Rational(v.cauh.pElems[1], m) && Rational(v.cauh.pElems[2], s)) {
            deg = d + m / 60.0 + s / 3600.0;
            ok = true;
        }
    }
    PropVariantClear(&v);
    if (!ok) return false;
    PROPVARIANT ref;
    if (Query(r, L"/ifd/gps/{ushort=" + std::to_wstring(refTag) + L"}", &ref)) {
        std::wstring s;
        if (AsString(ref, s) && (s[0] == L'S' || s[0] == L'W')) deg = -deg;
        PropVariantClear(&ref);
    }
    return true;
}

// ---- JPEG-Segmente (verlustfreies Entfernen) --------------------------------

bool ReadAll(const std::wstring& path, std::vector<BYTE>& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart < (1LL << 31);
    if (ok) {
        data.resize((size_t)sz.QuadPart);
        DWORD got = 0;
        ok = ReadFile(h, data.data(), (DWORD)data.size(), &got, nullptr) && got == data.size();
    }
    CloseHandle(h);
    return ok;
}

bool WriteAll(const std::wstring& path, const std::vector<BYTE>& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &put, nullptr) && put == data.size();
    CloseHandle(h);
    return ok;
}

// ---- WIC Re-Encode ----------------------------------------------------------

struct EncodeOpts {
    int rotateCw = 0;            // 0/90/180/270 zusaetzlich zur EXIF-Orientation
    bool keepMetadata = false;
    bool crop = false;           // Ausschnitt in Bruchteilen (0..1) des aufrecht stehenden Bildes
    double cx0 = 0, cy0 = 0, cx1 = 1, cy1 = 1;
    UINT maxEdge = 0;            // > 0: laengste Kante auf diesen Wert verkleinern (nie vergroessern)
    float quality = 0.95f;       // nur JPEG
};

// Drehung der Pixel, dann Encode in dasselbe Containerformat ohne Metadaten
// (keepMetadata=false) bzw. mit kopierten Metadaten und Orientation=1.
bool Reencode(const std::wstring& src, const std::wstring& dst, const EncodeOpts& o, std::wstring* err) {
    const int extraRotateCw = o.rotateCw;
    const bool keepMetadata = o.keepMetadata;
    auto f = MakeFactory();
    if (!f) { SetErr(err, L"WIC nicht verfuegbar"); return false; }

    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromFilename(src.c_str(), nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnDemand, &dec))) {
        SetErr(err, L"Datei konnte nicht gelesen werden");
        return false;
    }
    GUID container{};
    dec->GetContainerFormat(&container);
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) { SetErr(err, L"Kein Bild-Frame"); return false; }

    // Orientation einbrennen (Tag wird beim Re-Encode nicht mehr geschrieben bzw. auf 1 gesetzt).
    ComPtr<IWICBitmapSource> cur = frame;
    WICBitmapTransformOptions t = OrientationToTransform(ReadOrientation(frame.Get()));
    if (t != WICBitmapTransformRotate0) {
        ComPtr<IWICBitmapFlipRotator> fr;
        if (SUCCEEDED(f->CreateBitmapFlipRotator(&fr)) && SUCCEEDED(fr->Initialize(cur.Get(), t))) cur = fr;
    }
    if (extraRotateCw != 0) {
        ComPtr<IWICBitmapFlipRotator> fr;
        WICBitmapTransformOptions o = extraRotateCw == 90    ? WICBitmapTransformRotate90
                                      : extraRotateCw == 180 ? WICBitmapTransformRotate180
                                                             : WICBitmapTransformRotate270;
        if (FAILED(f->CreateBitmapFlipRotator(&fr)) || FAILED(fr->Initialize(cur.Get(), o))) {
            SetErr(err, L"Drehen fehlgeschlagen");
            return false;
        }
        cur = fr;
    }
    if (o.crop) {
        UINT cw = 0, ch = 0;
        cur->GetSize(&cw, &ch);
        auto px = [](double frac, UINT total) { return (UINT)std::clamp((long long)std::llround(frac * total), 0ll, (long long)total); };
        UINT x0 = std::min(px(o.cx0, cw), cw - 1), y0 = std::min(px(o.cy0, ch), ch - 1);
        UINT x1 = std::max(px(o.cx1, cw), x0 + 1), y1 = std::max(px(o.cy1, ch), y0 + 1);
        WICRect rc{(INT)x0, (INT)y0, (INT)(std::min(x1, cw) - x0), (INT)(std::min(y1, ch) - y0)};
        ComPtr<IWICBitmapClipper> cl;
        if (FAILED(f->CreateBitmapClipper(&cl)) || FAILED(cl->Initialize(cur.Get(), &rc))) {
            SetErr(err, L"Zuschneiden fehlgeschlagen");
            return false;
        }
        cur = cl;
    }
    if (o.maxEdge > 0) {
        UINT sw = 0, sh = 0;
        cur->GetSize(&sw, &sh);
        if (std::max(sw, sh) > o.maxEdge) {
            const double sc = (double)o.maxEdge / std::max(sw, sh);
            const UINT nw = std::max(1u, (UINT)std::lround(sw * sc)), nh = std::max(1u, (UINT)std::lround(sh * sc));
            ComPtr<IWICFormatConverter> cv;   // Skalierer arbeitet nicht mit Palettenformaten
            ComPtr<IWICBitmapScaler> scaler;
            if (FAILED(f->CreateFormatConverter(&cv)) ||
                FAILED(cv->Initialize(cur.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                      WICBitmapPaletteTypeCustom)) ||
                FAILED(f->CreateBitmapScaler(&scaler)) ||
                FAILED(scaler->Initialize(cv.Get(), nw, nh, WICBitmapInterpolationModeFant))) {
                SetErr(err, L"Verkleinern fehlgeschlagen");
                return false;
            }
            cur = scaler;
        }
    }

    ComPtr<IWICBitmapEncoder> enc;
    if (FAILED(f->CreateEncoder(container, nullptr, &enc))) {
        SetErr(err, L"Dateiformat kann nicht geschrieben werden");
        return false;
    }
    std::wstring tmp = dst + L".pctmp";
    ComPtr<IWICStream> stream;
    if (FAILED(f->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(tmp.c_str(), GENERIC_WRITE)) ||
        FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache))) {
        SetErr(err, L"Zieldatei kann nicht angelegt werden");
        return false;
    }

    ComPtr<IWICBitmapFrameEncode> out;
    ComPtr<IPropertyBag2> props;
    if (FAILED(enc->CreateNewFrame(&out, &props))) { SetErr(err, L"Encoder-Fehler"); return false; }
    if (container == GUID_ContainerFormatJpeg && props) {
        PROPBAG2 opt{};
        opt.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_R4;
        v.fltVal = o.quality;
        props->Write(1, &opt, &v);
    }
    out->Initialize(props.Get());

    UINT w = 0, h = 0;
    cur->GetSize(&w, &h);
    out->SetSize(w, h);
    double dx = 96, dy = 96;
    cur->GetResolution(&dx, &dy);
    out->SetResolution(dx, dy);
    WICPixelFormatGUID pf{};
    cur->GetPixelFormat(&pf);
    out->SetPixelFormat(&pf);

    // Farbprofil ist keine "Metadatum" im Datenschutzsinn -> behalten.
    UINT nCtx = 0;
    frame->GetColorContexts(0, nullptr, &nCtx);
    if (nCtx > 0 && nCtx < 8) {
        std::vector<ComPtr<IWICColorContext>> ctx(nCtx);
        std::vector<IWICColorContext*> raw(nCtx);
        for (UINT i = 0; i < nCtx; ++i) { f->CreateColorContext(&ctx[i]); raw[i] = ctx[i].Get(); }
        UINT got = 0;
        if (SUCCEEDED(frame->GetColorContexts(nCtx, raw.data(), &got)) && got == nCtx)
            out->SetColorContexts(got, raw.data());
    }

    if (keepMetadata) {
        ComPtr<IWICMetadataBlockReader> br;
        ComPtr<IWICMetadataBlockWriter> bw;
        if (SUCCEEDED(frame.As(&br)) && SUCCEEDED(out.As(&bw)) && SUCCEEDED(bw->InitializeFromBlockReader(br.Get()))) {
            ComPtr<IWICMetadataQueryWriter> qw;
            if (SUCCEEDED(out->GetMetadataQueryWriter(&qw))) {
                PROPVARIANT ori;
                PropVariantInit(&ori);
                ori.vt = VT_UI2;
                ori.uiVal = 1;  // Pixel sind bereits aufrecht
                qw->SetMetadataByName(L"/app1/ifd/{ushort=274}", &ori);
            }
        }
    }

    HRESULT hr = out->WriteSource(cur.Get(), nullptr);
    if (SUCCEEDED(hr)) hr = out->Commit();
    if (SUCCEEDED(hr)) hr = enc->Commit();
    out.Reset();
    props.Reset();
    enc.Reset();
    stream.Reset();
    dec.Reset();
    frame.Reset();
    cur.Reset();
    if (FAILED(hr)) {
        DeleteFileW(tmp.c_str());
        SetErr(err, L"Schreiben fehlgeschlagen");
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        SetErr(err, L"Zieldatei kann nicht ersetzt werden");
        return false;
    }
    return true;
}

}  // namespace

// ---- oeffentliche API -------------------------------------------------------

bool IsSupportedImage(const std::wstring& path) {
    static const wchar_t* exts[] = {L"jpg", L"jpeg", L"jpe", L"png", L"bmp", L"gif", L"tif", L"tiff",
                                    L"webp", L"heic", L"heif", L"jxr", L"wdp"};
    std::wstring e = LowerExt(path);
    for (auto x : exts)
        if (e == x) return true;
    return false;
}

bool ParseExifDate(const std::wstring& s, ULONGLONG& utc) {
    int y, mo, d, h, mi, se;
    if (s.size() < 19 || swscanf_s(s.c_str(), L"%d:%d:%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) return false;
    if (y < 1900 || y > 2999 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 ||
        se < 0 || se > 60)
        return false;
    SYSTEMTIME local{}, st{};
    local.wYear = (WORD)y; local.wMonth = (WORD)mo; local.wDay = (WORD)d;
    local.wHour = (WORD)h; local.wMinute = (WORD)mi; local.wSecond = (WORD)std::min(se, 59);
    FILETIME ft;
    if (!TzSpecificLocalTimeToSystemTime(nullptr, &local, &st) || !SystemTimeToFileTime(&st, &ft)) return false;
    utc = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return true;
}

bool ReadQuickMeta(const std::wstring& path, QuickMeta& out) {
    out = QuickMeta{};
    auto f = MakeFactory();
    if (!f) return false;
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)))
        return false;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICMetadataQueryReader> q;
    if (FAILED(dec->GetFrame(0, &frame)) || FAILED(frame->GetMetadataQueryReader(&q))) return true;
    PROPVARIANT pv;
    std::wstring s;
    for (const wchar_t* tag : {L"/ifd/exif/{ushort=36867}", L"/ifd/exif/{ushort=36868}", L"/ifd/{ushort=306}"}) {
        if (Query(q.Get(), tag, &pv)) {
            const bool ok = AsString(pv, s) && ParseExifDate(s, out.taken);
            PropVariantClear(&pv);
            if (ok) break;
        }
    }
    double lat, lon;
    if (ReadGpsCoord(q.Get(), 2, 1, lat) && ReadGpsCoord(q.Get(), 4, 3, lon)) {
        out.hasGps = true;
    } else if (Query(q.Get(), L"/ifd/gps/{ushort=1}", &pv)) {
        out.hasGps = true;
        PropVariantClear(&pv);
    }
    return true;
}

bool HashFile(const std::wstring& path, uint64_t& h1, uint64_t& h2) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    h1 = 1469598103934665603ull;  // FNV-1a
    h2 = 0x9E3779B97F4A7C15ull;   // zweiter Lauf mit anderem Startwert und Multiplikator
    std::vector<BYTE> buf(1 << 20);
    bool ok = true;
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(h, buf.data(), (DWORD)buf.size(), &got, nullptr)) { ok = false; break; }
        if (got == 0) break;
        for (DWORD i = 0; i < got; ++i) {
            h1 = (h1 ^ buf[i]) * 1099511628211ull;
            h2 = (h2 ^ buf[i]) * 0x2545F4914F6CDD1Dull + i;
        }
    }
    CloseHandle(h);
    return ok;
}

bool ComputeDHash(const std::wstring& path, uint64_t& hash) {
    auto f = MakeFactory();
    if (!f) return false;
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)))
        return false;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> conv;
    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(dec->GetFrame(0, &frame)) || FAILED(f->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat8bppGray, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom)) ||
        FAILED(f->CreateBitmapScaler(&scaler)) ||
        FAILED(scaler->Initialize(conv.Get(), 9, 8, WICBitmapInterpolationModeFant)))
        return false;
    BYTE px[9 * 8];
    if (FAILED(scaler->CopyPixels(nullptr, 9, sizeof px, px))) return false;
    hash = 0;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) hash = (hash << 1) | (px[y * 9 + x] < px[y * 9 + x + 1] ? 1u : 0u);
    return true;
}

int HammingDistance(uint64_t a, uint64_t b) {
    uint64_t v = a ^ b;
    int n = 0;
    for (; v; v &= v - 1) ++n;
    return n;
}

WICBitmapTransformOptions OrientationToTransform(int o) {
    switch (o) {
        case 2: return WICBitmapTransformFlipHorizontal;
        case 3: return WICBitmapTransformRotate180;
        case 4: return WICBitmapTransformFlipVertical;
        case 5: return (WICBitmapTransformOptions)(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
        case 6: return WICBitmapTransformRotate90;
        case 7: return (WICBitmapTransformOptions)(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
        case 8: return WICBitmapTransformRotate270;
        default: return WICBitmapTransformRotate0;
    }
}

int ReadOrientation(IWICBitmapFrameDecode* frame) {
    ComPtr<IWICMetadataQueryReader> q;
    if (!frame || FAILED(frame->GetMetadataQueryReader(&q))) return 1;
    PROPVARIANT pv;
    int o = 1;
    if (Query(q.Get(), L"/ifd/{ushort=274}", &pv)) {
        unsigned long long v;
        if (AsUInt(pv, v) && v >= 1 && v <= 8) o = (int)v;
        PropVariantClear(&pv);
    }
    return o;
}

bool ReadImageInfo(const std::wstring& path, ImageInfo& info) {
    info = ImageInfo{};
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) {
        info.fileSize = ((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
        info.modified = fa.ftLastWriteTime;
    }
    auto f = MakeFactory();
    if (!f) return false;
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnDemand, &dec)))
        return false;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) return false;
    frame->GetSize(&info.width, &info.height);

    ComPtr<IWICMetadataQueryReader> q;
    if (FAILED(frame->GetMetadataQueryReader(&q))) return true;
    PROPVARIANT pv;
    std::wstring s, make, model;

    if (Query(q.Get(), L"/ifd/{ushort=271}", &pv)) { if (AsString(pv, s)) make = s; PropVariantClear(&pv); }
    if (Query(q.Get(), L"/ifd/{ushort=272}", &pv)) { if (AsString(pv, s)) model = s; PropVariantClear(&pv); }
    if (!make.empty() || !model.empty()) {
        // "Canon Canon EOS R5" vermeiden
        std::wstring cam = (!model.empty() && model.find(make) == 0) ? model : (make + L" " + model);
        info.rows.push_back({L"Kamera", cam, false});
    }
    if (Query(q.Get(), L"/ifd/exif/{ushort=42036}", &pv)) {  // LensModel
        if (AsString(pv, s)) info.rows.push_back({L"Objektiv", s, false});
        PropVariantClear(&pv);
    }
    if (Query(q.Get(), L"/ifd/exif/{ushort=36867}", &pv)) {  // DateTimeOriginal "YYYY:MM:DD HH:MM:SS"
        if (AsString(pv, s)) {
            if (s.size() >= 10) { s[4] = L'-'; s[7] = L'-'; }
            info.rows.push_back({L"Aufnahme", s, false});
        }
        PropVariantClear(&pv);
    }
    double d;
    std::wstring exposure;
    if (Query(q.Get(), L"/ifd/exif/{ushort=33437}", &pv)) {  // FNumber
        if (AsRational(pv, d)) exposure += Fmt(L"f/%.1f  ", d);
        PropVariantClear(&pv);
    }
    if (Query(q.Get(), L"/ifd/exif/{ushort=33434}", &pv)) {  // ExposureTime
        if (AsRational(pv, d) && d > 0) {
            wchar_t b[32];
            if (d < 1) swprintf_s(b, L"1/%d s  ", (int)std::lround(1.0 / d));
            else swprintf_s(b, L"%.1f s  ", d);
            exposure += b;
        }
        PropVariantClear(&pv);
    }
    unsigned long long u;
    if (Query(q.Get(), L"/ifd/exif/{ushort=34855}", &pv)) {  // ISO
        if (AsUInt(pv, u)) exposure += L"ISO " + std::to_wstring(u);
        PropVariantClear(&pv);
    }
    if (!exposure.empty()) info.rows.push_back({L"Belichtung", exposure, false});
    if (Query(q.Get(), L"/ifd/exif/{ushort=37386}", &pv)) {  // FocalLength
        if (AsRational(pv, d)) info.rows.push_back({L"Brennweite", Fmt(L"%.0f mm", d), false});
        PropVariantClear(&pv);
    }
    info.orientation = ReadOrientation(frame.Get());

    double lat, lon;
    if (ReadGpsCoord(q.Get(), 2, 1, lat) && ReadGpsCoord(q.Get(), 4, 3, lon)) {
        info.hasGps = true;
        wchar_t b[64];
        swprintf_s(b, L"%.5f, %.5f", lat, lon);
        info.rows.push_back({L"GPS-Standort", b, true});
    } else if (Query(q.Get(), L"/ifd/gps/{ushort=1}", &pv)) {  // GPS-Block vorhanden, aber nicht lesbar
        info.hasGps = true;
        info.rows.push_back({L"GPS-Standort", L"vorhanden", true});
        PropVariantClear(&pv);
    }
    info.hasExif = !info.rows.empty();
    return true;
}

bool StripMetadata(const std::wstring& src, const std::wstring& dst, std::wstring* err) {
    std::wstring ext = LowerExt(src);
    if (IsJpegExt(ext)) {
        // Rotations-Tag? Dann muss die Drehung in die Pixel, sonst wirkt das Bild nach dem Strippen gekippt.
        int orientation = 1;
        {
            auto f = MakeFactory();
            ComPtr<IWICBitmapDecoder> dec;
            ComPtr<IWICBitmapFrameDecode> fr;
            if (f && SUCCEEDED(f->CreateDecoderFromFilename(src.c_str(), nullptr, GENERIC_READ,
                                                            WICDecodeMetadataCacheOnDemand, &dec)) &&
                SUCCEEDED(dec->GetFrame(0, &fr)))
                orientation = ReadOrientation(fr.Get());
        }
        if (orientation <= 1) {
            std::vector<BYTE> in, out;
            if (!ReadAll(src, in) || !bytes::StripJpeg(in, out)) {
                SetErr(err, L"JPEG konnte nicht verarbeitet werden");
                return false;
            }
            std::wstring tmp = dst + L".pctmp";
            if (!WriteAll(tmp, out) || !MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                DeleteFileW(tmp.c_str());
                SetErr(err, L"Schreiben fehlgeschlagen");
                return false;
            }
            return true;
        }
        return Reencode(src, dst, EncodeOpts{}, err);
    }
    if (ext == L"png" || ext == L"tif" || ext == L"tiff" || ext == L"jxr" || ext == L"wdp")
        return Reencode(src, dst, EncodeOpts{}, err);
    if (ext == L"webp") {
        std::vector<BYTE> in, out;
        if (!ReadAll(src, in) || !bytes::StripWebp(in, out)) {
            SetErr(err, L"WebP konnte nicht verarbeitet werden");
            return false;
        }
        std::wstring tmp = dst + L".pctmp";
        if (!WriteAll(tmp, out) || !MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(tmp.c_str());
            SetErr(err, L"Schreiben fehlgeschlagen");
            return false;
        }
        return true;
    }
    if (ext == L"bmp") {  // BMP kennt keine EXIF-/GPS-Daten: nichts zu entfernen, Kopie genuegt
        if (_wcsicmp(src.c_str(), dst.c_str()) == 0) return true;
        if (!CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
            SetErr(err, L"Kopieren fehlgeschlagen");
            return false;
        }
        return true;
    }
    SetErr(err, L"Format wird nicht unterstuetzt (GIF, HEIC)");
    return false;
}

bool CanStripMetadata(const std::wstring& path) {
    const std::wstring e = LowerExt(path);
    return IsJpegExt(e) || e == L"png" || e == L"tif" || e == L"tiff" || e == L"jxr" || e == L"wdp" || e == L"webp" ||
           e == L"bmp";
}

bool CanReencode(const std::wstring& path) {
    const std::wstring e = LowerExt(path);
    return IsJpegExt(e) || e == L"png" || e == L"tif" || e == L"tiff" || e == L"jxr" || e == L"wdp" || e == L"bmp";
}

bool RotateImage(const std::wstring& path, bool clockwise, std::wstring* err) {
    if (!CanReencode(path)) {
        SetErr(err, L"Drehen wird fuer dieses Format nicht unterstuetzt");
        return false;
    }
    EncodeOpts o;
    o.rotateCw = clockwise ? 90 : 270;
    o.keepMetadata = true;
    return Reencode(path, path, o, err);
}

bool CropImage(const std::wstring& src, const std::wstring& dst, double x0, double y0, double x1, double y1,
               std::wstring* err) {
    if (!CanReencode(src)) {
        SetErr(err, L"Zuschneiden wird fuer dieses Format nicht unterstuetzt");
        return false;
    }
    EncodeOpts o;
    o.keepMetadata = true;
    o.crop = true;
    o.cx0 = x0; o.cy0 = y0; o.cx1 = x1; o.cy1 = y1;
    return Reencode(src, dst, o, err);
}

bool ResizeImage(const std::wstring& src, const std::wstring& dst, UINT maxEdge, std::wstring* err) {
    if (!CanReencode(src)) {
        SetErr(err, L"Verkleinern wird fuer dieses Format nicht unterstuetzt");
        return false;
    }
    EncodeOpts o;   // ohne Metadaten: gedacht zum Teilen
    o.maxEdge = maxEdge;
    o.quality = 0.85f;
    return Reencode(src, dst, o, err);
}

}  // namespace imgops
