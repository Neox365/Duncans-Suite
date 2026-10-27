// Test der QuickConvert-Engine (WIC + Media Foundation). Nur Windows.
// g++ -std=c++17 -DUNICODE -D_UNICODE -I.. ConvertTest.cpp ../Convert.cpp -lwindowscodecs -lmfplat -lmfreadwrite -lmfuuid -lole32 -loleaut32 -luuid
#include <windows.h>
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

    conv::MediaShutdown();
    if (failures == 0) std::printf("alle Tests bestanden\n");
    return failures ? 1 : 0;
}
