#pragma once
// ZIP lesen und schreiben fuer Duncan's Office (DOCX ist ein ZIP-Archiv mit XML-Dateien).
// Lesen: Eintraege "gespeichert" (Methode 0) und "Deflate" (Methode 8); eigener Inflate-Dekoder nach dem Verfahren von zlibs "puff",
// CRC-32 wird geprueft, Ausgabe ist begrenzt (Schutz vor Zip-Bomben). Es wird nie etwas auf die Platte entpackt, nur benannte Eintraege gelesen.
// Schreiben: nur "gespeicherte" Eintraege (ohne Kompression), das ist ein gueltiges ZIP, das Word und andere Programme lesen.
// Reine Logik ohne Win32, damit sie getrennt testbar bleibt.
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace zip {

inline uint32_t Crc32(const void* data, size_t n, uint32_t crc = 0)
{
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = ~crc;
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

namespace detail {

struct Huff {
    unsigned short count[16];
    unsigned short symbol[320];
};

struct State {
    const unsigned char* in = nullptr;
    size_t len = 0, pos = 0;
    unsigned long bitbuf = 0;
    int bitcnt = 0;
    std::string* out = nullptr;
    size_t maxOut = 0;
    bool err = false;
};

inline int Bits(State& s, int need)
{
    unsigned long val = s.bitbuf;
    while (s.bitcnt < need) {
        if (s.pos >= s.len) { s.err = true; return 0; }
        val |= static_cast<unsigned long>(s.in[s.pos++]) << s.bitcnt;
        s.bitcnt += 8;
    }
    s.bitbuf = val >> need;
    s.bitcnt -= need;
    return static_cast<int>(val & ((1UL << need) - 1));
}

inline int Decode(State& s, const Huff& h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; ++len) {
        code |= Bits(s, 1);
        if (s.err) return -1;
        const int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

inline int Construct(Huff& h, const short* length, int n)
{
    for (int len = 0; len <= 15; ++len) h.count[len] = 0;
    for (int sym = 0; sym < n; ++sym) ++h.count[length[sym]];
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= 15; ++len) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return left;
    }
    unsigned short offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; ++len) offs[len + 1] = static_cast<unsigned short>(offs[len] + h.count[len]);
    for (int sym = 0; sym < n; ++sym)
        if (length[sym] != 0) h.symbol[offs[length[sym]]++] = static_cast<unsigned short>(sym);
    return left;
}

inline bool Codes(State& s, const Huff& lencode, const Huff& distcode)
{
    static const short lens[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static const short lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    static const short dists[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
    static const short dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
    for (;;) {
        int sym = Decode(s, lencode);
        if (sym < 0 || s.err) return false;
        if (sym < 256) {
            if (s.out->size() >= s.maxOut) return false;
            s.out->push_back(static_cast<char>(sym));
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29) return false;
            int len = lens[sym] + Bits(s, lext[sym]);
            const int ds = Decode(s, distcode);
            if (ds < 0 || ds >= 30 || s.err) return false;
            const size_t dist = static_cast<size_t>(dists[ds] + Bits(s, dext[ds]));
            if (s.err || dist > s.out->size() || s.out->size() + static_cast<size_t>(len) > s.maxOut) return false;
            size_t from = s.out->size() - dist;
            while (len--) s.out->push_back((*s.out)[from++]);
        }
    }
}

inline bool Fixed(State& s)
{
    static bool built = false;
    static Huff lencode, distcode;
    if (!built) {
        short lengths[288];
        int sym = 0;
        for (; sym < 144; ++sym) lengths[sym] = 8;
        for (; sym < 256; ++sym) lengths[sym] = 9;
        for (; sym < 280; ++sym) lengths[sym] = 7;
        for (; sym < 288; ++sym) lengths[sym] = 8;
        Construct(lencode, lengths, 288);
        for (sym = 0; sym < 30; ++sym) lengths[sym] = 5;
        Construct(distcode, lengths, 30);
        built = true;
    }
    return Codes(s, lencode, distcode);
}

inline bool Dynamic(State& s)
{
    static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    const int nlen = Bits(s, 5) + 257, ndist = Bits(s, 5) + 1, ncode = Bits(s, 4) + 4;
    if (s.err || nlen > 286 || ndist > 30) return false;
    short lengths[320];
    int index = 0;
    for (; index < ncode; ++index) lengths[order[index]] = static_cast<short>(Bits(s, 3));
    for (; index < 19; ++index) lengths[order[index]] = 0;
    Huff lencode, distcode;
    if (Construct(lencode, lengths, 19) != 0) return false;
    index = 0;
    while (index < nlen + ndist) {
        int sym = Decode(s, lencode);
        if (sym < 0 || s.err) return false;
        if (sym < 16) { lengths[index++] = static_cast<short>(sym); continue; }
        int len = 0, rep;
        if (sym == 16) {
            if (index == 0) return false;
            len = lengths[index - 1];
            rep = 3 + Bits(s, 2);
        } else if (sym == 17) rep = 3 + Bits(s, 3);
        else rep = 11 + Bits(s, 7);
        if (index + rep > nlen + ndist) return false;
        while (rep--) lengths[index++] = static_cast<short>(len);
    }
    if (lengths[256] == 0) return false;
    int err = Construct(lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) return false;
    err = Construct(distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) return false;
    return Codes(s, lencode, distcode);
}

}  // namespace detail

// Rohes Deflate (ohne zlib-Kopf) entpacken; false bei kaputten Daten oder wenn mehr als maxOut Bytes entstuenden
inline bool Inflate(const unsigned char* src, size_t srcLen, std::string& out, size_t maxOut)
{
    out.clear();
    detail::State s;
    s.in = src;
    s.len = srcLen;
    s.out = &out;
    s.maxOut = maxOut;
    int last;
    do {
        last = detail::Bits(s, 1);
        const int type = detail::Bits(s, 2);
        if (s.err) return false;
        bool ok;
        if (type == 0) {
            s.bitbuf = 0;
            s.bitcnt = 0;
            if (s.pos + 4 > s.len) return false;
            const unsigned len = s.in[s.pos] | (s.in[s.pos + 1] << 8), nlen = s.in[s.pos + 2] | (s.in[s.pos + 3] << 8);
            s.pos += 4;
            if (len != (~nlen & 0xFFFF) || s.pos + len > s.len || out.size() + len > maxOut) return false;
            out.append(reinterpret_cast<const char*>(s.in + s.pos), len);
            s.pos += len;
            ok = true;
        } else if (type == 1) ok = detail::Fixed(s);
        else if (type == 2) ok = detail::Dynamic(s);
        else return false;
        if (!ok) return false;
    } while (!last);
    return true;
}

struct Entry {
    std::string name;
    int method = 0;
    uint32_t crc = 0, csize = 0, usize = 0, offset = 0;
};

namespace detail {
inline uint32_t U16(const std::string& d, size_t p) { return static_cast<unsigned char>(d[p]) | (static_cast<unsigned char>(d[p + 1]) << 8); }
inline uint32_t U32(const std::string& d, size_t p) { return U16(d, p) | (U16(d, p + 2) << 16); }
inline void Put16(std::string& o, uint32_t v) { o.push_back(static_cast<char>(v & 0xFF)); o.push_back(static_cast<char>((v >> 8) & 0xFF)); }
inline void Put32(std::string& o, uint32_t v) { Put16(o, v & 0xFFFF); Put16(o, v >> 16); }
}  // namespace detail

// Zentrales Verzeichnis lesen; false, wenn das kein (unterstuetztes) ZIP ist
inline bool ReadDirectory(const std::string& data, std::vector<Entry>& entries)
{
    entries.clear();
    if (data.size() < 22) return false;
    size_t eocd = std::string::npos;
    const size_t lowest = data.size() > 22 + 65535 ? data.size() - 22 - 65535 : 0;
    for (size_t p = data.size() - 22 + 1; p-- > lowest;)
        if (detail::U32(data, p) == 0x06054B50u) { eocd = p; break; }
    if (eocd == std::string::npos) return false;
    const uint32_t count = detail::U16(data, eocd + 10), cdOffset = detail::U32(data, eocd + 16);
    if (count == 0xFFFF || cdOffset == 0xFFFFFFFFu || cdOffset >= data.size()) return false;     // ZIP64: nicht unterstuetzt
    size_t p = cdOffset;
    for (uint32_t i = 0; i < count; ++i) {
        if (p + 46 > data.size() || detail::U32(data, p) != 0x02014B50u) return false;
        Entry e;
        e.method = static_cast<int>(detail::U16(data, p + 10));
        e.crc = detail::U32(data, p + 16);
        e.csize = detail::U32(data, p + 20);
        e.usize = detail::U32(data, p + 24);
        const uint32_t nameLen = detail::U16(data, p + 28), extra = detail::U16(data, p + 30), comment = detail::U16(data, p + 32);
        e.offset = detail::U32(data, p + 42);
        if (p + 46 + nameLen > data.size()) return false;
        e.name = data.substr(p + 46, nameLen);
        entries.push_back(std::move(e));
        p += 46 + nameLen + extra + comment;
    }
    return true;
}

// Inhalt eines Eintrags (maxSize begrenzt die entpackte Groesse); false bei Fehler, Verschluesselung oder falscher Pruefsumme
inline bool Extract(const std::string& data, const Entry& e, std::string& out, size_t maxSize = 64u * 1024 * 1024)
{
    out.clear();
    if (e.usize > maxSize || e.csize == 0xFFFFFFFFu || e.usize == 0xFFFFFFFFu) return false;
    if (static_cast<size_t>(e.offset) + 30 > data.size() || detail::U32(data, e.offset) != 0x04034B50u) return false;
    const uint32_t flags = detail::U16(data, e.offset + 6);
    if (flags & 1) return false;                                              // verschluesselt
    const size_t start = static_cast<size_t>(e.offset) + 30 + detail::U16(data, e.offset + 26) + detail::U16(data, e.offset + 28);
    if (start + e.csize > data.size()) return false;
    if (e.method == 0) {
        if (e.csize != e.usize) return false;
        out.assign(data, start, e.csize);
    } else if (e.method == 8) {
        if (!Inflate(reinterpret_cast<const unsigned char*>(data.data()) + start, e.csize, out, maxSize)) return false;
        if (out.size() != e.usize) return false;
    } else return false;
    return Crc32(out.data(), out.size()) == e.crc;
}

// Eintrag nach Name (ohne Gross-/Kleinschreibung) lesen
inline bool ReadEntry(const std::string& data, const std::vector<Entry>& entries, const std::string& name, std::string& out, size_t maxSize = 64u * 1024 * 1024)
{
    auto lower = [](std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); return s; };
    const std::string want = lower(name);
    for (const Entry& e : entries)
        if (lower(e.name) == want) return Extract(data, e, out, maxSize);
    return false;
}

