// Test der Auto-Lautstaerke-Regelung (equalizer/AutoGain.h)
// Baut mit MinGW:  g++ -std=c++17 -I.. AutoGainTest.cpp -o AutoGainTest.exe
#include "../AutoGain.h"

#include <cstdio>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

// Laesst die Regelung sec Sekunden mit einer Quelle laufen, deren Spitze (ohne Anhebung) srcPeak ist; gemessen wird srcPeak + extra.
static void Run(eq::AutoGain& a, double srcPeak, double sec, int maxExtra, double* worst = nullptr)
{
    const double dt = 0.06;
    for (double t = 0; t < sec; t += dt) {
        const double measured = srcPeak + a.extra;
        if (worst && measured > *worst) *worst = measured;
        a.Update(measured, dt, maxExtra);
    }
}

int main()
{
    // Leise Musik (Spitze -12 dB): die Anhebung steigt langsam und stoppt mit genug Abstand zur Obergrenze
    {
        eq::AutoGain a;
        Run(a, -12.0, 3.0, 20);
        CHECK(a.extra >= 1 && a.extra <= 2);                 // nach 3 Sekunden erst 1 bis 2 dB (langsam)
        Run(a, -12.0, 60.0, 20);
        CHECK(a.extra == 8);                                 // -12 + 8 = -4 dB: noch unter der Obergrenze -3
        CHECK(-12.0 + a.extra <= eq::AutoGain::kCeiling);
    }
    // Obergrenze maxExtra wird nie ueberschritten
    {
        eq::AutoGain a;
        Run(a, -30.0, 120.0, 6);
        CHECK(a.extra == 6);
        Run(a, -30.0, 10.0, 3);                              // maxExtra sinkt (z. B. Regler geaendert): sofort begrenzt
        CHECK(a.extra == 3);
    }
    // Plötzlich laute Musik: sofort absenken, nie lange ueber der Panikgrenze
    {
        eq::AutoGain a;
        Run(a, -12.0, 60.0, 20);
        CHECK(a.extra == 8);
        double worst = -100;
        Run(a, -2.0, 20.0, 20, &worst);                      // Quelle mit Spitze -2 dB: schon mit extra 0 knapp
        CHECK(a.extra == 0);
        CHECK(a.extra == 0 && -2.0 + a.extra > eq::AutoGain::kPanic - 1.0);
    }
    // Ein einzelner Peak waehrend der Wiedergabe senkt ab, danach geht es wieder langsam nach oben
    {
        eq::AutoGain a;
        Run(a, -14.0, 80.0, 20);
        const int top = a.extra;
        CHECK(top >= 8);
        a.Update(-0.5, 0.06, 20);
        CHECK(a.extra < top);
        const int after = a.extra;
        Run(a, -14.0, 80.0, 20);
        CHECK(a.extra >= after);
    }
    // Stille aendert nichts
    {
        eq::AutoGain a;
        Run(a, -12.0, 30.0, 20);
        const int e = a.extra;
        for (int i = 0; i < 1000; ++i) a.Update(-100.0, 0.06, 20);
        CHECK(a.extra == e);
    }
    // Kein Spielraum (maxExtra 0): bleibt bei 0
    {
        eq::AutoGain a;
        Run(a, -30.0, 60.0, 0);
        CHECK(a.extra == 0);
    }
    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
