#include "ClipRec.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shlobj.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace cliprec {

int SegmentsToKeep(int seconds, int segmentSeconds)
{
    seconds = std::clamp(seconds, kMinSeconds, kMaxSeconds);
    segmentSeconds = std::max(1, segmentSeconds);
    return (seconds + segmentSeconds - 1) / segmentSeconds + 1;
}

int AutoBitrateKbps(int width, int height, int fps)
{
    const double bits = static_cast<double>(width) * height * std::max(1, fps) * 0.1;
    return std::clamp(static_cast<int>(bits / 1000.0), 3000, 25000);
}

std::wstring ClipFileName(const SYSTEMTIME& t)
{
    wchar_t b[64];
    swprintf(b, 64, L"Clip_%04d-%02d-%02d_%02d-%02d-%02d.mp4", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return b;
}

std::wstring DefaultClipDir()
{
    wchar_t p[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_MYVIDEO, nullptr, SHGFP_TYPE_CURRENT, p)) || !p[0])
        return L"";
    return std::wstring(p) + L"\\Boost-Clips";
}

namespace {

constexpr LONGLONG kSecond = 10000000LL;     // Media-Foundation-Zeit: 100 ns

std::wstring TempBase()
{
    wchar_t t[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, t);
    return std::wstring(t) + L"DuncansBoostClips";
}

void MakeDirs(const std::wstring& dir) { SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr); }

// Ein laufender Abschnitt: Sink-Writer, der Rohbilder (RGB32) in H.264/MP4 verwandelt
struct SegmentWriter {
    ComPtr<IMFSinkWriter> w;
    std::wstring path;
    LONGLONG start = -1;       // Zeit des ersten Bildes
    bool ok = false;
};

bool OpenSegment(SegmentWriter& s, const std::wstring& path, UINT w, UINT h, int fps, int kbps)
{
    s = SegmentWriter();
    s.path = path;
    ComPtr<IMFAttributes> at;
    if (FAILED(MFCreateAttributes(&at, 4))) return false;
    at->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    at->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    if (FAILED(MFCreateSinkWriterFromURL(path.c_str(), nullptr, at.Get(), &s.w))) return false;

    ComPtr<IMFMediaType> out;
    if (FAILED(MFCreateMediaType(&out))) return false;
    out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    out->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(kbps) * 1000);
    out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    out->SetUINT32(MF_MT_MPEG2_PROFILE, 77);                 // eAVEncH264VProfile_Main
    MFSetAttributeSize(out.Get(), MF_MT_FRAME_SIZE, w, h);
    MFSetAttributeRatio(out.Get(), MF_MT_FRAME_RATE, static_cast<UINT32>(fps), 1);
    MFSetAttributeRatio(out.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    DWORD stream = 0;
    if (FAILED(s.w->AddStream(out.Get(), &stream))) return false;

    ComPtr<IMFMediaType> in;
    if (FAILED(MFCreateMediaType(&in))) return false;
    in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    in->SetUINT32(MF_MT_DEFAULT_STRIDE, w * 4);              // positiv: Bild liegt von oben nach unten
    MFSetAttributeSize(in.Get(), MF_MT_FRAME_SIZE, w, h);
    MFSetAttributeRatio(in.Get(), MF_MT_FRAME_RATE, static_cast<UINT32>(fps), 1);
    MFSetAttributeRatio(in.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(s.w->SetInputMediaType(stream, in.Get(), nullptr))) return false;
    if (FAILED(s.w->BeginWriting())) return false;
    s.ok = true;
    return true;
}

bool WriteFrame(SegmentWriter& s, const std::vector<BYTE>& bgra, UINT w, UINT h, LONGLONG now, LONGLONG frameDur)
{
    if (!s.ok) return false;
    if (s.start < 0) s.start = now;
    const DWORD len = w * h * 4;
    ComPtr<IMFMediaBuffer> buf;
    if (FAILED(MFCreateMemoryBuffer(len, &buf))) return false;
    BYTE* dst = nullptr;
    if (FAILED(buf->Lock(&dst, nullptr, nullptr))) return false;
    std::memcpy(dst, bgra.data(), len);
    buf->Unlock();
    buf->SetCurrentLength(len);
    ComPtr<IMFSample> smp;
    if (FAILED(MFCreateSample(&smp))) return false;
    smp->AddBuffer(buf.Get());
    smp->SetSampleTime(now - s.start);
    smp->SetSampleDuration(frameDur);
    return SUCCEEDED(s.w->WriteSample(0, smp.Get()));
}