// ZIP aus (Name, Inhalt)-Paaren erzeugen, ohne Kompression
inline std::string Write(const std::vector<std::pair<std::string, std::string>>& files)
{
    using namespace detail;
    std::string out, cd;
    for (const auto& f : files) {
        const uint32_t crc = Crc32(f.second.data(), f.second.size());
        const uint32_t offset = static_cast<uint32_t>(out.size());
        Put32(out, 0x04034B50u); Put16(out, 20); Put16(out, 0x0800); Put16(out, 0);          // Version 2.0, UTF-8-Namen, gespeichert
        Put16(out, 0); Put16(out, 0x0021);                                                     // Zeit 00:00, Datum 01.01.1980
        Put32(out, crc); Put32(out, static_cast<uint32_t>(f.second.size())); Put32(out, static_cast<uint32_t>(f.second.size()));
        Put16(out, static_cast<uint32_t>(f.first.size())); Put16(out, 0);
        out += f.first;
        out += f.second;
        Put32(cd, 0x02014B50u); Put16(cd, 20); Put16(cd, 20); Put16(cd, 0x0800); Put16(cd, 0);
        Put16(cd, 0); Put16(cd, 0x0021);
        Put32(cd, crc); Put32(cd, static_cast<uint32_t>(f.second.size())); Put32(cd, static_cast<uint32_t>(f.second.size()));
        Put16(cd, static_cast<uint32_t>(f.first.size())); Put16(cd, 0); Put16(cd, 0); Put16(cd, 0); Put16(cd, 0); Put32(cd, 0);
        Put32(cd, offset);
        cd += f.first;
    }
    const uint32_t cdOffset = static_cast<uint32_t>(out.size());
    out += cd;
    Put32(out, 0x06054B50u); Put16(out, 0); Put16(out, 0);
    Put16(out, static_cast<uint32_t>(files.size())); Put16(out, static_cast<uint32_t>(files.size()));
    Put32(out, static_cast<uint32_t>(cd.size())); Put32(out, cdOffset); Put16(out, 0);
    return out;
}

}  // namespace zip
