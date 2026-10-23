#pragma once
// Textkodierung fuer Duncan's Office: UTF-8 (mit/ohne BOM), UTF-16 LE und Windows-1252 als Rueckfall.
// Reine Logik ohne Win32, damit sie getrennt testbar bleibt.
#include <string>

namespace codec {

// Strenge UTF-8-Dekodierung; ok = false bei ungueltigen Bytefolgen (dann ist das Ergebnis unbrauchbar)
inline std::wstring Utf8ToWide(const std::string& s, bool* ok = nullptr)
{
    std::wstring out;
    out.reserve(s.size());
    bool good = true;
    size_t i = 0;
    while (i < s.size() && good) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned long cp = 0;
        int extra = 0;
        if (c < 0x80) { cp = c; }
        else if ((c & 0xE0) == 0xC0 && c >= 0xC2) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0 && c <= 0xF4) { cp = c & 0x07; extra = 3; }
        else { good = false; break; }
        if (extra && i + static_cast<size_t>(extra) >= s.size()) { good = false; break; }
        for (int k = 1; k <= extra; ++k) {
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) { good = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!good) break;
        if ((extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { good = false; break; }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        }
        else out.push_back(static_cast<wchar_t>(cp));
        i += 1 + extra;
    }
    if (ok) *ok = good;
    return out;
}

inline std::string WideToUtf8(const std::wstring& w)
{
    std::string out;
    out.reserve(w.size() + w.size() / 4);
    for (size_t i = 0; i < w.size(); ++i) {
        unsigned long cp = w[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size() && w[i + 1] >= 0xDC00 && w[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<unsigned long>(w[i + 1]) - 0xDC00);
            ++i;
        }
        else if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;   // einzelnes Surrogat
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) { out.push_back(static_cast<char>(0xE0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        else { out.push_back(static_cast<char>(0xF0 | (cp >> 18))); out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    }
    return out;
}

inline std::wstring Cp1252ToWide(const std::string& s)
{
    static const wchar_t hi[32] = { 0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
                                    0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178 };
    std::wstring out;
    out.reserve(s.size());
    for (unsigned char c : s) out.push_back(c >= 0x80 && c < 0xA0 ? hi[c - 0x80] : static_cast<wchar_t>(c));
    return out;
}

// Erkennt BOM (UTF-8, UTF-16 LE), sonst gueltiges UTF-8, sonst Windows-1252
inline std::wstring DecodeAuto(const std::string& b)
{
    if (b.size() >= 3 && static_cast<unsigned char>(b[0]) == 0xEF && static_cast<unsigned char>(b[1]) == 0xBB && static_cast<unsigned char>(b[2]) == 0xBF)
        return Utf8ToWide(b.substr(3));
    if (b.size() >= 2 && static_cast<unsigned char>(b[0]) == 0xFF && static_cast<unsigned char>(b[1]) == 0xFE) {
        std::wstring w;
        for (size_t i = 2; i + 1 < b.size(); i += 2)
            w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(b[i]) | (static_cast<unsigned char>(b[i + 1]) << 8)));
        return w;
    }
    bool ok = false;
    std::wstring w = Utf8ToWide(b, &ok);
    return ok ? w : Cp1252ToWide(b);
}

// UTF-8 mit BOM, damit Excel und Editoren Umlaute sicher erkennen
inline std::string EncodeUtf8Bom(const std::wstring& w) { return std::string("\xEF\xBB\xBF") + WideToUtf8(w); }

}  // namespace codec
