// Test fuer ByteStrip.h (laeuft auch unter Linux):  g++ -std=c++17 -I.. ByteStripTest.cpp -o t && ./t
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../ByteStrip.h"

using Bytes = std::vector<uint8_t>;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static void seg(Bytes& b, uint8_t marker, const std::string& payload) {
    b.push_back(0xFF); b.push_back(marker);
    size_t len = payload.size() + 2;
    b.push_back((uint8_t)(len >> 8)); b.push_back((uint8_t)len);
    b.insert(b.end(), payload.begin(), payload.end());
}

static bool has(const Bytes& b, const std::string& s) {
    return std::search(b.begin(), b.end(), s.begin(), s.end()) != b.end();
}

static void jpeg() {
    Bytes in = {0xFF, 0xD8};
    seg(in, 0xE0, std::string("JFIF\0\1\1\0\0\1\0\1\0\0", 14));
    seg(in, 0xE1, std::string("Exif\0\0GPSDATA", 13));
    seg(in, 0xE1, "http://ns.adobe.com/xap/1.0/\0XMPDATA");
    seg(in, 0xE2, std::string("ICC_PROFILE\0\1\1ICCDATA", 21));
    seg(in, 0xE2, std::string("MPF\0MPFDATA", 11));
    seg(in, 0xED, "Photoshop 3.0 IPTCDATA");
    seg(in, 0xFE, "Kommentar");
    seg(in, 0xEE, std::string("Adobe\0dADOBEDATA", 16));
    seg(in, 0xDB, std::string(65, 'q'));
    seg(in, 0xC0, std::string(15, 'c'));
    in.push_back(0xFF); in.push_back(0xDA); in.push_back(0); in.push_back(8);
    for (int i = 0; i < 6; ++i) in.push_back((uint8_t)i);
    for (int i = 0; i < 50; ++i) { in.push_back(0xAB); if (i % 10 == 0) in.push_back(0x00); }
    in.push_back(0xFF); in.push_back(0xD9);

    Bytes out;
    CHECK(imgops::bytes::StripJpeg(in, out));
    CHECK(!has(out, "GPSDATA"));
    CHECK(!has(out, "XMPDATA"));
    CHECK(!has(out, "MPFDATA"));
    CHECK(!has(out, "IPTCDATA"));
    CHECK(!has(out, "Kommentar"));
    CHECK(has(out, "JFIF"));
    CHECK(has(out, "ICCDATA"));
    CHECK(has(out, "ADOBEDATA"));
    CHECK(out.size() < in.size());
    CHECK(out[0] == 0xFF && out[1] == 0xD8 && out[out.size() - 2] == 0xFF && out.back() == 0xD9);
    // Bilddaten ab SOS unveraendert
    Bytes tail(in.end() - 70, in.end());
    CHECK(std::search(out.begin(), out.end(), tail.begin(), tail.end()) != out.end());
    // erneutes Strippen aendert nichts mehr
    Bytes again;
    CHECK(imgops::bytes::StripJpeg(out, again) && again == out);
    // kaputte Eingaben
    Bytes bad = {0x00, 0x01, 0x02, 0x03};
    CHECK(!imgops::bytes::StripJpeg(bad, out));
    Bytes trunc(in.begin(), in.begin() + 30);
    CHECK(!imgops::bytes::StripJpeg(trunc, out));
}

static void chunk(Bytes& b, const char* cc, const std::string& payload) {
    b.insert(b.end(), cc, cc + 4);
    uint32_t n = (uint32_t)payload.size();
    for (int i = 0; i < 4; ++i) b.push_back((uint8_t)(n >> (8 * i)));
    b.insert(b.end(), payload.begin(), payload.end());
    if (n & 1) b.push_back(0);
}

static void webp() {
    Bytes in = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P'};
    std::string vp8x(10, '\0');
    vp8x[0] = (char)(0x20 | 0x08 | 0x04);  // ICC + EXIF + XMP
    chunk(in, "VP8X", vp8x);
    chunk(in, "ICCP", "ICCDATA");
    chunk(in, "VP8 ", "BILDDATEN");
    chunk(in, "EXIF", "GPSDATA!");
    chunk(in, "XMP ", "XMPDATA");
    uint32_t riff = (uint32_t)in.size() - 8;
    for (int i = 0; i < 4; ++i) in[4 + i] = (uint8_t)(riff >> (8 * i));

    Bytes out;
    CHECK(imgops::bytes::StripWebp(in, out));
    CHECK(!has(out, "GPSDATA"));
    CHECK(!has(out, "XMPDATA"));
    CHECK(has(out, "ICCDATA"));
    CHECK(has(out, "BILDDATEN"));
    CHECK((out[12 + 8] & 0x0C) == 0 && (out[12 + 8] & 0x20) == 0x20);   // EXIF/XMP-Flags weg, ICC-Flag bleibt
    uint32_t sz = out[4] | (out[5] << 8) | (out[6] << 16) | ((uint32_t)out[7] << 24);
    CHECK(sz == out.size() - 8);
    Bytes bad = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'A', 'V', 'I', ' '};
    CHECK(!imgops::bytes::StripWebp(bad, out));
    Bytes trunc(in.begin(), in.begin() + 20);
    trunc[16] = 0xFF;  // Chunk-Groesse passt nicht
    CHECK(!imgops::bytes::StripWebp(trunc, out));
}

int main() {
    jpeg();
    webp();
    std::printf(failures ? "%d FEHLER\n" : "alle Tests bestanden\n", failures);
    return failures ? 1 : 0;
}