void CloseSegment(SegmentWriter& s)
{
    if (s.w) s.w->Finalize();
    s.w.Reset();
    s.ok = false;
}

// Haengt fertige Abschnitte (H.264 unveraendert, ohne neu zu kodieren) zu einer Datei zusammen
bool Remux(const std::vector<std::wstring>& segs, const std::wstring& out, std::wstring& err)
{
    ComPtr<IMFSinkWriter> w;
    LONGLONG offset = 0;
    bool wrote = false;
    for (const std::wstring& path : segs) {
        ComPtr<IMFSourceReader> r;
        if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &r))) continue;   // unlesbarer Abschnitt: ueberspringen
        r->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
        r->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
        if (!w) {
            ComPtr<IMFMediaType> nat;
            if (FAILED(r->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &nat))) continue;
            ComPtr<IMFAttributes> at;
            MFCreateAttributes(&at, 1);
            if (at) at->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
            if (FAILED(MFCreateSinkWriterFromURL(out.c_str(), nullptr, at.Get(), &w))) { err = L"Die Zieldatei konnte nicht angelegt werden."; return false; }
            DWORD stream = 0;
            if (FAILED(w->AddStream(nat.Get(), &stream)) || FAILED(w->SetInputMediaType(stream, nat.Get(), nullptr)) || FAILED(w->BeginWriting())) {
                err = L"Der Videoabschnitt passt nicht zusammen.";
                return false;
            }
        }
        LONGLONG segEnd = 0;
        for (;;) {
            DWORD flags = 0;
            ComPtr<IMFSample> smp;
            if (FAILED(r->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, nullptr, &smp))) break;
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
            if (!smp) continue;
            LONGLONG t = 0, d = 0;
            smp->GetSampleTime(&t);
            smp->GetSampleDuration(&d);
            if (d <= 0) d = kSecond / 30;
            segEnd = std::max(segEnd, t + d);
            smp->SetSampleTime(t + offset);
            if (FAILED(w->WriteSample(0, smp.Get()))) { err = L"Beim Schreiben der Datei ist ein Fehler aufgetreten."; return false; }
            wrote = true;
        }
        offset += segEnd;
    }
    if (!w || !wrote) { err = L"Es ist noch nichts aufgenommen."; return false; }
    if (FAILED(w->Finalize())) { err = L"Die Datei konnte nicht abgeschlossen werden."; return false; }
    return true;
}

}  // namespace

struct Recorder::Impl {
    Options o;
    std::atomic<bool> stop{ false }, running{ false };
    std::thread th;
    std::mutex mu;                           // schuetzt segs, problem, pending, Speicher-Handshake
    std::deque<std::wstring> segs;           // fertige Abschnitte, aelteste zuerst
    std::vector<std::wstring> pendingDelete; // Abschnitte, die gerade gelesen werden und danach geloescht werden
    int saving = 0;
    std::wstring problem;
    std::atomic<unsigned> frames{ 0 };
    HANDLE saveReq = nullptr, saveDone = nullptr, ready = nullptr;
    std::wstring startErr;
    bool startOk = false;
    std::wstring tmp;
    unsigned segCounter = 0;
    UINT width = 0, height = 0;

    // Desktop Duplication
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<IDXGIOutputDuplication> dup;
    ComPtr<ID3D11Texture2D> staging;
    UINT stW = 0, stH = 0;

    void SetProblem(const std::wstring& p) { std::lock_guard<std::mutex> l(mu); problem = p; }

