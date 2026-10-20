#pragma once
// Verlustfreies Entfernen von Metadaten auf Byte-Ebene (JPEG, WebP). Plattformunabhaengig und deshalb
// einzeln testbar (photo/tests/ByteStripTest.cpp).
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace imgops::bytes {

inline bool IsIccSegment(const std::vector<uint8_t>& d, size_t payload, size_t len) {
    static const char tag[] = "ICC_PROFILE";
    return len >= sizeof(tag) && memcmp(&d[payload], tag, sizeof(tag) - 1) == 0;
}

// Behaelt: JFIF (APP0), ICC-Profil (APP2), Adobe (APP14) und alle Bildsegmente.
// Entfernt: Exif/XMP (APP1), MPF & Co (APP2 ohne ICC), APP3..APP13, APP15, Kommentare.
inline bool StripJpeg(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    if (in.size() < 4 || in[0] != 0xFF || in[1] != 0xD8) return false;
    out.clear();
    out.reserve(in.size());
    out.push_back(0xFF);
    out.push_back(0xD8);
    size_t i = 2;
    while (i + 2 <= in.size()) {
        if (in[i] != 0xFF) return false;
        uint8_t m = in[i + 1];
        if (m == 0xFF) { ++i; continue; }  // Fuell-Bytes
        if (m == 0xDA || m == 0xD9) {      // SOS / EOI: Rest unveraendert uebernehmen
            out.insert(out.end(), in.begin() + i, in.end());
            return true;
        }
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD8)) {  // Marker ohne Laenge
            out.push_back(0xFF);
            out.push_back(m);
            i += 2;
            continue;
        }
        if (i + 4 > in.size()) return false;
        size_t len = ((size_t)in[i + 2] << 8) | in[i + 3];
        if (len < 2 || i + 2 + len > in.size()) return false;
        bool drop = m == 0xE1 || (m >= 0xE3 && m <= 0xED) || m == 0xEF || m == 0xFE ||
                    (m == 0xE2 && !IsIccSegment(in, i + 4, len - 2));
        if (!drop) out.insert(out.end(), in.begin() + i, in.begin() + i + 2 + len);
        i += 2 + len;
    }
    return false;
}

// WebP (RIFF): entfernt die Chunks EXIF und XMP verlustfrei und loescht die zugehoerigen Flags im VP8X-Chunk.
inline bool StripWebp(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    if (in.size() < 12 || memcmp(in.data(), "RIFF", 4) != 0 || memcmp(in.data() + 8, "WEBP", 4) != 0) return false;
    out.assign(in.begin(), in.begin() + 12);
    size_t pos = 12;
    while (pos + 8 <= in.size()) {
        const size_t sz = (size_t)in[pos + 4] | ((size_t)in[pos + 5] << 8) | ((size_t)in[pos + 6] << 16) | ((size_t)in[pos + 7] << 24);
        if (sz > in.size() - pos - 8) return false;
        const size_t total = std::min(in.size() - pos, 8 + sz + (sz & 1));
        const bool drop = memcmp(&in[pos], "EXIF", 4) == 0 || memcmp(&in[pos], "XMP ", 4) == 0;
        if (!drop) {
            const size_t at = out.size();
            out.insert(out.end(), in.begin() + pos, in.begin() + pos + total);
            if (memcmp(&in[pos], "VP8X", 4) == 0 && sz >= 10) out[at + 8] &= (uint8_t)~(0x08 | 0x04);
        }
        pos += total;
    }
    const size_t riff = out.size() - 8;
    for (int i = 0; i < 4; ++i) out[4 + i] = (uint8_t)(riff >> (8 * i));
    return true;
}

}  // namespace imgops::bytes
