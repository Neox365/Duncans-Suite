#include "ImageOps.h"

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

bool IsIccSegment(const std::vector<BYTE>& d, size_t payload, size_t len) {
    static const char tag[] = "ICC_PROFILE";
    return len >= sizeof(tag) && memcmp(&d[payload], tag, sizeof(tag) - 1) == 0;
}

// Behaelt: JFIF (APP0), ICC-Profil (APP2), Adobe (APP14) und alle Bildsegmente.
// Entfernt: Exif/XMP (APP1), MPF & Co (APP2 ohne ICC), APP3..APP13, APP15, Kommentare.
bool StripJpegBytes(const std::vector<BYTE>& in, std::vector<BYTE>& out) {
    if (in.size() < 4 || in[0] != 0xFF || in[1] != 0xD8) return false;
    out.clear();
    out.reserve(in.size());
    out.push_back(0xFF);
    out.push_back(0xD8);
    size_t i = 2;
    while (i + 2 <= in.size()) {
        if (in[i] != 0xFF) return false;
        BYTE m = in[i + 1];
        if (m == 0xFF) { ++i; continue; }  // Fuell-Bytes
        if (m == 0xDA || m == 0xD9) {      // SOS / EOI: Rest unveraendert uebernehmen
            out.insert(out.end(), in.begin() + i, in.end());
            return true;
        }
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD8)) {  // Marker ohne Laenge
            out.push_back(0xFF);
            out.push_back(m);
            i += 2;
            continue;
        }
        if (i + 4 > in.size()) return false;
        size_t len = ((size_t)in[i + 2] << 8) | in[i + 3];
        if (len < 2 || i + 2 + len > in.size()) return false;
        bool drop = m == 0xE1 || (m >= 0xE3 && m <= 0xED) || m == 0xEF || m == 0xFE ||
                    (m == 0xE2 && !IsIccSegment(in, i + 4, len - 2));
        if (!drop) out.insert(out.end(), in.begin() + i, in.begin() + i + 2 + len);
        i += 2 + len;
    }
    return false;
}

// ---- WIC Re-Encode ----------------------------------------------------------

// Drehung der Pixel, dann Encode in dasselbe Containerformat ohne Metadaten
// (keepMetadata=false) bzw. mit kopierten Metadaten und Orientation=1.
bool Reencode(const std::wstring& src, const std::wstring& dst, int extraRotateCw, bool keepMetadata,
              std::wstring* err) {
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
        v.fltVal = 0.95f;
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
                PROPVARIANT o;
                PropVariantInit(&o);
                o.vt = VT_UI2;
                o.uiVal = 1;  // Pixel sind bereits aufrecht
                qw->SetMetadataByName(L"/app1/ifd/{ushort=274}", &o);
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
            if (!ReadAll(src, in) || !StripJpegBytes(in, out)) {
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
        return Reencode(src, dst, 0, false, err);
    }
    if (ext == L"png" || ext == L"tif" || ext == L"tiff" || ext == L"jxr" || ext == L"wdp")
        return Reencode(src, dst, 0, false, err);
    SetErr(err, L"Format wird (noch) nicht unterstuetzt");
    return false;
}

bool RotateImage(const std::wstring& path, bool clockwise, std::wstring* err) {
    return Reencode(path, path, clockwise ? 90 : 270, true, err);
}

}  // namespace imgops
