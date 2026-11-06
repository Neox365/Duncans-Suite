#include "Convert.h"

#include <windows.h>
#include <wincodec.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <codecapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <vector>

// Aeltere MinGW-Header (nur fuer die Logik-Tests) kennen diese Werte nicht; MSVC/Windows-SDK schon.
#ifndef __MSVC_VER_DEFINED_FALLBACKS
#if !defined(_MSC_VER)
#define WICBitmapInterpolationModeHighQualityCubic ((WICBitmapInterpolationMode)4)
static const GUID MFAudioFormat_FLAC = { 0x0000F1AC, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
#endif
#endif

namespace conv {
namespace {

// Minimaler COM-Zeiger (MinGW liefert kein WRL)
template <class T>
class Com {
public:
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { Reset(); }
    T** operator&() { Reset(); return &p_; }
    T* operator->() const { return p_; }
    T* Get() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    void Reset() { if (p_) { p_->Release(); p_ = nullptr; } }
private:
    T* p_ = nullptr;
};

void SetErr(std::wstring* err, const std::wstring& text) { if (err) *err = text; }

std::wstring Hex(HRESULT hr) {
    wchar_t buf[24];
    swprintf(buf, 24, L"0x%08X", static_cast<unsigned>(hr));
    return buf;
}

std::wstring LowerExt(const std::wstring& path) {
    const size_t dot = path.find_last_of(L'.');
    const size_t sep = path.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (sep != std::wstring::npos && dot < sep)) return L"";
    std::wstring e = path.substr(dot + 1);
    for (auto& c : e) c = static_cast<wchar_t>(std::towlower(c));
    return e;
}

bool InList(const std::wstring& e, std::initializer_list<const wchar_t*> list) {
    for (const wchar_t* s : list) if (e == s) return true;
    return false;
}

// Container, die wincodec.h je nach Toolchain nicht mitbringt
const GUID kContainerWebp = {0xe094b0e2, 0x67f2, 0x45b3, {0xb0, 0xea, 0x11, 0x53, 0x37, 0xca, 0x7c, 0xf3}};
const GUID kContainerHeif = {0xe1e62521, 0x6787, 0x405b, {0xa3, 0x39, 0x50, 0x07, 0x15, 0xb5, 0x76, 0x3f}};

const GUID& ContainerOf(ImageFmt f) {
    switch (f) {
        case ImageFmt::Png:  return GUID_ContainerFormatPng;
        case ImageFmt::Jpeg: return GUID_ContainerFormatJpeg;
        case ImageFmt::Webp: return kContainerWebp;
        case ImageFmt::Bmp:  return GUID_ContainerFormatBmp;
        case ImageFmt::Heic: return kContainerHeif;
        case ImageFmt::Gif:  return GUID_ContainerFormatGif;
        case ImageFmt::Pdf:  break;   // kein WIC-Encoder
    }
    return GUID_ContainerFormatPng;
}

bool HasAlpha(ImageFmt f) { return f == ImageFmt::Png || f == ImageFmt::Webp || f == ImageFmt::Heic || f == ImageFmt::Gif; }
bool HasQuality(ImageFmt f) { return f == ImageFmt::Jpeg || f == ImageFmt::Webp || f == ImageFmt::Heic; }

// EXIF-Ausrichtung (1..8) eines JPEG-Frames, sonst 1
int ExifOrientation(IWICBitmapFrameDecode* frame) {
    Com<IWICMetadataQueryReader> q;
    if (FAILED(frame->GetMetadataQueryReader(&q))) return 1;
    PROPVARIANT v;
    PropVariantInit(&v);
    int o = 1;
    if (SUCCEEDED(q->GetMetadataByName(L"/app1/ifd/{ushort=274}", &v)) && v.vt == VT_UI2 && v.uiVal >= 1 && v.uiVal <= 8)
        o = v.uiVal;
    PropVariantClear(&v);
    return o;
}

WICBitmapTransformOptions OrientationToTransform(int o) {
    switch (o) {
        case 2: return WICBitmapTransformFlipHorizontal;
        case 3: return WICBitmapTransformRotate180;
        case 4: return WICBitmapTransformFlipVertical;
        case 5: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
        case 6: return WICBitmapTransformRotate90;
        case 7: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
        case 8: return WICBitmapTransformRotate270;
        default: return WICBitmapTransformRotate0;
    }
}

}  // namespace

Kind KindOfFile(const std::wstring& path) {
    const std::wstring e = LowerExt(path);
    if (InList(e, {L"png", L"jpg", L"jpeg", L"jpe", L"jfif", L"bmp", L"dib", L"gif", L"tif", L"tiff", L"webp", L"heic", L"heif",
                   L"jxr", L"wdp", L"ico"}))
        return Kind::Image;
    if (InList(e, {L"mp3", L"wav", L"m4a", L"aac", L"wma", L"flac", L"mp2", L"mp4", L"m4v", L"mov", L"wmv", L"avi", L"mkv",
                   L"webm", L"mpg", L"mpeg", L"3gp", L"asf", L"ts"}))
        return Kind::Media;
    return Kind::Unknown;
}

const wchar_t* ImageExt(ImageFmt f) {
    switch (f) {
        case ImageFmt::Png:  return L"png";
        case ImageFmt::Jpeg: return L"jpg";
        case ImageFmt::Webp: return L"webp";
        case ImageFmt::Bmp:  return L"bmp";
        case ImageFmt::Heic: return L"heic";
        case ImageFmt::Gif:  return L"gif";
        case ImageFmt::Pdf:  return L"pdf";
    }
    return L"png";
}

const wchar_t* AudioExt(AudioFmt f) {
    switch (f) {
        case AudioFmt::Mp3: return L"mp3";
        case AudioFmt::M4a: return L"m4a";
        case AudioFmt::Wav: return L"wav";
        case AudioFmt::Mp4: return L"mp4";
        case AudioFmt::Mkv: return L"mkv";
        case AudioFmt::Flac: return L"flac";
    }
    return L"mp3";
}

int BitrateKbps(AudioFmt f, int quality) {
    quality = std::clamp(quality, 0, 100);
    static const int kMp3[] = {128, 160, 192, 224, 256, 320};   // vom Windows-MP3-Encoder unterstuetzte Stufen bei 44,1 kHz Stereo
    static const int kAac[] = {96, 128, 160, 192};              // vom Windows-AAC-Encoder unterstuetzt
    switch (f) {
        case AudioFmt::Mp3: return kMp3[(quality * 5 + 50) / 100];
        case AudioFmt::M4a:
        case AudioFmt::Mkv:
        case AudioFmt::Mp4: return kAac[(quality * 3 + 50) / 100];
        case AudioFmt::Wav:
        case AudioFmt::Flac: return 0;
    }
    return 0;
}

int VideoBitrateKbps(int width, int height, double fps, int quality) {
    const double bpp = 0.04 + 0.16 * std::clamp(quality, 0, 100) / 100.0;
    const double kbps = static_cast<double>(width) * height * std::clamp(fps, 1.0, 120.0) * bpp / 1000.0;
    return static_cast<int>(std::clamp(kbps, 300.0, 40000.0));
}

int EncoderKbpsForSize(AudioFmt f, long long maxBytes, double seconds) {
    if (f == AudioFmt::Wav || f == AudioFmt::Flac) return -1;
    if (seconds <= 0 || maxBytes <= 0) return 0;
    static const int kMp3[] = {128, 160, 192, 224, 256, 320};
    static const int kAac[] = {96, 128, 160, 192};
    const double allowed = static_cast<double>(maxBytes) * 8 * 0.97 / seconds / 1000.0;     // 3 % Reserve fuer Container und Tags
    int best = 0;
    if (f == AudioFmt::Mp3) { for (int k : kMp3) if (k <= allowed) best = k; }
    else { for (int k : kAac) if (k <= allowed) best = k; }
    return best;
}

VideoPlan PlanVideoForSize(int width, int height, double fps, double seconds, long long maxBytes, int qualityKbps, int qualityAudioKbps, bool hasAudio) {
    VideoPlan p;
    p.width = width & ~1;
    p.height = height & ~1;
    if (seconds <= 0 || maxBytes <= 0 || width < 16 || height < 16) return p;
    fps = std::clamp(fps, 1.0, 120.0);
    const double totalKbps = static_cast<double>(maxBytes) * 8 * 0.93 / seconds / 1000.0;   // 7 % Reserve fuer Container und Encoder-Schwankungen
    p.audioKbps = hasAudio ? (totalKbps >= 400 ? std::max(96, qualityAudioKbps) : 96) : 0;
    double video = totalKbps - p.audioKbps;
    if (video < 100) return p;                                                                // nicht machbar
    video = std::min(video, static_cast<double>(std::max(qualityKbps, 100)));                 // die Grenze macht nie groesser als der Regler
    p.videoKbps = static_cast<int>(video);
    const double bpp = video * 1000.0 / (static_cast<double>(width) * height * fps);
    if (bpp < 0.04) {                                                                         // zu wenig Bits je Bildpunkt: Auflösung senken
        const double pixels = video * 1000.0 / (fps * 0.05);
        const double s = std::sqrt(pixels / (static_cast<double>(width) * height));
        p.width = std::max(160, static_cast<int>(width * s)) & ~1;
        p.height = std::max(90, static_cast<int>(height * s)) & ~1;
        p.width = std::min(p.width, width & ~1);
        p.height = std::min(p.height, height & ~1);
    }
    p.feasible = true;
    return p;
}

std::vector<int> ImageEdgeSteps(int maxEdge) {
    std::vector<int> out = {maxEdge};
    for (int e : {2560, 1920, 1600, 1280, 1024, 800, 640, 480, 320, 240})
        if (maxEdge == 0 || e < maxEdge) out.push_back(e);
    return out;
}

bool IsAudioOnlyFile(const std::wstring& path) {
    return InList(LowerExt(path), {L"mp3", L"wav", L"m4a", L"aac", L"wma", L"flac", L"mp2"});
}

AudioFmt EffectiveFmt(const std::wstring& src, AudioFmt requested) {
    return (requested == AudioFmt::Mp4 || requested == AudioFmt::Mkv) && IsAudioOnlyFile(src) ? AudioFmt::M4a : requested;
}

bool MediaStartup() { return SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE)); }
void MediaShutdown() { MFShutdown(); }

