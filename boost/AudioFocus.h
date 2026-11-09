#pragma once
// Duncan's Boost - Ton nach Fokus: Steht ein angehaktes Spiel im Vordergrund, werden andere Programme leiser (Browser, Musik), und
// beim Wegschalten (Alt+Tab) wird das Spiel stumm. Regelt nur die Lautstaerke je Programm ueber die normale Windows-Lautstaerkemischung
// (wie der Regler im Lautstaerkemixer). Kein Eingriff in Spiele, keine Treiber, keine Administratorrechte.
// Die Entscheidungen (AudioFocus) sind von Windows getrennt (AudioBackend), damit sie sich testen lassen.
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace boostmode {

// Eine Audio-Sitzung eines Programms (Chrome hat oft mehrere)
struct AudioSession {
    std::wstring id;        // eindeutige Kennung der Sitzung
    std::wstring exe;       // EXE-Name klein, z. B. "chrome.exe"; leer bei Systemtoenen
    float volume = 1.0f;    // 0..1
    bool muted = false;
};

class AudioBackend {
public:
    virtual ~AudioBackend() = default;
    virtual std::vector<AudioSession> List() = 0;
    virtual bool SetVolume(const std::wstring& id, float volume) = 0;
    virtual bool SetMute(const std::wstring& id, bool mute) = 0;
};

struct AudioOptions {
    bool duck = false;          // andere Programme leiser, solange ein Spiel im Vordergrund ist
    bool muteGame = false;      // Spiel stumm, solange es nicht im Vordergrund ist
    float duckLevel = 0.4f;     // andere Programme auf diesen Anteil ihrer Lautstaerke
};

// Chat- und Sprachprogramme werden nie leiser gestellt (man will Mitspieler hoeren)
bool IsNeverDucked(const std::wstring& exeLower);

class AudioFocus {
public:
    // Ein Durchlauf: gameExes = angehakte Spiele (klein), foregroundExe = EXE des Vordergrundfensters (klein, leer = unbekannt)
    void Tick(AudioBackend& audio, const std::vector<std::wstring>& gameExes, const std::wstring& foregroundExe, const AudioOptions& opt);
    // Alles zurueck auf den Zustand vor Boost (beim Beenden, beim Ausschalten der Optionen)
    void RestoreAll(AudioBackend& audio);
    bool Active() const { return !ducked_.empty() || !mutedByUs_.empty(); }

private:
    struct Ducked { float orig = 1.0f, applied = 1.0f; bool userOwned = false; };
    std::map<std::wstring, Ducked> ducked_;
    std::set<std::wstring> mutedByUs_;
};

// Windows-Anbindung (Core Audio). nullptr, wenn kein Wiedergabegeraet da ist. Aufrufer muss COM initialisiert haben.
std::unique_ptr<AudioBackend> MakeSystemAudio();
// EXE-Name (klein) des Programms im Vordergrund; leer = unbekannt
std::wstring ForegroundExe();

}  // namespace boostmode
