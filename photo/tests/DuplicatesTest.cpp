// Test fuer Duplicates.h und ReadQuickMeta/ParseExifDate (nur Windows, braucht WIC).
// g++ -std=c++17 -I.. DuplicatesTest.cpp ../ImageOps.cpp -lwindowscodecs -lole32 -loleaut32 -luuid -lpropsys -o t.exe
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdio>
#include <string>
#include <vector>

#include "../Duplicates.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static void WriteBmp(const std::wstring& path, int w, int h, unsigned char (*px)(int, int, int, int)) {
    const int row = (w * 3 + 3) & ~3;
    std::vector<unsigned char> d(54 + row * h, 0);
    auto put32 = [&](int o, unsigned v) { for (int i = 0; i < 4; ++i) d[o + i] = (unsigned char)(v >> (8 * i)); };
    d[0] = 'B'; d[1] = 'M';
    put32(2, (unsigned)d.size()); put32(10, 54); put32(14, 40); put32(18, w); put32(22, h);
    d[26] = 1; d[28] = 24; put32(34, row * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned char v = px(x, y, w, h);
            unsigned char* p = &d[54 + (h - 1 - y) * row + x * 3];
            p[0] = p[1] = p[2] = v;
        }
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD wr;
    WriteFile(f, d.data(), (DWORD)d.size(), &wr, nullptr);
    CloseHandle(f);
}

// 8x8 Bloecke mit festem, unregelmaessigem Muster (unabhaengig von der Bildgroesse)
static unsigned char Blocks(int x, int y, int w, int h) {
    const int bx = x * 8 / w, by = y * 8 / h;
    return (unsigned char)(((bx * 73 + by * 151 + (bx ^ by) * 29) * 37) % 256);
}
static unsigned char Checker(int x, int y, int, int) { return (unsigned char)((((x / 4) + (y / 4)) & 1) ? 255 : 0); }
static unsigned char Flat(int, int, int, int) { return 128; }
static unsigned char BlocksDim(int x, int y, int w, int h) { return (unsigned char)(Blocks(x, y, w, h) / 4); }  // gleiche Struktur, viel dunkler

// JPEG schreiben (WIC), damit der Pfad mit DCT-Verkleinerung beim Dekodieren mitgetestet wird
static bool WriteJpeg(const std::wstring& path, int w, int h, unsigned char (*px)(int, int, int, int)) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> f;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> fr;
    ComPtr<IPropertyBag2> props;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) ||
        FAILED(f->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(f->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &enc)) ||
        FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache)) || FAILED(enc->CreateNewFrame(&fr, &props)) ||
        FAILED(fr->Initialize(props.Get())) || FAILED(fr->SetSize(w, h)))
        return false;
    WICPixelFormatGUID pf = GUID_WICPixelFormat24bppBGR;
    fr->SetPixelFormat(&pf);
    std::vector<BYTE> buf((size_t)w * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) buf[((size_t)y * w + x) * 3] = buf[((size_t)y * w + x) * 3 + 1] = buf[((size_t)y * w + x) * 3 + 2] = px(x, y, w, h);
    return SUCCEEDED(fr->WritePixels(h, w * 3, (UINT)buf.size(), buf.data())) && SUCCEEDED(fr->Commit()) &&
           SUCCEEDED(enc->Commit());
}