// ---------------------------------------------------------------------------------------------------------------------
// Bilder
// ---------------------------------------------------------------------------------------------------------------------

bool ConvertImageCore(const std::wstring& src, const std::wstring& dst, ImageFmt fmt, int quality, std::wstring* err, int maxEdge) {
    if (fmt == ImageFmt::Pdf) { SetErr(err, L"PDF entsteht nur aus mehreren Bildern zusammen"); return false; }
    Com<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { SetErr(err, L"WIC nicht verfügbar (" + Hex(hr) + L")"); return false; }

    Com<IWICBitmapDecoder> dec;
    hr = factory->CreateDecoderFromFilename(src.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec);
    if (FAILED(hr)) {
        const std::wstring e = LowerExt(src);
        SetErr(err, (e == L"heic" || e == L"heif") ? L"HEIC lässt sich nur mit der Windows-Erweiterung „HEIF-Bilderweiterungen“ lesen"
                   : e == L"webp"                  ? L"WebP lässt sich nur mit der Windows-Erweiterung „WebP-Bilderweiterungen“ lesen"
                                                   : L"Bild kann nicht gelesen werden (" + Hex(hr) + L")");
        return false;
    }
    Com<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) { SetErr(err, L"Bild enthält keine Daten"); return false; }

    // Quelle: Ausrichtung anwenden und als 32-Bit-BGRA lesen
    Com<IWICBitmapSource> cur;
    cur.Reset();
    {
        IWICBitmapSource* s = frame.Get();
        s->AddRef();
        const int orient = LowerExt(src) == L"jpg" || LowerExt(src) == L"jpeg" || LowerExt(src) == L"jpe" || LowerExt(src) == L"jfif"
                               ? ExifOrientation(frame.Get()) : 1;
        if (orient != 1) {
            Com<IWICBitmapFlipRotator> rot;
            if (SUCCEEDED(factory->CreateBitmapFlipRotator(&rot)) && SUCCEEDED(rot->Initialize(s, OrientationToTransform(orient)))) {
                s->Release();
                s = rot.Get();
                s->AddRef();
            }
        }
        Com<IWICFormatConverter> bgra;
        if (FAILED(factory->CreateFormatConverter(&bgra)) ||
            FAILED(bgra->Initialize(s, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
            s->Release();
            SetErr(err, L"Pixelformat wird nicht unterstützt");
            return false;
        }
        s->Release();
        bgra->AddRef();
        *(&cur) = bgra.Get();
    }

    UINT w = 0, h = 0;
    cur->GetSize(&w, &h);
    if (w == 0 || h == 0) { SetErr(err, L"Bild ist leer"); return false; }
    // Verkleinern (hochwertig, nie vergroessern): laengste Kante auf maxEdge
    Com<IWICBitmapScaler> scaler;
    IWICBitmapSource* pixSrc = cur.Get();
    if (maxEdge > 0 && std::max(w, h) > static_cast<UINT>(maxEdge)) {
        const double f = static_cast<double>(maxEdge) / std::max(w, h);
        const UINT nw = std::max<UINT>(1, static_cast<UINT>(w * f + 0.5)), nh = std::max<UINT>(1, static_cast<UINT>(h * f + 0.5));
        if (FAILED(factory->CreateBitmapScaler(&scaler)) || FAILED(scaler->Initialize(cur.Get(), nw, nh, WICBitmapInterpolationModeHighQualityCubic))) {
            SetErr(err, L"Bild lässt sich nicht verkleinern");
            return false;
        }
        pixSrc = scaler.Get();
        w = nw;
        h = nh;
    }
    const UINT stride = w * 4;
    std::vector<BYTE> px(static_cast<size_t>(stride) * h);
    if (FAILED(pixSrc->CopyPixels(nullptr, stride, static_cast<UINT>(px.size()), px.data()))) { SetErr(err, L"Bild kann nicht gelesen werden"); return false; }

    // Formate ohne Transparenz: auf Weiss setzen
    if (!HasAlpha(fmt)) {
        for (size_t i = 0; i < px.size(); i += 4) {
            const unsigned a = px[i + 3];
            if (a == 255) continue;
            for (int c = 0; c < 3; ++c) px[i + c] = static_cast<BYTE>((px[i + c] * a + 255 * (255 - a) + 127) / 255);
            px[i + 3] = 255;
        }
    }
    Com<IWICBitmap> bmp;
    if (FAILED(factory->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppBGRA, stride, static_cast<UINT>(px.size()), px.data(), &bmp))) {
        SetErr(err, L"Zu wenig Speicher für das Bild");
        return false;
    }

    // Ziel
    Com<IWICBitmapEncoder> enc;
    hr = factory->CreateEncoder(ContainerOf(fmt), nullptr, &enc);
    if (FAILED(hr)) {
        SetErr(err, fmt == ImageFmt::Webp ? L"WebP kann auf diesem Rechner nicht geschrieben werden (kein WIC-Encoder installiert)"
                  : fmt == ImageFmt::Heic ? L"HEIC kann nur mit den Windows-Erweiterungen „HEIF-Bilderweiterungen“ und „HEVC-Videoerweiterungen“ geschrieben werden"
                                          : L"Encoder nicht verfügbar (" + Hex(hr) + L")");
        return false;
    }
    Com<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(dst.c_str(), GENERIC_WRITE))) {
        SetErr(err, L"Zieldatei kann nicht angelegt werden");
        return false;
    }
    bool ok = false;
    std::wstring why = L"Schreiben fehlgeschlagen";
    do {
        if (FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache))) { why = L"Encoder lässt sich nicht starten (Codec fehlt?)"; break; }
        Com<IWICBitmapFrameEncode> out;
        Com<IPropertyBag2> bag;
        if (FAILED(enc->CreateNewFrame(&out, &bag))) break;
        if (HasQuality(fmt)) {
            PROPBAG2 opt = {};
            wchar_t name[] = L"ImageQuality";
            opt.pstrName = name;
            VARIANT v;
            VariantInit(&v);
            v.vt = VT_R4;
            v.fltVal = std::clamp(quality, 1, 100) / 100.0f;
            bag->Write(1, &opt, &v);   // Fehler ignorieren: dann gilt der Standardwert des Codecs
        }
        if (FAILED(out->Initialize(bag.Get())) || FAILED(out->SetSize(w, h))) break;
        WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
        if (FAILED(out->SetPixelFormat(&pf))) break;
        Com<IWICBitmapSource> source;
        if (pf == GUID_WICPixelFormat32bppBGRA) {
            bmp->AddRef();
            *(&source) = bmp.Get();
        } else if (pf == GUID_WICPixelFormat8bppIndexed) {   // GIF: Farbpalette (256 Farben, eine davon fuer Transparenz) berechnen und mit Fehlerverteilung umsetzen
            Com<IWICPalette> pal;
            Com<IWICFormatConverter> cv;
            if (FAILED(factory->CreatePalette(&pal)) || FAILED(pal->InitializeFromBitmap(bmp.Get(), 256, TRUE)) || FAILED(out->SetPalette(pal.Get())) ||
                FAILED(factory->CreateFormatConverter(&cv)) ||
                FAILED(cv->Initialize(bmp.Get(), pf, WICBitmapDitherTypeErrorDiffusion, pal.Get(), 50.0, WICBitmapPaletteTypeCustom))) {
                why = L"GIF-Farbpalette kann nicht berechnet werden";
                break;
            }
            cv->AddRef();
            *(&source) = cv.Get();
        } else {
            Com<IWICFormatConverter> cv;
            if (FAILED(factory->CreateFormatConverter(&cv)) ||
                FAILED(cv->Initialize(bmp.Get(), pf, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                why = L"Zielformat unterstützt dieses Pixelformat nicht";
                break;
            }
            cv->AddRef();
            *(&source) = cv.Get();
        }
        hr = out->WriteSource(source.Get(), nullptr);
        if (FAILED(hr)) { why = L"Codec meldet einen Fehler (" + Hex(hr) + L")"; break; }
        if (FAILED(out->Commit()) || FAILED(enc->Commit())) break;
        ok = true;
    } while (false);

    stream.Reset();
    enc.Reset();
    if (!ok) {
        DeleteFileW(dst.c_str());
        SetErr(err, why);
    }
    return ok;
}

