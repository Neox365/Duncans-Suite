#pragma once
// Suche in der Suite: Tippen filtert die Programmkarten. Gesucht wird in Name, Beschreibung und Stichwoertern
// (ohne Gross-/Kleinschreibung, Umlaute gelten als ae/oe/ue/ss). Mehrere Woerter: alle muessen vorkommen.
#include <string>
#include <vector>

namespace cardsearch {

inline std::wstring Fold(const std::wstring& in)
{
    std::wstring out;
    for (wchar_t c : in)
    {
        switch (c)
        {
        case L'\u00E4': case L'\u00C4': out += L"ae"; break;
        case L'\u00F6': case L'\u00D6': out += L"oe"; break;
        case L'\u00FC': case L'\u00DC': out += L"ue"; break;
        case L'\u00DF': out += L"ss"; break;
        default:
            out += (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
        }
    }
    return out;
}

inline std::vector<std::wstring> Words(const std::wstring& query)
{
    std::vector<std::wstring> w;
    std::wstring cur;
    for (wchar_t c : Fold(query))
    {
        if (c == L' ' || c == L'\t')
        {
            if (!cur.empty()) w.push_back(cur);
            cur.clear();
        }
        else cur += c;
    }
    if (!cur.empty()) w.push_back(cur);
    return w;
}

// Leere Suche passt immer. keywords: bereits kleingeschrieben, ohne Umlaute.
inline bool Matches(const std::wstring& query, const std::wstring& title, const std::wstring& description, const std::wstring& keywords)
{
    const std::vector<std::wstring> words = Words(query);
    if (words.empty()) return true;
    const std::wstring hay = Fold(title) + L" " + Fold(description) + L" " + keywords;
    for (const std::wstring& w : words)
        if (hay.find(w) == std::wstring::npos) return false;
    return true;
}

}  // namespace cardsearch
