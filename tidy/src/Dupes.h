#pragma once
// Duncan's Tidy - Doppelte Dateien im Downloads-Ordner finden.
// Zwei Dateien gelten nur als doppelt, wenn Groesse UND Inhalt (SHA-256 ueber die ganze Datei) gleich sind; der Name zaehlt nicht.
// Von jeder Gruppe identischer Dateien bleibt eine Kopie unberuehrt (die aelteste, bei gleichem Alter die mit dem kuerzesten Namen),
// alle anderen gelten als "doppelt" und landen in der Kategorie "Doppelte Dateien" (im Papierkorb, wie alle Downloads).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace dupes {

struct File {
    std::wstring path;
    unsigned long long size = 0;
    unsigned long long mtime = 0;   // letzte Aenderung (FILETIME), kleiner = aelter
};

// SHA-256 ueber die ganze Datei, in Stuecken gelesen (keine ganze Datei im Speicher). Leer bei Fehler.
inline std::string HashFile(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return "";
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string hex;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 && BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        std::vector<UCHAR> buf(1 << 20);
        bool ok = true;
        for (;;) {
            DWORD got = 0;
            if (!ReadFile(f, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr)) { ok = false; break; }
            if (got == 0) break;
            if (BCryptHashData(hash, buf.data(), got, 0) != 0) { ok = false; break; }
        }
        UCHAR digest[32];
        if (ok && BCryptFinishHash(hash, digest, sizeof digest, 0) == 0) {
            for (UCHAR b : digest) {
                char t[3];
                snprintf(t, sizeof t, "%02x", b);
                hex += t;
            }
        }
    }
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    return hex;
}

// Liefert die Indizes (in files) aller ueberzaehligen Kopien. hashOf liefert den Inhalts-Hash (leer = nicht lesbar, wird ignoriert).
// Leere Dateien zaehlen nie als doppelt. Dateien mit einzigartiger Groesse werden gar nicht erst gelesen.
inline std::vector<size_t> FindRedundant(const std::vector<File>& files, const std::function<std::string(const std::wstring&)>& hashOf,
                                         const std::function<bool()>& cancelled = nullptr) {
    std::map<unsigned long long, std::vector<size_t>> bySize;
    for (size_t i = 0; i < files.size(); ++i)
        if (files[i].size > 0) bySize[files[i].size].push_back(i);
    std::vector<size_t> out;
    for (const auto& sz : bySize) {
        if (sz.second.size() < 2) continue;
        std::map<std::string, std::vector<size_t>> byHash;
        for (size_t i : sz.second) {
            if (cancelled && cancelled()) return {};
            const std::string h = hashOf(files[i].path);
            if (!h.empty()) byHash[h].push_back(i);
        }
        for (auto& g : byHash) {
            if (g.second.size() < 2) continue;
            const size_t keep = *std::min_element(g.second.begin(), g.second.end(), [&](size_t a, size_t b) {
                if (files[a].mtime != files[b].mtime) return files[a].mtime < files[b].mtime;
                if (files[a].path.size() != files[b].path.size()) return files[a].path.size() < files[b].path.size();
                return files[a].path < files[b].path;
            });
            for (size_t i : g.second)
                if (i != keep) out.push_back(i);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace dupes
