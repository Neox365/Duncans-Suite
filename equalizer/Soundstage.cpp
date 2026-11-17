#include "Soundstage.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace eq {

namespace {

const double kPi = 3.14159265358979323846;

struct RoomSpec {
    const wchar_t* name;
    const wchar_t* hint;
    int width, cross, depth;       // empfohlene Regler
    double rt60;                   // Nachhallzeit in s
    double preMs;                  // Vorverzoegerung des Halls
    double damp;                   // Daempfung der Hoehen im Hall (0 = hell ... 1 = sehr dunkel)
    double tail;                   // Pegel des Halls
    double er;                     // Pegel der fruehen Reflexionen
    int nEr;
    double erMs[7];
};

const RoomSpec kRooms[kRoomCount] = {
    { L"Aus", L"Kein Raum. Nur Bühnenbreite und Crossfeed wirken.", 100, 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0, {} },
    { L"Gaming / Ortung", L"Enge, präzise Reflexionen ohne Hall-Fahne: Schritte und Richtungen bleiben klar.", 120, 20, 10, 0.12, 2.0, 0.40, 0.35, 0.55, 5, { 3.7, 6.9, 10.4, 14.8, 20.3 } },
    { L"Kino", L"Warmer, breiter Raum mit viel Tiefe, wie ein Heimkino.", 130, 30, 45, 0.55, 12.0, 0.55, 0.80, 0.60, 6, { 8.5, 13.9, 21.7, 30.6, 42.8, 56.1 } },
    { L"Studio", L"Trocken und neutral, wie vor Nahfeld-Monitoren.", 100, 15, 10, 0.18, 3.0, 0.45, 0.45, 0.50, 4, { 2.9, 4.8, 7.6, 11.3 } },
    { L"Konzertsaal", L"Große räumliche Tiefe für Musik und Live-Streams.", 140, 35, 70, 1.60, 28.0, 0.70, 1.00, 0.55, 7, { 17.0, 25.6, 37.9, 52.4, 70.8, 92.7, 118.0 } },
    { L"Wohnzimmer", L"Gemütlich und leicht gedämpft, wie Musik im Wohnzimmer.", 110, 25, 25, 0.38, 6.0, 0.60, 0.55, 0.60, 5, { 2.4, 4.1, 6.6, 9.8, 13.5 } },
    { L"Kathedrale", L"Riesiger, langer Nachhall mit weichen Höhen. Für Orgel, Chor und Ambient.", 150, 30, 80, 2.80, 45.0, 0.75, 1.00, 0.45, 7, { 26.0, 41.0, 58.0, 79.0, 103.0, 131.0, 160.0 } },
    { L"Badezimmer", L"Kleiner, harter, heller Raum, wie Singen unter der Dusche.", 105, 20, 40, 0.90, 1.5, 0.05, 0.70, 0.90, 7, { 1.6, 2.7, 3.9, 5.3, 6.8, 8.6, 10.7 } },
};

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed) {}
    double next() {                     // gleichverteilt in -1 ... 1 (kleiner LCG, reicht fuer Rauschen)
        s = s * 1664525u + 1013904223u;
        return (static_cast<double>(s >> 8) / 8388608.0) - 1.0;
    }
};

// Tiefpass 2. Ordnung (RBJ, Q = 0,707), angewendet auf eine Folge
void LowpassInPlace(std::vector<double>& x, double fc)
{
    const double w0 = 2.0 * kPi * fc / kStageSampleRate, c = std::cos(w0), al = std::sin(w0) / (2.0 * 0.7071);
    const double a0 = 1.0 + al;
    const double b0 = (1.0 - c) / 2.0 / a0, b1 = (1.0 - c) / a0, b2 = b0, a1 = -2.0 * c / a0, a2 = (1.0 - al) / a0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (double& v : x) {
        const double y = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = v; y2 = y1; y1 = y;
        v = y;
    }
}

std::vector<double> Lowpass(size_t n, double fc)    // Impulsantwort des Tiefpasses
{
    std::vector<double> d(n, 0.0);
    d[0] = 1.0;
    LowpassInPlace(d, fc);
    return d;
}

std::vector<double> Conv(const std::vector<double>& a, const std::vector<double>& b)
{
    std::vector<double> o(a.size() + b.size() - 1, 0.0);
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] == 0.0) continue;
        for (size_t j = 0; j < b.size(); ++j) o[i + j] += a[i] * b[j];
    }
    return o;
}

void AddAt(std::vector<double>& dst, const std::vector<double>& src, size_t at, double gain)
{
    if (dst.size() < at + src.size()) dst.resize(at + src.size(), 0.0);
    for (size_t i = 0; i < src.size(); ++i) dst[at + i] += gain * src[i];
}

size_t MsToSamples(double ms) { return static_cast<size_t>(std::lround(ms * kStageSampleRate / 1000.0)); }

