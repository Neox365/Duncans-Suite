// Test der QuickConvert-Engine (WIC + Media Foundation). Nur Windows.
// g++ -std=c++17 -DUNICODE -D_UNICODE -I.. ConvertTest.cpp ../Convert.cpp -lwindowscodecs -lmfplat -lmfreadwrite -lmfuuid -lole32 -loleaut32 -luuid
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    // Verkleinern: laengste Kante auf maxEdge (Seitenverhaeltnis bleibt, nie vergroessern); Groesse aus dem BMP-Kopf lesen
    auto bmpDims = [](const std::wstring& path, int& w, int& h) {
        w = h = 0;
        HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return;
        BYTE hd[26] = {}; DWORD rd = 0;
        ReadFile(f, hd, sizeof(hd), &rd, nullptr); CloseHandle(f);
        if (rd == sizeof(hd)) { w = *(int*)&hd[18]; h = std::abs(*(int*)&hd[22]); }
    };
    int bw = 0, bh = 0;
    CHECK(conv::ConvertImage(bmp, Tmp(L"s32.bmp"), conv::ImageFmt::Bmp, 80, &err, 32));
    bmpDims(Tmp(L"s32.bmp"), bw, bh);
    CHECK(bw == 32 && bh == 24);
    CHECK(conv::ConvertImage(bmp, Tmp(L"s200.bmp"), conv::ImageFmt::Bmp, 80, &err, 200));   // groesser als das Bild: unveraendert
    bmpDims(Tmp(L"s200.bmp"), bw, bh);
    CHECK(bw == 64 && bh == 48);
    CHECK(conv::ConvertImage(bmp, Tmp(L"s0.bmp"), conv::ImageFmt::Bmp, 80, &err, 0));       // 0 = Original
    bmpDims(Tmp(L"s0.bmp"), bw, bh);
    CHECK(bw == 64 && bh == 48);
    // GIF: Kopf "GIF8", Rueckweg ueber den Decoder
    CHECK(conv::ConvertImage(bmp, Tmp(L"out.gif"), conv::ImageFmt::Gif, 80, &err));
    {
        std::string gif; HANDLE f = CreateFileW(Tmp(L"out.gif").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        char hd[6] = {}; DWORD rd = 0; if (f != INVALID_HANDLE_VALUE) { ReadFile(f, hd, 6, &rd, nullptr); CloseHandle(f); }
        CHECK(rd == 6 && std::string(hd, 4) == "GIF8");
    }
    CHECK(conv::ConvertImage(Tmp(L"out.gif"), Tmp(L"gif_back.png"), conv::ImageFmt::Png, 100, &err));
    // PDF: zwei Bilder (eines hochkant, eines quer) in ein PDF; Kopf, Seitenzahl, Ende pruefen
    {
        const std::wstring wide = Tmp(L"wide.bmp"), tall = Tmp(L"tall.bmp"), pdf = Tmp(L"multi.pdf");
        WriteBmp(wide, 80, 40); WriteBmp(tall, 40, 80);
        std::atomic<bool> noCancel{false};
        int lastP = -1;
        CHECK(conv::ConvertImagesToPdf({wide, tall}, pdf, 80, 0, noCancel, [&](int pct) { lastP = pct; }, &err));
        CHECK(lastP == 100);
        std::string d; HANDLE f = CreateFileW(pdf.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        char buf[65536]; DWORD rd = 0;
        if (f != INVALID_HANDLE_VALUE) { while (ReadFile(f, buf, sizeof buf, &rd, nullptr) && rd > 0) d.append(buf, rd); CloseHandle(f); }
        CHECK(d.compare(0, 5, "%PDF-") == 0);
        CHECK(d.find("/Count 2") != std::string::npos);
        CHECK(d.find("/DCTDecode") != std::string::npos);
        CHECK(d.find("[0 0 841.89 595.28]") != std::string::npos);   // das breite Bild: Querformat
        CHECK(d.find("[0 0 595.28 841.89]") != std::string::npos);   // das hohe Bild: Hochformat
        CHECK(d.size() > 8 && d.compare(d.size() - 6, 5, "%%EOF") == 0);
        CHECK(Size(pdf + L".tmp0.jpg") < 0);                         // keine Hilfsdateien uebrig
        CHECK(!conv::ConvertImagesToPdf({Tmp(L"in.wav")}, Tmp(L"bad.pdf"), 80, 0, noCancel, nullptr, &err));   // kein Bild: Fehler mit Dateiname, keine Datei
        CHECK(err.find(L"qc_in.wav") != std::wstring::npos && Size(Tmp(L"bad.pdf")) < 0);
        std::atomic<bool> stop{true};
        CHECK(!conv::ConvertImagesToPdf({wide}, Tmp(L"stop.pdf"), 80, 0, stop, nullptr, &err) && err == L"Abgebrochen" && Size(Tmp(L"stop.pdf")) < 0);
    }
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
    {   // Video -> MKV: Matroska-Kopf, Zwischendatei weg, von Windows wieder lesbar (Ton extrahierbar), Audio-Quelle -> M4A
        int lastK = -1;
        std::wstring k = Tmp(L"vid_out.mkv");
        DeleteFileW(k.c_str());
        bool ok = conv::ConvertMedia(vid, k, conv::AudioFmt::Mkv, 60, cancel, [&](int p) { lastK = p; }, &err);
        std::wprintf(L"Video -> MKV: %ls %ls (%lld Bytes)\n", ok ? L"ok" : L"Fehler", ok ? L"" : err.c_str(), Size(k));
        CHECK(ok); CHECK(Size(k) > 1000); CHECK(lastK == 100);
        CHECK(Size(k + L".tmp.mp4") < 0);
        FILE* fp = _wfopen(k.c_str(), L"rb");
        unsigned char head[4] = {};
        if (fp) { fread(head, 1, 4, fp); fclose(fp); }
        CHECK(head[0] == 0x1A && head[1] == 0x45 && head[2] == 0xDF && head[3] == 0xA3);
        std::wstring mp3 = Tmp(L"mkv_audio.mp3");
        DeleteFileW(mp3.c_str());
        CHECK(conv::ConvertMedia(k, mp3, conv::AudioFmt::Mp3, 60, cancel, nullptr, &err));
        CHECK(Size(mp3) > 1000);
        std::wstring back = Tmp(L"mkv_back.mp4");
        DeleteFileW(back.c_str());
        CHECK(conv::ConvertMedia(k, back, conv::AudioFmt::Mp4, 30, cancel, nullptr, &err));   // Bild laesst sich aus dem MKV wieder dekodieren
        CHECK(Size(back) > 1000);
        CHECK(conv::EffectiveFmt(L"x.mp3", conv::AudioFmt::Mkv) == conv::AudioFmt::M4a && std::wstring(conv::AudioExt(conv::AudioFmt::Mkv)) == L"mkv");
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


    // ---- Zieldateigroesse und FLAC ----
    {
        using conv::AudioFmt;
        // Rechenhilfen
        CHECK(conv::EncoderKbpsForSize(AudioFmt::M4a, 130000, 10) == 96);            // 10 s: 130 KB erlauben ~99 kbit/s -> Stufe 96
        CHECK(conv::EncoderKbpsForSize(AudioFmt::M4a, 400000, 10) == 192);           // reichlich Platz: hoechste Stufe
        CHECK(conv::EncoderKbpsForSize(AudioFmt::M4a, 50000, 10) == 0);              // passt nicht
        CHECK(conv::EncoderKbpsForSize(AudioFmt::Mp3, 200000, 10) == 128);
        CHECK(conv::EncoderKbpsForSize(AudioFmt::Mp3, 100000, 10) == 0);
        CHECK(conv::EncoderKbpsForSize(AudioFmt::Wav, 1000000, 10) == -1 && conv::EncoderKbpsForSize(AudioFmt::Flac, 1000000, 10) == -1);
        CHECK(conv::EncoderKbpsForSize(AudioFmt::M4a, 1000000, 0) == 0);
        conv::VideoPlan big = conv::PlanVideoForSize(1920, 1080, 30, 600, 25ll * 1024 * 1024, 8000, 192, true);   // 10 Minuten 1080p in 25 MB
        CHECK(big.feasible && big.audioKbps == 96 && big.videoKbps >= 100 && big.videoKbps < 300);
        CHECK(big.width < 1920 && big.height < 1080 && big.width % 2 == 0 && big.height % 2 == 0 && big.width >= 160);   // Aufloesung wird gesenkt
        CHECK(big.width * 1080 == (big.height * 1920) || std::abs(big.width * 1080.0 - big.height * 1920.0) < 1920.0 * 4);   // Seitenverhaeltnis bleibt
        conv::VideoPlan roomy = conv::PlanVideoForSize(1280, 720, 30, 20, 25ll * 1024 * 1024, 4000, 128, true);    // 20 s, viel Platz: Regler gilt, keine Verkleinerung
        CHECK(roomy.feasible && roomy.width == 1280 && roomy.height == 720 && roomy.videoKbps == 4000 && roomy.audioKbps == 128);
        CHECK(!conv::PlanVideoForSize(1920, 1080, 30, 36000, 1024 * 1024, 4000, 128, true).feasible);              // 10 Stunden in 1 MB
        CHECK(conv::PlanVideoForSize(640, 480, 30, 60, 5ll * 1024 * 1024, 2000, 128, false).audioKbps == 0);       // ohne Ton keine Tonrate
        CHECK(!conv::PlanVideoForSize(640, 480, 30, 0, 1000, 2000, 128, true).feasible);
        const std::vector<int> e0 = conv::ImageEdgeSteps(0), e1 = conv::ImageEdgeSteps(1280);
        CHECK(e0.size() == 11 && e0[0] == 0 && e0[1] == 2560 && e0.back() == 240);
        CHECK(e1.size() == 7 && e1[0] == 1280 && e1[1] == 1024 && e1.back() == 240);                                // nur kleinere Stufen

        // Bild mit Zielgroesse: Rauschen laesst JPEG gross werden
        const std::wstring noisy = Tmp(L"noisy.bmp");
        {
            const int w = 640, h = 480, row = w * 3;
            std::vector<BYTE> d(54 + row * h, 0);
            d[0] = 'B'; d[1] = 'M'; Put(d, 2, (unsigned)d.size(), 4); Put(d, 10, 54, 4); Put(d, 14, 40, 4); Put(d, 18, w, 4); Put(d, 22, h, 4);
            d[26] = 1; d[28] = 24; Put(d, 34, row * h, 4);
            unsigned r = 12345;
            for (size_t i = 54; i < d.size(); ++i) { r = r * 1664525u + 1013904223u; d[i] = (BYTE)(r >> 24); }
            HANDLE f = CreateFileW(noisy.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            DWORD wr; WriteFile(f, d.data(), (DWORD)d.size(), &wr, nullptr); CloseHandle(f);
        }
        const std::wstring full = Tmp(L"noisy_full.jpg"), limited = Tmp(L"noisy_limit.jpg"), tiny = Tmp(L"noisy_tiny.jpg"), png = Tmp(L"noisy_limit.png");
        DeleteFileW(full.c_str()); DeleteFileW(limited.c_str()); DeleteFileW(tiny.c_str()); DeleteFileW(png.c_str());
        CHECK(conv::ConvertImage(noisy, full, conv::ImageFmt::Jpeg, 90, &err));
        const long long fullSize = Size(full);
        CHECK(fullSize > 150000);
        CHECK(conv::ConvertImage(noisy, limited, conv::ImageFmt::Jpeg, 90, &err, 0, 100000));     // unter 100 KB gedrueckt
        CHECK(Size(limited) > 0 && Size(limited) <= 100000);
        CHECK(Size(limited) > 40000);                                                              // nicht unnoetig stark verkleinert (hoechste passende Stufe)
        CHECK(!conv::ConvertImage(noisy, tiny, conv::ImageFmt::Jpeg, 90, &err, 0, 600) && !err.empty() && Size(tiny) < 0);   // unmoeglich: Fehler, keine Datei
        std::wprintf(L"Zielgroesse Bild: %lld -> %lld Bytes; Fehlertext bei 600 Bytes: %ls\n", fullSize, Size(limited), err.c_str());
        CHECK(conv::ConvertImage(noisy, png, conv::ImageFmt::Png, 90, &err, 0, 250000));          // PNG: nur ueber kleinere Bildkante
        CHECK(Size(png) > 0 && Size(png) <= 250000);
        const long long loose = 10ll * 1024 * 1024;                                                // grosszuegige Grenze aendert nichts am Regler
        const std::wstring same = Tmp(L"noisy_loose.jpg");
        DeleteFileW(same.c_str());
        CHECK(conv::ConvertImage(noisy, same, conv::ImageFmt::Jpeg, 90, &err, 0, loose));
        CHECK(Size(same) == fullSize);

        // Audio: 10 s Sinuston
        std::atomic<bool> noCancel{false};
        const std::wstring wav10 = Tmp(L"sine10.wav");
        WriteSineWav(wav10, 10);
        const std::wstring a1 = Tmp(L"sized.m4a"), a2 = Tmp(L"sized.mp3"), a3 = Tmp(L"sized_fail.m4a"), a4 = Tmp(L"sized.wav");
        for (const auto& f : {a1, a2, a3, a4}) DeleteFileW(f.c_str());
        CHECK(conv::ConvertMedia(wav10, a1, AudioFmt::M4a, 100, noCancel, nullptr, &err, 130000));
        CHECK(Size(a1) > 0 && Size(a1) <= 130000);
        CHECK(conv::ConvertMedia(wav10, a2, AudioFmt::Mp3, 100, noCancel, nullptr, &err, 200000));
        CHECK(Size(a2) > 0 && Size(a2) <= 200000);
        CHECK(!conv::ConvertMedia(wav10, a3, AudioFmt::M4a, 100, noCancel, nullptr, &err, 50000) && !err.empty() && Size(a3) < 0);
        CHECK(!conv::ConvertMedia(wav10, a4, AudioFmt::Wav, 100, noCancel, nullptr, &err, 500000) && !err.empty() && Size(a4) < 0);

        // FLAC: verlustfrei, Datei beginnt mit "fLaC", ist kleiner als die WAV-Quelle und laesst sich wieder lesen
        const std::wstring fl = Tmp(L"sine10.flac"), back = Tmp(L"sine10_back.wav");
        DeleteFileW(fl.c_str()); DeleteFileW(back.c_str());
        const bool flacOk = conv::ConvertMedia(wav10, fl, AudioFmt::Flac, 100, noCancel, nullptr, &err);
        std::wprintf(L"FLAC: %ls %ls (%lld Bytes, WAV %lld)\n", flacOk ? L"ok" : L"nicht moeglich", flacOk ? L"" : err.c_str(), Size(fl), Size(wav10));
        CHECK(flacOk);
        if (flacOk) {
            CHECK(Size(fl) > 1000 && Size(fl) < Size(wav10));
            char magic[4] = {};
            HANDLE h = CreateFileW(fl.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            DWORD rd = 0; ReadFile(h, magic, 4, &rd, nullptr); CloseHandle(h);
            CHECK(memcmp(magic, "fLaC", 4) == 0);
            CHECK(conv::ConvertMedia(fl, back, AudioFmt::Wav, 100, noCancel, nullptr, &err));
            CHECK(Size(back) > 1700000 && Size(back) < 1900000);                                   // 10 s 44,1 kHz Stereo 16 Bit = ~1,76 MB
        }
        CHECK(std::wstring(conv::AudioExt(AudioFmt::Flac)) == L"flac" && conv::BitrateKbps(AudioFmt::Flac, 50) == 0);

        // Video: Zielgroesse (Testvideo 320x240, 1 s)
        const std::wstring v1 = Tmp(L"sized_vid.mp4"), v2 = Tmp(L"sized_vid.mkv"), v3 = Tmp(L"unsized_vid.mp4");
        for (const auto& f : {v1, v2, v3}) DeleteFileW(f.c_str());
        const std::wstring src = Tmp(L"vid_for_size.mp4");
        DeleteFileW(src.c_str());
        if (MakeTestVideo(src)) {
            CHECK(conv::ConvertMedia(src, v3, AudioFmt::Mp4, 90, noCancel, nullptr, &err));
            const long long unsized = Size(v3);
            const long long limit = unsized * 4 / 5;
            CHECK(conv::ConvertMedia(src, v1, AudioFmt::Mp4, 90, noCancel, nullptr, &err, limit));
            std::wprintf(L"Zielgroesse Video: ohne Grenze %lld Bytes, Grenze %lld -> %lld Bytes (%ls)\n", unsized, limit, Size(v1), err.c_str());
            CHECK(Size(v1) > 0 && Size(v1) <= limit);
            CHECK(conv::ConvertMedia(src, v2, AudioFmt::Mkv, 90, noCancel, nullptr, &err, limit));
            CHECK(Size(v2) > 0 && Size(v2) <= limit);
            CHECK(!conv::ConvertMedia(src, Tmp(L"sized_fail.mp4"), AudioFmt::Mp4, 90, noCancel, nullptr, &err, 3000) && !err.empty() && Size(Tmp(L"sized_fail.mp4")) < 0);
        }
    }

    conv::MediaShutdown();
    if (failures == 0) std::printf("alle Tests bestanden\n");
    return failures ? 1 : 0;
}
