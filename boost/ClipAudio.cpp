#include "ClipAudio.h"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

using Microsoft::WRL::ComPtr;

namespace cliprec {

void ToStereo(const float* in, size_t frames, int channels, std::vector<float>& out)
{
    if (channels < 1) return;
    out.reserve(out.size() + frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        const float* f = in + i * static_cast<size_t>(channels);
        float l = f[0], r = channels >= 2 ? f[1] : f[0];
        if (channels >= 3) { l += 0.7f * f[2]; r += 0.7f * f[2]; }
        out.push_back(l);
        out.push_back(r);
    }
}

void Resampler::Process(const float* in, size_t frames, int rate, std::vector<float>& out)
{
    if (frames == 0) return;
    if (rate == kAudioRate || rate <= 0) {
        out.insert(out.end(), in, in + frames * 2);
        lastL = in[(frames - 1) * 2];
        lastR = in[(frames - 1) * 2 + 1];
        pos = 0;
        return;
    }
    const double step = static_cast<double>(rate) / kAudioRate;
    const double n = static_cast<double>(frames);
    while (pos < n - 1) {
        const long i = static_cast<long>(std::floor(pos));
        const double f = pos - static_cast<double>(i);
        const float al = i < 0 ? lastL : in[static_cast<size_t>(i) * 2], ar = i < 0 ? lastR : in[static_cast<size_t>(i) * 2 + 1];
        const float bl = in[static_cast<size_t>(i + 1) * 2], br = in[static_cast<size_t>(i + 1) * 2 + 1];
        out.push_back(static_cast<float>(al + (bl - al) * f));
        out.push_back(static_cast<float>(ar + (br - ar) * f));
        pos += step;
    }
    pos -= n;
    lastL = in[(frames - 1) * 2];
    lastR = in[(frames - 1) * 2 + 1];
}

void MixInto(float* a, const float* b, size_t samples, float gainB)
{
    for (size_t i = 0; i < samples; ++i) a[i] = std::clamp(a[i] + b[i] * gainB, -1.0f, 1.0f);
}

void FloatToPcm16(const float* in, size_t samples, short* out)
{
    for (size_t i = 0; i < samples; ++i) {
        const float v = std::clamp(in[i], -1.0f, 1.0f);
        out[i] = static_cast<short>(std::lround(v * 32767.0f));
    }
}

struct AudioSource::Impl {
    std::thread th;
    std::atomic<bool> stop{ false }, running{ false };
    std::mutex mu;
    std::deque<float> fifo;                      // Stereo, verschraenkt, 48 kHz
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> cap;
    WAVEFORMATEX* wf = nullptr;
    int channels = 2, rate = kAudioRate, bits = 32;
    bool isFloat = true;
    Resampler rs;
    bool loopback = true, initOk = false;
    std::wstring initErr;
    HANDLE ready = nullptr;

    ~Impl() { if (wf) CoTaskMemFree(wf); }

