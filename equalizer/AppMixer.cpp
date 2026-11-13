#include "AppMixer.h"

#include <windows.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <cwctype>
#include <map>

namespace mixer {

namespace {

std::wstring Lower(std::wstring s)
{
    for (wchar_t& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

std::wstring ProcessPath(DWORD pid)
{
    std::wstring path;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h) {
        wchar_t buf[MAX_PATH * 2] = {};
        DWORD n = MAX_PATH * 2;
        if (QueryFullProcessImageNameW(h, 0, buf, &n)) path.assign(buf, n);
        CloseHandle(h);
    }
    return path;
}

struct Fake {
    std::vector<Entry> rows = {{L"spiel", L"Spiel", 0.65f, false}, {L"discord", L"Discord", 1.f, false}, {L"chrome", L"Chrome", 0.4f, false}, {L"musik", L"Musik", 0.8f, true}};
};
Fake& FakeRows()
{
    static Fake f;
    return f;
}

// Ruft fn(key, name, ISimpleAudioVolume*) fuer jede aktive Sitzung des Standardgeraets auf (COM wird hier ein- und wieder ausgeschaltet)
template <class Fn>
void ForEachSession(Fn fn)
{
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IMMDeviceEnumerator* en = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&en))) && en) {
        IMMDevice* dev = nullptr;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev)) && dev) {
            IAudioSessionManager2* mgr = nullptr;
            if (SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&mgr))) && mgr) {
                IAudioSessionEnumerator* list = nullptr;
                if (SUCCEEDED(mgr->GetSessionEnumerator(&list)) && list) {
                    int count = 0;
                    list->GetCount(&count);
                    for (int i = 0; i < count; ++i) {
                        IAudioSessionControl* ctl = nullptr;
                        if (FAILED(list->GetSession(i, &ctl)) || !ctl) continue;
                        IAudioSessionControl2* ctl2 = nullptr;
                        if (SUCCEEDED(ctl->QueryInterface(__uuidof(IAudioSessionControl2), reinterpret_cast<void**>(&ctl2))) && ctl2) {
                            AudioSessionState state = AudioSessionStateInactive;
                            ctl2->GetState(&state);
                            DWORD pid = 0;
                            ctl2->GetProcessId(&pid);
                            const bool system = ctl2->IsSystemSoundsSession() == S_OK;
                            ISimpleAudioVolume* vol = nullptr;
                            if (state == AudioSessionStateActive && pid != GetCurrentProcessId() &&
                                SUCCEEDED(ctl2->QueryInterface(__uuidof(ISimpleAudioVolume), reinterpret_cast<void**>(&vol))) && vol) {
                                std::wstring key, name;
                                if (system) {
                                    key = L"#system";
                                    name = L"Systemklänge";
                                } else {
                                    const std::wstring path = ProcessPath(pid);
                                    name = DisplayNameFromPath(path);
                                    key = Lower(name);
                                }
                                if (!key.empty()) fn(key, name, vol);
                                vol->Release();
                            }
                            ctl2->Release();
                        }
                        ctl->Release();
                    }
                    list->Release();
                }
                mgr->Release();
            }
            dev->Release();
        }
        en->Release();
    }
    if (SUCCEEDED(init)) CoUninitialize();
}

}  // namespace

bool FakeMode()
{
    wchar_t b[8] = {};
    return GetEnvironmentVariableW(L"DUNCANS_EQ_FAKEMIXER", b, 8) > 0 && b[0] == L'1';
}

std::wstring DisplayNameFromPath(const std::wstring& path)
{
    size_t a = path.find_last_of(L"\\/");
    std::wstring n = a == std::wstring::npos ? path : path.substr(a + 1);
    if (n.size() > 4 && Lower(n.substr(n.size() - 4)) == L".exe") n.resize(n.size() - 4);
    if (!n.empty()) n[0] = static_cast<wchar_t>(std::towupper(n[0]));
    return n;
}

std::vector<Entry> List()
{
    if (FakeMode()) return FakeRows().rows;
    std::map<std::wstring, Entry> byKey;
    ForEachSession([&](const std::wstring& key, const std::wstring& name, ISimpleAudioVolume* vol) {
        float v = 1.f;
        BOOL m = FALSE;
        vol->GetMasterVolume(&v);
        vol->GetMute(&m);
        auto it = byKey.find(key);
        if (it == byKey.end()) {
            Entry e;
            e.key = key;
            e.name = name;
            e.volume = v;
            e.muted = m != FALSE;
            byKey[key] = e;
        } else {
            it->second.volume = std::max(it->second.volume, v);
            it->second.muted = it->second.muted && m != FALSE;   // stumm nur, wenn alle Sitzungen stumm sind
        }
    });
    std::vector<Entry> out;
    for (auto& kv : byKey) out.push_back(kv.second);
    return out;
}

bool SetVolume(const std::wstring& key, float volume)
{
    volume = std::clamp(volume, 0.f, 1.f);
    if (FakeMode()) {
        for (Entry& e : FakeRows().rows) if (e.key == key) e.volume = volume;
        return true;
    }
    bool any = false;
    ForEachSession([&](const std::wstring& k, const std::wstring&, ISimpleAudioVolume* vol) {
        if (k == key && SUCCEEDED(vol->SetMasterVolume(volume, nullptr))) any = true;
    });
    return any;
}

bool SetMuted(const std::wstring& key, bool muted)
{
    if (FakeMode()) {
        for (Entry& e : FakeRows().rows) if (e.key == key) e.muted = muted;
        return true;
    }
    bool any = false;
    ForEachSession([&](const std::wstring& k, const std::wstring&, ISimpleAudioVolume* vol) {
        if (k == key && SUCCEEDED(vol->SetMute(muted ? TRUE : FALSE, nullptr))) any = true;
    });
    return any;
}

}  // namespace mixer
