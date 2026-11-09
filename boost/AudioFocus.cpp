#include "AudioFocus.h"

#include <algorithm>
#include <cmath>

namespace boostmode {

namespace {
bool InList(const std::vector<std::wstring>& v, const std::wstring& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
bool Near(float a, float b) { return std::fabs(a - b) <= 0.02f; }
}  // namespace

bool IsNeverDucked(const std::wstring& exe) {
    static const wchar_t* const kChat[] = {
        L"discord.exe", L"discordptb.exe", L"discordcanary.exe", L"ts3client_win64.exe", L"ts3client_win32.exe", L"teamspeak.exe",
        L"mumble.exe", L"teams.exe", L"ms-teams.exe", L"zoom.exe", L"skype.exe", L"slack.exe" };
    for (const wchar_t* c : kChat) if (exe == c) return true;
    return false;
}

void AudioFocus::Tick(AudioBackend& audio, const std::vector<std::wstring>& gameExes, const std::wstring& fg, const AudioOptions& opt) {
    const std::vector<AudioSession> sessions = audio.List();
    const bool gameFg = !fg.empty() && InList(gameExes, fg);
    bool gameRunning = false;
    for (const AudioSession& s : sessions) if (!s.exe.empty() && InList(gameExes, s.exe)) gameRunning = true;

    // Sitzungen, die es nicht mehr gibt, vergessen
    std::set<std::wstring> alive;
    for (const AudioSession& s : sessions) alive.insert(s.id);
    for (auto it = ducked_.begin(); it != ducked_.end();) it = alive.count(it->first) ? std::next(it) : ducked_.erase(it);
    for (auto it = mutedByUs_.begin(); it != mutedByUs_.end();) it = alive.count(*it) ? std::next(it) : mutedByUs_.erase(it);

    for (const AudioSession& s : sessions) {
        if (s.exe.empty()) continue;                                    // Systemtoene bleiben unberuehrt
        const bool isGame = InList(gameExes, s.exe);
        if (isGame) {
            // Spiel stumm, solange es nicht im Vordergrund ist
            if (opt.muteGame && gameRunning && !gameFg) {
                if (!s.muted && !mutedByUs_.count(s.id) && audio.SetMute(s.id, true)) mutedByUs_.insert(s.id);
            } else if (mutedByUs_.count(s.id)) {
                audio.SetMute(s.id, false);
                mutedByUs_.erase(s.id);
            }
            continue;
        }
        if (IsNeverDucked(s.exe)) continue;
        auto it = ducked_.find(s.id);
        if (opt.duck && gameFg) {
            if (it == ducked_.end()) {
                Ducked d;
                d.orig = s.volume;
                d.applied = s.volume * opt.duckLevel;
                if (audio.SetVolume(s.id, d.applied)) ducked_[s.id] = d;
            } else if (!it->second.userOwned && !Near(s.volume, it->second.applied)) {
                it->second.userOwned = true;                            // du hast den Regler selbst bewegt: nicht dagegen arbeiten
            }
        } else if (it != ducked_.end()) {
            if (!it->second.userOwned && Near(s.volume, it->second.applied)) audio.SetVolume(s.id, it->second.orig);
            ducked_.erase(it);
        }
    }
}

void AudioFocus::RestoreAll(AudioBackend& audio) {
    if (!Active()) return;
    for (const AudioSession& s : audio.List()) {
        auto d = ducked_.find(s.id);
        if (d != ducked_.end() && !d->second.userOwned && Near(s.volume, d->second.applied)) audio.SetVolume(s.id, d->second.orig);
        if (mutedByUs_.count(s.id)) audio.SetMute(s.id, false);
    }
    ducked_.clear();
    mutedByUs_.clear();
}

}  // namespace boostmode