namespace {
long long FileBytes(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) return -1;
    return (static_cast<long long>(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
}
std::wstring MbText(long long bytes) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%.1f", bytes / (1024.0 * 1024.0));
    for (wchar_t* c = buf; *c; ++c) if (*c == L'.') *c = L',';
    return buf;
}
}  // namespace

bool ConvertImage(const std::wstring& src, const std::wstring& dst, ImageFmt fmt, int quality, std::wstring* err, int maxEdge, long long maxBytes) {
    if (maxBytes <= 0 || fmt == ImageFmt::Pdf) return ConvertImageCore(src, dst, fmt, quality, err, maxEdge);
    // Zieldateigroesse: je Bildkante die hoechste Qualitaet suchen, die passt (Halbierungssuche), sonst naechste kleinere Kante
    const bool hasQ = HasQuality(fmt);
    const int top = std::clamp(quality, 1, 100), lowest = 25;
    std::wstring lastErr;
    for (int edge : ImageEdgeSteps(maxEdge)) {
        auto attempt = [&](int q, long long& size) {
            DeleteFileW(dst.c_str());
            if (!ConvertImageCore(src, dst, fmt, q, &lastErr, edge)) return false;
            size = FileBytes(dst);
            return true;
        };
        long long size = 0;
        if (!attempt(hasQ ? top : 100, size)) { SetErr(err, lastErr); return false; }           // echter Fehler (Codec fehlt o. ae.): nicht weitersuchen
        if (size <= maxBytes) return true;
        if (!hasQ) continue;                                                                    // kein Qualitaetsregler: nur kleinere Kante hilft
        if (!attempt(lowest, size)) { SetErr(err, lastErr); return false; }
        if (size > maxBytes) continue;                                                          // selbst mit niedrigster Qualitaet zu gross: kleinere Kante
        int lo = lowest, hi = top;                                                              // lo passt, hi passt nicht
        for (int i = 0; i < 6 && hi - lo > 1; ++i) {
            const int mid = (lo + hi) / 2;
            long long ms = 0;
            if (!attempt(mid, ms)) { SetErr(err, lastErr); return false; }
            if (ms <= maxBytes) lo = mid; else hi = mid;
        }
        if (!attempt(lo, size)) { SetErr(err, lastErr); return false; }                         // die beste passende Stufe endgueltig schreiben
        if (size <= maxBytes) return true;
    }
    DeleteFileW(dst.c_str());
    SetErr(err, L"Die Zielgröße von " + MbText(maxBytes) + L" MB wird nicht erreicht" + (hasQ ? L" (auch nicht mit kleinerem Bild)" : L" – JPG oder WEBP lassen sich stärker verkleinern"));
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// Bilder -> PDF
// ---------------------------------------------------------------------------------------------------------------------

namespace {

bool ReadFileBytes(const std::wstring& path, std::string& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    out.clear();
    char buf[65536];
    DWORD n = 0;
    while (ReadFile(f, buf, sizeof(buf), &n, nullptr) && n > 0) out.append(buf, n);
    CloseHandle(f);
    return !out.empty();
}

// Breite und Hoehe aus dem JPEG-Kopf (SOF-Marker)
bool JpegSize(const std::string& d, int& w, int& h) {
    size_t i = 2;
    while (i + 9 < d.size()) {
        if (static_cast<unsigned char>(d[i]) != 0xFF) { ++i; continue; }
        const unsigned char m = static_cast<unsigned char>(d[i + 1]);
        if (m == 0xFF) { ++i; continue; }
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            h = (static_cast<unsigned char>(d[i + 5]) << 8) | static_cast<unsigned char>(d[i + 6]);
            w = (static_cast<unsigned char>(d[i + 7]) << 8) | static_cast<unsigned char>(d[i + 8]);
            return w > 0 && h > 0;
        }
        i += 2 + ((static_cast<unsigned char>(d[i + 2]) << 8) | static_cast<unsigned char>(d[i + 3]));
    }
    return false;
}

}  // namespace

