#pragma once
// Duncan's Equalizer - Rechenkern des Live-Spektrums (reine Logik, ohne Windows, testbar): FFT und Einteilung in Frequenzbaender.
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace spectrum {

constexpr int kFft = 4096;        // Fensterlaenge in Abtastwerten (bei 48 kHz rund 85 ms)
constexpr int kBands = 96;        // logarithmisch verteilte Baender von 20 Hz bis 20 kHz
constexpr float kFloorDb = -100.f;

inline double BandEdgeHz(int b) { return 20.0 * std::pow(1000.0, static_cast<double>(b) / kBands); }
inline double BandCenterHz(int b) { return std::sqrt(BandEdgeHz(b) * BandEdgeHz(b + 1)); }

// Radix-2-FFT an Ort und Stelle (Laenge Zweierpotenz)
inline void Fft(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * 3.14159265358979323846 / static_cast<double>(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// x: kFft Abtastwerte (Mono, -1 ... 1), rate: Abtastrate. out: kBands Pegel in dBFS (ein reiner Sinus mit Amplitude 1 ergibt etwa 0 dB).
inline void Analyze(const float* x, double rate, float* out)
{
    std::vector<std::complex<double>> a(kFft);
    double wsum = 0;
    for (int i = 0; i < kFft; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / (kFft - 1));   // Hann-Fenster gegen Ausfransen
        a[static_cast<size_t>(i)] = std::complex<double>(x[i] * w, 0.0);
        wsum += w;
    }
    Fft(a);
    const double norm = 2.0 / wsum, df = rate / kFft;
    std::vector<double> mag(kFft / 2);
    for (int k = 0; k < kFft / 2; ++k) mag[static_cast<size_t>(k)] = std::abs(a[static_cast<size_t>(k)]) * norm;
    for (int b = 0; b < kBands; ++b) {
        const double lo = BandEdgeHz(b), hi = BandEdgeHz(b + 1);
        int k0 = static_cast<int>(std::ceil(lo / df)), k1 = static_cast<int>(std::floor(hi / df));
        k1 = std::min(k1, kFft / 2 - 1);
        double m = 0;
        if (k1 >= k0 && k0 >= 1) {
            for (int k = k0; k <= k1; ++k) m = std::max(m, mag[static_cast<size_t>(k)]);
        } else {                                                   // schmales Band zwischen zwei Bins: Wert dazwischen
            const double pos = BandCenterHz(b) / df;
            const int k = std::clamp(static_cast<int>(std::floor(pos)), 1, kFft / 2 - 2);
            const double t = std::clamp(pos - k, 0.0, 1.0);
            m = mag[static_cast<size_t>(k)] * (1 - t) + mag[static_cast<size_t>(k + 1)] * t;
        }
        const double db = 20.0 * std::log10(std::max(m, 1e-5));
        out[b] = static_cast<float>(std::max(db, static_cast<double>(kFloorDb)));
    }
}

}  // namespace spectrum
