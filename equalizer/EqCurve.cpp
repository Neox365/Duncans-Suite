#include "EqCurve.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace eq {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 48000.0;

std::vector<std::string> Tokens(const std::string& line)
{
    std::vector<std::string> t;
    std::istringstream in(line);
    std::string w;
    while (in >> w)
        t.push_back(w);
    return t;
}

bool ToNumber(const std::string& s, double& v)
{
    if (s.empty())
        return false;
    char* end = nullptr;
    v = std::strtod(s.c_str(), &end);
    return end && *end == '\0' && std::isfinite(v);
}

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
};

Biquad MakeBiquad(const Filter& f)
{
    const double q = f.q > 0 ? f.q : DefaultQ(f.type);
    const double w0 = 2 * kPi * std::clamp(f.fc, 1.0, kRate / 2 - 1) / kRate;
    const double c = std::cos(w0), s = std::sin(w0);
    const double alpha = s / (2 * q);
    const double A = std::pow(10.0, f.gain / 40.0);
    Biquad b;
    if (f.type == "PK") {
        b = {1 + alpha * A, -2 * c, 1 - alpha * A, 1 + alpha / A, -2 * c, 1 - alpha / A};
    } else if (f.type == "LSC") {
        const double sq = 2 * std::sqrt(A) * alpha;
        b = {A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
             (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq};
    } else if (f.type == "HSC") {
        const double sq = 2 * std::sqrt(A) * alpha;
        b = {A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
             (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq};
    } else if (f.type == "LP") {
        b = {(1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha};
    } else if (f.type == "HP") {
        b = {(1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha};
    } else {   // NO
        b = {1, -2 * c, 1, 1 + alpha, -2 * c, 1 - alpha};
    }
    return b;
}

double BiquadDb(const Biquad& b, double freq)
{
    const double w = 2 * kPi * std::clamp(freq, 1.0, kRate / 2) / kRate;
    const std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2 * w);
    const std::complex<double> num = b.b0 + b.b1 * z1 + b.b2 * z2, den = b.a0 + b.a1 * z1 + b.a2 * z2;
    const double m = std::abs(num) / std::max(1e-12, std::abs(den));
    return 20 * std::log10(std::max(1e-9, m));
}

struct GraphicPoint {
    double f, db;
};

// Graphic-EQ: dB-Werte werden ueber der logarithmischen Frequenzachse linear verbunden, ausserhalb der Stuetzpunkte bleibt der Rand stehen
double GraphicDb(const std::vector<GraphicPoint>& pts, double freq)
{
    if (pts.empty())
        return 0;
    if (freq <= pts.front().f)
        return pts.front().db;
    if (freq >= pts.back().f)
        return pts.back().db;
    for (size_t i = 1; i < pts.size(); ++i) {
        if (freq <= pts[i].f) {
            const double t = (std::log(freq) - std::log(pts[i - 1].f)) / (std::log(pts[i].f) - std::log(pts[i - 1].f));
            return pts[i - 1].db + t * (pts[i].db - pts[i - 1].db);
        }
    }
    return 0;
}

}  // namespace

bool FilterUsesGain(const std::string& type) { return type == "PK" || type == "LSC" || type == "HSC"; }
bool FilterUsesQ(const std::string& type) { return type == "PK" || type == "NO"; }

const wchar_t* FilterTypeName(const std::string& type)
{
    if (type == "PK") return L"Glocke";
    if (type == "LSC") return L"Tiefen-Shelf";
    if (type == "HSC") return L"Höhen-Shelf";
    if (type == "LP") return L"Tiefpass";
    if (type == "HP") return L"Hochpass";
    if (type == "NO") return L"Kerbfilter";
    return L"?";
}

double DefaultQ(const std::string& type)
{
    if (type == "PK") return 1.0;
    if (type == "NO") return 4.0;
    return 0.7071;
}

bool ParseFilter(const std::string& line, Filter& out)
{
    const std::vector<std::string> t = Tokens(line);
    if (t.size() < 4 || t[0] != "Filter:")
        return false;
    Filter f;
    f.gain = 0;
    f.q = 0;
    if (t[1] == "ON") f.on = true;
    else if (t[1] == "OFF") f.on = false;
    else return false;
    f.type = t[2];
    if (f.type != "PK" && f.type != "LSC" && f.type != "HSC" && f.type != "LP" && f.type != "HP" && f.type != "NO")
        return false;
    bool haveFc = false;
    for (size_t i = 3; i < t.size();) {
        double v = 0;
        if (t[i] == "Fc" && i + 1 < t.size() && ToNumber(t[i + 1], v)) { f.fc = v; haveFc = true; i += 2; if (i < t.size() && t[i] == "Hz") ++i; }
        else if (t[i] == "Gain" && i + 1 < t.size() && ToNumber(t[i + 1], v)) { f.gain = v; i += 2; if (i < t.size() && t[i] == "dB") ++i; }
        else if (t[i] == "Q" && i + 1 < t.size() && ToNumber(t[i + 1], v)) { f.q = v; i += 2; }
        else return false;   // z. B. "BW Oct": hier nicht unterstuetzt
    }
    if (!haveFc)
        return false;
    f.fc = std::clamp(f.fc, kMinFc, kMaxFc);
    f.gain = FilterUsesGain(f.type) ? std::clamp(f.gain, -kMaxFilterGain, kMaxFilterGain) : 0;
    f.q = f.q > 0 ? std::clamp(f.q, kMinQ, kMaxQ) : 0;
    out = f;
    return true;
}

std::string FormatFilter(const Filter& f)
{
    char buf[160];
    auto num = [](double v, const char* fmt) {
        char b[32];
        std::snprintf(b, sizeof b, fmt, v);
        return std::string(b);
    };
    std::string s = std::string("Filter: ") + (f.on ? "ON " : "OFF ") + f.type + " Fc " + num(std::clamp(f.fc, kMinFc, kMaxFc), "%.1f") + " Hz";
    if (FilterUsesGain(f.type))
        s += " Gain " + num(std::clamp(f.gain, -kMaxFilterGain, kMaxFilterGain), "%.1f") + " dB";
    if (FilterUsesQ(f.type) || f.q > 0)
        s += " Q " + num(f.q > 0 ? std::clamp(f.q, kMinQ, kMaxQ) : DefaultQ(f.type), "%.2f");
    std::snprintf(buf, sizeof buf, "%s", s.c_str());
    return buf;
}

std::vector<double> LogFreqs(int n, double lo, double hi)
{
    std::vector<double> f;
    if (n < 2)
        return f;
    for (int i = 0; i < n; ++i)
        f.push_back(lo * std::pow(hi / lo, static_cast<double>(i) / (n - 1)));
    return f;
}

std::vector<double> ResponseDbFromConfig(const std::string& config, const std::vector<double>& freqs, bool includePreamp)
{
    std::vector<double> db(freqs.size(), 0.0);
    std::vector<Biquad> filters;
    std::vector<std::vector<GraphicPoint>> graphics;   // jede GraphicEQ-Zeile fuer sich (Equalizer APO schaltet sie hintereinander)
    double preamp = 0;
    std::istringstream in(config);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (line.compare(0, 7, "Preamp:") == 0) {
            double v = 0;
            const std::vector<std::string> t = Tokens(line);
            if (t.size() >= 2 && ToNumber(t[1], v)) preamp += v;
        } else if (line.compare(0, 11, "GraphicEQ: ") == 0) {
            std::string body = line.substr(11);
            std::replace(body.begin(), body.end(), ';', ' ');
            const std::vector<std::string> t = Tokens(body);
            std::vector<GraphicPoint> graphic;
            for (size_t i = 0; i + 1 < t.size(); i += 2) {
                double f = 0, g = 0;
                if (ToNumber(t[i], f) && ToNumber(t[i + 1], g) && f > 0) graphic.push_back({f, g});
            }
            std::sort(graphic.begin(), graphic.end(), [](const GraphicPoint& a, const GraphicPoint& b) { return a.f < b.f; });
            if (!graphic.empty()) graphics.push_back(std::move(graphic));
        } else if (line.compare(0, 7, "Filter:") == 0) {
            Filter f;
            if (ParseFilter(line, f) && f.on) filters.push_back(MakeBiquad(f));
        }
    }
    for (size_t i = 0; i < freqs.size(); ++i) {
        double v = 0;
        for (const auto& g : graphics) v += GraphicDb(g, freqs[i]);
        for (const Biquad& b : filters) v += BiquadDb(b, freqs[i]);
        db[i] = v + (includePreamp ? preamp : 0.0);
    }
    return db;
}

std::vector<double> ResponseDb(const Settings& s, const std::vector<double>& freqs, bool includePreamp)
{
    Settings on = s;
    on.enabled = true;   // die Kurve zeigt, was der Equalizer taete, auch wenn er gerade aus ist
    on.balance = 0;      // Balance und Mono betreffen den Frequenzgang nicht (sonst wuerde "Preamp" der einen Seite die Kurve verschieben)
    on.mono = false;
    on.delay = 0;        // Verzoegerung und Mikrofon-Abschnitt gehoeren nicht zur Kurve der Wiedergabe
    on.micMode = 0;
    return ResponseDbFromConfig(BuildConfig(on), freqs, includePreamp);
}

}  // namespace eq
