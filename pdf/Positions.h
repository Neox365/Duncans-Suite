#pragma once
// Duncan's PDF - merkt sich pro Datei die zuletzt gelesene Seite (positions.txt neben settings.ini).
// Der Schluessel ist ein Hash des kleingeschriebenen Pfads (kein Klartext-Pfad auf der Platte); gespeichert werden die letzten 200 Dateien.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <string>
#include <utility>
#include <vector>

namespace pdfpos {

using List = std::vector<std::pair<std::string, int>>;   // aelteste zuerst
constexpr size_t kMaxEntries = 200;

inline std::string KeyFor(const std::wstring& path) {
    unsigned long long h = 1469598103934665603ull;      // FNV-1a
    for (wchar_t c : path) {
        const unsigned v = static_cast<unsigned>(std::towlower(c));
        for (int i = 0; i < 2; ++i) { h ^= (v >> (8 * i)) & 0xFFu; h *= 1099511628211ull; }
    }
    char buf[20];
    snprintf(buf, sizeof buf, "%016llx", h);
    return buf;
}

// -1 = nichts gemerkt
inline int Get(const List& l, const std::string& key) {
    for (const auto& e : l) if (e.first == key) return e.second;
    return -1;
}

// Eintrag setzen und ans Ende (= neueste) stellen; ueber kMaxEntries fliegen die aeltesten raus
inline void Set(List& l, const std::string& key, int page) {
    l.erase(std::remove_if(l.begin(), l.end(), [&](const auto& e) { return e.first == key; }), l.end());
    l.emplace_back(key, std::max(0, page));
    if (l.size() > kMaxEntries) l.erase(l.begin(), l.begin() + static_cast<std::ptrdiff_t>(l.size() - kMaxEntries));
}

inline std::string Serialize(const List& l) {
    std::string out;
    for (const auto& e : l) out += e.first + "=" + std::to_string(e.second) + "\n";
    return out;
}

inline List Parse(const std::string& text) {
    List l;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        const size_t eq = line.find('=');
        if (eq != 16 || line.size() < 18 || line.size() > 28) continue;                 // 16 Hex-Zeichen, "=", Seitenzahl
        const std::string key = line.substr(0, 16), num = line.substr(17);
        bool ok = std::all_of(key.begin(), key.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) &&
                  std::all_of(num.begin(), num.end(), [](char c) { return c >= '0' && c <= '9'; }) && num.size() <= 7;
        if (ok) Set(l, key, std::stoi(num));
    }
    return l;
}

inline List Load(const std::wstring& file) {
    HANDLE f = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    std::string data;
    char buf[8192];
    DWORD got = 0;
    while (ReadFile(f, buf, sizeof buf, &got, nullptr) && got > 0 && data.size() < (1u << 20)) data.append(buf, got);
    CloseHandle(f);
    return Parse(data);
}

inline bool Save(const std::wstring& file, const List& l) {
    const std::string data = Serialize(l);
    HANDLE f = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    const bool ok = WriteFile(f, data.data(), static_cast<DWORD>(data.size()), &w, nullptr) && w == data.size();
    CloseHandle(f);
    return ok;
}

}  // namespace pdfpos
