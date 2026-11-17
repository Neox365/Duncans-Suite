#pragma once
// Duncan's Equalizer - Raumklang (Soundstage): Kopfhoerer-Crossfeed, Buehnenbreite und ein simulierter Raum.
// Der Ton wird von Equalizer APO verarbeitet. Dieses Modul rechnet daher eine 4-Kanal-Impulsantwort (L->L, L->R, R->L, R->R),
// die APO per Faltung anwendet. Keine Echtzeit-Schleife im eigenen Programm, keine Fremd-DLL.
#include <string>
#include <vector>

namespace eq {

constexpr int kRoomCount = 8;                       // 0 Aus, 1 Gaming, 2 Kino, 3 Studio, 4 Konzertsaal, 5 Wohnzimmer, 6 Kathedrale, 7 Badezimmer
constexpr int kStageWidthMin = 50, kStageWidthMax = 160;
constexpr int kStageSampleRate = 48000;
constexpr int kStageAngleMin = 10, kStageAngleMax = 60;   // Lautsprecher-Winkel des Kopfmodells in Grad
constexpr int kStageToneMax = 6;                    // Bass und Dialog: -6 ... +6 dB
constexpr int kStageLevelMax = 8;                   // Lautstaerke-Ausgleich in dB

struct StageSettings {
    bool on = false;            // Raumklang an
    int room = 0;               // Raum-Voreinstellung (0 = kein Raum, nur Crossfeed/Breite)
    int width = 100;            // Buehnenbreite in %: 0..100 enger, 100 unveraendert, bis 160 weiter
    int cross = 0;              // Kopfhoerer-Crossfeed 0..100 %
    int depth = 0;              // Raumtiefe (Anteil Hall) 0..100 %
    int angle = 30;             // Winkel der virtuellen Lautsprecher (nur Kopfmodell): 10 eng ... 60 weit
    int bass = 0;               // Bass im Raumklang -6..+6 dB (Tiefenregler bei 100 Hz)
    int voice = 0;              // Dialog im Raumklang -6..+6 dB (Stimmbereich um 2,5 kHz)
    bool head = false;          // Kopfmodell (HRTF-Naeherung) statt einfachem Crossfeed
    int level = 0;              // Lautstaerke-Ausgleich 0..8 dB (Raeume machen den Ton leiser)
    int upmix = 0;              // Surround: Stereo auf die hinteren und seitlichen Lautsprecher hochmischen 0..100 %
    bool surround = false;      // virtuelles 7.1-Surround ueber Kopfhoerer (Windows muss auf 7.1 stehen)
};

const wchar_t* RoomName(int room);
const wchar_t* RoomHint(int room);                  // ein Satz fuer die Karte
StageSettings RoomDefaults(int room);               // empfohlene Regler zur Voreinstellung (on bleibt unveraendert)
void ClampStage(StageSettings& s);
// true, wenn die Faltung etwas aendern wuerde (an, und mindestens Breite != 100, Crossfeed > 0 oder Raum mit Tiefe > 0)
bool StageActive(const StageSettings& s);
// Mehrkanal-Impulsantwort: 4 Kanaele (L->L, L->R, R->L, R->R), kStageSampleRate, 32 Bit Float.
// Mit surround: 8 Kanaele fuer die Mitte/Seiten-Paare (vorn, Mitte, leer, hinten, seitlich), siehe BuildMain
constexpr int kSurroundChannels = 8;
struct StageIr {
    int channels = 4;                       // 4 = Stereo-Raumklang, 8 = Surround
    std::vector<float> ch[8];
    size_t length() const { return ch[0].size(); }
};
StageIr BuildStageIr(const StageSettings& s);
// Surround: Raum, Hall und Buehnenbreite als Mitte/Seite-Paar (2 Mono-Kanaele), wirkt nach dem Surround auf beide Ohren
bool SurroundFxActive(const StageSettings& s);
StageIr BuildSurroundFxIr(const StageSettings& s);
// WAV-Datei (IEEE-Float, 4 Kanaele) als Bytes
std::string StageIrWav(const StageIr& ir);
// Hoerprobe: kurze Rauschimpulse wandern von links nach rechts und zurueck zur Mitte (Stereo, 16 Bit, 48 kHz), danach Stille fuer den Nachhall
std::string StageTestWav();

}  // namespace eq
