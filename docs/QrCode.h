#pragma once
// QR-Code-Erzeugung (ISO/IEC 18004): Byte-Modus, Fehlerkorrektur M, Version 1 bis 10 (bis 213 Bytes). Reine Logik ohne Windows-Abhaengigkeit.
// Genug fuer den GiroCode (EPC-QR, typisch 100 bis 200 Bytes). Der Aufbau folgt der Referenzbeschreibung in ISO 18004:
// Datenstrom -> Reed-Solomon je Block -> Verschachteln -> Funktionsmuster -> Zickzack-Platzierung -> beste von 8 Masken.
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace qr {

constexpr int kMaxVersion = 10;
constexpr int kEccPerBlock[kMaxVersion + 1] = {0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26};   // Stufe M
constexpr int kNumBlocks[kMaxVersion + 1] = {0, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5};               // Stufe M

inline int NumRawDataModules(int ver)
{
    int result = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        const int numAlign = ver / 7 + 2;
        result -= (25 * numAlign - 10) * numAlign - 55;
        if (ver >= 7) result -= 36;
    }
    return result;
}

inline int NumDataCodewords(int ver) { return NumRawDataModules(ver) / 8 - kEccPerBlock[ver] * kNumBlocks[ver]; }

// Wie viele Bytes passen (Byte-Modus) in Version ver?
inline int ByteCapacity(int ver)
{
    const int lenBits = ver <= 9 ? 8 : 16;
    return (NumDataCodewords(ver) * 8 - 4 - lenBits) / 8;
}

// ---- Reed-Solomon ueber GF(256), Polynom 0x11D ----
inline uint8_t GfMul(uint8_t x, uint8_t y)
{
    int z = 0;
    for (int i = 7; i >= 0; --i) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return static_cast<uint8_t>(z);
}

inline std::vector<uint8_t> RsGenerator(int degree)
{
    std::vector<uint8_t> result(static_cast<size_t>(degree), 0);
    result.back() = 1;
    uint8_t root = 1;
    for (int i = 0; i < degree; ++i) {
        for (size_t j = 0; j < result.size(); ++j) {
            result[j] = GfMul(result[j], root);
            if (j + 1 < result.size()) result[j] ^= result[j + 1];
        }
        root = GfMul(root, 0x02);
    }
    return result;
}

inline std::vector<uint8_t> RsRemainder(const std::vector<uint8_t>& data, const std::vector<uint8_t>& gen)
{
    std::vector<uint8_t> result(gen.size(), 0);
    for (uint8_t b : data) {
        const uint8_t factor = b ^ result.front();
        result.erase(result.begin());
        result.push_back(0);
        for (size_t i = 0; i < result.size(); ++i) result[i] ^= GfMul(gen[i], factor);
    }
    return result;
}

struct Code {
    int version = 0, size = 0, mask = 0;
    std::vector<std::vector<bool>> modules;   // [y][x], true = dunkel
    bool Dark(int x, int y) const { return x >= 0 && y >= 0 && x < size && y < size && modules[static_cast<size_t>(y)][static_cast<size_t>(x)]; }
};

namespace detail {

struct Builder {
    int ver, size;
    std::vector<std::vector<bool>> mod, func;

    explicit Builder(int v) : ver(v), size(v * 4 + 17), mod(static_cast<size_t>(size), std::vector<bool>(static_cast<size_t>(size), false)),
                              func(static_cast<size_t>(size), std::vector<bool>(static_cast<size_t>(size), false)) {}

    void SetFunc(int x, int y, bool dark)
    {
        mod[static_cast<size_t>(y)][static_cast<size_t>(x)] = dark;
        func[static_cast<size_t>(y)][static_cast<size_t>(x)] = true;
    }

    std::vector<int> AlignPositions() const
    {
        if (ver == 1) return {};
        const int numAlign = ver / 7 + 2;
        const int step = ver == 32 ? 26 : (ver * 4 + numAlign * 2 + 1) / (numAlign * 2 - 2) * 2;
        std::vector<int> r;
        for (int pos = size - 7; static_cast<int>(r.size()) < numAlign - 1; pos -= step) r.insert(r.begin(), pos);
        r.insert(r.begin(), 6);
        return r;
    }

    void DrawFinder(int cx, int cy)
    {
        for (int dy = -4; dy <= 4; ++dy)
            for (int dx = -4; dx <= 4; ++dx) {
                const int dist = std::max(std::abs(dx), std::abs(dy));
                const int x = cx + dx, y = cy + dy;
                if (x >= 0 && x < size && y >= 0 && y < size) SetFunc(x, y, dist != 2 && dist != 4);
            }
    }

    void DrawAlign(int cx, int cy)
    {
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) SetFunc(cx + dx, cy + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
    }

