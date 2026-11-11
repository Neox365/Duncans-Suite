#pragma once
// Aenderungsprotokoll (GoBD): Zeile = Zeit, Aktion, Beleg, Einzelheit, Pruefsumme. Jede Zeile enthaelt die Pruefsumme der vorigen Zeile
// (Hash-Kette); wer nachtraeglich eine Zeile aendert, loescht oder einfuegt, bricht die Kette, und Verify findet die Stelle.
// Das ist ein Manipulationsnachweis, kein Schutz: wer die Datei komplett neu schreibt, kann auch eine neue Kette bauen.
#include <string>
#include <vector>

#include "Sha256.h"
#include "Store.h"

namespace docs {

inline std::string LogClean(const std::wstring& w)
{
    std::wstring s = w;
    for (wchar_t& c : s)
        if (c == L'\t' || c == L'\n' || c == L'\r') c = L' ';
    return WideToUtf8(s);
}

// Hash der vorigen vollstaendigen Zeile (ohne Zeilenende); "0" fuer die erste Zeile
inline std::string LogLine(const std::wstring& timeStamp, const std::wstring& action, const std::wstring& doc, const std::wstring& detail, const std::string& prevLine)
{
    const std::string prev = prevLine.empty() ? std::string(64, '0') : sha256::Hex(prevLine);
    return LogClean(timeStamp) + "\t" + LogClean(action) + "\t" + LogClean(doc) + "\t" + LogClean(detail) + "\t" + prev;
}

inline std::vector<std::string> LogSplit(const std::string& text)
{
    std::vector<std::string> out;
    size_t p = 0;
    while (p < text.size()) {
        size_t nl = text.find('\n', p);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(p, nl - p);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) out.push_back(line);
        p = nl + 1;
    }
    return out;
}

// 0 = Kette in Ordnung, sonst die (1-basierte) erste kaputte Zeile
inline size_t LogVerify(const std::string& text)
{
    const std::vector<std::string> lines = LogSplit(text);
    std::string prev;
    for (size_t i = 0; i < lines.size(); ++i) {
        const size_t cut = lines[i].rfind('\t');
        if (cut == std::string::npos) return i + 1;
        const std::string expect = prev.empty() ? std::string(64, '0') : sha256::Hex(prev);
        if (lines[i].substr(cut + 1) != expect) return i + 1;
        prev = lines[i];
    }
    return 0;
}

}  // namespace docs