// Hall: abklingendes, gedaempftes Rauschen; wird auf Energie 1 gebracht
std::vector<double> Tail(const RoomSpec& r, uint32_t seed)
{
    const size_t len = static_cast<size_t>(r.rt60 * 1.05 * kStageSampleRate) + 1;
    std::vector<double> n(len);
    Rng rng(seed);
    const double a = 1.0 - 0.85 * r.damp;            // Ein-Pol-Tiefpass: dunklere Raeume = kleineres a
    double y = 0.0;
    for (size_t i = 0; i < len; ++i) {
        const double t = static_cast<double>(i) / kStageSampleRate;
        const double env = std::exp(-6.9078 * t / r.rt60);
        const double fade = std::min(1.0, static_cast<double>(i) / 96.0);        // weicher Einsatz, kein Knacken
        y += a * (rng.next() - y);
        n[i] = y * env * fade;
    }
    double e = 0.0;
    for (double v : n) e += v * v;
    if (e > 0.0) for (double& v : n) v /= std::sqrt(e);
    return n;
}

}  // namespace

const wchar_t* RoomName(int room) { return kRooms[std::clamp(room, 0, kRoomCount - 1)].name; }
const wchar_t* RoomHint(int room) { return kRooms[std::clamp(room, 0, kRoomCount - 1)].hint; }

StageSettings RoomDefaults(int room)
{
    const RoomSpec& r = kRooms[std::clamp(room, 0, kRoomCount - 1)];
    StageSettings s;
    s.room = std::clamp(room, 0, kRoomCount - 1);
    s.width = r.width; s.cross = r.cross; s.depth = r.depth;
    return s;
}

void ClampStage(StageSettings& s)
{
    s.room = std::clamp(s.room, 0, kRoomCount - 1);
    s.width = std::clamp(s.width, kStageWidthMin, kStageWidthMax);
    s.cross = std::clamp(s.cross, 0, 100);
    s.depth = std::clamp(s.depth, 0, 100);
    s.level = std::clamp(s.level, 0, kStageLevelMax);
}

bool StageActive(const StageSettings& in)
{
    StageSettings s = in;
    ClampStage(s);
    return s.on && (s.width != 100 || s.cross > 0 || (s.room > 0 && s.depth > 0));
}

StageIr BuildStageIr(const StageSettings& in)
{
    StageSettings s = in;
    ClampStage(s);
    const size_t kBase = 1024;

    // 1. Buehnenbreite: Seitensignal ueber 120 Hz mit Faktor w, darunter unveraendert (Bass bleibt in der Mitte)
    const double w = s.width / 100.0;
    std::vector<double> lp120 = Lowpass(kBase, 120.0);
    std::vector<double> F(kBase);
    for (size_t i = 0; i < kBase; ++i) F[i] = (1.0 - w) * lp120[i] + (i == 0 ? w : 0.0);
    std::vector<double> A(kBase), B(kBase);          // gleiche Seite / andere Seite
    for (size_t i = 0; i < kBase; ++i) {
        const double d = i == 0 ? 1.0 : 0.0;
        A[i] = (d + F[i]) / 2.0;
        B[i] = (d - F[i]) / 2.0;
    }

    // 2. Crossfeed: der andere Kanal kommt ~0,27 ms spaeter und ohne Hoehen (Tiefpass 700 Hz) leiser ans Ohr
    const double c = s.cross / 100.0;
    std::vector<double> LL = A, LR = B;
    if (c > 0.0) {
        const double direct = 1.0 - 0.25 * c, k = 0.5 * c;
        std::vector<double> z(kBase, 0.0), lp = Lowpass(kBase - 13, 700.0);
        for (size_t i = 0; i < lp.size(); ++i) z[i + 13] = lp[i];     // 13 Abtastwerte bei 48 kHz = 0,27 ms
        std::vector<double> zA = Conv(z, A), zB = Conv(z, B);
        LL.assign(zA.size(), 0.0);
        LR.assign(zA.size(), 0.0);
        for (size_t i = 0; i < A.size(); ++i) { LL[i] += direct * A[i]; LR[i] += direct * B[i]; }
        for (size_t i = 0; i < zA.size(); ++i) { LL[i] += k * zB[i]; LR[i] += k * zA[i]; }
    }
    std::vector<double> T[4] = { LL, LR, LR, LL };    // L->L, L->R, R->L, R->R (symmetrisch)

    // 3. Raum: fruehe Reflexionen (mit einfacher Kopf-Naeherung fuer das fernere Ohr) und Hall, parallel zum Direktsignal
    if (s.room > 0 && s.depth > 0) {
        const RoomSpec& r = kRooms[s.room];
        const double wet = s.depth / 100.0;
        const std::vector<double> head = Lowpass(48, 3500.0);      // fernes Ohr: weniger Hoehen
        std::vector<double> W[4];
        for (int inp = 0; inp < 2; ++inp) {
            const int same = inp == 0 ? 0 : 3, other = inp == 0 ? 1 : 2;       // Kanalnummern L->L / R->R und L->R / R->L
            for (int i = 0; i < r.nEr; ++i) {
                const double side = (i % 2 == 0) ? 1.0 : -1.0;               // abwechselnd links/rechts einfallend
                const double t = r.erMs[i] * (inp == 0 ? 1.0 : 1.06);
                const double g = r.er * wet * std::pow(0.78, i);
                const bool toSame = side > 0;                                  // fuer beide Eingaenge gleich, sonst waere ein Ohr lauter
                const double gNear = toSame ? 1.0 : 0.55, gFar = toSame ? 0.55 : 1.0;
                AddAt(W[same], { 1.0 }, MsToSamples(t), g * gNear);
                AddAt(W[other], head, MsToSamples(t) + MsToSamples(0.25), g * gFar);
            }
            const std::vector<double> a = Tail(r, 1234u + 17u * inp), b = Tail(r, 98765u + 31u * inp);
            const size_t pre = MsToSamples(r.preMs);
            AddAt(W[same], a, pre, r.tail * wet * 0.5);
            AddAt(W[other], b, pre, r.tail * wet * 0.5);
        }
        for (int k = 0; k < 4; ++k) {
            if (T[k].size() < W[k].size()) T[k].resize(W[k].size(), 0.0);
            for (size_t i = 0; i < W[k].size(); ++i) T[k][i] += W[k][i];
        }
    }

    // 4. Pegel: nie lauter als das Original (Zeilenenergie je Ausgang <= 1), damit nichts uebersteuert
    size_t len = 0;
    for (auto& t : T) len = std::max(len, t.size());
    for (auto& t : T) t.resize(len, 0.0);
    double e[2] = { 0.0, 0.0 };
    for (size_t i = 0; i < len; ++i) {
        e[0] += T[0][i] * T[0][i] + T[2][i] * T[2][i];          // Ausgang L: von L und von R
        e[1] += T[1][i] * T[1][i] + T[3][i] * T[3][i];
    }
    const double emax = std::max(e[0], e[1]);
    const double scale = emax > 1.0 ? 1.0 / std::sqrt(emax) : 1.0;
    while (len > 64 && std::fabs(T[0][len - 1]) < 1e-6 && std::fabs(T[1][len - 1]) < 1e-6 && std::fabs(T[2][len - 1]) < 1e-6 && std::fabs(T[3][len - 1]) < 1e-6) --len;

    StageIr ir;
    for (int k = 0; k < 4; ++k) {
        ir.ch[k].resize(len);
        for (size_t i = 0; i < len; ++i) ir.ch[k][i] = static_cast<float>(T[k][i] * scale);
    }
    return ir;
}

