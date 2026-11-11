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
        while (tok.size() > 1 && tok.back() == L'.') tok.pop_back();   // Satzpunkt hinter dem Datum: "bis zum 05.12.2026."
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

// Typische Fehler der Texterkennung in Zahlen und Daten reparieren, bevor Betrag und Faelligkeit gesucht werden:
// "1.200,oo" -> "1.200,00" (Buchstabe o/O in einem Zahlenwort) und "25.112026" -> "25.11.2026" (verschluckter Punkt vor dem Jahr)
inline std::wstring CleanOcrText(const std::wstring& in)
{
    std::wstring out = in;
    size_t i = 0;
    while (i < out.size()) {
        if (out[i] == L' ' || out[i] == L'\n' || out[i] == L'\r' || out[i] == L'\t') { ++i; continue; }
        size_t j = i;
        while (j < out.size() && out[j] != L' ' && out[j] != L'\n' && out[j] != L'\r' && out[j] != L'\t') ++j;
        bool digit = false, onlyNum = true;
        for (size_t k = i; k < j; ++k) {
            const wchar_t c = out[k];
            if (scan_detail::IsDigit(c)) digit = true;
            else if (!(c == L'.' || c == L',' || c == L'o' || c == L'O' || c == 0x20AC || c == L'%' || c == L'-')) onlyNum = false;
        }
        if (digit && onlyNum)
            for (size_t k = i; k < j; ++k)
                if (out[k] == L'o' || out[k] == L'O') out[k] = L'0';
        if (j - i == 9 && out[i + 2] == L'.' && out[i + 5] != L'.') {   // tt.mmjjjj
            bool dig = true;
            for (size_t k = i; k < j; ++k) if (k != i + 2 && !scan_detail::IsDigit(out[k])) dig = false;
            if (dig) { out.insert(i + 5, 1, L'.'); ++j; }
        }
        i = j;
    }
    return out;
}

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
            const std::wstring before = low.substr(p > 90 ? p - 90 : 0, p > 90 ? 90 : p);   // ein Satz wie "Bitte ueberweisen Sie den Rechnungsbetrag von 1.662,22 EUR bis zum ..."
            if ((before.find(L"zahl") != std::wstring::npos || before.find(L"überweis") != std::wstring::npos || before.find(L"ueberweis") != std::wstring::npos) &&
                DateAfter(text, p + 7, 20, r.dueIso))
                r.hasDue = true;
        }
    }
    return r;
}

}  // namespace docs
