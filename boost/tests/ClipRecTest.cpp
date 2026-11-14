// Test der Video-Clip-Aufnahme (Bildschirm -> Abschnitte -> MP4). Braucht einen Bildschirm und einen H.264-Kodierer; baut mit MSVC:
//   cl /std:c++17 /EHsc /DUNICODE /D_UNICODE ClipRecTest.cpp ..\ClipRec.cpp /link mfplat.lib mfreadwrite.lib mfuuid.lib d3d11.lib dxgi.lib shell32.lib ole32.lib user32.lib
// Aufruf: ClipRecTest.exe [Ausgabeordner]   (schreibt zusaetzlich frame.bmp mit einem Bild aus dem Clip zur Sichtkontrolle)
#include "../ClipRec.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <cstdio>
#include <vector>

using Microsoft::WRL::ComPtr;
static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    using namespace cliprec;
    CHECK(SegmentsToKeep(30, 10) == 4 && SegmentsToKeep(5, 10) == 2 && SegmentsToKeep(999, 10) == 13);
    CHECK(AutoBitrateKbps(1920, 1080, 30) > 5000 && AutoBitrateKbps(320, 240, 30) == 3000 && AutoBitrateKbps(7680, 4320, 60) == 25000);
    SYSTEMTIME st{}; st.wYear = 2026; st.wMonth = 11; st.wDay = 4; st.wHour = 9; st.wMinute = 5; st.wSecond = 3;
    CHECK(ClipFileName(st) == L"Clip_2026-11-04_09-05-03.mp4");

    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    const std::wstring out = std::wstring(tmp) + L"clipreptest_out";
    const std::wstring seg = std::wstring(tmp) + L"clipreptest_seg";
    Options o; o.seconds = 10; o.segmentSeconds = 2; o.fps = 30; o.outDir = out; o.tempDir = seg;
    Recorder rec;
    std::wstring err;
    if (!rec.Start(o, err)) { std::wprintf(L"Aufnahme nicht moeglich: %s\n", err.c_str()); std::printf("%d Pruefungen, %d Fehler (Aufnahme uebersprungen)\n", g_n, g_fail); return g_fail ? 1 : 0; }
    CHECK(rec.Running());
    std::wstring none, e0;
    Sleep(500);
    for (int i = 0; i < 70; ++i) { Sleep(100); if (i % 10 == 0) SetCursorPos(100 + i * 5, 200); }      // 7 s, mit etwas Bewegung auf dem Bildschirm
    std::wstring file;
    const bool saved = rec.Save(file, err);
    if (!saved) std::wprintf(L"Speichern: %s\n", err.c_str());
    CHECK(saved);
    CHECK(rec.FramesWritten() > 100);
    rec.Stop();
    CHECK(!rec.Running());
    if (saved) {
        WIN32_FILE_ATTRIBUTE_DATA a{};
        CHECK(GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &a) && a.nFileSizeLow > 20000);
        MFStartup(MF_VERSION);
        ComPtr<IMFSourceReader> r;
        ComPtr<IMFAttributes> ra; MFCreateAttributes(&ra, 1); ra->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        CHECK(SUCCEEDED(MFCreateSourceReaderFromURL(file.c_str(), ra.Get(), &r)));
        if (r) {
            PROPVARIANT dur; PropVariantInit(&dur);
            if (SUCCEEDED(r->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &dur))) {
                const double sec = static_cast<double>(dur.uhVal.QuadPart) / 1e7;
                std::printf("Dauer: %.2f s, Dateigroesse: %lu KB\n", sec, a.nFileSizeLow / 1024);
                CHECK(sec > 5.5 && sec < 10.5);
            } else CHECK(false);
            // ein Bild dekodieren und als BMP ablegen
            ComPtr<IMFMediaType> want;
            MFCreateMediaType(&want);
            want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            want->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            CHECK(SUCCEEDED(r->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, want.Get())));
            ComPtr<IMFMediaType> cur;
            r->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur);
            UINT32 w = 0, h = 0;
            MFGetAttributeSize(cur.Get(), MF_MT_FRAME_SIZE, &w, &h);
            LONG stride = static_cast<LONG>(MFGetAttributeUINT32(cur.Get(), MF_MT_DEFAULT_STRIDE, w * 4));
            int count = 0;
            std::vector<BYTE> lastFrame;
            for (;;) {
                DWORD flags = 0; ComPtr<IMFSample> s;
                if (FAILED(r->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, nullptr, &s)) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) break;
                if (!s) continue;
                ++count;
                if (count == 60) {
                    ComPtr<IMFMediaBuffer> b; s->ConvertToContiguousBuffer(&b);
                    BYTE* d = nullptr; DWORD len = 0;
                    if (b && SUCCEEDED(b->Lock(&d, nullptr, &len))) { lastFrame.assign(d, d + len); b->Unlock(); }
                }
            }
            std::printf("Bilder im Clip: %d, %ux%u, Stride %ld\n", count, w, h, stride);
            CHECK(count > 150 && count < 330);
            if (lastFrame.size() >= static_cast<size_t>(w) * h * 4) {
                bool dark = true;
                for (size_t i = 0; i < lastFrame.size(); i += 4097) if (lastFrame[i] > 20) { dark = false; break; }
                CHECK(!dark);
                const std::string path = argc > 1 ? std::string(argv[1]) + "\\frame.bmp" : "frame.bmp";
                FILE* f = fopen(path.c_str(), "wb");
                if (f) {
                    const bool bottomUp = stride < 0;
                    const int rowBytes = static_cast<int>(w) * 4;
                    BITMAPFILEHEADER fh{}; BITMAPINFOHEADER ih{};
                    ih.biSize = sizeof(ih); ih.biWidth = static_cast<LONG>(w); ih.biHeight = -static_cast<LONG>(h); ih.biPlanes = 1; ih.biBitCount = 32;
                    fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + rowBytes * h;
                    fwrite(&fh, sizeof(fh), 1, f); fwrite(&ih, sizeof(ih), 1, f);
                    for (UINT32 y = 0; y < h; ++y) fwrite(lastFrame.data() + static_cast<size_t>(bottomUp ? h - 1 - y : y) * rowBytes, rowBytes, 1, f);
                    fclose(f);
                    std::printf("Beispielbild: %s\n", path.c_str());
                }
            }
        }
        MFShutdown();
        DeleteFileW(file.c_str());
    }
    RemoveDirectoryW(out.c_str());
    RemoveDirectoryW(seg.c_str());
    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
