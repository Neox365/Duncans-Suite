#pragma once
// Passwort-Check fuer Duncan's Vault: findet schwache, haeufig benutzte, mehrfach verwendete und lange nicht geaenderte Passwoerter.
// Reine Logik ohne Oberflaeche (testbar). Alles geschieht im Tresor selbst, es wird nichts ins Netz geschickt
// (ein Abgleich mit geleakten Passwoertern waere nur online moeglich und ist deshalb nicht enthalten).
#include <cwctype>
#include <string>
#include <vector>

#include "Vault.h"

namespace vault {

enum class Issue { Common, Weak, Reused, Old };

struct Finding {
    std::wstring id;      // Id des betroffenen Eintrags
    Issue issue = Issue::Weak;
    std::wstring detail;  // Erklaerung in einem Satz
};

struct AuditResult {
    int checked = 0;          // Eintraege mit Passwort, die geprueft wurden
    int common = 0, weak = 0, reused = 0, old = 0;   // Anzahl betroffener Eintraege je Art (reused: Eintraege, nicht Gruppen)
    int reusedGroups = 0;
    std::vector<Finding> findings;   // nach Schwere sortiert: haeufig, schwach, mehrfach, alt
    int Problems() const { return common + weak + reused + old; }
};

constexpr long long kOldSeconds = 365ll * 24 * 3600;   // aelter als ein Jahr

namespace audit_detail {

inline std::wstring Stem(const std::wstring& low)   // ohne Ziffern und Sonderzeichen am Ende ("passwort2024!" -> "passwort")
{
    size_t e = low.size();
    while (e > 0 && !(low[e - 1] >= L'a' && low[e - 1] <= L'z') && !(low[e - 1] >= 0xE0)) --e;
    return low.substr(0, e);
}

inline bool IsRun(const std::wstring& s)   // 123456, abcdef, 654321: jedes Zeichen eins hoeher oder tiefer
{
    if (s.size() < 6) return false;
    int dir = 0;
    for (size_t i = 1; i < s.size(); ++i) {
        const int d = static_cast<int>(s[i]) - static_cast<int>(s[i - 1]);
        if (d != 1 && d != -1) return false;
        if (i == 1) dir = d;
        else if (d != dir) return false;
    }
    return true;
}

}  // namespace audit_detail

// Steht das Passwort (oder sein Wortkern ohne angehaengte Ziffern) auf der Liste haeufiger Passwoerter oder ist es ein simples Muster?
inline bool IsCommonPassword(const std::wstring& pw)
{
    using namespace audit_detail;
    if (pw.empty()) return false;
    const std::wstring low = Lower(pw);
    static const wchar_t* const kFull[] = {L"123456", L"1234567", L"12345678", L"123456789", L"1234567890", L"12345", L"1234", L"111111", L"000000", L"654321", L"121212",
                                           L"123123", L"666666", L"112233", L"987654321", L"abc123", L"qwerty123", L"1q2w3e4r", L"1qaz2wsx", L"qwertz123", L"zaq12wsx"};
    static const wchar_t* const kWords[] = {L"password", L"passwort", L"passw0rd", L"pass", L"qwerty", L"qwertz", L"qwertyuiop", L"asdfgh", L"asdfghjkl", L"hallo", L"hello", L"welcome",
                                            L"willkommen", L"admin", L"administrator", L"login", L"letmein", L"master", L"dragon", L"sommer", L"winter", L"fruehling", L"herbst",
                                            L"fussball", L"football", L"baseball", L"iloveyou", L"monkey", L"sunshine", L"geheim", L"secret", L"test", L"testtest", L"user",
                                            L"computer", L"internet", L"changeme", L"abc", L"schatz", L"liebe", L"michael", L"daniel", L"thomas", L"superman", L"batman",
                                            L"starwars", L"princess", L"shadow", L"trustno", L"freedom", L"whatever", L"zuhause", L"hammer", L"mustang"};
    for (const wchar_t* w : kFull)
        if (low == w) return true;
    const std::wstring stem = Stem(low);
    if (!stem.empty())
        for (const wchar_t* w : kWords)
            if (stem == w) return true;
    bool same = true;
    for (wchar_t c : low) if (c != low[0]) same = false;
    if (same && low.size() >= 4) return true;   // aaaaaaaa, 11111111
    return IsRun(low);
}

inline AuditResult Audit(const std::vector<Entry>& entries, long long now)
{
    AuditResult r;
    std::vector<size_t> idx;   // Passwort-Eintraege mit nicht leerem Passwort
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].cat == Cat::Password && !entries[i].secret.empty()) idx.push_back(i);
    r.checked = static_cast<int>(idx.size());

    std::vector<Finding> common, weak, reused, old;
    for (size_t i : idx) {
        const Entry& e = entries[i];
        if (IsCommonPassword(e.secret)) {
            common.push_back({e.id, Issue::Common, L"Steht auf der Liste häufig benutzter Passwörter oder folgt einem einfachen Muster."});
        } else {
            const Strength s = Rate(e.secret);
            if (s.level <= 1) weak.push_back({e.id, Issue::Weak, std::wstring(L"Bewertung: ") + s.label + L". Länger machen und Zeichenarten mischen."});
        }
        if (e.modified > 0 && now - e.modified > kOldSeconds) {
            const long long months = (now - e.modified) / (30ll * 24 * 3600);
            old.push_back({e.id, Issue::Old, L"Seit " + std::to_wstring(months) + L" Monaten nicht geändert."});
        }
    }
    // mehrfach verwendete Passwoerter: gleiche Passwoerter bilden eine Gruppe
    std::vector<bool> done(idx.size(), false);
    for (size_t a = 0; a < idx.size(); ++a) {
        if (done[a]) continue;
        std::vector<size_t> group{a};
        for (size_t b = a + 1; b < idx.size(); ++b)
            if (!done[b] && entries[idx[b]].secret == entries[idx[a]].secret) { group.push_back(b); done[b] = true; }
        done[a] = true;
        if (group.size() < 2) continue;
        ++r.reusedGroups;
        for (size_t g : group) {
            std::wstring others;
            for (size_t h : group)
                if (h != g) others += (others.empty() ? L"" : L", ") + entries[idx[h]].title;
            reused.push_back({entries[idx[g]].id, Issue::Reused, L"Dasselbe Passwort steht auch bei: " + others + L"."});
        }
    }
    r.common = static_cast<int>(common.size());
    r.weak = static_cast<int>(weak.size());
    r.reused = static_cast<int>(reused.size());
    r.old = static_cast<int>(old.size());
    for (auto* v : {&common, &weak, &reused, &old}) r.findings.insert(r.findings.end(), v->begin(), v->end());
    return r;
}

// Titel der anderen Eintraege (Passwort-Kategorie), die dasselbe Passwort benutzen; leer, wenn es einmalig ist
inline std::vector<std::wstring> SharedWith(const std::vector<Entry>& entries, const Entry& e)
{
    std::vector<std::wstring> t;
    if (e.cat != Cat::Password || e.secret.empty()) return t;
    for (const Entry& x : entries)
        if (x.id != e.id && x.cat == Cat::Password && x.secret == e.secret) t.push_back(x.title);
    return t;
}

}  // namespace vault
