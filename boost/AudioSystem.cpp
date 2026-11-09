// Windows-Anbindung fuer "Ton nach Fokus": Lautstaerke und Stummschaltung je Programm ueber Core Audio (wie der Lautstaerkemixer).
#include "AudioFocus.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include <functional>

namespace boostmode {

namespace {

template <class T>
struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

std::wstring ExeOfPid(DWORD pid) {
    if (pid == 0) return L"";
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return L"";
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD n = MAX_PATH * 2;
    std::wstring out;
    if (QueryFullProcessImageNameW(h, 0, buf, &n)) {
        out.assign(buf, n);
        const size_t slash = out.find_last_of(L'\\');
        if (slash != std::wstring::npos) out.erase(0, slash + 1);
        for (wchar_t& c : out) if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + 32);
    }
    CloseHandle(h);
    return out;
}

class SystemAudio : public AudioBackend {
public:
    bool Init() {
        Com<IMMDeviceEnumerator> en;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&en)))) return false;
        Com<IMMDevice> dev;
        if (FAILED(en->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev))) return false;
        return SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&mgr_)));
    }
    ~SystemAudio() override { if (mgr_) mgr_->Release(); }

    std::vector<AudioSession> List() override {
        std::vector<AudioSession> out;
        Each([&](IAudioSessionControl2* c, ISimpleAudioVolume* v, const std::wstring& id) {
            AudioSession s;
            s.id = id;
            DWORD pid = 0;
            c->GetProcessId(&pid);
            if (pid != GetCurrentProcessId() && c->IsSystemSoundsSession() != S_OK) s.exe = ExeOfPid(pid);
            float vol = 1.0f; BOOL mute = FALSE;
            v->GetMasterVolume(&vol);
            v->GetMute(&mute);
            s.volume = vol;
            s.muted = mute != FALSE;
            out.push_back(s);
        });
        return out;
    }
    bool SetVolume(const std::wstring& id, float volume) override {
        bool ok = false;
        Each([&](IAudioSessionControl2*, ISimpleAudioVolume* v, const std::wstring& sid) {
            if (sid == id) ok = SUCCEEDED(v->SetMasterVolume(volume < 0 ? 0 : (volume > 1 ? 1 : volume), nullptr));
        });
        return ok;
    }
    bool SetMute(const std::wstring& id, bool mute) override {
        bool ok = false;
        Each([&](IAudioSessionControl2*, ISimpleAudioVolume* v, const std::wstring& sid) {
            if (sid == id) ok = SUCCEEDED(v->SetMute(mute ? TRUE : FALSE, nullptr));
        });
        return ok;
    }

private:
    void Each(const std::function<void(IAudioSessionControl2*, ISimpleAudioVolume*, const std::wstring&)>& fn) {
        Com<IAudioSessionEnumerator> list;
        if (!mgr_ || FAILED(mgr_->GetSessionEnumerator(&list))) return;
        int n = 0;
        list->GetCount(&n);
        for (int i = 0; i < n; ++i) {
            Com<IAudioSessionControl> c;
            if (FAILED(list->GetSession(i, &c))) continue;
            Com<IAudioSessionControl2> c2;
            Com<ISimpleAudioVolume> vol;
            if (FAILED(c->QueryInterface(__uuidof(IAudioSessionControl2), reinterpret_cast<void**>(&c2)))) continue;
            if (FAILED(c->QueryInterface(__uuidof(ISimpleAudioVolume), reinterpret_cast<void**>(&vol)))) continue;
            LPWSTR raw = nullptr;
            if (FAILED(c2->GetSessionInstanceIdentifier(&raw)) || !raw) continue;
            const std::wstring id = raw;
            CoTaskMemFree(raw);
            fn(c2.p, vol.p, id);
        }
    }
    IAudioSessionManager2* mgr_ = nullptr;
};

}  // namespace

std::unique_ptr<AudioBackend> MakeSystemAudio() {
    auto a = std::make_unique<SystemAudio>();
    if (!a->Init()) return nullptr;
    return a;
}

std::wstring ForegroundExe() {
    HWND h = GetForegroundWindow();
    if (!h) return L"";
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    return ExeOfPid(pid);
}

}  // namespace boostmode
