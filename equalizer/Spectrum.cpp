#include "Spectrum.h"

#include <windows.h>
#include <audioclient.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace spectrum {

namespace {

CRITICAL_SECTION g_cs;
bool g_csInit = false;
HANDLE g_thread = nullptr;
volatile LONG g_stop = 0;
volatile LONG g_running = 0;
Snapshot g_snap;

void EnsureCs()
{
    if (!g_csInit) {
        InitializeCriticalSection(&g_cs);
        g_csInit = true;
        for (float& b : g_snap.bands) b = kFloorDb;
    }
}

bool FakeMode()
{
    wchar_t b[8] = {};
    return GetEnvironmentVariableW(L"DUNCANS_EQ_FAKEMIXER", b, 8) > 0 && b[0] == L'1';
}

float ToDb(float lin) { return lin <= 1e-5f ? kFloorDb : std::max(kFloorDb, 20.f * std::log10(lin)); }

std::wstring DeviceId(IMMDeviceEnumerator* en)
{
    std::wstring id;
    IMMDevice* dev = nullptr;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev)) && dev) {
        LPWSTR s = nullptr;
        if (SUCCEEDED(dev->GetId(&s)) && s) {
            id = s;
            CoTaskMemFree(s);
        }
        dev->Release();
    }
    return id;
}

// Ein Durchgang auf dem aktuellen Standardgeraet; endet bei Stopp-Wunsch, Fehler oder Geraetewechsel
void CaptureOnce(IMMDeviceEnumerator* en)
{
    IMMDevice* dev = nullptr;
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev)) || !dev) return;
    const std::wstring startId = DeviceId(en);
    IAudioClient* client = nullptr;
    if (SUCCEEDED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client))) && client) {
        WAVEFORMATEX* fmt = nullptr;
        if (SUCCEEDED(client->GetMixFormat(&fmt)) && fmt) {
            bool isFloat = fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
            if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE && fmt->cbSize >= 22) {
                const WAVEFORMATEXTENSIBLE* fx = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt);
                isFloat = IsEqualGUID(fx->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != 0;
            }
            const int ch = std::max<int>(1, fmt->nChannels), bits = fmt->wBitsPerSample;
            const double rate = fmt->nSamplesPerSec;
            const bool supported = isFloat ? bits == 32 : (bits == 16 || bits == 24 || bits == 32);
            IAudioCaptureClient* cap = nullptr;
            if (supported && SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, fmt, nullptr)) &&
                SUCCEEDED(client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&cap))) && cap && SUCCEEDED(client->Start())) {
                std::vector<float> ring(kFft, 0.f), frame(kFft);
                int fill = 0, sinceAnalysis = 0;
                const int hop = std::max(256, static_cast<int>(rate / 50));          // rund 50 Auswertungen pro Sekunde
                float bands[kBands];
                for (float& b : bands) b = kFloorDb;
                float pl = kFloorDb, pr = kFloorDb;
                LARGE_INTEGER freq, last;
                QueryPerformanceFrequency(&freq);
                QueryPerformanceCounter(&last);
                DWORD lastCheck = GetTickCount();
                auto push = [&](float s) {
                    ring[static_cast<size_t>(fill)] = s;
                    fill = (fill + 1) % kFft;
                    ++sinceAnalysis;
                };
                while (!g_stop) {
                    Sleep(8);
                    LARGE_INTEGER now;
                    QueryPerformanceCounter(&now);
                    const double dt = static_cast<double>(now.QuadPart - last.QuadPart) / static_cast<double>(freq.QuadPart);
                    last = now;
                    float framePeakL = 0, framePeakR = 0;
                    UINT32 got = 0;
                    UINT32 packet = 0;
                    while (SUCCEEDED(cap->GetNextPacketSize(&packet)) && packet > 0) {
                        BYTE* data = nullptr;
                        UINT32 frames = 0;
                        DWORD flags = 0;
                        if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
                        for (UINT32 i = 0; i < frames; ++i) {
                            float l = 0, r = 0, sum = 0;
                            for (int c = 0; c < ch; ++c) {
                                float v = 0;
                                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                                    const BYTE* p = data + (static_cast<size_t>(i) * ch + c) * (bits / 8);
                                    if (isFloat) { float f; std::memcpy(&f, p, 4); v = f; }
                                    else if (bits == 16) { short s; std::memcpy(&s, p, 2); v = s / 32768.f; }
                                    else if (bits == 24) { int s = (p[0] << 8) | (p[1] << 16) | (p[2] << 24); v = static_cast<float>(s / 2147483648.0); }
                                    else { int s; std::memcpy(&s, p, 4); v = static_cast<float>(s / 2147483648.0); }
                                }
                                if (c == 0) l = v;
                                if (c == 1) r = v;
                                sum += v;
                            }
                            if (ch == 1) r = l;
                            framePeakL = std::max(framePeakL, std::fabs(l));
                            framePeakR = std::max(framePeakR, std::fabs(r));
                            push(std::max(-1.f, std::min(1.f, sum / static_cast<float>(ch))));
                        }
                        got += frames;
                        cap->ReleaseBuffer(frames);
                    }
                    if (got == 0) {                                       // Stille: Loopback liefert dann keine Pakete
                        const int n = std::min<int>(kFft, static_cast<int>(dt * rate));
                        for (int i = 0; i < n; ++i) push(0.f);
                    }
                    pl = std::max(ToDb(framePeakL), pl - static_cast<float>(24.0 * dt));
                    pr = std::max(ToDb(framePeakR), pr - static_cast<float>(24.0 * dt));
                    if (sinceAnalysis >= hop) {
                        for (int i = 0; i < kFft; ++i) frame[static_cast<size_t>(i)] = ring[static_cast<size_t>((fill + i) % kFft)];   // aeltester zuerst
                        float now2[kBands];
                        Analyze(frame.data(), rate, now2);
                        const float fall = static_cast<float>(30.0 * sinceAnalysis / rate);                                           // 30 dB pro Sekunde
                        for (int b = 0; b < kBands; ++b) bands[b] = std::max(now2[b], bands[b] - fall);
                        sinceAnalysis = 0;
                    }
                    EnterCriticalSection(&g_cs);
                    std::memcpy(g_snap.bands, bands, sizeof bands);
                    g_snap.peakL = pl;
                    g_snap.peakR = pr;
                    LeaveCriticalSection(&g_cs);
                    if (GetTickCount() - lastCheck > 1000) {                // Geraetewechsel erkennen
                        lastCheck = GetTickCount();
                        if (DeviceId(en) != startId) break;
                    }
                }
                client->Stop();
            }
            if (cap) cap->Release();
            CoTaskMemFree(fmt);
        }
        client->Release();
    }
    dev->Release();
}