bool ConvertImagesToPdf(const std::vector<std::wstring>& srcs, const std::wstring& dst, int quality, int maxEdge, const std::atomic<bool>& cancel,
                        const std::function<void(int)>& progress, std::wstring* err) {
    struct Page { std::string jpg; int w = 0, h = 0; };
    std::vector<Page> pages;
    for (size_t i = 0; i < srcs.size(); ++i) {
        if (cancel.load()) { SetErr(err, L"Abgebrochen"); return false; }
        const std::wstring tmp = dst + L".tmp" + std::to_wstring(i) + L".jpg";
        std::wstring e;
        Page pg;
        const bool ok = ConvertImage(srcs[i], tmp, ImageFmt::Jpeg, quality, &e, maxEdge) && ReadFileBytes(tmp, pg.jpg) && JpegSize(pg.jpg, pg.w, pg.h);
        DeleteFileW(tmp.c_str());
        if (!ok) {
            const size_t cut = srcs[i].find_last_of(L"\\/");
            SetErr(err, (cut == std::wstring::npos ? srcs[i] : srcs[i].substr(cut + 1)) + L": " + (e.empty() ? L"Bild kann nicht gelesen werden" : e));
            return false;
        }
        pages.push_back(std::move(pg));
        if (progress) progress(static_cast<int>((i + 1) * 90 / srcs.size()));
    }
    if (pages.empty()) { SetErr(err, L"Keine Bilder"); return false; }

    // Objekte: 1 Katalog, 2 Seitenbaum, je Seite 3 Objekte (Seite, Inhalt, Bild)
    std::string pdf = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<size_t> off(3 + pages.size() * 3, 0);
    auto beginObj = [&](size_t n) { off[n] = pdf.size(); pdf += std::to_string(n) + " 0 obj\n"; };
    auto num = [](double v) { char b[32]; snprintf(b, sizeof b, "%.2f", v); return std::string(b); };
    beginObj(1);
    pdf += "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
    beginObj(2);
    pdf += "<< /Type /Pages /Count " + std::to_string(pages.size()) + " /Kids [";
    for (size_t i = 0; i < pages.size(); ++i) pdf += std::to_string(3 + i * 3) + " 0 R ";
    pdf += "] >>\nendobj\n";
    constexpr double kA4W = 595.28, kA4H = 841.89, kMargin = 28.0;
    for (size_t i = 0; i < pages.size(); ++i) {
        const Page& pg = pages[i];
        const bool landscape = pg.w > pg.h;
        const double pw = landscape ? kA4H : kA4W, ph = landscape ? kA4W : kA4H;
        const double scale = std::min({(pw - 2 * kMargin) / pg.w, (ph - 2 * kMargin) / pg.h, 0.75});   // 0,75 = 96 dpi in Punkten: kleine Bilder bleiben klein
        const double iw = pg.w * scale, ih = pg.h * scale, ix = (pw - iw) / 2, iy = (ph - ih) / 2;
        const size_t pageObj = 3 + i * 3, contObj = pageObj + 1, imgObj = pageObj + 2;
        beginObj(pageObj);
        pdf += "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + num(pw) + " " + num(ph) + "] /Resources << /XObject << /Im0 " + std::to_string(imgObj) +
               " 0 R >> >> /Contents " + std::to_string(contObj) + " 0 R >>\nendobj\n";
        const std::string content = "q " + num(iw) + " 0 0 " + num(ih) + " " + num(ix) + " " + num(iy) + " cm /Im0 Do Q";
        beginObj(contObj);
        pdf += "<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "\nendstream\nendobj\n";
        beginObj(imgObj);
        pdf += "<< /Type /XObject /Subtype /Image /Width " + std::to_string(pg.w) + " /Height " + std::to_string(pg.h) +
               " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length " + std::to_string(pg.jpg.size()) + " >>\nstream\n";
        pdf += pg.jpg;
        pdf += "\nendstream\nendobj\n";
    }
    const size_t xref = pdf.size();
    pdf += "xref\n0 " + std::to_string(off.size()) + "\n0000000000 65535 f \n";
    for (size_t n = 1; n < off.size(); ++n) { char b[32]; snprintf(b, sizeof b, "%010zu 00000 n \n", off[n]); pdf += b; }
    pdf += "trailer\n<< /Size " + std::to_string(off.size()) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";

    HANDLE f = CreateFileW(dst.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0;
    const bool ok = f != INVALID_HANDLE_VALUE && WriteFile(f, pdf.data(), static_cast<DWORD>(pdf.size()), &written, nullptr) && written == pdf.size();
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    if (!ok) { DeleteFileW(dst.c_str()); SetErr(err, L"PDF kann nicht geschrieben werden"); return false; }
    if (progress) progress(100);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Audio / Video
// ---------------------------------------------------------------------------------------------------------------------

namespace {

constexpr UINT32 kRate = 44100, kChannels = 2, kBits = 16;

void Put32(BYTE* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = static_cast<BYTE>(v >> (8 * i)); }

bool WriteWavHeader(HANDLE f, uint32_t dataBytes) {
    BYTE h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, kChannels, 0};
    Put32(h + 4, 36 + dataBytes);
    Put32(h + 24, kRate);
    Put32(h + 28, kRate * kChannels * kBits / 8);
    h[32] = static_cast<BYTE>(kChannels * kBits / 8);
    h[33] = 0;
    h[34] = kBits;
    h[35] = 0;
    memcpy(h + 36, "data", 4);
    Put32(h + 40, dataBytes);
    DWORD wr = 0;
    return WriteFile(f, h, sizeof(h), &wr, nullptr) && wr == sizeof(h);
}


// Video -> MP4 (H.264 + AAC) ueber den Windows-Encoder. Videobild wird auf gerade Seitenlaengen gebracht (H.264 verlangt das).
bool ConvertVideoMp4(const std::wstring& src, const std::wstring& dst, int quality, const std::atomic<bool>& cancel,
                     const std::function<void(int)>& progress, std::wstring* err, long long maxBytes = 0) {
    Com<IMFAttributes> ra;
    MFCreateAttributes(&ra, 1);
    ra->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);   // erlaubt Farbraum-Umwandlung und Skalierung
    Com<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(src.c_str(), ra.Get(), &reader);
    if (FAILED(hr)) { SetErr(err, L"Datei kann nicht geöffnet werden – Format oder Codec wird von Windows nicht unterstützt (" + Hex(hr) + L")"); return false; }

    // Streams der Quelle: erster Video- und erster Audio-Stream
    DWORD vSrc = MAXDWORD, aSrc = MAXDWORD;
    for (DWORD i = 0; i < 32; ++i) {
        Com<IMFMediaType> t;
        if (FAILED(reader->GetNativeMediaType(i, 0, &t))) { if (i > 8) break; continue; }
        GUID major{};
        t->GetGUID(MF_MT_MAJOR_TYPE, &major);
        if (major == MFMediaType_Video && vSrc == MAXDWORD) vSrc = i;
        else if (major == MFMediaType_Audio && aSrc == MAXDWORD) aSrc = i;
    }
    if (vSrc == MAXDWORD) { SetErr(err, L"Die Datei enthält kein Videobild"); return false; }

    Com<IMFMediaType> nv;
    reader->GetNativeMediaType(vSrc, 0, &nv);
    UINT32 w = 0, h = 0, fn = 30, fd = 1;
    MFGetAttributeSize(nv.Get(), MF_MT_FRAME_SIZE, &w, &h);
    MFGetAttributeRatio(nv.Get(), MF_MT_FRAME_RATE, &fn, &fd);
    if (fd == 0 || fn == 0) { fn = 30; fd = 1; }
    w &= ~1u; h &= ~1u;
    if (w < 16 || h < 16) { SetErr(err, L"Das Videobild ist zu klein"); return false; }
    const double fps = static_cast<double>(fn) / fd;

    // Zieldateigroesse: Bitrate und ggf. kleinere Aufloesung planen (Quelle wird vom Source Reader skaliert)
    int plannedVideoKbps = 0, plannedAudioKbps = 0;
    if (maxBytes > 0) {
        PROPVARIANT dv;
        PropVariantInit(&dv);
        LONGLONG dur = 0;
        if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &dv)) && dv.vt == VT_UI8)
            dur = static_cast<LONGLONG>(dv.uhVal.QuadPart);
        PropVariantClear(&dv);
        if (dur <= 0) { SetErr(err, L"Die Länge des Videos ist unbekannt, eine Zielgröße ist nicht möglich"); return false; }
        const VideoPlan plan = PlanVideoForSize(static_cast<int>(w), static_cast<int>(h), fps, static_cast<double>(dur) / 1e7, maxBytes,
                                                VideoBitrateKbps(static_cast<int>(w), static_cast<int>(h), fps, quality), BitrateKbps(AudioFmt::M4a, quality), aSrc != MAXDWORD);
        if (!plan.feasible) { SetErr(err, L"Das Video ist zu lang für diese Zielgröße – auch mit der niedrigsten Qualität wäre die Datei größer"); return false; }
        w = static_cast<UINT32>(plan.width);
        h = static_cast<UINT32>(plan.height);
        plannedVideoKbps = plan.videoKbps;
        plannedAudioKbps = plan.audioKbps;
    }

    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    reader->SetStreamSelection(vSrc, TRUE);
    Com<IMFMediaType> vin;
    MFCreateMediaType(&vin);
    vin->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    vin->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    vin->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(vin.Get(), MF_MT_FRAME_SIZE, w, h);
    MFSetAttributeRatio(vin.Get(), MF_MT_FRAME_RATE, fn, fd);
    MFSetAttributeRatio(vin.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    hr = reader->SetCurrentMediaType(vSrc, nullptr, vin.Get());
    if (FAILED(hr)) { SetErr(err, L"Das Videoformat der Quelle kann nicht gelesen werden (" + Hex(hr) + L")"); return false; }

    Com<IMFMediaType> pcm;
    if (aSrc != MAXDWORD) {
        reader->SetStreamSelection(aSrc, TRUE);
        MFCreateMediaType(&pcm);
        pcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        pcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, kChannels);
        pcm->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate);
        pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, kBits);
        pcm->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, kChannels * kBits / 8);
        pcm->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kRate * kChannels * kBits / 8);
        pcm->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
        if (FAILED(reader->SetCurrentMediaType(aSrc, nullptr, pcm.Get()))) {   // Tonspur nicht lesbar: Video ohne Ton
            reader->SetStreamSelection(aSrc, FALSE);
            pcm.Reset();
            aSrc = MAXDWORD;
        }
    }

    LONGLONG duration = 0;
    {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &v)) && v.vt == VT_UI8)
            duration = static_cast<LONGLONG>(v.uhVal.QuadPart);
        PropVariantClear(&v);
    }

    Com<IMFAttributes> wa;
    MFCreateAttributes(&wa, 1);
    wa->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    Com<IMFSinkWriter> writer;
    bool ok = false;
    std::wstring why = L"Konvertierung fehlgeschlagen";
    do {
        hr = MFCreateSinkWriterFromURL(dst.c_str(), nullptr, wa.Get(), &writer);
        if (FAILED(hr)) { why = L"Zieldatei kann nicht angelegt werden (" + Hex(hr) + L")"; break; }

        Com<IMFMediaType> vout;
        MFCreateMediaType(&vout);
        vout->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        vout->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        vout->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(plannedVideoKbps ? plannedVideoKbps : VideoBitrateKbps(static_cast<int>(w), static_cast<int>(h), fps, quality)) * 1000u);
        vout->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        vout->SetUINT32(MF_MT_MPEG2_PROFILE, 77);   // eAVEncH264VProfile_Main
        MFSetAttributeSize(vout.Get(), MF_MT_FRAME_SIZE, w, h);
        MFSetAttributeRatio(vout.Get(), MF_MT_FRAME_RATE, fn, fd);
        MFSetAttributeRatio(vout.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        DWORD vOut = 0, aOut = 0;
        hr = writer->AddStream(vout.Get(), &vOut);
        if (FAILED(hr)) { why = L"Der H.264-Encoder von Windows ist nicht verfügbar (" + Hex(hr) + L")"; break; }
        Com<IMFAttributes> encCfg;                                           // mit Zielgroesse: konstante Bitrate, damit die Datei nicht ueber die Grenze waechst
        if (maxBytes > 0) {
            MFCreateAttributes(&encCfg, 1);
            encCfg->SetUINT32(CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_CBR);
        }
        hr = writer->SetInputMediaType(vOut, vin.Get(), encCfg.Get());
        if (FAILED(hr)) { why = L"Der Video-Encoder nimmt das Bildformat nicht an (" + Hex(hr) + L")"; break; }

        if (pcm) {
            Com<IMFMediaType> aout;
            MFCreateMediaType(&aout);
            aout->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            aout->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
            aout->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, kChannels);
            aout->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate);
            aout->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, kBits);
            aout->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (plannedAudioKbps ? plannedAudioKbps : BitrateKbps(AudioFmt::M4a, quality)) * 1000 / 8);
            aout->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 1);
            aout->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
            aout->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
            hr = writer->AddStream(aout.Get(), &aOut);
            if (FAILED(hr)) { why = L"Der AAC-Encoder von Windows ist nicht verfügbar (" + Hex(hr) + L")"; break; }
            hr = writer->SetInputMediaType(aOut, pcm.Get(), nullptr);
            if (FAILED(hr)) { why = L"Der Audio-Encoder nimmt das Format nicht an (" + Hex(hr) + L")"; break; }
        }
        hr = writer->BeginWriting();
        if (FAILED(hr)) { why = L"Encoder lässt sich nicht starten (" + Hex(hr) + L")"; break; }

        bool vDone = false, aDone = !pcm, failed = false, aborted = false;
        int lastPct = -1;
        while (!(vDone && aDone)) {
            if (cancel.load()) { aborted = true; break; }
            DWORD si = 0, flags = 0;
            LONGLONG ts = 0;
            Com<IMFSample> sample;
            hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_ANY_STREAM), 0, &si, &flags, &ts, &sample);
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) { failed = true; why = L"Lesefehler in der Quelldatei"; break; }
            const bool isVideo = si == vSrc;
            if (sample) {
                hr = writer->WriteSample(isVideo ? vOut : aOut, sample.Get());
                if (FAILED(hr)) { failed = true; why = L"Encoder meldet einen Fehler (" + Hex(hr) + L")"; break; }
                if (isVideo && progress && duration > 0) {
                    const int pct = static_cast<int>(std::clamp<LONGLONG>(ts * 100 / duration, 0, 99));
                    if (pct != lastPct) { lastPct = pct; progress(pct); }
                }
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) (isVideo ? vDone : aDone) = true;
        }
        if (aborted) { why = L"Abgebrochen"; break; }
        if (failed) break;
        hr = writer->Finalize();
        if (FAILED(hr)) { why = L"Datei konnte nicht abgeschlossen werden (" + Hex(hr) + L")"; break; }
        ok = true;
    } while (false);

    writer.Reset();
    if (ok) {
        if (progress) progress(100);
    } else {
        DeleteFileW(dst.c_str());
        SetErr(err, why);
    }
    return ok;
}