int main() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ULONGLONG t = 0;
    CHECK(imgops::ParseExifDate(L"2024:05:17 13:45:00", t) && t > 0);
    ULONGLONG t2 = 0;
    CHECK(imgops::ParseExifDate(L"2024:05:17 13:45:01", t2) && t2 == t + 10000000ull);
    CHECK(!imgops::ParseExifDate(L"0000:00:00 00:00:00", t2));
    CHECK(!imgops::ParseExifDate(L"kaputt", t2));
    CHECK(imgops::HammingDistance(0, 0) == 0 && imgops::HammingDistance(0xFF, 0) == 8 && imgops::HammingDistance(~0ull, 0) == 64);

    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"dupTest" + std::to_wstring(GetCurrentProcessId()) + L"\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    WriteBmp(dir + L"a.bmp", 64, 64, Blocks);
    CopyFileW((dir + L"a.bmp").c_str(), (dir + L"b.bmp").c_str(), FALSE);
    WriteBmp(dir + L"c.bmp", 128, 128, Blocks);   // gleiches Motiv, andere Groesse -> aehnlich
    WriteBmp(dir + L"d.bmp", 64, 64, Checker);    // anderes Bild
    WriteBmp(dir + L"e.bmp", 64, 64, Flat);       // einfarbig: nie als aehnlich werten
    WriteBmp(dir + L"f.bmp", 32, 32, Flat);
    WriteBmp(dir + L"g.bmp", 64, 64, BlocksDim);

    std::vector<dup::File> files;
    for (const wchar_t* n : {L"a.bmp", L"b.bmp", L"c.bmp", L"d.bmp", L"e.bmp", L"f.bmp"}) {
        WIN32_FILE_ATTRIBUTE_DATA fa{};
        GetFileAttributesExW((dir + n).c_str(), GetFileExInfoStandard, &fa);
        files.push_back({dir + n, ((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow});
    }
    dup::Progress pr;
    auto groups = dup::Find(files, pr);
    CHECK(groups.size() == 1);
    if (groups.size() == 1) {
        auto& g = groups[0];
        CHECK(g.members.size() == 3 && g.members[0] == 0 && g.members[1] == 1 && g.members[2] == 2);
        CHECK(!g.exact);  // c ist nur aehnlich
    }

    // nur a und b: identisch
    std::vector<dup::File> two = {files[0], files[1], files[3]};
    dup::Progress pr2;
    auto g2 = dup::Find(two, pr2);
    CHECK(g2.size() == 1 && g2[0].exact && g2[0].members.size() == 2);

    // gleiche Struktur, aber ganz andere Helligkeit/Farbe: nicht aehnlich (der dHash allein wuerde sie verwechseln)
    {
        imgops::ImageSig sa, sg;
        CHECK(imgops::ComputeSignature(dir + L"a.bmp", sa) && imgops::ComputeSignature(dir + L"g.bmp", sg));
        CHECK(imgops::HammingDistance(sa.dhash, sg.dhash) <= dup::kSimilarMaxDistance);
        CHECK(!imgops::SignaturesSimilar(sa, sg, dup::kSimilarMaxDistance, dup::kSimilarMaxColorDiff));
        std::vector<dup::File> pair = {files[0], {dir + L"g.bmp", 0}};
        WIN32_FILE_ATTRIBUTE_DATA fa{};
        GetFileAttributesExW(pair[1].path.c_str(), GetFileExInfoStandard, &fa);
        pair[1].size = ((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
        dup::Progress prp;
        CHECK(dup::Find(pair, prp).empty());
    }

    // Abbruch liefert nichts
    dup::Progress pr3;
    pr3.cancel = true;
    CHECK(dup::Find(files, pr3).empty());

    // JPEG: grosse (DCT-Verkleinerung) und kleine Fassung desselben Motivs haben aehnliche Hashes, auch zur BMP-Fassung
    CHECK(WriteJpeg(dir + L"j1.jpg", 512, 512, Blocks));
    CHECK(WriteJpeg(dir + L"j2.jpg", 128, 128, Blocks));
    imgops::ImageSig sj1, sj2, sa;
    CHECK(imgops::ComputeSignature(dir + L"j1.jpg", sj1) && imgops::ComputeSignature(dir + L"j2.jpg", sj2) &&
          imgops::ComputeSignature(dir + L"a.bmp", sa));
    CHECK(imgops::SignaturesSimilar(sj1, sj2, dup::kSimilarMaxDistance, dup::kSimilarMaxColorDiff));
    CHECK(imgops::SignaturesSimilar(sj1, sa, dup::kSimilarMaxDistance, dup::kSimilarMaxColorDiff));
    DeleteFileW((dir + L"j1.jpg").c_str());
    DeleteFileW((dir + L"j2.jpg").c_str());

    imgops::QuickMeta qm;
    CHECK(imgops::ReadQuickMeta(dir + L"a.bmp", qm) && qm.taken == 0 && !qm.hasGps);

    for (const wchar_t* n : {L"a.bmp", L"b.bmp", L"c.bmp", L"d.bmp", L"e.bmp", L"f.bmp", L"g.bmp"}) DeleteFileW((dir + n).c_str());
    RemoveDirectoryW(dir.c_str());
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures ? 1 : 0;
}
