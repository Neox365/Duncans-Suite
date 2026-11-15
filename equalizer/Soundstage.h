#pragma once
// Duncan's Equalizer - Raumklang (Soundstage): Kopfhoerer-Crossfeed, Buehnenbreite und ein simulierter Raum.
// Der Ton wird von Equalizer APO verarbeitet. Dieses Modul rechnet daher eine 4-Kanal-Impulsantwort (L->L, L->R, R->L, R->R),
// die APO per Faltung anwendet. Keine Echtzeit-Schleife im eigenen Programm, keine Fremd-DLL.
#include <string>
#include <vector>

namespace eq {

constexpr int kRoomCount = 5;                       // 0 Aus, 1 Gaming, 2 Kino, 3 Studio, 4 Konzertsaal
constexpr int kStageWidthMin = 50, kStageWidthMax = 160;
constexpr int kStageSampleRate = 48000;

struct StageSettings {
    bool on = false;            // Raumklang an
    int room = 0;               // Raum-Voreinstellung (0 = kein Raum, nur Crossfeed/Breite)
    int width = 100;            // Buehnenbreite in %: 0..100 enger, 100 unveraendert, bis 160 weiter
    int cross = 0;              // Kopfhoerer-Crossfeed 0..100 %
    int depth = 0;              // Raumtiefe (Anteil Hall) 0..100 %
};

const wchar_t* RoomName(int room);
const wchar_t* RoomHint(int room);                  // ein Satz fuer die Karte
StageSettings RoomDefaults(int room);               // empfohlene Regler zur Voreinstellung (on bleibt unveraendert)
void ClampStage(StageSettings& s);
// true, wenn die Faltung etwas aendern wuerde (an, und mindestens Breite != 100, Crossfeed > 0 oder Raum mit Tiefe > 0)
bool StageActive(const StageSettings& s);
// Mehrkanal-Impulsantwort: 4 Kanaele (L->L, L->R, R->L, R->R), kStageSampleRate, 32 Bit Float
struct StageIr {
    std::vector<float> ch[4];
    size_t length() const { return ch[0].size(); }
};
StageIr BuildStageIr(const StageSettings& s);
// WAV-Datei (IEEE-Float, 4 Kanaele) als Bytes
std::string StageIrWav(const StageIr& ir);

}  // namespace eq