std::string StageIrWav(const StageIr& ir)
{
    const uint32_t n = static_cast<uint32_t>(ir.length()), ch = 4, bytes = n * ch * 4;
    std::string o;
    auto u32 = [&](uint32_t v) { o.append(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { o.append(reinterpret_cast<const char*>(&v), 2); };
    o += "RIFF"; u32(36 + bytes); o += "WAVEfmt "; u32(16);
    u16(3); u16(static_cast<uint16_t>(ch)); u32(kStageSampleRate); u32(kStageSampleRate * ch * 4); u16(static_cast<uint16_t>(ch * 4)); u16(32);
    o += "data"; u32(bytes);
    o.reserve(o.size() + bytes);
    for (uint32_t i = 0; i < n; ++i)
        for (int k = 0; k < 4; ++k) o.append(reinterpret_cast<const char*>(&ir.ch[k][i]), 4);
    return o;
}

std::string StageTestWav()
{
    const int rate = kStageSampleRate;
    const double pans[] = { -1.0, -0.5, 0.0, 0.5, 1.0, 0.5, 0.0, -0.5, -1.0, 0.0 };       // links ... rechts ... zurueck ... Mitte
    const size_t burst = rate * 22 / 100, gap = rate * 10 / 100, tail = rate * 15 / 10;
    const size_t n = sizeof(pans) / sizeof(pans[0]) * (burst + gap) + tail;
    std::vector<int16_t> pcm(n * 2, 0);
    Rng rng(4711u);
    size_t pos = 0;
    for (double pan : pans)
    {
        std::vector<double> b(burst);
        for (double& v : b) v = rng.next();
        LowpassInPlace(b, 6000.0);
        const double th = (pan + 1.0) * kPi / 4.0, gl = std::cos(th), gr = std::sin(th);
        for (size_t i = 0; i < burst; ++i)
        {
            const double edge = static_cast<double>(std::min(i, burst - 1 - i));
            const double env = std::min(1.0, edge / (0.012 * rate));                       // weicher Ein- und Ausklang
            const double v = b[i] * env * 0.45;
            pcm[(pos + i) * 2] = static_cast<int16_t>(std::lround(std::clamp(v * gl, -1.0, 1.0) * 32767.0));
            pcm[(pos + i) * 2 + 1] = static_cast<int16_t>(std::lround(std::clamp(v * gr, -1.0, 1.0) * 32767.0));
        }
        pos += burst + gap;
    }
    const uint32_t bytes = static_cast<uint32_t>(pcm.size() * 2);
    std::string o;
    auto u32 = [&](uint32_t v) { o.append(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { o.append(reinterpret_cast<const char*>(&v), 2); };
    o += "RIFF"; u32(36 + bytes); o += "WAVEfmt "; u32(16);
    u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16);
    o += "data"; u32(bytes);
    o.append(reinterpret_cast<const char*>(pcm.data()), bytes);
    return o;
}

}  // namespace eq
