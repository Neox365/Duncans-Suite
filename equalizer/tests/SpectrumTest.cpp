// Test des Spektrum-Rechenkerns (equalizer/SpectrumDsp.h)
// Baut mit MinGW:  g++ -std=c++17 -I.. SpectrumTest.cpp -o SpectrumTest.exe
#include "../SpectrumDsp.h"

#include <cstdio>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

static int BandOf(double hz)
{
    for (int b = 0; b < spectrum::kBands; ++b) if (hz >= spectrum::BandEdgeHz(b) && hz < spectrum::BandEdgeHz(b + 1)) return b;
    return -1;
}

int main()
{
    using namespace spectrum;
    CHECK(std::fabs(BandEdgeHz(0) - 20.0) < 1e-9);
    CHECK(std::fabs(BandEdgeHz(kBands) - 20000.0) < 1e-6);

    const double rate = 48000;
    std::vector<float> x(kFft);
    float db[kBands];

    // Stille
    Analyze(x.data(), rate, db);
    for (int b = 0; b < kBands; ++b) CHECK(db[b] <= -99.f);

    // Sinus 1 kHz, Amplitude 0,5 (= -6 dBFS): Spitze im passenden Band, weit entfernte Baender leise
    for (int i = 0; i < kFft; ++i) x[static_cast<size_t>(i)] = 0.5f * static_cast<float>(std::sin(2 * 3.14159265358979323846 * 1000.0 * i / rate));
    Analyze(x.data(), rate, db);
    const int b1k = BandOf(1000.0);
    float best = -200; int bestB = -1;
    for (int b = 0; b < kBands; ++b) if (db[b] > best) { best = db[b]; bestB = b; }
    CHECK(std::abs(bestB - b1k) <= 1);
    CHECK(best > -7.5f && best < -4.5f);
    CHECK(db[BandOf(100.0)] < -60.f);
    CHECK(db[BandOf(8000.0)] < -60.f);

    // Sinus 100 Hz, volle Aussteuerung: nahe 0 dB, auch in den schmalen Baendern unten
    for (int i = 0; i < kFft; ++i) x[static_cast<size_t>(i)] = static_cast<float>(std::sin(2 * 3.14159265358979323846 * 100.0 * i / rate));
    Analyze(x.data(), rate, db);
    best = -200; bestB = -1;
    for (int b = 0; b < kBands; ++b) if (db[b] > best) { best = db[b]; bestB = b; }
    CHECK(std::abs(bestB - BandOf(100.0)) <= 1);
    CHECK(best > -3.f && best < 0.5f);

    // Sinus 40 Hz (schmales Band, liegt zwischen zwei FFT-Bins)
    for (int i = 0; i < kFft; ++i) x[static_cast<size_t>(i)] = 0.5f * static_cast<float>(std::sin(2 * 3.14159265358979323846 * 40.0 * i / rate));
    Analyze(x.data(), rate, db);
    CHECK(db[BandOf(40.0)] > -12.f);
    CHECK(db[BandOf(4000.0)] < -60.f);

    // FFT-Grundlage: Gleichanteil und Impuls
    std::vector<std::complex<double>> a(8, 1.0);
    Fft(a);
    CHECK(std::abs(a[0] - 8.0) < 1e-9 && std::abs(a[1]) < 1e-9);
    std::vector<std::complex<double>> imp(8, 0.0);
    imp[0] = 1.0;
    Fft(imp);
    for (auto& v : imp) CHECK(std::abs(v - 1.0) < 1e-9);

    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