// ---------------------------------------------------------------------------------------------------------------------
// Matroska (MKV): Windows liest MKV, schreibt es aber nicht. Das Video wird deshalb wie bei MP4 kodiert (H.264 + AAC) und danach
// ohne erneute Kodierung in einen Matroska-Container umgepackt (schlanker Muxer: Header, Info, Tracks, Cluster mit SimpleBlocks, Cues).
// ---------------------------------------------------------------------------------------------------------------------

using Bytes = std::vector<BYTE>;

void EId(Bytes& o, uint32_t id) {
    const int n = id > 0xFFFFFF ? 4 : id > 0xFFFF ? 3 : id > 0xFF ? 2 : 1;
    for (int i = n - 1; i >= 0; --i) o.push_back(static_cast<BYTE>(id >> (8 * i)));
}

void ESize(Bytes& o, uint64_t sz) {
    int n = 1;
    while (n < 8 && sz >= (1ull << (7 * n)) - 1) ++n;
    o.push_back(static_cast<BYTE>((0x80 >> (n - 1)) | (sz >> (8 * (n - 1)))));
    for (int i = n - 2; i >= 0; --i) o.push_back(static_cast<BYTE>(sz >> (8 * i)));
}

void EBin(Bytes& o, uint32_t id, const void* d, size_t n) {
    EId(o, id);
    ESize(o, n);
    const BYTE* p = static_cast<const BYTE*>(d);
    o.insert(o.end(), p, p + n);
}

void EUint(Bytes& o, uint32_t id, uint64_t v) {
    int n = 1;
    while (n < 8 && (v >> (8 * n)) != 0) ++n;
    EId(o, id);
    ESize(o, static_cast<uint64_t>(n));
    for (int i = n - 1; i >= 0; --i) o.push_back(static_cast<BYTE>(v >> (8 * i)));
}

void EStr(Bytes& o, uint32_t id, const char* s) { EBin(o, id, s, strlen(s)); }

void EFloat64(Bytes& o, uint32_t id, double v) {
    uint64_t bits = 0;
    memcpy(&bits, &v, 8);
    EId(o, id);
    ESize(o, 8);
    for (int i = 7; i >= 0; --i) o.push_back(static_cast<BYTE>(bits >> (8 * i)));
}