    bool InitDuplication(std::wstring& err)
    {
        dup.Reset(); staging.Reset(); ctx.Reset(); dev.Reset();
        ComPtr<IDXGIFactory1> fac;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(fac.GetAddressOf())))) { err = L"Die Grafikschnittstelle ist nicht verfuegbar."; return false; }
        HMONITOR primary = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        for (UINT a = 0;; ++a) {
            ComPtr<IDXGIAdapter1> ad;
            if (fac->EnumAdapters1(a, &ad) == DXGI_ERROR_NOT_FOUND) break;
            for (UINT i = 0;; ++i) {
                ComPtr<IDXGIOutput> out;
                if (ad->EnumOutputs(i, &out) == DXGI_ERROR_NOT_FOUND) break;
                DXGI_OUTPUT_DESC d{};
                out->GetDesc(&d);
                if (d.Monitor != primary) continue;
                ComPtr<ID3D11Device> device;
                ComPtr<ID3D11DeviceContext> context;
                D3D_FEATURE_LEVEL fl;
                if (FAILED(D3D11CreateDevice(ad.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, &fl, &context))) continue;
                ComPtr<IDXGIOutput1> o1;
                if (FAILED(out.As(&o1))) continue;
                ComPtr<IDXGIOutputDuplication> dd;
                const HRESULT hr = o1->DuplicateOutput(device.Get(), &dd);
                if (FAILED(hr)) { err = L"Der Bildschirm kann gerade nicht aufgenommen werden."; continue; }
                DXGI_OUTDUPL_DESC dd2{};
                dd->GetDesc(&dd2);
                dev = device; ctx = context; dup = dd;
                width = dd2.ModeDesc.Width & ~1u;
                height = dd2.ModeDesc.Height & ~1u;
                return true;
            }
        }
        if (err.empty()) err = L"Der Hauptbildschirm wurde nicht gefunden.";
        return false;
    }

    // holt (falls vorhanden) das neueste Bild in "last"; Rueckgabe: 1 neues Bild, 0 keines, -1 Duplication verloren
    int Grab(std::vector<BYTE>& last)
    {
        DXGI_OUTDUPL_FRAME_INFO info{};
        ComPtr<IDXGIResource> res;
        const HRESULT hr = dup->AcquireNextFrame(0, &info, &res);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) return 0;
        if (hr == DXGI_ERROR_ACCESS_LOST) return -1;
        if (FAILED(hr)) return -1;
        int result = 0;
        if (info.LastPresentTime.QuadPart != 0) {
            ComPtr<ID3D11Texture2D> tex;
            if (SUCCEEDED(res.As(&tex))) {
                D3D11_TEXTURE2D_DESC td{};
                tex->GetDesc(&td);
                if (!staging || stW != td.Width || stH != td.Height) {
                    td.Usage = D3D11_USAGE_STAGING;
                    td.BindFlags = 0;
                    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                    td.MiscFlags = 0;
                    staging.Reset();
                    if (SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &staging))) { stW = td.Width; stH = td.Height; }
                }
                if (staging) {
                    ctx->CopyResource(staging.Get(), tex.Get());
                    D3D11_MAPPED_SUBRESOURCE m{};
                    if (SUCCEEDED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) {
                        last.resize(static_cast<size_t>(width) * height * 4);
                        for (UINT y = 0; y < height && y < stH; ++y)
                            std::memcpy(last.data() + static_cast<size_t>(y) * width * 4, static_cast<const BYTE*>(m.pData) + static_cast<size_t>(y) * m.RowPitch, static_cast<size_t>(std::min(width, stW)) * 4);
                        ctx->Unmap(staging.Get(), 0);
                        result = 1;
                    }
                }
            }
        }
        dup->ReleaseFrame();
        return result;
    }

    std::wstring SegPath() { return tmp + L"\\seg_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(segCounter++) + L".mp4"; }

    void DeleteSegment(const std::wstring& p)
    {
        if (saving > 0) pendingDelete.push_back(p);
        else DeleteFileW(p.c_str());
    }

    void FlushPending()
    {
        if (saving == 0) {
            for (const std::wstring& p : pendingDelete) DeleteFileW(p.c_str());
            pendingDelete.clear();
        }
    }

    void FinishSegment(SegmentWriter& s)
    {
        const std::wstring path = s.path;
        const bool hadFrames = s.start >= 0;
        CloseSegment(s);
        std::lock_guard<std::mutex> l(mu);
        if (hadFrames && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) segs.push_back(path);
        else DeleteFileW(path.c_str());
        const size_t keep = static_cast<size_t>(SegmentsToKeep(o.seconds, o.segmentSeconds));
        while (segs.size() > keep) { DeleteSegment(segs.front()); segs.pop_front(); }
        FlushPending();
    }

    void Run()
    {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        MFStartup(MF_VERSION);
        std::wstring err;
        bool ok = InitDuplication(err);
        const int fps = std::clamp(o.fps, 10, 60);
        const LONGLONG frameDur = kSecond / fps;
        int kbps = 0;
        if (ok) {
            kbps = o.bitrateKbps > 0 ? o.bitrateKbps : AutoBitrateKbps(static_cast<int>(width), static_cast<int>(height), fps);
            SegmentWriter probe;
            const std::wstring pp = SegPath();
            if (!OpenSegment(probe, pp, width, height, fps, kbps)) { ok = false; err = L"Es wurde kein Videokodierer gefunden."; }
            CloseSegment(probe);
            DeleteFileW(pp.c_str());
        }
        startOk = ok;
        startErr = err;
        SetEvent(ready);
        if (!ok) { running = false; MFShutdown(); CoUninitialize(); return; }

        LARGE_INTEGER freq, t0;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&t0);
        auto now = [&]() { LARGE_INTEGER c; QueryPerformanceCounter(&c); return static_cast<LONGLONG>((c.QuadPart - t0.QuadPart) * kSecond / freq.QuadPart); };

        std::vector<BYTE> last;
        bool haveFrame = false;
        SegmentWriter seg;
        LONGLONG next = 0, lastNewFrame = 0;
        while (!stop) {
            if (WaitForSingleObject(saveReq, 0) == WAIT_OBJECT_0) {               // Speichern: laufenden Abschnitt abschliessen
                if (seg.ok) FinishSegment(seg);
                SetEvent(saveDone);
            }
            const LONGLONG t = now();
            if (t < next) {
                const LONGLONG waitMs = (next - t) / 10000;
                Sleep(static_cast<DWORD>(std::clamp<LONGLONG>(waitMs, 1, 20)));
                continue;
            }
            next = std::max(next + frameDur, t - frameDur * 3);                    // bei Rueckstand nicht aufholen
            const int g = Grab(last);
            if (g < 0) {                                                           // z. B. Aufloesungswechsel: neu verbinden
                if (seg.ok) FinishSegment(seg);
                std::wstring e2;
                if (!InitDuplication(e2)) { SetProblem(e2); Sleep(500); continue; }
                if ((width & ~1u) == 0) continue;
                SetProblem(L"");
                continue;
            }
            if (g > 0) { haveFrame = true; lastNewFrame = t; SetProblem(L""); }
            if (!haveFrame) { if (t > 3 * kSecond) SetProblem(L"Es kommen keine Bilder vom Bildschirm."); continue; }
            if (!seg.ok) {
                if (!OpenSegment(seg, SegPath(), width, height, fps, kbps)) { SetProblem(L"Der Videokodierer konnte nicht gestartet werden."); Sleep(500); continue; }
            }
            if (last.size() == static_cast<size_t>(width) * height * 4 && WriteFrame(seg, last, width, height, t, frameDur)) ++frames;
            if (seg.start >= 0 && t - seg.start >= static_cast<LONGLONG>(o.segmentSeconds) * kSecond) FinishSegment(seg);
            (void)lastNewFrame;
        }
        if (seg.ok) CloseSegment(seg);
        const std::wstring leftover = seg.path;
        if (!leftover.empty()) DeleteFileW(leftover.c_str());
        MFShutdown();
        CoUninitialize();
        running = false;
    }
};

