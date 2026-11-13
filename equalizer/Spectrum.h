#pragma once
// Duncan's Equalizer - Live-Spektrum: schneidet den Ton des Standard-Ausgabegeraets mit (WASAPI-Loopback, nur lesend, nichts wird
// gespeichert oder gesendet), rechnet ihn in 96 Frequenzbaender um und liefert zusaetzlich die Spitzenpegel links/rechts.
#include "SpectrumDsp.h"

namespace spectrum {

struct Snapshot {
    float bands[kBands];       // dBFS, langsam abfallend geglaettet
    float peakL = kFloorDb;    // Spitzenpegel in dBFS (fallend geglaettet)
    float peakR = kFloorDb;
    bool running = false;      // der Mitschnitt laeuft
    bool fake = false;         // Testdaten (DUNCANS_EQ_FAKEMIXER=1)
};

void Start();                  // startet den Mitschnitt (mehrfaches Aufrufen schadet nicht)
void Stop();                   // beendet ihn und gibt das Geraet frei
bool Running();
Snapshot Get();

}  // namespace spectrum
