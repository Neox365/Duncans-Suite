#pragma once
// Betrag und Faelligkeit aus dem Text einer Rechnung (z. B. aus dem PDF) erraten. Reine Logik ohne Windows-Abhaengigkeit (testbar).
// Es wird nur geraten, wenn ein Stichwort davorsteht ("Gesamtbetrag", "zahlbar bis" ...). Das Ergebnis ist ein Vorschlag, der Nutzer prueft ihn.
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

#include "Store.h"

namespace docs {

struct ScannedInvoice {
    bool hasAmount = false;
    long long cents = 0;
    bool hasDue = false;
    std::wstring dueIso;
};

namespace scan_detail {

inline std::wstring LowerAll(std::wstring s)
{
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

inline bool IsDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }

// Alle Geldbetraege mit zwei Nachkommastellen im Fenster [from, from + window) des Textes; "19,00 %" (Prozent) und Datumsangaben zaehlen nicht.
inline std::vector<long long> AmountsAfter(const std::wstring& t, size_t from, size_t window)
{
    std::vector<long long> out;
    const size_t end = std::min(t.size(), from + window);
    for (size_t i = from; i < end;) {
        if (!IsDigit(t[i])) { ++i; continue; }
        size_t j = i;
        while (j < t.size() && (IsDigit(t[j]) || ((t[j] == L'.' || t[j] == L',') && j + 1 < t.size() && IsDigit(t[j + 1])))) ++j;
        const std::wstring tok = t.substr(i, j - i);
        const size_t sep = tok.find_last_of(L".,");
        const bool twoDec = sep != std::wstring::npos && tok.size() - sep - 1 == 2;
        const bool isDate = std::count(tok.begin(), tok.end(), L'.') >= 2 && sep != std::wstring::npos && tok.find(L',') == std::wstring::npos;   // 15.11.2026
        size_t k = j;
        while (k < t.size() && t[k] == L' ') ++k;
        const bool percent = k < t.size() && t[k] == L'%';
        if (twoDec && !isDate && !percent && (i == 0 || t[i - 1] != L'-')) {
            bool ok = false;
            const long long c = ParseAmountCents(tok, ok);
            if (ok && c > 0) out.push_back(c);
        }
        i = j > i ? j : i + 1;
    }
    return out;
}

// erstes Datum tt.mm.jj(jj) im Fenster
inline bool DateAfter(const std::wstring& t, size_t from, size_t window, std::wstring& iso)
{
    const size_t end = std::min(t.size(), from + window);
    for (size_t i = from; i < end; ++i) {
        if (!IsDigit(t[i])) continue;
        size_t j = i;
        while (j < t.size() && (IsDigit(t[j]) || t[j] == L'.')) ++j;
        std::wstring tok = t.substr(i, j - i);
        if (std::count(tok.begin(), tok.end(), L'.') != 2) { i = j; continue; }
        const size_t p2 = tok.find_last_of(L'.');
        if (tok.size() - p2 - 1 == 2) tok = tok.substr(0, p2 + 1) + L"20" + tok.substr(p2 + 1);   // 15.11.26 -> 15.11.2026
        std::wstring parsed;
        if (ParseDate(tok, parsed) && !parsed.empty()) { iso = parsed; return true; }
        i = j;
    }
    return false;
}

}  // namespace scan_detail

inline ScannedInvoice ScanInvoiceText(const std::wstring& text)
{
    using namespace scan_detail;
    ScannedInvoice r;
    const std::wstring low = LowerAll(text);

    // Betrag: erst die eindeutigen Stichwoerter, dann die allgemeinen; je Stufe zaehlt der groesste Betrag (brutto vor netto)
    static const std::vector<std::vector<std::wstring>> kTiers = {
        {L"gesamtbetrag", L"rechnungsbetrag", L"endbetrag", L"zahlbetrag", L"zu zahlen", L"gesamtsumme", L"bruttobetrag", L"summe brutto", L"amount due", L"invoice total", L"total amount"},
        {L"gesamt", L"summe", L"total", L"betrag"},
    };
    for (const auto& tier : kTiers) {
        long long best = 0;
        for (const auto& kw : tier)
            for (size_t p = low.find(kw); p != std::wstring::npos; p = low.find(kw, p + kw.size()))
                for (long long c : AmountsAfter(text, p + kw.size(), 60)) best = std::max(best, c);
        if (best > 0) { r.hasAmount = true; r.cents = best; break; }
    }

    // Faelligkeit
    static const std::vector<std::wstring> kDue = {L"fällig", L"faellig", L"zahlbar bis", L"zahlungsziel", L"zahlen sie bis", L"zahlung bis", L"due date", L"payable by"};
    for (const auto& kw : kDue) {
        for (size_t p = low.find(kw); p != std::wstring::npos && !r.hasDue; p = low.find(kw, p + kw.size()))
            if (DateAfter(text, p + kw.size(), 40, r.dueIso)) r.hasDue = true;
        if (r.hasDue) break;
    }
    if (!r.hasDue) {   // "bis zum 15.11.2026" nur, wenn kurz davor von Zahlung/Ueberweisung die Rede ist
        for (size_t p = low.find(L"bis zum"); p != std::wstring::npos && !r.hasDue; p = low.find(L"bis zum", p + 7)) {
            const std::wstring before = low.substr(p > 40 ? p - 40 : 0, p > 40 ? 40 : p);
            if ((before.find(L"zahl") != std::wstring::npos || before.find(L"überweis") != std::wstring::npos || before.find(L"ueberweis") != std::wstring::npos) &&
                DateAfter(text, p + 7, 20, r.dueIso))
                r.hasDue = true;
        }
    }
    return r;
}

}  // namespace docs
