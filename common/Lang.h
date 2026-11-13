// Sprachumschaltung Deutsch / Englisch fuer alle Programme der Suite.
// Die Texte stehen im Code auf Deutsch. lang::Tr(text) liefert bei englischer Oberflaeche die Uebersetzung aus der Tabelle,
// sonst (oder wenn nichts passt) den Text unveraendert. So bleiben Benutzereingaben (Namen, Dateinamen) immer unberuehrt.
//
// Tabelleneintraege {deutsch, englisch}:
//   - ohne Platzhalter: der ganze Text muss genau passen
//   - mit {*} im deutschen Text: steht fuer beliebigen Text (z. B. eine Zahl oder einen Namen); im englischen Text
//     setzen {1}, {2} ... den jeweils erfassten Teil wieder ein. Erfasste Teile werden selbst noch einmal uebersetzt.
//
// Welche Sprache gilt: Umgebungsvariable DUNCANS_LANG (de/en), sonst die Datei %LOCALAPPDATA%\DuncansSuite\lang.txt
// ("de", "en" oder "auto"), sonst die Windows-Sprache (Deutsch -> Deutsch, alles andere -> Englisch).
#pragma once

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <string>
#include <unordered_map>
#include <vector>

namespace lang {

struct Entry {
    const wchar_t* de;
    const wchar_t* en;
};

enum class Mode { Auto, German, English };

namespace detail {

struct Pattern {
    std::vector<std::wstring> seg;   // Textstuecke zwischen den {*}
    std::wstring en;
    size_t weight = 0;               // Laenge der festen Teile: Genaueres zuerst
};

struct State {
    bool inited = false;
    bool english = false;
    Mode mode = Mode::Auto;
    std::unordered_map<std::wstring, std::wstring> exact;
    std::vector<Pattern> pats;
    std::unordered_map<std::wstring, std::wstring> cache;
};

inline State& S()
{
    static State s;
    return s;
}

inline std::wstring SettingPath()
{
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::wstring();
    return std::wstring(buf) + L"\\DuncansSuite\\lang.txt";
}

inline Mode ReadMode()
{
    wchar_t env[16] = {};
    if (GetEnvironmentVariableW(L"DUNCANS_LANG", env, 16) > 0) {
        if (lstrcmpiW(env, L"en") == 0) return Mode::English;
        if (lstrcmpiW(env, L"de") == 0) return Mode::German;
    }
    const std::wstring p = SettingPath();
    if (p.empty()) return Mode::Auto;
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return Mode::Auto;
    char b[16] = {};
    DWORD got = 0;
    ReadFile(h, b, 15, &got, nullptr);
    CloseHandle(h);
    if (got >= 2 && b[0] == 'e' && b[1] == 'n') return Mode::English;
    if (got >= 2 && b[0] == 'd' && b[1] == 'e') return Mode::German;
    return Mode::Auto;
}

inline bool Resolve(Mode m)
{
    if (m == Mode::English) return true;
    if (m == Mode::German) return false;
    return PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_GERMAN;
}

inline void AddOne(State& s, const Entry& e)
{
    const std::wstring de = e.de;
    if (de.find(L"{*}") == std::wstring::npos) {
        s.exact[de] = e.en;
        return;
    }
    Pattern p;
    size_t pos = 0;
    for (;;) {
        const size_t f = de.find(L"{*}", pos);
        if (f == std::wstring::npos) {
            p.seg.push_back(de.substr(pos));
            break;
        }
        p.seg.push_back(de.substr(pos, f - pos));
        pos = f + 3;
    }
    for (const auto& x : p.seg) p.weight += x.size();
    p.en = e.en;
    s.pats.push_back(std::move(p));
    std::stable_sort(s.pats.begin(), s.pats.end(), [](const Pattern& a, const Pattern& b) { return a.weight > b.weight; });
}

inline bool Match(const Pattern& p, const std::wstring& t, std::vector<std::wstring>& caps)
{
    caps.clear();
    const auto& g = p.seg;
    if (t.size() < p.weight) return false;
    if (t.compare(0, g[0].size(), g[0]) != 0) return false;
    size_t pos = g[0].size();
    const size_t n = g.size();
    for (size_t i = 1; i + 1 < n; ++i) {
        const size_t f = t.find(g[i], pos);
        if (f == std::wstring::npos) return false;
        caps.push_back(t.substr(pos, f - pos));
        pos = f + g[i].size();
    }
    const std::wstring& last = g[n - 1];
    if (t.size() < pos + last.size()) return false;
    const size_t tail = t.size() - last.size();
    if (t.compare(tail, last.size(), last) != 0) return false;
    caps.push_back(t.substr(pos, tail - pos));
    return true;
}

}   // namespace detail

// Ordnet eine Tabelle zu (einmal pro Programm beim Start aufrufen). Mehrfaches Aufrufen schadet nicht.
inline void Add(const Entry* e, size_t n)
{
    auto& s = detail::S();
    for (size_t i = 0; i < n; ++i) detail::AddOne(s, e[i]);
    s.cache.clear();
}

template <size_t N>
inline void Add(const Entry (&e)[N])
{
    Add(e, N);
}

inline void Init()
{
    auto& s = detail::S();
    if (s.inited) return;
    s.inited = true;
    s.mode = detail::ReadMode();
    s.english = detail::Resolve(s.mode);
}

inline bool English()
{
    Init();
    return detail::S().english;
}

inline Mode CurrentMode()
{
    Init();
    return detail::S().mode;
}

// Stellt die Sprache um und merkt sie sich fuer alle Programme der Suite ("auto" loescht die Wahl).
inline void SetMode(Mode m)
{
    auto& s = detail::S();
    s.inited = true;
    s.mode = m;
    s.english = detail::Resolve(m);
    s.cache.clear();
    const std::wstring p = detail::SettingPath();
    if (p.empty()) return;
    const size_t slash = p.rfind(L'\\');
    CreateDirectoryW(p.substr(0, slash).c_str(), nullptr);
    if (m == Mode::Auto) {
        DeleteFileW(p.c_str());
        return;
    }
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    const char* v = m == Mode::English ? "en" : "de";
    DWORD w = 0;
    WriteFile(h, v, 2, &w, nullptr);
    CloseHandle(h);
}

inline std::wstring Tr(const std::wstring& t);

// Gemeinsame Texte der Karten und Meldungsfenster (alle Programme)
inline void AddCommon()
{
    static const Entry kCommon[] = {
        {L"Teil von Duncan's Suite", L"Part of Duncan's Suite"},
        {L"GitHub öffnen", L"Open GitHub"},
        {L"Diagnose kopieren", L"Copy diagnostics"},
        {L"Kopiert ✓", L"Copied ✓"},
        {L"Schließen", L"Close"},
        {L"Version", L"Version"},
        {L"Entwickler", L"Developer"},
        {L"Quellcode", L"Source code"},
        {L"Teil von", L"Part of"},
        {L"Datenschutz", L"Privacy"},
        {L"Alle Daten werden zu 100 % lokal auf deinem Gerät gespeichert. Es erfolgt keine Übertragung personenbezogener Daten an externe Server.",
         L"All data is stored 100% locally on your device. No personal data is sent to external servers."},
        {L"Hilfe", L"Help"},
        {L"Abbrechen", L"Cancel"},
        {L"Ja", L"Yes"},
        {L"Nein", L"No"},
        {L"Wiederholen", L"Retry"},
        {L"Übernehmen", L"Apply"},
        {L"Speichern", L"Save"},
        {L"Nicht speichern", L"Don't save"},
        {L"Fehler", L"Error"},
        {L"Hinweis", L"Notice"},
        {L"Sprache", L"Language"},
        {L"Sprache: automatisch", L"Language: automatic"},
        {L"Sprache: Deutsch", L"Language: German"},
        {L"Sprache: Englisch", L"Language: English"},
    };
    Add(kCommon);
}

inline std::wstring Tr(const std::wstring& t)
{
    auto& s = detail::S();
    Init();
    if (!s.english || t.empty()) return t;
    static bool common = false;
    if (!common) {
        common = true;
        AddCommon();
    }
    auto c = s.cache.find(t);
    if (c != s.cache.end()) return c->second;
    std::wstring out = t;
    auto e = s.exact.find(t);
    if (e != s.exact.end()) {
        out = e->second;
    } else if (t.size() > 2) {
        std::vector<std::wstring> caps;
        for (const auto& p : s.pats) {
            if (!detail::Match(p, t, caps)) continue;
            out.clear();
            const std::wstring& tpl = p.en;
            for (size_t i = 0; i < tpl.size(); ++i) {
                if (tpl[i] == L'{' && i + 2 < tpl.size() + 0 && tpl[i + 1] >= L'1' && tpl[i + 1] <= L'9' && tpl[i + 2] == L'}') {
                    const size_t k = static_cast<size_t>(tpl[i + 1] - L'1');
                    if (k < caps.size()) out += Tr(caps[k]);
                    i += 2;
                } else {
                    out += tpl[i];
                }
            }
            break;
        }
    }
    if (out == t) {
        // Zusammengesetzte Zeilen "Teil  ·  Teil  ·  Teil": jedes Stueck fuer sich uebersetzen (Trenner bleiben)
        const size_t dot = t.find(L'·');
        if (dot != std::wstring::npos) {
            std::wstring res;
            size_t pos = 0;
            while (pos <= t.size()) {
                size_t d = t.find(L'·', pos);
                const size_t end = d == std::wstring::npos ? t.size() : d;
                size_t a = pos, b = end;
                while (a < b && t[a] == L' ') ++a;
                while (b > a && t[b - 1] == L' ') --b;
                res += t.substr(pos, a - pos) + Tr(t.substr(a, b - a)) + t.substr(b, end - b);
                if (d == std::wstring::npos) break;
                res += L'·';
                pos = d + 1;
            }
            out = res;
        }
    }
    if (s.cache.size() > 6000) s.cache.clear();
    s.cache[t] = out;
    return out;
}

inline std::wstring Tr(const wchar_t* t)
{
    return t ? Tr(std::wstring(t)) : std::wstring();
}

}   // namespace lang