void EMaster(Bytes& o, uint32_t id, const Bytes& body) {
    EId(o, id);
    ESize(o, body.size());
    o.insert(o.end(), body.begin(), body.end());
}

// Start- und Laenge der NAL-Einheiten eines Annex-B-Datenstroms (00 00 01 / 00 00 00 01)
std::vector<std::pair<size_t, size_t>> AnnexBNals(const BYTE* p, size_t n) {
    std::vector<std::pair<size_t, size_t>> out;
    size_t start = SIZE_MAX;
    for (size_t i = 0; i + 2 < n;) {
        if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1) {
            if (start != SIZE_MAX) {
                size_t e = i;
                while (e > start && p[e - 1] == 0) --e;
                out.push_back({start, e - start});
            }
            start = i + 3;
            i += 3;
        } else {
            ++i;
        }
    }
    if (start != SIZE_MAX && start < n) {
        size_t e = n;
        while (e > start && p[e - 1] == 0) --e;
        out.push_back({start, e - start});
    }
    return out;
}

bool IsAnnexB(const BYTE* p, size_t n) { return n > 4 && p[0] == 0 && p[1] == 0 && (p[2] == 1 || (p[2] == 0 && p[3] == 1)); }

// avcC (CodecPrivate fuer H.264) aus SPS/PPS im Annex-B-Format
bool BuildAvcC(const Bytes& seq, Bytes& avcc) {
    std::vector<Bytes> sps, pps;
    for (const auto& nal : AnnexBNals(seq.data(), seq.size())) {
        if (nal.second == 0) continue;
        const int type = seq[nal.first] & 0x1F;
        Bytes n(seq.begin() + nal.first, seq.begin() + nal.first + nal.second);
        if (type == 7 && sps.size() < 31) sps.push_back(std::move(n));
        else if (type == 8 && pps.size() < 255) pps.push_back(std::move(n));
    }
    if (sps.empty() || pps.empty() || sps[0].size() < 4) return false;
    avcc = {1, sps[0][1], sps[0][2], sps[0][3], 0xFF, static_cast<BYTE>(0xE0 | sps.size())};
    for (const auto& s : sps) { avcc.push_back(static_cast<BYTE>(s.size() >> 8)); avcc.push_back(static_cast<BYTE>(s.size())); avcc.insert(avcc.end(), s.begin(), s.end()); }
    avcc.push_back(static_cast<BYTE>(pps.size()));
    for (const auto& s : pps) { avcc.push_back(static_cast<BYTE>(s.size() >> 8)); avcc.push_back(static_cast<BYTE>(s.size())); avcc.insert(avcc.end(), s.begin(), s.end()); }
    return true;
}

