#pragma once
// Aenderungsprotokoll der Programme (changelog.txt im Repository-Wurzelordner), Anzeige im Info-Dialog der Suite.
//
//   [DuncansPhotoCommander.exe]          Abschnitt eines Programms (Schluessel = EXE-Name)
//   ## 0.2.5 | 2026-10-20                Eintrag: Titel (meist die Version) | Datum
//   - Stichpunkt                         Folgezeilen mit zwei Leerzeichen davor gehoeren zum Stichpunkt
//   # Kommentar
//
// Der Text kommt auch aus dem Netz und wird nur angezeigt: Parse begrenzt Groesse und Mengen, entfernt
// Steuerzeichen und liest nie etwas anderes als Text.
#include <cstddef>
#include <string>
#include <vector>

namespace changelog {

struct Entry {
    std::wstring title, date;
    std::vector<std::wstring> items;
};
struct Section {
    std::wstring key;
    std::vector<Entry> entries;
};

constexpr size_t kMaxText = 256 * 1024;   // mehr wird ignoriert
constexpr size_t kMaxSections = 16, kMaxEntries = 80, kMaxItems = 40, kMaxItemChars = 500;

inline std::wstring Utf8ToWide(const std::string& s)
{
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned long cp = 0xFFFD;
        size_t len = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size() && (s[i + 1] & 0xC0) == 0x80) {
            cp = ((c & 0x1Fu) << 6) | (s[i + 1] & 0x3Fu);
            len = 2;
            if (cp < 0x80) cp = 0xFFFD;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size() && (s[i + 1] & 0xC0) == 0x80 && (s[i + 2] & 0xC0) == 0x80) {
            cp = ((c & 0x0Fu) << 12) | ((s[i + 1] & 0x3Fu) << 6) | (s[i + 2] & 0x3Fu);
            len = 3;
            if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size() && (s[i + 1] & 0xC0) == 0x80 && (s[i + 2] & 0xC0) == 0x80 &&
                   (s[i + 3] & 0xC0) == 0x80) {
            cp = ((c & 0x07u) << 18) | ((s[i + 1] & 0x3Fu) << 12) | ((s[i + 2] & 0x3Fu) << 6) | (s[i + 3] & 0x3Fu);
            len = 4;
            if (cp < 0x10000 || cp > 0x10FFFF) cp = 0xFFFD;
        }
        i += len;
        if (cp >= 0x10000 && sizeof(wchar_t) == 2) {   // Windows: UTF-16 mit Ersatzpaar
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        } else if (cp < 0x20 && cp != '\t' && cp != '\n' && cp != '\r') {
            out.push_back(L' ');   // Steuerzeichen nicht anzeigen
        } else {
            out.push_back(static_cast<wchar_t>(cp));
        }
    }
    return out;
}

inline std::wstring Trim(const std::wstring& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r')) ++a;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r')) --b;
    return s.substr(a, b - a);
}

inline std::vector<Section> Parse(const std::string& utf8)
{
    std::vector<Section> out;
    std::string text = utf8.size() > kMaxText ? utf8.substr(0, kMaxText) : utf8;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);   // BOM
    const std::wstring all = Utf8ToWide(text);

    Section* sec = nullptr;
    Entry* entry = nullptr;
    size_t pos = 0;
    while (pos <= all.size()) {
        size_t nl = all.find(L'\n', pos);
        if (nl == std::wstring::npos) nl = all.size();
        const std::wstring raw = all.substr(pos, nl - pos);
        pos = nl + 1;
        const std::wstring line = Trim(raw);
        if (line.empty()) continue;

        if (line.compare(0, 2, L"##") == 0) {   // Eintrag (vor dem Kommentar pruefen, beide beginnen mit #)
            if (!sec || sec->entries.size() >= kMaxEntries) { entry = nullptr; continue; }
            const std::wstring head = Trim(line.substr(2));
            const size_t bar = head.find(L'|');
            sec->entries.push_back({});
            entry = &sec->entries.back();
            entry->title = Trim(bar == std::wstring::npos ? head : head.substr(0, bar));
            entry->date = bar == std::wstring::npos ? L"" : Trim(head.substr(bar + 1));
        } else if (line[0] == L'#') {
            continue;   // Kommentar
        } else if (line[0] == L'[' && line.back() == L']') {
            entry = nullptr;
            if (out.size() >= kMaxSections) { sec = nullptr; continue; }
            out.push_back({});
            sec = &out.back();
            sec->key = Trim(line.substr(1, line.size() - 2));
        } else if (line.compare(0, 2, L"- ") == 0) {
            if (!entry || entry->items.size() >= kMaxItems) continue;
            entry->items.push_back(Trim(line.substr(2)).substr(0, kMaxItemChars));
        } else if ((raw.compare(0, 2, L"  ") == 0) && entry && !entry->items.empty()) {   // Folgezeile
            std::wstring& last = entry->items.back();
            if (last.size() < kMaxItemChars) last = (last + L" " + line).substr(0, kMaxItemChars);
        }
    }
    return out;
}

inline const Section* Find(const std::vector<Section>& all, const std::wstring& key)
{
    for (const auto& s : all)
        if (s.key == key) return &s;
    return nullptr;
}

}  // namespace changelog