DWORD WINAPI ThreadProc(LPVOID)
{
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* en = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&en))) && en) {
        InterlockedExchange(&g_running, 1);
        while (!g_stop) {
            CaptureOnce(en);
            for (int i = 0; i < 20 && !g_stop; ++i) Sleep(50);         // kurz warten, bevor es nach Fehler oder Geraetewechsel neu versucht wird
        }
        en->Release();
    }
    InterlockedExchange(&g_running, 0);
    if (SUCCEEDED(init)) CoUninitialize();
    return 0;
}

}  // namespace

void Start()
{
    EnsureCs();
    if (FakeMode() || g_thread) return;
    g_stop = 0;
    g_thread = CreateThread(nullptr, 0, ThreadProc, nullptr, 0, nullptr);
}

void Stop()
{
    EnsureCs();
    if (!g_thread) return;
    InterlockedExchange(&g_stop, 1);
    WaitForSingleObject(g_thread, 3000);
    CloseHandle(g_thread);
    g_thread = nullptr;
    EnterCriticalSection(&g_cs);
    for (float& b : g_snap.bands) b = kFloorDb;
    g_snap.peakL = g_snap.peakR = kFloorDb;
    LeaveCriticalSection(&g_cs);
}

bool Running() { return FakeMode() || g_thread != nullptr; }

Snapshot Get()
{
    EnsureCs();
    Snapshot s;
    if (FakeMode()) {                                               // erfundenes, sich bewegendes Spektrum fuer Bildschirmfotos
        s.fake = s.running = true;
        for (int b = 0; b < kBands; ++b) {
            const double x = static_cast<double>(b) / kBands;
            s.bands[b] = static_cast<float>(-28.0 - 55.0 * x + 14.0 * std::sin(b * 0.55) + 8.0 * std::sin(b * 1.7) - (x > 0.85 ? (x - 0.85) * 150.0 : 0.0));
        }
        s.peakL = -9.f;
        s.peakR = -14.f;
        return s;
    }
    EnterCriticalSection(&g_cs);
    s = g_snap;
    LeaveCriticalSection(&g_cs);
    s.running = g_thread != nullptr && g_running != 0;
    return s;
}

}  // namespace spectrum
