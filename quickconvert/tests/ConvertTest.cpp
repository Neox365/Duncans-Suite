// Test der QuickConvert-Engine (WIC + Media Foundation). Nur Windows.
// g++ -std=c++17 -DUNICODE -D_UNICODE -I.. ConvertTest.cpp ../Convert.cpp -lwindowscodecs -lmfplat -lmfreadwrite -lmfuuid -lole32 -loleaut32 -luuid
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../Convert.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static std::wstring Tmp(const wchar_t* name) {
    wchar_t d[MAX_PATH];
    GetTempPathW(MAX_PATH, d);
    return std::wstring(d) + L"qc_" + name;
}
static long long Size(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) return -1;
    return ((long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
}
static void Put(std::vector<BYTE>& d, int o, unsigned v, int n) { for (int i = 0; i < n; ++i) d[o + i] = (BYTE)(v >> (8 * i)); }

static void WriteSineWav(const std::wstring& path, int seconds) {
    const int rate = 44100, ch = 2, n = rate * seconds;
    std::vector<BYTE> d(44 + n * ch * 2);
    memcpy(&d[0], "RIFF", 4); Put(d, 4, 36 + n * 4, 4); memcpy(&d[8], "WAVEfmt ", 8);
    Put(d, 16, 16, 4); Put(d, 20, 1, 2); Put(d, 22, ch, 2); Put(d, 24, rate, 4); Put(d, 28, rate * 4, 4); Put(d, 32, 4, 2); Put(d, 34, 16, 2);
    memcpy(&d[36], "data", 4); Put(d, 40, n * 4, 4);
    for (int i = 0; i < n; ++i) {
        short s = (short)(12000 * std::sin(2 * 3.14159265 * 440 * i / rate));
        Put(d, 44 + i * 4, (unsigned short)s, 2); Put(d, 46 + i * 4, (unsigned short)s, 2);
    }
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD wr; WriteFile(f, d.data(), (DWORD)d.size(), &wr, nullptr); CloseHandle(f);
}
static void WriteBmp(const std::wstring& path, int w, int h) {
    const int row = (w * 3 + 3) & ~3;
    std::vector<BYTE> d(54 + row * h, 0);
    d[0] = 'B'; d[1] = 'M'; Put(d, 2, (unsigned)d.size(), 4); Put(d, 10, 54, 4); Put(d, 14, 40, 4); Put(d, 18, w, 4); Put(d, 22, h, 4);
    d[26] = 1; d[28] = 24; Put(d, 34, row * h, 4);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) { BYTE* p = &d[54 + y * row + x * 3]; p[0] = x * 4; p[1] = y * 4; p[2] = 200; }
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD wr; WriteFile(f, d.data(), (DWORD)d.size(), &wr, nullptr); CloseHandle(f);
}

// Erzeugt ein Testvideo (320x240, 30 Bilder, 1 s Sinuston) als H.264/AAC-MP4 ueber den Windows-Encoder
static bool MakeTestVideo(const std::wstring& path) {
    IMFSinkWriter* w = nullptr;
    if (FAILED(MFCreateSinkWriterFromURL(path.c_str(), nullptr, nullptr, &w))) return false;
    const UINT W = 320, H = 240;
    IMFMediaType *vo = nullptr, *vi = nullptr, *ao = nullptr, *ai = nullptr;
    MFCreateMediaType(&vo);
    vo->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video); vo->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    vo->SetUINT32(MF_MT_AVG_BITRATE, 800000); vo->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    vo->SetUINT64(MF_MT_FRAME_SIZE, ((UINT64)W << 32) | H); vo->SetUINT64(MF_MT_FRAME_RATE, ((UINT64)30 << 32) | 1); vo->SetUINT64(MF_MT_PIXEL_ASPECT_RATIO, ((UINT64)1 << 32) | 1);
    MFCreateMediaType(&vi);
    vi->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video); vi->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    vi->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive); vi->SetUINT32(MF_MT_DEFAULT_STRIDE, W * 4);
    vi->SetUINT64(MF_MT_FRAME_SIZE, ((UINT64)W << 32) | H); vi->SetUINT64(MF_MT_FRAME_RATE, ((UINT64)30 << 32) | 1); vi->SetUINT64(MF_MT_PIXEL_ASPECT_RATIO, ((UINT64)1 << 32) | 1);
    MFCreateMediaType(&ao);
    ao->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio); ao->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    ao->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2); ao->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100); ao->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    ao->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000); ao->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 1);
    ao->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0); ao->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
    MFCreateMediaType(&ai);
    ai->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio); ai->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    ai->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2); ai->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100); ai->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    ai->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4); ai->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 176400);
    DWORD vs = 0, as = 0;
    bool ok = SUCCEEDED(w->AddStream(vo, &vs)) && SUCCEEDED(w->SetInputMediaType(vs, vi, nullptr)) &&
              SUCCEEDED(w->AddStream(ao, &as)) && SUCCEEDED(w->SetInputMediaType(as, ai, nullptr)) && SUCCEEDED(w->BeginWriting());
    auto mk = [](const BYTE* data, DWORD len, LONGLONG t, LONGLONG d) {
        IMFMediaBuffer* b = nullptr; IMFSample* s = nullptr; BYTE* p = nullptr;
        MFCreateMemoryBuffer(len, &b); b->Lock(&p, nullptr, nullptr); memcpy(p, data, len); b->Unlock(); b->SetCurrentLength(len);
        MFCreateSample(&s); s->AddBuffer(b); s->SetSampleTime(t); s->SetSampleDuration(d); b->Release(); return s;
    };
    if (ok) {
        std::vector<BYTE> pcm(44100 * 4);
        for (int i = 0; i < 44100; ++i) { short v = (short)(10000 * std::sin(2 * 3.14159265 * 440 * i / 44100)); memcpy(&pcm[i * 4], &v, 2); memcpy(&pcm[i * 4 + 2], &v, 2); }
        IMFSample* as_ = mk(pcm.data(), (DWORD)pcm.size(), 0, 10000000);
        ok = SUCCEEDED(w->WriteSample(as, as_)); as_->Release();
        std::vector<BYTE> fr(W * H * 4);
        for (int f = 0; f < 30 && ok; ++f) {
            for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) { BYTE* q = &fr[(y * W + x) * 4]; q[0] = (BYTE)(x + f * 8); q[1] = (BYTE)y; q[2] = (BYTE)(f * 8); q[3] = 255; }
            IMFSample* s = mk(fr.data(), (DWORD)fr.size(), (LONGLONG)f * 333333, 333333);
            ok = SUCCEEDED(w->WriteSample(vs, s)); s->Release();
        }
        ok = ok && SUCCEEDED(w->Finalize());
    }
    vo->Release(); vi->Release(); ao->Release(); ai->Release(); w->Release();
    return ok;
}

