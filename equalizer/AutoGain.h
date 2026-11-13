#pragma once
// Duncan's Equalizer - Auto-Lautstaerke (Beta): hebt die Vorverstaerkung langsam an, solange die Musik nach dem Equalizer genug Luft nach
// oben hat, und senkt sie sofort wieder, wenn es knapp wird. Reine Regel-Logik ohne Windows (testbar).
// Eingabe ist der gemessene Spitzenpegel (dBFS) des Tons nach dem Equalizer. Die Anhebung (extra) ist ganzzahlig in dB und
// geht nie ueber maxExtra hinaus (das Programm waehlt maxExtra so, dass der Pegel des Originals nicht ueberschritten wird).
#include <algorithm>
#include <cmath>

namespace eq {

struct AutoGain {
    static constexpr double kCeiling = -3.0;     // Ziel: Spitzen bleiben unter diesem Pegel
    static constexpr double kPanic = -1.5;       // darueber wird sofort abgesenkt
    static constexpr double kRiseHeadroom = 2.0; // so viel Luft braucht es fuer jeden Schritt nach oben
    static constexpr double kCalmSeconds = 2.0;  // so lange muss die Luft vorhanden sein
    static constexpr double kSettleSeconds = 1.0;// nach jeder Aenderung warten, bis sich der Pegel eingeschwungen hat
    static constexpr double kSilence = -60.0;     // darunter gilt es als Stille
    static constexpr double kQuiet = -40.0;       // leiser als das: kein Anheben (Hintergrund, Pausen)
    static constexpr double kHoldFall = 2.0;     // der Spitzenwert faellt nur mit 2 dB pro Sekunde (rund 4 Sekunden Gedaechtnis)

    int extra = 0;           // aktuelle Anhebung in dB (>= 0)
    double hold = -100.0;    // gehaltener Spitzenpegel
    double calm = 0.0;
    double settle = 0.0;

    void Reset() { *this = AutoGain(); }

    // peakDb: Spitzenpegel seit dem letzten Aufruf, dt: Sekunden seit dem letzten Aufruf. Rueckgabe: extra hat sich geaendert.
    bool Update(double peakDb, double dt, int maxExtra)
    {
        const int before = extra;
        maxExtra = std::max(0, maxExtra);
        settle = std::max(0.0, settle - dt);
        if (peakDb <= kSilence)                            // Stille oder Pause: nichts veraendern, Gedaechtnis einfrieren
            return false;
        hold = std::max(peakDb, hold - kHoldFall * dt);
        if (extra > maxExtra)
            extra = maxExtra;
        if (hold > kPanic) {                               // zu knapp: sofort zurueck
            if (extra > 0) {
                const int cut = std::max(1, static_cast<int>(std::ceil(hold - kCeiling)));
                extra = std::max(0, extra - cut);
                hold -= cut;                               // der Pegel sinkt um so viel
                settle = kSettleSeconds * 2;
            }
            calm = 0.0;
        } else if (hold > kQuiet) {                         // Musik laeuft: bei genug Luft langsam anheben
            if (kCeiling - hold >= kRiseHeadroom && extra < maxExtra) {
                calm += dt;
                if (calm >= kCalmSeconds && settle <= 0.0) {
                    ++extra;
                    calm = 0.0;
                    settle = kSettleSeconds;
                    hold += 1.0;                           // der Pegel steigt um 1 dB
                }
            } else {
                calm = 0.0;
            }
        }                                                  // Stille: alles halten
        return extra != before;
    }
};

}  // namespace eq
