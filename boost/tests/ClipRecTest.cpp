// Test der Video-Clip-Aufnahme (Bildschirm -> Abschnitte -> MP4). Braucht einen Bildschirm und einen H.264-Kodierer; baut mit MSVC:
//   cl /std:c++17 /EHsc /DUNICODE /D_UNICODE ClipRecTest.cpp ..\ClipRec.cpp /link mfplat.lib mfreadwrite.lib mfuuid.lib d3d11.lib dxgi.lib shell32.lib ole32.lib user32.lib
// Aufruf: ClipRecTest.exe [Ausgabeordner]   (schreibt zusaetzlich frame.bmp mit einem Bild aus dem Clip zur Sichtkontrolle)
#include "../ClipRec.h"
#include "../ClipAudio.h"
#include <algorithm>
#include <cmath>
#include <thread>

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

    {   // Ton-Hilfen
        const float mono[3] = { 0.5f, -0.25f, 1.0f };
        std::vector<float> st;
        ToStereo(mono, 3, 1, st);
        CHECK(st.size() == 6 && st[0] == 0.5f && st[1] == 0.5f && st[2] == -0.25f && st[5] == 1.0f);
        const float six[6] = { 0.2f, 0.4f, 1.0f, 0.9f, 0.9f, 0.9f };               // L R C LFE Ls Rs
        st.clear();
        ToStereo(six, 1, 6, st);
        CHECK(st.size() == 2 && std::fabs(st[0] - 0.9f) < 1e-5 && std::fabs(st[1] - 1.1f) < 1e-5);
        std::vector<float> in(960, 0.0f), out;
        for (int i = 0; i < 480; ++i) { in[i * 2] = static_cast<float>(i); in[i * 2 + 1] = static_cast<float>(i); }
        Resampler rs;
        rs.Process(in.data(), 480, kAudioRate, out);
        CHECK(out.size() == 960 && out[10] == 5.0f);                                // gleiche Rate: unveraendert
        Resampler r2;
        out.clear();
        for (int b = 0; b < 10; ++b) r2.Process(in.data(), 480, 24000, out);       // 24 kHz -> 48 kHz: etwa doppelt so viele Frames
        CHECK(out.size() / 2 > 9500 && out.size() / 2 < 9700);
        Resampler r3;
        out.clear();
        for (int b = 0; b < 10; ++b) r3.Process(in.data(), 480, 96000, out);       // 96 kHz -> 48 kHz: etwa halb so viele
        CHECK(out.size() / 2 > 2350 && out.size() / 2 < 2450);
        float a[4] = { 0.8f, -0.8f, 0.1f, 0.0f };
        const float bb[4] = { 0.5f, -0.5f, 0.1f, 0.2f };
        MixInto(a, bb, 4, 1.0f);
        CHECK(a[0] == 1.0f && a[1] == -1.0f && std::fabs(a[2] - 0.2f) < 1e-6 && std::fabs(a[3] - 0.2f) < 1e-6);
        short pcm[4];
        const float f4[4] = { 1.5f, -1.5f, 0.5f, 0.0f };
        FloatToPcm16(f4, 4, pcm);
        CHECK(pcm[0] == 32767 && pcm[1] == -32767 && pcm[2] == 16384 && pcm[3] == 0);
    }
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    const std::wstring out = std::wstring(tmp) + L"clipreptest_out";
    const std::wstring seg = std::wstring(tmp) + L"clipreptest_seg";
    Options o; o.seconds = 10; o.segmentSeconds = 2; o.fps = 30; o.audio = kAudioSystem; o.outDir = out; o.tempDir = seg;
    if (argc > 2) o.audio = 0;                                              // zweiter Parameter: ohne Ton (kein Piepen)
    Recorder rec;
    std::wstring err;
    if (!rec.Start(o, err)) { std::printf("Aufnahme nicht moeglich: %ls\n", err.c_str()); std::printf("%d Pruefungen, %d Fehler (Aufnahme uebersprungen)\n", g_n, g_fail); return g_fail ? 1 : 0; }
    CHECK(rec.Running());
    std::wstring none, e0;
    Sleep(500);
    std::thread tone([&]() { if (!o.audio) return; Sleep(1500); Beep(880, 1800); Sleep(500); Beep(1320, 1200); });     // hoerbarer Ton fuer die Systemton-Aufnahme
    for (int i = 0; i < 70; ++i) { Sleep(100); if (i % 10 == 0) SetCursorPos(100 + i * 5, 200); }      // 7 s, mit etwas Bewegung auf dem Bildschirm
    tone.join();
    std::wstring file;
    const bool saved = rec.Save(file, err);
    if (!saved) std::printf("Speichern: %ls\n", err.c_str());
    CHECK(saved);
    CHECK(rec.FramesWritten() > 100);
    rec.Stop();
    CHECK(!rec.Running());
    if (saved) {
        WIN32_FILE_ATTRIBUTE_DATA a{};
        CHECK(GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &a) && a.nFileSizeLow > 20000);
        MFStartup(MF_VERSION);
        {   // Diagnose: Spuren der fertigen Datei
            ComPtr<IMFSourceReader> sr; MFCreateSourceReaderFromURL(file.c_str(), nullptr, &sr);
            int vf = 0, af = 0; LONGLONG vEnd = 0, aEnd = 0;
            if (sr) {
                sr->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, TRUE);
                int open = 2;
                for (;;) {
                    DWORD idx = 0, fl = 0; LONGLONG ts = 0; ComPtr<IMFSample> sm;
                    if (FAILED(sr->ReadSample(MF_SOURCE_READER_ANY_STREAM, 0, &idx, &fl, &ts, &sm))) break;
                    if (fl & MF_SOURCE_READERF_ENDOFSTREAM) { if (--open <= 0) break; continue; }
                    if (!sm) continue;
                    ComPtr<IMFMediaType> mt; sr->GetNativeMediaType(idx, 0, &mt); GUID mj{}; if (mt) mt->GetGUID(MF_MT_MAJOR_TYPE, &mj);
                    if (mj == MFMediaType_Video) { ++vf; vEnd = ts; } else { ++af; aEnd = ts; }
                }
            }
            std::printf("  fertige Datei: Videopakete %d (bis %.2f s), Tonpakete %d (bis %.2f s)\n", vf, vEnd / 1e7, af, aEnd / 1e7);
        }
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
            {   // Tonspur pruefen: vorhanden, Dauer ungefaehr wie das Bild, hoerbarer Ausschlag (Piepton)
                ComPtr<IMFSourceReader> ar;
                CHECK(SUCCEEDED(MFCreateSourceReaderFromURL(file.c_str(), ra.Get(), &ar)));
                ComPtr<IMFMediaType> pcmWant;
                MFCreateMediaType(&pcmWant);
                pcmWant->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
                pcmWant->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
                ar->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
                ar->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
                const HRESULT hr = ar->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pcmWant.Get());
                CHECK(SUCCEEDED(hr));
                double secs = 0; int peak = 0;
                if (SUCCEEDED(hr)) {
                    ComPtr<IMFMediaType> cm; ar->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &cm);
                    const UINT32 ch = MFGetAttributeUINT32(cm.Get(), MF_MT_AUDIO_NUM_CHANNELS, 2), sps = MFGetAttributeUINT32(cm.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
                    unsigned long long frames = 0;
                    for (;;) {
                        DWORD fl = 0; ComPtr<IMFSample> sm;
                        if (FAILED(ar->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &fl, nullptr, &sm)) || (fl & MF_SOURCE_READERF_ENDOFSTREAM)) break;
                        if (!sm) continue;
                        ComPtr<IMFMediaBuffer> bf; sm->ConvertToContiguousBuffer(&bf);
                        BYTE* d = nullptr; DWORD len = 0;
                        if (bf && SUCCEEDED(bf->Lock(&d, nullptr, &len))) {
                            const short* sp = reinterpret_cast<const short*>(d);
                            for (DWORD i = 0; i < len / 2; ++i) peak = std::max(peak, std::abs(static_cast<int>(sp[i])));
                            frames += len / (2 * ch);
                            bf->Unlock();
                        }
                    }
                    secs = static_cast<double>(frames) / sps;
                }
                std::printf("Ton: %.2f s, Spitzenwert %d\n", secs, peak);
                CHECK(!o.audio || (secs > 5.5 && secs < 10.8));
                CHECK(!o.audio || peak > 1500);                                                    // der Piepton ist zu hoeren
            }
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