    void DrawFormat(int mask)
    {
        const int data = (0 << 3) | mask;   // Stufe M = 00
        int rem = data;
        for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
        const int bits = ((data << 10) | rem) ^ 0x5412;
        auto bit = [&](int i) { return ((bits >> i) & 1) != 0; };
        for (int i = 0; i <= 5; ++i) SetFunc(8, i, bit(i));
        SetFunc(8, 7, bit(6));
        SetFunc(8, 8, bit(7));
        SetFunc(7, 8, bit(8));
        for (int i = 9; i < 15; ++i) SetFunc(14 - i, 8, bit(i));
        for (int i = 0; i < 8; ++i) SetFunc(size - 1 - i, 8, bit(i));
        for (int i = 8; i < 15; ++i) SetFunc(8, size - 15 + i, bit(i));
        SetFunc(8, size - 8, true);   // festes dunkles Modul
    }

    void DrawFunctionPatterns()
    {
        for (int i = 0; i < size; ++i) {
            SetFunc(6, i, i % 2 == 0);
            SetFunc(i, 6, i % 2 == 0);
        }
        DrawFinder(3, 3);
        DrawFinder(size - 4, 3);
        DrawFinder(3, size - 4);
        const std::vector<int> pos = AlignPositions();
        const int n = static_cast<int>(pos.size());
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j) {
                if ((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0)) continue;   // an den Suchmustern
                DrawAlign(pos[static_cast<size_t>(i)], pos[static_cast<size_t>(j)]);
            }
        DrawFormat(0);   // Platz reservieren, wird nach der Maskenwahl neu geschrieben
        if (ver >= 7) {
            int rem = ver;
            for (int i = 0; i < 12; ++i) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
            const long bits = (static_cast<long>(ver) << 12) | rem;
            for (int i = 0; i < 18; ++i) {
                const bool b = ((bits >> i) & 1) != 0;
                const int a = size - 11 + i % 3, c = i / 3;
                SetFunc(a, c, b);
                SetFunc(c, a, b);
            }
        }
    }

    void DrawCodewords(const std::vector<uint8_t>& data)
    {
        size_t i = 0;
        for (int right = size - 1; right >= 1; right -= 2) {
            if (right == 6) right = 5;
            for (int vert = 0; vert < size; ++vert)
                for (int j = 0; j < 2; ++j) {
                    const int x = right - j;
                    const bool upward = ((right + 1) & 2) == 0;
                    const int y = upward ? size - 1 - vert : vert;
                    if (!func[static_cast<size_t>(y)][static_cast<size_t>(x)] && i < data.size() * 8) {
                        mod[static_cast<size_t>(y)][static_cast<size_t>(x)] = ((data[i >> 3] >> (7 - (i & 7))) & 1) != 0;
                        ++i;
                    }
                }
        }
    }

    void ApplyMask(int m)
    {
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                bool inv = false;
                switch (m) {
                case 0: inv = (x + y) % 2 == 0; break;
                case 1: inv = y % 2 == 0; break;
                case 2: inv = x % 3 == 0; break;
                case 3: inv = (x + y) % 3 == 0; break;
                case 4: inv = (x / 3 + y / 2) % 2 == 0; break;
                case 5: inv = x * y % 2 + x * y % 3 == 0; break;
                case 6: inv = (x * y % 2 + x * y % 3) % 2 == 0; break;
                case 7: inv = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
                }
                if (inv && !func[static_cast<size_t>(y)][static_cast<size_t>(x)])
                    mod[static_cast<size_t>(y)][static_cast<size_t>(x)] = !mod[static_cast<size_t>(y)][static_cast<size_t>(x)];
            }
    }

    long Penalty() const
    {
        long p = 0;
        auto at = [&](int x, int y) { return mod[static_cast<size_t>(y)][static_cast<size_t>(x)]; };
        for (int pass = 0; pass < 2; ++pass)   // N1: Zeilen und Spalten mit 5 oder mehr gleichen Modulen, N3: Suchmuster-aehnliche Folgen
            for (int a = 0; a < size; ++a) {
                int run = 1;
                std::vector<bool> line;
                for (int b = 0; b < size; ++b) line.push_back(pass == 0 ? at(b, a) : at(a, b));
                for (int b = 1; b < size; ++b) {
                    if (line[static_cast<size_t>(b)] == line[static_cast<size_t>(b - 1)]) ++run;
                    else { if (run >= 5) p += 3 + (run - 5); run = 1; }
                }
                if (run >= 5) p += 3 + (run - 5);
                static const int p1[11] = {1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0}, p2[11] = {0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1};
                for (int b = 0; b + 11 <= size; ++b) {
                    bool m1 = true, m2 = true;
                    for (int k = 0; k < 11; ++k) {
                        m1 = m1 && line[static_cast<size_t>(b + k)] == (p1[k] != 0);
                        m2 = m2 && line[static_cast<size_t>(b + k)] == (p2[k] != 0);
                    }
                    if (m1) p += 40;
                    if (m2) p += 40;
                }
            }
        for (int y = 0; y + 1 < size; ++y)   // N2: 2x2-Bloecke gleicher Farbe
            for (int x = 0; x + 1 < size; ++x)
                if (at(x, y) == at(x + 1, y) && at(x, y) == at(x, y + 1) && at(x, y) == at(x + 1, y + 1)) p += 3;
        long dark = 0;   // N4: Anteil dunkler Module
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) dark += at(x, y) ? 1 : 0;
        const long total = static_cast<long>(size) * size;
        const long dev = dark * 20 > total * 10 ? dark * 20 - total * 10 : total * 10 - dark * 20;
        p += ((dev + total - 1) / total - 1) * 10;
        return p;
    }
};

}  // namespace detail

