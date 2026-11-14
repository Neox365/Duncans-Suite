#pragma once
// Duncan's Boost - Ton fuer Video-Clips: nimmt den Systemton (alles, was der PC abspielt) und/oder das Standard-Mikrofon ueber WASAPI auf und
// liefert ihn als Strom von Stereo-Float-Werten bei 48 kHz. Der Aufrufer holt in festen Abstaenden Bloecke ab (auch wenn nichts abgespielt
// wird: dann kommen Nullen), so laeuft der Ton gleichmaessig mit der Uhr und bleibt zum Bild synchron.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstddef>
#include <string>
#include <vector>

namespace cliprec {

constexpr int kAudioRate = 48000;

// --- reine Rechenhilfen (getrennt testbar) ---
// Wandelt n Frames mit "channels" Kanaelen (float, verschraenkt) in Stereo um: 1 Kanal wird verdoppelt, 2 bleibt, ab 3: links/rechts plus
// 0,7 x Mitte (Kanal 3), der Rest wird verworfen.
void ToStereo(const float* in, size_t frames, int channels, std::vector<float>& out);
// Einfacher Wandler auf 48 kHz (linear, mit Zustand ueber Bloecke hinweg). in: Stereo bei "rate", out: Stereo bei 48 kHz (angehaengt).
struct Resampler {
    double pos = 0;               // Position im naechsten Block (in Eingabe-Frames, relativ zum Blockanfang)
    float lastL = 0, lastR = 0;   // letzter Frame des vorigen Blocks
    void Process(const float* in, size_t frames, int rate, std::vector<float>& out);
};
// Mischt b (mit Verstaerkung) auf a (Stereo, gleiche Laenge), begrenzt auf -1 ... 1
void MixInto(float* a, const float* b, size_t samples, float gainB);
// float (-1 ... 1) -> 16 Bit mit Begrenzung
void FloatToPcm16(const float* in, size_t samples, short* out);

class AudioSource {
public:
    AudioSource();
    ~AudioSource();
    AudioSource(const AudioSource&) = delete;
    AudioSource& operator=(const AudioSource&) = delete;
    // loopback = true: Systemton des Standard-Ausgabegeraets; false: Standard-Mikrofon. false + Meldung, wenn nicht verfuegbar.
    bool Start(bool loopback, std::wstring& err);
    void Stop();
    bool Running() const;
    // Holt bis zu "frames" Stereo-Frames (48 kHz) ab und haengt sie an out an; gibt die Anzahl zurueck. Es kann weniger kommen (dann auffuellen).
    size_t Read(size_t frames, std::vector<float>& out);

private:
    struct Impl;
    Impl* p;
};

}  // namespace cliprec
