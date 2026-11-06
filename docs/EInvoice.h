#pragma once
// E-Rechnung (XRechnung / ZUGFeRD / Factur-X) lesen: Rechnungsnummer, Datum, Faelligkeit, Rechnungssteller und Betrag aus dem XML.
// Unterstuetzt die beiden Syntaxen der EN 16931: UN/CEFACT CII (XRechnung-CII, ZUGFeRD, Factur-X) und UBL (XRechnung-UBL, Rechnung und Gutschrift).
// Es wird nichts validiert oder veraendert; die Datei dient nur zum Vorbelegen von Betrag, Faelligkeit und Gläubiger beim Import.
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

#include "Store.h"

namespace docs {

struct EInvoice {
    bool valid = false;            // Betrag gefunden
    bool credit = false;           // Gutschrift (Betrag wird negativ)
    std::wstring number;           // Rechnungsnummer (BT-1)
    std::wstring issueIso;         // Rechnungsdatum (BT-2), ISO
    std::wstring dueIso;           // Faelligkeit (BT-9), ISO, kann leer sein
    std::wstring seller;           // Rechnungssteller (BT-27)
    std::wstring iban;             // Zahlungskonto (BT-84), kann leer sein
    std::wstring currency;         // z. B. "EUR"
    long long cents = kUnknown;    // zu zahlender Betrag (BT-115, sonst BT-112), negativ bei Gutschrift
    const wchar_t* format = L"";   // "XRechnung/ZUGFeRD (CII)" oder "XRechnung (UBL)"
};

namespace einv {

inline std::string Trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

inline void AppendUtf8(std::string& out, unsigned long cp)
{
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
}

inline std::string DecodeEntities(std::string s)
{
    {   // numerische Entities &#246; und &#xF6;
        std::string out;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '&' && i + 3 < s.size() && s[i + 1] == '#') {
                const size_t semi = s.find(';', i);
                if (semi != std::string::npos && semi - i <= 10) {
                    const bool hex = s[i + 2] == 'x' || s[i + 2] == 'X';
                    const std::string num = s.substr(i + (hex ? 3 : 2), semi - i - (hex ? 3 : 2));
                    char* end = nullptr;
                    const unsigned long cp = num.empty() ? 0 : std::strtoul(num.c_str(), &end, hex ? 16 : 10);
                    if (!num.empty() && end && *end == '\0' && cp > 0 && cp < 0x110000) { AppendUtf8(out, cp); i = semi; continue; }
                }
            }
            out += s[i];
        }
        s.swap(out);
    }
    struct E { const char* from; const char* to; };
    static const E table[] = { { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" }, { "&apos;", "'" }, { "&amp;", "&" } };
    for (const E& e : table) {
        size_t p = 0;
        const std::string from = e.from;
        while ((p = s.find(from, p)) != std::string::npos) { s.replace(p, from.size(), e.to); p += std::strlen(e.to); }
    }
    return s;
}

// Erstes Element mit dem lokalen Namen (Namensraum-Vorsatz egal) ab Position from; inner = Inhalt zwischen den Tags
inline bool Element(const std::string& xml, const std::string& local, size_t from, std::string& inner)
{
    size_t pos = from;
    while ((pos = xml.find('<', pos)) != std::string::npos) {
        const size_t gt = xml.find('>', pos);
        if (gt == std::string::npos) return false;
        const char c = pos + 1 < xml.size() ? xml[pos + 1] : '\0';
        if (c == '/' || c == '?' || c == '!') { pos = gt + 1; continue; }
        size_t nameEnd = pos + 1;
        while (nameEnd < gt && !std::isspace(static_cast<unsigned char>(xml[nameEnd])) && xml[nameEnd] != '/') ++nameEnd;
        const std::string qn = xml.substr(pos + 1, nameEnd - pos - 1);
        const size_t colon = qn.find(':');
        if ((colon == std::string::npos ? qn : qn.substr(colon + 1)) == local) {
            if (xml[gt - 1] == '/') { inner.clear(); return true; }
            const std::string close = "</" + qn + ">";
            const size_t e = xml.find(close, gt + 1);
            if (e == std::string::npos) return false;
            inner = xml.substr(gt + 1, e - gt - 1);
            return true;
        }
        pos = gt + 1;
    }
    return false;
}

inline std::string Section(const std::string& xml, const std::string& local)
{
    std::string in;
    return Element(xml, local, 0, in) ? in : std::string();
}

inline std::string Text(const std::string& xml, const std::string& local)
{
    std::string in;
    return Element(xml, local, 0, in) ? DecodeEntities(Trim(in)) : std::string();
}

// "1190.00" / "-12.5" / "0.005" -> Cent (auf volle Cent gerundet); false bei ungueltigem Text
inline bool DecimalToCents(const std::string& in, long long& cents)
{
    const std::string s = Trim(in);
    size_t i = 0;
    bool neg = false;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) { neg = s[i] == '-'; ++i; }
    long long whole = 0;
    int digits = 0;
    for (; i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])); ++i, ++digits) {
        whole = whole * 10 + (s[i] - '0');
        if (whole > 100000000000000LL) return false;
    }
    long long frac = 0;
    int fd = 0, third = 0;
    if (i < s.size() && s[i] == '.') {
        for (++i; i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])); ++i, ++digits) {
            if (fd < 2) { frac = frac * 10 + (s[i] - '0'); ++fd; }
            else if (fd == 2 && third == 0) { third = s[i] - '0'; ++fd; }
        }
    }
    if (i != s.size() || digits == 0) return false;
    while (fd < 2) { frac *= 10; ++fd; }
    long long v = whole * 100 + frac + (third >= 5 ? 1 : 0);
    cents = neg ? -v : v;
    return true;
}

