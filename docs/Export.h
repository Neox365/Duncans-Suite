#pragma once
// CSV-Export der Belege fuer Excel, Steuerberater oder Buchhaltung. Reine Logik ohne Oberflaeche (testbar).
// Format: UTF-8 mit BOM (Excel erkennt Umlaute), Semikolon als Trenner, Dezimalkomma ohne Tausenderpunkt (deutsches Excel rechnet damit),
// Datum als TT.MM.JJJJ. Felder mit Semikolon, Anfuehrungszeichen oder Zeilenumbruch stehen in Anfuehrungszeichen.
// Texte, die mit = + - @ beginnen, bekommen ein Hochkomma davor, damit Excel sie nicht als Formel ausfuehrt.
#include <algorithm>
#include <string>
#include <vector>

#include "Store.h"

namespace docs {

// 123456 -> "1234,56"; -5 -> "-0,05"; kUnknown -> ""
inline std::wstring CsvAmount(long long cents)
{
    if (cents == kUnknown) return L"";
    std::wstring s = FormatAmount(cents, false);
    s.erase(std::remove(s.begin(), s.end(), L'.'), s.end());   // Tausenderpunkte weg
    return s;
}

inline std::wstring CsvText(std::wstring s)
{
    for (auto& c : s)
        if (c == L'\t' || c == L'\r') c = L' ';
    if (!s.empty() && (s[0] == L'=' || s[0] == L'+' || s[0] == L'-' || s[0] == L'@')) s.insert(s.begin(), L'\'');
    return s;
}

inline std::wstring CsvField(const std::wstring& s)
{
    if (s.find_first_of(L";\"\n\r") == std::wstring::npos) return s;
    std::wstring q = L"\"";
    for (wchar_t c : s) {
        if (c == L'"') q += L'"';
        q += c;
    }
    return q + L"\"";
}

constexpr const wchar_t* kCsvHeader =
    L"Datei;Ordner;Jahr;Gläubiger;Betrag (EUR);Fällig am;Status;Art;Rate;Tags;Notiz";

// Eine Zeile je Beleg, Kopfzeile oben. Zeilenende CRLF (Excel).
inline std::string BuildCsv(const std::vector<Doc>& docs)
{
    std::wstring out = std::wstring(kCsvHeader) + L"\r\n";
    for (const Doc& d : docs) {
        std::wstring folder = d.rel;
        const size_t cut = folder.find_last_of(L"\\/");
        folder = cut == std::wstring::npos ? L"" : folder.substr(0, cut);
        const std::wstring cells[] = {
            CsvField(CsvText(d.name)),
            CsvField(CsvText(folder)),
            CsvField(d.year),
            CsvField(CsvText(d.creditor)),
            CsvField(CsvAmount(d.m.cents)),
            CsvField(FormatDate(d.m.due)),
            CsvField(StatusName(d.m.status)),
            CsvField(d.m.income ? L"Einnahme" : L"Ausgabe"),
            CsvField(CsvText(d.m.status == Status::Installment ? d.m.rate : std::wstring())),
            CsvField(CsvText(d.m.tags)),
            CsvField(CsvText(d.m.note)),
        };
        for (size_t i = 0; i < sizeof(cells) / sizeof(cells[0]); ++i) out += (i ? L";" : L"") + cells[i];
        out += L"\r\n";
    }
    return std::string("\xEF\xBB\xBF") + WideToUtf8(out);
}

}  // namespace docs
