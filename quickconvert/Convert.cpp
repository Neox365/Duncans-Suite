#include "Convert.h"

#include <windows.h>
#include <wincodec.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <vector>

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
    }
    return GUID_ContainerFormatPng;
}

bool HasAlpha(ImageFmt f) { return f == ImageFmt::Png || f == ImageFmt::Webp || f == ImageFmt::Heic; }
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
    }
    return L"png";
}

const wchar_t* AudioExt(AudioFmt f) {
    switch (f) {
        case AudioFmt::Mp3: return L"mp3";
        case AudioFmt::M4a: return L"m4a";
        case AudioFmt::Wav: return L"wav";
    }
    return L"mp3";
}

int BitrateKbps(AudioFmt f, int quality) {
    quality = std::clamp(quality, 0, 100);
    static const int kMp3[] = {128, 160, 192, 224, 256, 320};   // vom Windows-MP3-Encoder unterstuetzte Stufen bei 44,1 kHz Stereo
    static const int kAac[] = {96, 128, 160, 192};              // vom Windows-AAC-Encoder unterstuetzt
    switch (f) {
        case AudioFmt::Mp3: return kMp3[(quality * 5 + 50) / 100];
        case AudioFmt::M4a: return kAac[(quality * 3 + 50) / 100];
        case AudioFmt::Wav: return 0;
    }
    return 0;
}

bool MediaStartup() { return SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE)); }
void MediaShutdown() { MFShutdown(); }

// ---------------------------------------------------------------------------------------------------------------------
// Bilder
// ---------------------------------------------------------------------------------------------------------------------

bool ConvertImage(const std::wstring& src, const std::wstring& dst, ImageFmt fmt, int quality, std::wstring* err) {
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
    const UINT stride = w * 4;
    std::vector<BYTE> px(static_cast<size_t>(stride) * h);
    if (FAILED(cur->CopyPixels(nullptr, stride, static_cast<UINT>(px.size()), px.data()))) { SetErr(err, L"Bild kann nicht gelesen werden"); return false; }

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

}  // namespace

bool ConvertMedia(const std::wstring& src, const std::wstring& dst, AudioFmt fmt, int quality, const std::atomic<bool>& cancel,
                  const std::function<void(int)>& progress, std::wstring* err) {
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
            out->SetGUID(MF_MT_SUBTYPE, fmt == AudioFmt::Mp3 ? MFAudioFormat_MP3 : MFAudioFormat_AAC);
            out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, kChannels);
            out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate);
            if (fmt == AudioFmt::M4a) out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, kBits);   // der MP3-Encoder kennt das Attribut nicht
            out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, BitrateKbps(fmt, quality) * 1000 / 8);
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