bool RemuxToMkv(const std::wstring& src, const std::wstring& dst, const std::atomic<bool>& cancel, const std::function<void(int)>& progress,
                std::wstring* err) {
    Com<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(src.c_str(), nullptr, &reader);
    if (FAILED(hr)) { SetErr(err, L"Zwischendatei kann nicht gelesen werden (" + Hex(hr) + L")"); return false; }

    DWORD vS = MAXDWORD, aS = MAXDWORD;
    for (DWORD i = 0; i < 32; ++i) {
        Com<IMFMediaType> t;
        if (FAILED(reader->GetNativeMediaType(i, 0, &t))) { if (i > 8) break; continue; }
        GUID major{};
        t->GetGUID(MF_MT_MAJOR_TYPE, &major);
        if (major == MFMediaType_Video && vS == MAXDWORD) vS = i;
        else if (major == MFMediaType_Audio && aS == MAXDWORD) aS = i;
    }
    if (vS == MAXDWORD) { SetErr(err, L"Die Zwischendatei enthält kein Videobild"); return false; }

    Com<IMFMediaType> vt, at;
    reader->GetNativeMediaType(vS, 0, &vt);
    if (aS != MAXDWORD) reader->GetNativeMediaType(aS, 0, &at);
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    reader->SetStreamSelection(vS, TRUE);
    reader->SetCurrentMediaType(vS, nullptr, vt.Get());   // gleicher Typ wie die Quelle: Samples bleiben unveraendert (kein Decoder)
    if (at) {
        reader->SetStreamSelection(aS, TRUE);
        reader->SetCurrentMediaType(aS, nullptr, at.Get());
    }

    UINT32 w = 0, h = 0, fn = 30, fd = 1;
    MFGetAttributeSize(vt.Get(), MF_MT_FRAME_SIZE, &w, &h);
    MFGetAttributeRatio(vt.Get(), MF_MT_FRAME_RATE, &fn, &fd);
    if (fd == 0 || fn == 0) { fn = 30; fd = 1; }

    Bytes avcc;
    {
        UINT32 n = 0;
        Bytes seq;
        if (SUCCEEDED(vt->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &n)) && n > 0) {
            seq.resize(n);
            vt->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, seq.data(), n, &n);
        }
        if (!BuildAvcC(seq, avcc)) { SetErr(err, L"Die H.264-Parameter des Videos fehlen"); return false; }
    }

    Bytes asc;
    UINT32 channels = kChannels, rate = kRate;
    if (at) {
        channels = MFGetAttributeUINT32(at.Get(), MF_MT_AUDIO_NUM_CHANNELS, kChannels);
        rate = MFGetAttributeUINT32(at.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate);
        UINT32 n = 0;
        if (SUCCEEDED(at->GetBlobSize(MF_MT_USER_DATA, &n)) && n > 12) {   // 12 Byte HEAACWAVEINFO, danach die AudioSpecificConfig
            Bytes ud(n);
            at->GetBlob(MF_MT_USER_DATA, ud.data(), n, &n);
            asc.assign(ud.begin() + 12, ud.end());
        } else {
            static const UINT32 kRates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000};
            UINT32 idx = 4;
            for (UINT32 i = 0; i < 12; ++i) if (kRates[i] == rate) idx = i;
            const UINT32 bits = (2u << 11) | (idx << 7) | (channels << 3);
            asc = {static_cast<BYTE>(bits >> 8), static_cast<BYTE>(bits)};
        }
    }

    LONGLONG duration = 0;
    {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &v)) && v.vt == VT_UI8)
            duration = static_cast<LONGLONG>(v.uhVal.QuadPart);
        PropVariantClear(&v);
    }

    HANDLE f = CreateFileW(dst.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { SetErr(err, L"Zieldatei kann nicht angelegt werden"); return false; }
    uint64_t pos = 0;
    bool wrOk = true;
    auto put = [&](const Bytes& b) {
        DWORD wr = 0;
        if (!b.empty() && (!WriteFile(f, b.data(), static_cast<DWORD>(b.size()), &wr, nullptr) || wr != b.size())) wrOk = false;
        pos += b.size();
    };
    auto patch = [&](uint64_t at64, const Bytes& b) {
        LARGE_INTEGER li;
        li.QuadPart = static_cast<LONGLONG>(at64);
        DWORD wr = 0;
        if (!SetFilePointerEx(f, li, nullptr, FILE_BEGIN) || !WriteFile(f, b.data(), static_cast<DWORD>(b.size()), &wr, nullptr) || wr != b.size()) wrOk = false;
    };

    // EBML-Kopf und Segment (Groesse wird am Ende eingetragen)
    {
        Bytes body, el;
        EUint(body, 0x4286, 1);
        EUint(body, 0x42F7, 1);
        EUint(body, 0x42F2, 4);
        EUint(body, 0x42F3, 8);
        EStr(body, 0x4282, "matroska");
        EUint(body, 0x4287, 4);
        EUint(body, 0x4285, 2);
        EMaster(el, 0x1A45DFA3, body);
        EId(el, 0x18538067);
        el.insert(el.end(), {0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
        put(el);
    }
    const uint64_t segStart = pos;
    const uint64_t segSizeAt = pos - 8;

    uint64_t durationAt = 0;
    {
        Bytes body, el;
        EUint(body, 0x2AD7B1, 1000000);   // Zeitstempel in Millisekunden
        EFloat64(body, 0x4489, 0);
        const size_t durOff = body.size() - 8;
        EStr(body, 0x4D80, "Duncan's QuickConvert");
        EStr(body, 0x5741, "Duncan's QuickConvert");
        EId(el, 0x1549A966);
        ESize(el, body.size());
        durationAt = pos + el.size() + durOff;
        el.insert(el.end(), body.begin(), body.end());
        put(el);
    }
    {
        Bytes tracks, v, a;
        EUint(v, 0xD7, 1);
        EUint(v, 0x73C5, 1);
        EUint(v, 0x83, 1);
        EUint(v, 0x9C, 0);
        EStr(v, 0x86, "V_MPEG4/ISO/AVC");
        EBin(v, 0x63A2, avcc.data(), avcc.size());
        EUint(v, 0x23E383, 1000000000ull * fd / fn);
        Bytes vid;
        EUint(vid, 0xB0, w);
        EUint(vid, 0xBA, h);
        EMaster(v, 0xE0, vid);
        EMaster(tracks, 0xAE, v);
        if (at) {
            EUint(a, 0xD7, 2);
            EUint(a, 0x73C5, 2);
            EUint(a, 0x83, 2);
            EUint(a, 0x9C, 0);
            EStr(a, 0x86, "A_AAC");
            EBin(a, 0x63A2, asc.data(), asc.size());
            Bytes aud;
            EFloat64(aud, 0xB5, static_cast<double>(rate));
            EUint(aud, 0x9F, channels);
            EMaster(a, 0xE1, aud);
            EMaster(tracks, 0xAE, a);
        }
        Bytes el;
        EMaster(el, 0x1654AE6B, tracks);
        put(el);
    }

    struct Cue { int64_t ms; uint64_t cluster; };
    std::vector<Cue> cues;
    std::vector<int64_t> pendingCues;
    Bytes body;
    int64_t clusterMs = 0, maxMs = 0;
    auto flush = [&]() {
        if (body.empty()) return;
        const uint64_t clusterPos = pos - segStart;
        for (int64_t t : pendingCues) cues.push_back({t, clusterPos});
        pendingCues.clear();
        Bytes el, head;
        EUint(head, 0xE7, static_cast<uint64_t>(clusterMs));
        EId(el, 0x1F43B675);
        ESize(el, head.size() + body.size());
        el.insert(el.end(), head.begin(), head.end());
        put(el);
        put(body);
        body.clear();
    };

    bool vDone = false, aDone = !at, firstVideo = true, ok = false;
    std::wstring why = L"Umpacken fehlgeschlagen";
    int lastPct = -1;
    Bytes data;
    while (wrOk) {
        if (vDone && aDone) { ok = true; break; }
        if (cancel.load()) { why = L"Abgebrochen"; break; }
        DWORD si = 0, flags = 0;
        LONGLONG ts = 0;
        Com<IMFSample> sample;
        hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_ANY_STREAM), 0, &si, &flags, &ts, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) { why = L"Lesefehler in der Zwischendatei"; break; }
        const bool isVideo = si == vS;
        if (sample) {
            Com<IMFMediaBuffer> buf;
            BYTE* p = nullptr;
            DWORD len = 0;
            if (FAILED(sample->ConvertToContiguousBuffer(&buf)) || FAILED(buf->Lock(&p, nullptr, &len))) { why = L"Lesefehler in der Zwischendatei"; break; }
            data.clear();
            if (isVideo && IsAnnexB(p, len)) {   // Annex B (Startcodes) -> Laengenpraefix, wie es Matroska fuer H.264 verlangt
                for (const auto& nal : AnnexBNals(p, len)) {
                    const uint32_t n = static_cast<uint32_t>(nal.second);
                    data.insert(data.end(), {static_cast<BYTE>(n >> 24), static_cast<BYTE>(n >> 16), static_cast<BYTE>(n >> 8), static_cast<BYTE>(n)});
                    data.insert(data.end(), p + nal.first, p + nal.first + nal.second);
                }
            } else {
                data.assign(p, p + len);
            }
            buf->Unlock();

            const int64_t ms = std::max<LONGLONG>(ts, 0) / 10000;
            bool key = true;
            if (isVideo) {
                key = firstVideo || MFGetAttributeUINT32(sample.Get(), MFSampleExtension_CleanPoint, 0) != 0;
                firstVideo = false;
            }
            if (!body.empty() && ((isVideo && key && ms - clusterMs >= 5000) || ms - clusterMs > 30000)) flush();
            if (body.empty()) clusterMs = ms;
            if (isVideo && key) pendingCues.push_back(ms);
            const int64_t rel = std::clamp<int64_t>(ms - clusterMs, -32768, 32767);
            Bytes blk;
            blk.push_back(static_cast<BYTE>(0x80 | (isVideo ? 1 : 2)));
            blk.push_back(static_cast<BYTE>(rel >> 8));
            blk.push_back(static_cast<BYTE>(rel));
            blk.push_back(key ? 0x80 : 0);
            blk.insert(blk.end(), data.begin(), data.end());
            EId(body, 0xA3);
            ESize(body, blk.size());
            body.insert(body.end(), blk.begin(), blk.end());
            maxMs = std::max(maxMs, ms);

            if (isVideo && progress && duration > 0) {
                const int pct = static_cast<int>(std::clamp<LONGLONG>(ts * 100 / duration, 0, 99));
                if (pct != lastPct) { lastPct = pct; progress(pct); }
            }
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) (isVideo ? vDone : aDone) = true;
    }
    if (ok) {
        flush();
        Bytes cuesBody;
        for (const Cue& c : cues) {
            Bytes pt, tp;
            EUint(pt, 0xB3, static_cast<uint64_t>(c.ms));
            EUint(tp, 0xF7, 1);
            EUint(tp, 0xF1, c.cluster);
            EMaster(pt, 0xB7, tp);
            EMaster(cuesBody, 0xBB, pt);
        }
        Bytes el;
        EMaster(el, 0x1C53BB6B, cuesBody);
        put(el);
        Bytes sz = {0x01};
        const uint64_t segLen = pos - segStart;
        for (int i = 6; i >= 0; --i) sz.push_back(static_cast<BYTE>(segLen >> (8 * i)));
        patch(segSizeAt, sz);
        Bytes dur;
        const double d = static_cast<double>(maxMs + 1000ll * fd / fn);
        uint64_t bits = 0;
        memcpy(&bits, &d, 8);
        for (int i = 7; i >= 0; --i) dur.push_back(static_cast<BYTE>(bits >> (8 * i)));
        patch(durationAt, dur);
    }
    CloseHandle(f);
    if (!ok || !wrOk) {
        DeleteFileW(dst.c_str());
        SetErr(err, wrOk ? why : L"Zieldatei konnte nicht geschrieben werden (Speicherplatz?)");
        return false;
    }
    if (progress) progress(100);
    return true;
}

// Video -> MKV: erst als MP4 kodieren (90 %), dann umpacken (10 %)
bool ConvertVideoMkvOnce(const std::wstring& src, const std::wstring& dst, int quality, const std::atomic<bool>& cancel,
                         const std::function<void(int)>& progress, std::wstring* err, long long maxBytes) {
    const std::wstring tmp = dst + L".tmp.mp4";
    if (!ConvertVideoMp4(src, tmp, quality, cancel, [&](int p) { if (progress) progress(p * 9 / 10); }, err, maxBytes)) return false;
    const bool ok = RemuxToMkv(tmp, dst, cancel, [&](int p) { if (progress) progress(90 + p / 10); }, err);
    DeleteFileW(tmp.c_str());
    return ok;
}

