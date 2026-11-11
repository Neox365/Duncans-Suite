#pragma once
// GiroCode (EPC-QR, Version 002, Zeichensatz UTF-8) fuer Rechnungen: Die Banking-App liest Empfaenger, IBAN, Betrag und Verwendungszweck
// aus dem QR-Code und fuellt die Ueberweisung aus. Reine Logik.
#include <cstdint>
#include <string>

namespace giro {

inline std::wstring NormalizeIban(const std::wstring& in)
{
    std::wstring s;
    for (wchar_t c : in) {
        if (c == L' ' || c == L'\t' || c == 0xA0) continue;
        s.push_back(c >= L'a' && c <= L'z' ? static_cast<wchar_t>(c - 32) : c);
    }
    return s;
}

// Laenge 15 bis 34, zwei Buchstaben + zwei Ziffern + Rest alphanumerisch, Pruefsumme mod 97 == 1
inline bool IbanValid(const std::wstring& in)
{
    const std::wstring s = NormalizeIban(in);
    if (s.size() < 15 || s.size() > 34) return false;
    if (!(s[0] >= L'A' && s[0] <= L'Z' && s[1] >= L'A' && s[1] <= L'Z' && s[2] >= L'0' && s[2] <= L'9' && s[3] >= L'0' && s[3] <= L'9')) return false;
    const std::wstring rot = s.substr(4) + s.substr(0, 4);
    int rem = 0;
    for (wchar_t c : rot) {
        if (c >= L'0' && c <= L'9') rem = (rem * 10 + (c - L'0')) % 97;
        else if (c >= L'A' && c <= L'Z') rem = (rem * 100 + (c - L'A' + 10)) % 97;   // Buchstabe = zwei Ziffern (10..35)
        else return false;
    }
    return rem == 1;
}

inline std::string ToUtf8(const std::wstring& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        uint32_t cp = static_cast<uint32_t>(s[i]);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<uint32_t>(s[i + 1]) - 0xDC00);
            ++i;
        }
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

// Ein Feld des Datensatzes: Zeilenumbrueche und Steuerzeichen werden zu Leerzeichen, vorne/hinten getrimmt, auf maxChars gekuerzt
inline std::wstring Field(const std::wstring& in, size_t maxChars)
{
    std::wstring s;
    for (wchar_t c : in) s.push_back(c < 0x20 ? L' ' : c);
    size_t a = 0, b = s.size();
    while (a < b && s[a] == L' ') ++a;
    while (b > a && s[b - 1] == L' ') --b;
    s = s.substr(a, b - a);
    if (s.size() > maxChars) s.resize(maxChars);
    return s;
}

constexpr size_t kMaxBytes = 213;   // so viel passt in unseren QR-Code (Version 10, Stufe M); der EPC-Standard erlaubt 331

// Nutzdaten des GiroCodes, leer wenn nicht moeglich (ungueltige IBAN, Betrag nicht in 0,01 bis 999 999 999,99 EUR, kein Empfaenger).
// bic darf leer sein (im SEPA-Raum nicht noetig). text = Verwendungszweck (max. 140 Zeichen), wird bei Platzmangel gekuerzt.
inline std::string EpcPayload(const std::wstring& name, const std::wstring& iban, const std::wstring& bic, long long amountCents, const std::wstring& text)
{
    if (!IbanValid(iban) || amountCents < 1 || amountCents > 99999999999LL) return "";
    const std::wstring nm = Field(name, 70);
    if (nm.empty()) return "";
    std::wstring bc = NormalizeIban(bic);
    bool bicOk = bc.size() == 8 || bc.size() == 11;
    for (wchar_t c : bc) bicOk = bicOk && ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9'));
    if (!bicOk) bc.clear();
    const std::wstring amount = L"EUR" + std::to_wstring(amountCents / 100) + L"." + (amountCents % 100 < 10 ? L"0" : L"") + std::to_wstring(amountCents % 100);
    std::wstring tx = Field(text, 140);
    for (;;) {
        const std::string p = "BCD\n002\n1\nSCT\n" + ToUtf8(bc) + "\n" + ToUtf8(nm) + "\n" + ToUtf8(NormalizeIban(iban)) + "\n" + ToUtf8(amount) + "\n\n\n" + ToUtf8(tx);
        if (p.size() <= kMaxBytes) return p;
        if (tx.empty()) return "";
        tx.pop_back();
    }
}

}  // namespace giro