Recorder::Recorder() : p(new Impl) {}
Recorder::~Recorder() { Stop(); delete p; }

bool Recorder::Running() const { return p->running; }
unsigned Recorder::FramesWritten() const { return p->frames; }

std::wstring Recorder::Problem() const
{
    std::lock_guard<std::mutex> l(p->mu);
    return p->problem;
}

bool Recorder::Start(const Options& o, std::wstring& err)
{
    if (p->running) return true;
    p->o = o;
    p->o.seconds = std::clamp(o.seconds, kMinSeconds, kMaxSeconds);
    p->o.segmentSeconds = std::clamp(o.segmentSeconds, 1, 30);
    p->tmp = o.tempDir.empty() ? TempBase() : o.tempDir;
    MakeDirs(p->tmp);
    {
        // Reste frueherer Laeufe (z. B. nach Absturz) wegraeumen
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((p->tmp + L"\\seg_*.mp4").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do { DeleteFileW((p->tmp + L"\\" + fd.cFileName).c_str()); } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    p->stop = false;
    p->frames = 0;
    p->segs.clear();
    p->problem.clear();
    p->saveReq = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    p->saveDone = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    p->ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    p->running = true;
    p->th = std::thread([this]() { p->Run(); });
    WaitForSingleObject(p->ready, 8000);
    if (!p->startOk) {
        err = p->startErr.empty() ? L"Die Aufnahme konnte nicht gestartet werden." : p->startErr;
        if (p->th.joinable()) p->th.join();
        CloseHandle(p->saveReq); CloseHandle(p->saveDone); CloseHandle(p->ready);
        p->saveReq = p->saveDone = p->ready = nullptr;
        return false;
    }
    return true;
}

void Recorder::Stop()
{
    if (!p->th.joinable()) { p->running = false; return; }
    p->stop = true;
    p->th.join();
    {
        std::lock_guard<std::mutex> l(p->mu);
        for (const std::wstring& s : p->segs) DeleteFileW(s.c_str());
        p->segs.clear();
        for (const std::wstring& s : p->pendingDelete) DeleteFileW(s.c_str());
        p->pendingDelete.clear();
    }
    for (HANDLE* h : { &p->saveReq, &p->saveDone, &p->ready })
        if (*h) { CloseHandle(*h); *h = nullptr; }
    p->running = false;
}

bool Recorder::Save(std::wstring& outFile, std::wstring& err)
{
    if (!p->running) { err = L"Die Aufnahme laeuft nicht."; return false; }
    ResetEvent(p->saveDone);
    SetEvent(p->saveReq);
    if (WaitForSingleObject(p->saveDone, 8000) != WAIT_OBJECT_0) { err = L"Die Aufnahme antwortet nicht."; return false; }
    std::vector<std::wstring> list;
    {
        std::lock_guard<std::mutex> l(p->mu);
        list.assign(p->segs.begin(), p->segs.end());
        ++p->saving;                                  // diese Abschnitte nicht loeschen, solange sie gelesen werden
    }
    // so viele Abschnitte wie fuer "seconds" noetig, von hinten gezaehlt
    const size_t need = static_cast<size_t>((p->o.seconds + p->o.segmentSeconds - 1) / p->o.segmentSeconds) + 1;
    if (list.size() > need) list.erase(list.begin(), list.end() - static_cast<std::ptrdiff_t>(need));
    std::wstring dir = p->o.outDir.empty() ? DefaultClipDir() : p->o.outDir;
    bool ok = false;
    if (dir.empty()) err = L"Der Videos-Ordner wurde nicht gefunden.";
    else if (list.empty()) err = L"Es ist noch nichts aufgenommen.";
    else {
        MakeDirs(dir);
        SYSTEMTIME st;
        GetLocalTime(&st);
        outFile = dir + L"\\" + ClipFileName(st);
        MFStartup(MF_VERSION);
        ok = Remux(list, outFile, err);
        MFShutdown();
        if (!ok) DeleteFileW(outFile.c_str());
    }
    {
        std::lock_guard<std::mutex> l(p->mu);
        --p->saving;
        p->FlushPending();
    }
    return ok;
}

}  // namespace cliprec