    bool Init(std::wstring& err)
    {
        ComPtr<IMMDeviceEnumerator> en;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(en.GetAddressOf())))) { err = L"Die Audioschnittstelle ist nicht verfügbar."; return false; }
        ComPtr<IMMDevice> dev;
        if (FAILED(en->GetDefaultAudioEndpoint(loopback ? eRender : eCapture, eConsole, &dev)) || !dev) { err = loopback ? L"Kein Ausgabegerät für den Systemton gefunden." : L"Kein Mikrofon gefunden."; return false; }
        if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf())))) { err = loopback ? L"Der Systemton kann nicht aufgenommen werden." : L"Das Mikrofon ist nicht verfügbar (in den Windows-Datenschutzeinstellungen erlauben?)."; return false; }
        if (FAILED(client->GetMixFormat(&wf)) || !wf) { err = L"Das Tonformat ist nicht lesbar."; return false; }
        channels = wf->nChannels;
        rate = static_cast<int>(wf->nSamplesPerSec);
        bits = wf->wBitsPerSample;
        isFloat = wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wf->cbSize >= 22) {
            const auto* ex = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wf);
            isFloat = ex->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        }
        if (!((isFloat && bits == 32) || (!isFloat && (bits == 16 || bits == 32))) || channels < 1 || channels > 16) { err = L"Das Tonformat wird nicht unterstützt."; return false; }
        const DWORD flags = loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
        if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 2000000, 0, wf, nullptr))) { err = L"Der Ton konnte nicht gestartet werden."; return false; }
        if (FAILED(client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(cap.GetAddressOf())))) { err = L"Der Ton konnte nicht gestartet werden."; return false; }
        if (FAILED(client->Start())) { err = L"Der Ton konnte nicht gestartet werden."; return false; }
        return true;
    }

    void Run()
    {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        initOk = Init(initErr);
        SetEvent(ready);
        if (!initOk) { client.Reset(); cap.Reset(); CoUninitialize(); return; }
        std::vector<float> raw, st, conv;
        while (!stop) {
            Sleep(10);
            UINT32 pkt = 0;
            while (!stop && SUCCEEDED(cap->GetNextPacketSize(&pkt)) && pkt > 0) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
                raw.assign(static_cast<size_t>(frames) * channels, 0.0f);
                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
                    const size_t n = static_cast<size_t>(frames) * channels;
                    if (isFloat && bits == 32) std::memcpy(raw.data(), data, n * sizeof(float));
                    else if (!isFloat && bits == 16) for (size_t i = 0; i < n; ++i) raw[i] = reinterpret_cast<const short*>(data)[i] / 32768.0f;
                    else if (!isFloat && bits == 32) for (size_t i = 0; i < n; ++i) raw[i] = static_cast<float>(reinterpret_cast<const int*>(data)[i] / 2147483648.0);
                }
                cap->ReleaseBuffer(frames);
                st.clear();
                ToStereo(raw.data(), frames, channels, st);
                conv.clear();
                rs.Process(st.data(), st.size() / 2, rate, conv);
                std::lock_guard<std::mutex> l(mu);
                fifo.insert(fifo.end(), conv.begin(), conv.end());
                const size_t cap2 = static_cast<size_t>(kAudioRate) * 2 * 4;      // hoechstens 4 s vorhalten
                while (fifo.size() > cap2) fifo.pop_front();
            }
        }
        if (client) client->Stop();
        cap.Reset();
        client.Reset();
        CoUninitialize();
    }
};

AudioSource::AudioSource() : p(new Impl) {}
AudioSource::~AudioSource() { Stop(); delete p; }
bool AudioSource::Running() const { return p->running; }

bool AudioSource::Start(bool loopback, std::wstring& err)
{
    if (p->running) return true;
    p->stop = false;
    p->fifo.clear();
    p->rs = Resampler();
    p->loopback = loopback;
    p->initErr.clear();
    p->initOk = false;
    p->ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    p->running = true;
    p->th = std::thread([this]() { p->Run(); });
    WaitForSingleObject(p->ready, 8000);
    CloseHandle(p->ready);
    p->ready = nullptr;
    if (!p->initOk) {
        err = p->initErr.empty() ? L"Der Ton konnte nicht gestartet werden." : p->initErr;
        p->stop = true;
        if (p->th.joinable()) p->th.join();
        p->running = false;
        return false;
    }
    return true;
}

void AudioSource::Stop()
{
    if (!p->th.joinable()) { p->running = false; return; }
    p->stop = true;
    p->th.join();
    p->running = false;
    std::lock_guard<std::mutex> l(p->mu);
    p->fifo.clear();
}

size_t AudioSource::Read(size_t frames, std::vector<float>& out)
{
    std::lock_guard<std::mutex> l(p->mu);
    const size_t have = std::min(frames, p->fifo.size() / 2);
    for (size_t i = 0; i < have * 2; ++i) { out.push_back(p->fifo.front()); p->fifo.pop_front(); }
    return have;
}

}  // namespace cliprec