// Mit Zielgroesse: wird die Datei trotz CBR zu gross, noch bis zu zweimal mit entsprechend kleinerer Vorgabe kodieren
bool ConvertVideoSized(const std::wstring& src, const std::wstring& dst, bool mkv, int quality, const std::atomic<bool>& cancel,
                       const std::function<void(int)>& progress, std::wstring* err, long long maxBytes) {
    double factor = 1.0;
    for (int attempt = 0; attempt < 3; ++attempt) {
        const long long target = maxBytes > 0 ? static_cast<long long>(maxBytes * factor) : 0;
        const bool ok = mkv ? ConvertVideoMkvOnce(src, dst, quality, cancel, progress, err, target)
                            : ConvertVideoMp4(src, dst, quality, cancel, progress, err, target);
        if (!ok || maxBytes <= 0) return ok;
        const long long size = FileBytes(dst);
        if (size <= maxBytes) return true;
        if (attempt == 2 || size <= 0) break;
        factor *= static_cast<double>(maxBytes) / static_cast<double>(size) * 0.96;
        DeleteFileW(dst.c_str());
    }
    DeleteFileW(dst.c_str());
    SetErr(err, L"Die Zielgröße von " + MbText(maxBytes) + L" MB wird nicht erreicht");
    return false;
}

}  // namespace

bool ConvertMedia(const std::wstring& src, const std::wstring& dst, AudioFmt fmt, int quality, const std::atomic<bool>& cancel,
                  const std::function<void(int)>& progress, std::wstring* err, long long maxBytes) {
    if (fmt == AudioFmt::Mp4 || fmt == AudioFmt::Mkv) {
        if (IsAudioOnlyFile(src)) fmt = AudioFmt::M4a;   // ohne Videobild gibt es nichts nach MP4/MKV zu kodieren
        else return ConvertVideoSized(src, dst, fmt == AudioFmt::Mkv, quality, cancel, progress, err, maxBytes);
    }
    if (maxBytes > 0 && (fmt == AudioFmt::Wav || fmt == AudioFmt::Flac)) {
        SetErr(err, L"Eine Zielgröße geht bei WAV und FLAC nicht (verlustfrei) – nimm M4A oder MP3");
        return false;
    }
    Com<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(src.c_str(), nullptr, &reader);
    if (FAILED(hr)) { SetErr(err, L"Datei kann nicht geöffnet werden – Format oder Codec wird von Windows nicht unterstützt (" + Hex(hr) + L")"); return false; }

    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE);

    // Windows decodiert und resampelt auf PCM 44,1 kHz / 16 Bit / Stereo
    Com<IMFMediaType> pcm;
    MFCreateMediaType(&pcm);
    pcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, kChannels);
    pcm->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate);
    pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, kBits);
    pcm->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, kChannels * kBits / 8);
    pcm->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kRate * kChannels * kBits / 8);
    pcm->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    hr = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, pcm.Get());
    if (FAILED(hr)) { SetErr(err, L"Die Datei enthält keine lesbare Tonspur"); return false; }

    LONGLONG duration = 0;
    {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &v)) && v.vt == VT_UI8)
            duration = static_cast<LONGLONG>(v.uhVal.QuadPart);
        PropVariantClear(&v);
    }

    int kbps = BitrateKbps(fmt, quality);
    if (maxBytes > 0) {
        if (duration <= 0) { SetErr(err, L"Die Länge der Datei ist unbekannt, eine Zielgröße ist nicht möglich"); return false; }
        const int fit = EncoderKbpsForSize(fmt, maxBytes, static_cast<double>(duration) / 1e7);
        if (fit <= 0) { SetErr(err, L"Zu lang für diese Zielgröße – schon die niedrigste Stufe wäre größer"); return false; }
        kbps = std::min(kbps, fit);
    }

    int lastPct = -1;
    auto report = [&](LONGLONG ts) {
        if (!progress || duration <= 0) return;
        const int pct = static_cast<int>(std::clamp<LONGLONG>(ts * 100 / duration, 0, 99));
        if (pct != lastPct) { lastPct = pct; progress(pct); }
    };

    HANDLE wav = INVALID_HANDLE_VALUE;
    Com<IMFSinkWriter> writer;
    DWORD sinkStream = 0;
    bool ok = false;
    std::wstring why = L"Konvertierung fehlgeschlagen";

    do {
        if (fmt == AudioFmt::Wav) {
            wav = CreateFileW(dst.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (wav == INVALID_HANDLE_VALUE || !WriteWavHeader(wav, 0)) { why = L"Zieldatei kann nicht angelegt werden"; break; }
        } else {
            hr = MFCreateSinkWriterFromURL(dst.c_str(), nullptr, nullptr, &writer);
            if (FAILED(hr)) { why = L"Zieldatei kann nicht angelegt werden (" + Hex(hr) + L")"; break; }
            Com<IMFMediaType> out;
            MFCreateMediaType(&out);
            out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            out->SetGUID(MF_MT_SUBTYPE, fmt == AudioFmt::Mp3 ? MFAudioFormat_MP3 : (fmt == AudioFmt::Flac ? MFAudioFormat_FLAC : MFAudioFormat_AAC));
            out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, kChannels);
            out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate);
            if (fmt == AudioFmt::M4a || fmt == AudioFmt::Flac) out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, kBits);   // der MP3-Encoder kennt das Attribut nicht
            out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, fmt == AudioFmt::Flac ? kRate * kChannels * kBits / 8 : kbps * 1000 / 8);
            out->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 1);
            if (fmt == AudioFmt::M4a) {
                out->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
                out->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
            }
            hr = writer->AddStream(out.Get(), &sinkStream);
            if (FAILED(hr)) { why = L"Dieser Windows-Encoder ist nicht verfügbar (" + Hex(hr) + L")"; break; }
            hr = writer->SetInputMediaType(sinkStream, pcm.Get(), nullptr);
            if (FAILED(hr)) { why = L"Encoder nimmt das Audioformat nicht an (" + Hex(hr) + L")"; break; }
            hr = writer->BeginWriting();
            if (FAILED(hr)) { why = L"Encoder lässt sich nicht starten (" + Hex(hr) + L")"; break; }
        }

        uint64_t dataBytes = 0;
        bool failed = false, aborted = false;
        for (;;) {
            if (cancel.load()) { aborted = true; break; }
            DWORD actual = 0, flags = 0;
            LONGLONG ts = 0;
            Com<IMFSample> sample;
            hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, &actual, &flags, &ts, &sample);
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) { failed = true; why = L"Lesefehler in der Quelldatei"; break; }
            if (sample) {
                report(ts);
                if (fmt == AudioFmt::Wav) {
                    Com<IMFMediaBuffer> buf;
                    BYTE* p = nullptr;
                    DWORD len = 0;
                    if (FAILED(sample->ConvertToContiguousBuffer(&buf)) || FAILED(buf->Lock(&p, nullptr, &len))) { failed = true; break; }
                    DWORD wr = 0;
                    const bool w = WriteFile(wav, p, len, &wr, nullptr) && wr == len;
                    buf->Unlock();
                    dataBytes += len;
                    if (!w) { failed = true; why = L"Schreiben fehlgeschlagen (Datenträger voll?)"; break; }
                    if (dataBytes > 0xFFFFFF00ull) { failed = true; why = L"WAV-Dateien sind auf 4 GB begrenzt"; break; }
                } else {
                    hr = writer->WriteSample(sinkStream, sample.Get());
                    if (FAILED(hr)) { failed = true; why = L"Encoder meldet einen Fehler (" + Hex(hr) + L")"; break; }
                }
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        }
        if (aborted) { why = L"Abgebrochen"; break; }
        if (failed) break;

        if (fmt == AudioFmt::Wav) {
            SetFilePointer(wav, 0, nullptr, FILE_BEGIN);
            if (!WriteWavHeader(wav, static_cast<uint32_t>(dataBytes))) break;
        } else {
            hr = writer->Finalize();
            if (FAILED(hr)) { why = L"Datei konnte nicht abgeschlossen werden (" + Hex(hr) + L")"; break; }
        }
        ok = true;
    } while (false);

    writer.Reset();
    if (wav != INVALID_HANDLE_VALUE) CloseHandle(wav);
    if (ok) {
        if (progress) progress(100);
    } else {
        DeleteFileW(dst.c_str());
        SetErr(err, why);
    }
    return ok;
}

}  // namespace conv
