// Tests fuer den QR-Code-Erzeuger (QrCode.h). Reine Logik.
// g++ -std=c++17 -I.. QrTest.cpp -o QrTest.exe
// Mit einem Ordner als Argument schreibt der Test zusaetzlich jeden erzeugten Code als Textdatei (0/1-Matrix) dorthin,
// damit ein unabhaengiger Decoder (siehe tools/CheckQr.py) ihn lesen kann.
#include "../QrCode.h"

#include <cstdio>
#include <string>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

static std::string Payload(size_t n)
{
    std::string s;
    for (size_t i = 0; i < n; ++i) s.push_back(static_cast<char>('A' + (i * 7 + i / 5) % 26));
    return s;
}

int main(int argc, char** argv)
{
    // Kapazitaeten (Byte-Modus, Stufe M) laut ISO 18004
    const int cap[] = {0, 14, 26, 42, 62, 84, 106, 122, 152, 180, 213};
    for (int v = 1; v <= 10; ++v) CHECK(qr::ByteCapacity(v) == cap[v]);

    // Reed-Solomon: bekanntes Beispiel (Version 1-M, "HELLO WORLD" alphanumerisch): Datenwoerter -> 10 Pruefwoerter
    const std::vector<uint8_t> data = {32, 91, 11, 120, 209, 114, 220, 77, 67, 64, 236, 17, 236, 17, 236, 17};
    const std::vector<uint8_t> ecc = qr::RsRemainder(data, qr::RsGenerator(10));
    const std::vector<uint8_t> expect = {196, 35, 39, 119, 235, 215, 231, 226, 93, 23};
    CHECK(ecc == expect);

    // Version und Groesse je Nutzlast
    qr::Code c;
    CHECK(qr::Encode(Payload(10), c) && c.version == 1 && c.size == 21);
    CHECK(qr::Encode(Payload(14), c) && c.version == 1);
    CHECK(qr::Encode(Payload(15), c) && c.version == 2 && c.size == 25);
    CHECK(qr::Encode(Payload(106), c) && c.version == 6 && c.size == 41);
    CHECK(qr::Encode(Payload(107), c) && c.version == 7 && c.size == 45);
    CHECK(qr::Encode(Payload(213), c) && c.version == 10 && c.size == 57);
    CHECK(!qr::Encode(Payload(214), c));
    CHECK(qr::Encode("", c) && c.version == 1);
    // Feste Muster: Suchmuster in den drei Ecken, dunkles Modul, Zeitmuster
    CHECK(qr::Encode(Payload(30), c));
    for (int dy = 0; dy < 7; ++dy)
        for (int dx = 0; dx < 7; ++dx) {
            const bool ring = dx == 0 || dx == 6 || dy == 0 || dy == 6, core = dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4;
            CHECK(c.Dark(dx, dy) == (ring || core));
            CHECK(c.Dark(c.size - 7 + dx, dy) == (ring || core));
            CHECK(c.Dark(dx, c.size - 7 + dy) == (ring || core));
        }
    CHECK(c.Dark(8, c.size - 8));
    for (int i = 8; i < c.size - 8; ++i) CHECK(c.Dark(i, 6) == (i % 2 == 0) && c.Dark(6, i) == (i % 2 == 0));

    // Alle Versionen mit Rand- und Mittelwerten fuer den externen Decoder
    if (argc > 1) {
        const size_t lens[] = {1, 14, 15, 26, 27, 42, 43, 62, 63, 84, 85, 106, 107, 122, 123, 152, 153, 180, 181, 213};
        for (size_t n : lens) {
            qr::Code k;
            if (!qr::Encode(Payload(n), k)) { std::printf("FEHLER Encode %zu\n", n); ++g_fail; continue; }
            const std::string name = std::string(argv[1]) + "/qr_" + std::to_string(n) + ".txt";
            FILE* f = std::fopen(name.c_str(), "wb");
            if (!f) { std::printf("FEHLER: %s nicht schreibbar\n", name.c_str()); ++g_fail; continue; }
            std::fprintf(f, "%s\n", Payload(n).c_str());
            for (int y = 0; y < k.size; ++y) {
                for (int x = 0; x < k.size; ++x) std::fputc(k.Dark(x, y) ? '1' : '0', f);
                std::fputc('\n', f);
            }
            std::fclose(f);
        }
    }

    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