int main() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    CHECK(conv::MediaStartup());
    CHECK(conv::BitrateKbps(conv::AudioFmt::Mp3, 0) == 128);
    CHECK(conv::BitrateKbps(conv::AudioFmt::Mp3, 100) == 320);
    CHECK(conv::BitrateKbps(conv::AudioFmt::M4a, 100) == 192);
    CHECK(conv::KindOfFile(L"C:\\ordner\\a.JPG") == conv::Kind::Image);
    CHECK(conv::KindOfFile(L"a.mp4") == conv::Kind::Media);
    CHECK(conv::KindOfFile(L"a.txt") == conv::Kind::Unknown);

    std::wstring err;
    const std::wstring bmp = Tmp(L"in.bmp");
    WriteBmp(bmp, 64, 48);
    struct { conv::ImageFmt f; const wchar_t* n; bool must; } imgs[] = {
        {conv::ImageFmt::Png, L"out.png", true}, {conv::ImageFmt::Jpeg, L"out.jpg", true}, {conv::ImageFmt::Bmp, L"out2.bmp", true},
        {conv::ImageFmt::Webp, L"out.webp", false}, {conv::ImageFmt::Heic, L"out.heic", false}};
    for (auto& t : imgs) {
        std::wstring dst = Tmp(t.n);
        DeleteFileW(dst.c_str());
        bool ok = conv::ConvertImage(bmp, dst, t.f, 80, &err);
        std::wprintf(L"Bild -> %ls: %ls %ls (%lld Bytes)\n", t.n, ok ? L"ok" : L"nicht moeglich", ok ? L"" : err.c_str(), Size(dst));
        if (t.must) { CHECK(ok); CHECK(Size(dst) > 0); } else CHECK(ok || !err.empty());
        if (!ok) CHECK(Size(dst) < 0);   // keine halbe Datei zurueckgelassen
    }
    // JPEG mit niedriger Qualitaet ist kleiner als mit hoher
    std::wstring lo = Tmp(L"lo.jpg"), hi = Tmp(L"hi.jpg");
    CHECK(conv::ConvertImage(bmp, lo, conv::ImageFmt::Jpeg, 10, &err));
    CHECK(conv::ConvertImage(bmp, hi, conv::ImageFmt::Jpeg, 100, &err));
    CHECK(Size(lo) < Size(hi));
    // jpg -> png (Rueckweg ueber den Decoder)
    CHECK(conv::ConvertImage(hi, Tmp(L"back.png"), conv::ImageFmt::Png, 100, &err));

    const std::wstring wav = Tmp(L"in.wav");
    WriteSineWav(wav, 3);
    std::atomic<bool> cancel{false};
    int last = -1;
    struct { conv::AudioFmt f; const wchar_t* n; } auds[] = {{conv::AudioFmt::Mp3, L"out.mp3"}, {conv::AudioFmt::M4a, L"out.m4a"}, {conv::AudioFmt::Wav, L"out2.wav"}};
    for (auto& t : auds) {
        std::wstring dst = Tmp(t.n);
        DeleteFileW(dst.c_str());
        bool ok = conv::ConvertMedia(wav, dst, t.f, 60, cancel, [&](int p) { last = p; }, &err);
        std::wprintf(L"Audio -> %ls: %ls %ls (%lld Bytes)\n", t.n, ok ? L"ok" : L"Fehler", ok ? L"" : err.c_str(), Size(dst));
        CHECK(ok);
        CHECK(Size(dst) > 1000);
        CHECK(last == 100);
    }
    DeleteFileW(Tmp(L"back.wav").c_str());
    // mp3 -> wav: etwa 3 s PCM
    CHECK(conv::ConvertMedia(Tmp(L"out.mp3"), Tmp(L"back.wav"), conv::AudioFmt::Wav, 0, cancel, nullptr, &err));
    CHECK(Size(Tmp(L"back.wav")) > 44100 * 4 * 2);
    // Abbruch
    cancel = true;
    std::wstring c = Tmp(L"cancel.mp3");
    CHECK(!conv::ConvertMedia(wav, c, conv::AudioFmt::Mp3, 60, cancel, nullptr, &err));
    CHECK(err == L"Abgebrochen");
    CHECK(Size(c) < 0);
    // kein Audio / kaputte Datei
    cancel = false;
    CHECK(!conv::ConvertMedia(bmp, Tmp(L"bad.mp3"), conv::AudioFmt::Mp3, 60, cancel, nullptr, &err));
    CHECK(!conv::ConvertImage(wav, Tmp(L"bad.png"), conv::ImageFmt::Png, 80, &err));

    // Video -> MP4 (H.264 + AAC), Video -> MP3 (Ton extrahieren), Audio + Ziel MP4 -> M4A
    const std::wstring vid = Tmp(L"in.mp4");
    DeleteFileW(vid.c_str());
    CHECK(MakeTestVideo(vid));
    CHECK(Size(vid) > 1000);
    {
        int lastV = -1;
        std::wstring o1 = Tmp(L"vid_out.mp4"), o2 = Tmp(L"vid_hi.mp4");
        DeleteFileW(o1.c_str()); DeleteFileW(o2.c_str());
        bool a = conv::ConvertMedia(vid, o1, conv::AudioFmt::Mp4, 10, cancel, [&](int p) { lastV = p; }, &err);
        std::wprintf(L"Video -> MP4 (niedrig): %ls %ls (%lld Bytes)\n", a ? L"ok" : L"Fehler", a ? L"" : err.c_str(), Size(o1));
        CHECK(a); CHECK(Size(o1) > 1000); CHECK(lastV == 100);
        bool b = conv::ConvertMedia(vid, o2, conv::AudioFmt::Mp4, 100, cancel, nullptr, &err);
        CHECK(b); CHECK(Size(o2) > Size(o1));   // hoehere Qualitaet = groessere Datei
        // das Ergebnis ist wieder lesbar und hat Ton
        CHECK(conv::ConvertMedia(o1, Tmp(L"vid_audio.mp3"), conv::AudioFmt::Mp3, 60, cancel, nullptr, &err));
        CHECK(Size(Tmp(L"vid_audio.mp3")) > 1000);
    }
    CHECK(conv::ConvertMedia(vid, Tmp(L"vid_only_audio.mp3"), conv::AudioFmt::Mp3, 60, cancel, nullptr, &err));
    CHECK(conv::EffectiveFmt(L"x.mp3", conv::AudioFmt::Mp4) == conv::AudioFmt::M4a && conv::EffectiveFmt(L"x.mov", conv::AudioFmt::Mp4) == conv::AudioFmt::Mp4);
    {
        std::wstring m = Tmp(L"aud_as_mp4.m4a");
        DeleteFileW(m.c_str());
        CHECK(conv::ConvertMedia(wav, m, conv::AudioFmt::Mp4, 60, cancel, nullptr, &err));   // reines Audio -> M4A
        CHECK(Size(m) > 1000);
    }
    cancel = true;
    CHECK(!conv::ConvertMedia(vid, Tmp(L"vid_cancel.mp4"), conv::AudioFmt::Mp4, 60, cancel, nullptr, &err) && err == L"Abgebrochen");
    CHECK(Size(Tmp(L"vid_cancel.mp4")) < 0);
    cancel = false;
    CHECK(conv::ConvertMedia(wav, Tmp(L"x.mp3"), conv::AudioFmt::Mp3, 60, cancel, nullptr, &err));   // WAV -> MP3 bleibt ok
    CHECK(conv::VideoBitrateKbps(1920, 1080, 30, 0) < conv::VideoBitrateKbps(1920, 1080, 30, 100));
    CHECK(conv::VideoBitrateKbps(16, 16, 1, 0) == 300 && conv::VideoBitrateKbps(7680, 4320, 60, 100) == 40000);

    conv::MediaShutdown();
    if (failures == 0) std::printf("alle Tests bestanden\n");
    return failures ? 1 : 0;
}