inline std::wstring DateToIso(const std::string& in)
{
    const std::string s = Trim(in);
    std::string digits;
    for (char c : s) if (std::isdigit(static_cast<unsigned char>(c))) digits += c;
    if (digits.size() < 8) return L"";
    const int y = std::stoi(digits.substr(0, 4)), m = std::stoi(digits.substr(4, 2)), d = std::stoi(digits.substr(6, 2));
    return ValidDate(y, m, d) ? IsoFrom(y, m, d) : std::wstring();
}

inline std::wstring ToWide(const std::string& s) { return changelog::Utf8ToWide(s); }

}  // namespace einv

// Liest eine E-Rechnung aus XML (UTF-8). false, wenn das keine erkennbare E-Rechnung mit Betrag ist.
inline bool ParseEInvoice(const std::string& xml, EInvoice& out)
{
    out = EInvoice{};
    using namespace einv;
    const bool cii = xml.find("CrossIndustryInvoice") != std::string::npos;
    const bool ubl = !cii && (xml.find("ubl:schema:xsd:Invoice-2") != std::string::npos || xml.find("ubl:schema:xsd:CreditNote-2") != std::string::npos);
    if (!cii && !ubl) return false;
    long long cents = 0;
    if (cii) {
        out.format = L"XRechnung/ZUGFeRD (CII)";
        const std::string doc = Section(xml, "ExchangedDocument");
        out.number = ToWide(Text(doc, "ID"));
        out.credit = Text(doc, "TypeCode") == "381";
        out.issueIso = DateToIso(Text(Section(doc, "IssueDateTime"), "DateTimeString"));
        out.seller = ToWide(Text(Section(xml, "SellerTradeParty"), "Name"));
        out.currency = ToWide(Text(xml, "InvoiceCurrencyCode"));
        out.iban = ToWide(Text(Section(xml, "PayeePartyCreditorFinancialAccount"), "IBANID"));
        out.dueIso = DateToIso(Text(Section(Section(xml, "SpecifiedTradePaymentTerms"), "DueDateDateTime"), "DateTimeString"));
        const std::string sum = Section(xml, "SpecifiedTradeSettlementHeaderMonetarySummation");
        std::string amount = Text(sum, "DuePayableAmount");
        if (amount.empty()) amount = Text(sum, "GrandTotalAmount");
        if (!DecimalToCents(amount, cents)) return false;
    } else {
        out.format = L"XRechnung (UBL)";
        out.number = ToWide(Text(xml, "ID"));
        const std::string typeCode = Text(xml, "InvoiceTypeCode");
        out.credit = typeCode == "381" || xml.find("<CreditNote") != std::string::npos || xml.find(":CreditNote ") != std::string::npos || xml.find(":CreditNote>") != std::string::npos;
        out.issueIso = DateToIso(Text(xml, "IssueDate"));
        out.dueIso = DateToIso(Text(xml, "DueDate"));
        out.currency = ToWide(Text(xml, "DocumentCurrencyCode"));
        const std::string supplier = Section(xml, "AccountingSupplierParty");
        std::string name = Text(Section(supplier, "PartyLegalEntity"), "RegistrationName");
        if (name.empty()) name = Text(Section(supplier, "PartyName"), "Name");
        out.seller = ToWide(name);
        out.iban = ToWide(Text(Section(xml, "PayeeFinancialAccount"), "ID"));
        const std::string total = Section(xml, "LegalMonetaryTotal");
        std::string amount = Text(total, "PayableAmount");
        if (amount.empty()) amount = Text(total, "TaxInclusiveAmount");
        if (!DecimalToCents(amount, cents)) return false;
    }
    out.cents = out.credit ? -std::llabs(cents) : cents;
    out.valid = true;
    return true;
}

// Fallback ohne PDFium: sucht ein unkomprimiert eingebettetes CII-XML (ZUGFeRD/Factur-X) in den Rohdaten einer PDF; leer, wenn keins da ist
inline std::string FindEmbeddedXml(const std::string& raw)
{
    size_t p = 0;
    while ((p = raw.find("CrossIndustryInvoice", p)) != std::string::npos) {
        const size_t lt = raw.rfind('<', p);
        if (lt == std::string::npos) return std::string();
        if (raw[lt + 1] != '/') {
            const size_t decl = raw.rfind("<?xml", lt);
            const size_t begin = (decl != std::string::npos && lt - decl < 200) ? decl : lt;
            const size_t close = raw.find("CrossIndustryInvoice>", lt);
            if (close != std::string::npos) return raw.substr(begin, close + std::strlen("CrossIndustryInvoice>") - begin);
            return std::string();
        }
        p += 20;
    }
    return std::string();
}

}  // namespace docs