// Erzeugt den QR-Code fuer data (Bytes, z. B. UTF-8). false, wenn mehr als 213 Bytes.
inline bool Encode(const std::string& data, Code& out)
{
    int ver = 0;
    for (int v = 1; v <= kMaxVersion; ++v)
        if (static_cast<int>(data.size()) <= ByteCapacity(v)) { ver = v; break; }
    if (ver == 0) return false;

    // Datenstrom: Modus 0100, Laenge, Daten, Abschluss, Fuellbytes
    std::vector<bool> bits;
    auto put = [&](unsigned value, int count) { for (int i = count - 1; i >= 0; --i) bits.push_back(((value >> i) & 1) != 0); };
    put(0x4, 4);
    put(static_cast<unsigned>(data.size()), ver <= 9 ? 8 : 16);
    for (unsigned char c : data) put(c, 8);
    const size_t capBits = static_cast<size_t>(NumDataCodewords(ver)) * 8;
    for (int i = 0; i < 4 && bits.size() < capBits; ++i) bits.push_back(false);
    while (bits.size() % 8 != 0) bits.push_back(false);
    std::vector<uint8_t> dataCw(bits.size() / 8, 0);
    for (size_t i = 0; i < bits.size(); ++i) dataCw[i >> 3] = static_cast<uint8_t>(dataCw[i >> 3] | (bits[i] ? 1 : 0) << (7 - (i & 7)));
    for (uint8_t pad = 0xEC; dataCw.size() < capBits / 8; pad ^= 0xEC ^ 0x11) dataCw.push_back(pad);

    // Bloecke, Reed-Solomon, Verschachteln
    const int numBlocks = kNumBlocks[ver], eccLen = kEccPerBlock[ver], rawCw = NumRawDataModules(ver) / 8;
    const int numShort = numBlocks - rawCw % numBlocks, shortLen = rawCw / numBlocks;
    const std::vector<uint8_t> gen = RsGenerator(eccLen);
    std::vector<std::vector<uint8_t>> blocks;
    for (int i = 0, k = 0; i < numBlocks; ++i) {
        const int dl = shortLen - eccLen + (i < numShort ? 0 : 1);
        std::vector<uint8_t> dat(dataCw.begin() + k, dataCw.begin() + k + dl);
        k += dl;
        const std::vector<uint8_t> ecc = RsRemainder(dat, gen);
        if (i < numShort) dat.push_back(0);   // Platzhalter, damit alle Bloecke gleich lang sind
        dat.insert(dat.end(), ecc.begin(), ecc.end());
        blocks.push_back(std::move(dat));
    }
    std::vector<uint8_t> all;
    for (int i = 0; i < static_cast<int>(blocks[0].size()); ++i)
        for (int j = 0; j < numBlocks; ++j)
            if (i != shortLen - eccLen || j >= numShort) all.push_back(blocks[static_cast<size_t>(j)][static_cast<size_t>(i)]);

    detail::Builder b(ver);
    b.DrawFunctionPatterns();
    b.DrawCodewords(all);
    int best = 0;
    long bestPenalty = -1;
    for (int m = 0; m < 8; ++m) {
        b.ApplyMask(m);
        b.DrawFormat(m);
        const long p = b.Penalty();
        if (bestPenalty < 0 || p < bestPenalty) { best = m; bestPenalty = p; }
        b.ApplyMask(m);   // Maske zuruecknehmen (XOR)
    }
    b.ApplyMask(best);
    b.DrawFormat(best);
    out.version = ver;
    out.size = b.size;
    out.mask = best;
    out.modules = b.mod;
    return true;
}

}  // namespace qr
