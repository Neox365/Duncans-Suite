#pragma once
// Doppelte und aehnliche Bilder in einer Dateiliste finden.
//  1. Gleiche Groesse + gleicher Inhaltshash  -> identisch
//  2. Je ein Bild pro identischer Gruppe: 64-Bit-Differenzhash (dHash); kleiner Abstand -> aehnlich
// Laeuft komplett im Aufrufer-Thread plus kleinen Worker-Threads (eigenes COM je Thread), bricht ueber
// Progress::cancel ab. Reine Logik ohne UI, damit sie getrennt testbar bleibt.
#include <objbase.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <map>
#include <numeric>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "ImageOps.h"

namespace dup {

constexpr int kSimilarMaxDistance = 5;  // von 64 Bit; kleiner = strenger

struct File {
    std::wstring path;
    ULONGLONG size = 0;
};

struct Group {
    std::vector<size_t> members;  // Indizes in die uebergebene Dateiliste
    bool exact = false;           // alle Mitglieder byte-identisch
};

struct Progress {
    std::atomic<bool> cancel{false};
    std::atomic<int> stage{0};  // 1 = Dateivergleich, 2 = Bildvergleich
    std::atomic<size_t> done{0}, total{0};
};

namespace detail {

template <class F>
void ParallelFor(const std::vector<size_t>& items, Progress& pr, const std::function<void()>& tick, F fn) {
    pr.done = 0;
    pr.total = items.size();
    const unsigned hw = std::thread::hardware_concurrency();
    const unsigned nt = std::max(1u, std::min(4u, hw ? hw : 1u));
    std::atomic<size_t> next{0};
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < nt; ++t) {
        pool.emplace_back([&] {
            const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            for (;;) {
                const size_t k = next.fetch_add(1);
                if (k >= items.size() || pr.cancel) break;
                fn(items[k]);
                ++pr.done;
                if (tick) tick();
            }
            if (SUCCEEDED(hr)) CoUninitialize();
        });
    }
    for (auto& th : pool) th.join();
}

}  // namespace detail

inline std::vector<Group> Find(const std::vector<File>& files, Progress& pr, const std::function<void()>& tick = {}) {
    const size_t n = files.size();
    std::vector<size_t> parent(n);
    std::iota(parent.begin(), parent.end(), size_t(0));
    auto root = [&](size_t x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    };
    auto unite = [&](size_t a, size_t b) {
        a = root(a);
        b = root(b);
        if (a != b) parent[std::max(a, b)] = std::min(a, b);  // kleinster Index bleibt Wurzel
    };

    // Stufe 1: identische Dateien
    pr.stage = 1;
    std::map<ULONGLONG, std::vector<size_t>> bySize;
    for (size_t i = 0; i < n; ++i) bySize[files[i].size].push_back(i);
    std::vector<size_t> cand;
    for (auto& kv : bySize)
        if (kv.second.size() > 1) cand.insert(cand.end(), kv.second.begin(), kv.second.end());
    std::vector<uint64_t> h1(n, 0), h2(n, 0);
    std::vector<char> hashed(n, 0);
    detail::ParallelFor(cand, pr, tick, [&](size_t i) { hashed[i] = imgops::HashFile(files[i].path, h1[i], h2[i]); });
    if (pr.cancel) return {};
    std::map<std::tuple<ULONGLONG, uint64_t, uint64_t>, size_t> seen;
    for (size_t i : cand) {
        if (!hashed[i]) continue;
        auto key = std::make_tuple(files[i].size, h1[i], h2[i]);
        auto it = seen.find(key);
        if (it == seen.end()) seen.emplace(key, i);
        else unite(it->second, i);
    }
    std::vector<size_t> exactClass(n);
    for (size_t i = 0; i < n; ++i) exactClass[i] = root(i);

    // Stufe 2: ein Vertreter je identischer Gruppe, Vergleich ueber den Differenzhash
    pr.stage = 2;
    std::vector<size_t> reps;
    for (size_t i = 0; i < n; ++i)
        if (exactClass[i] == i) reps.push_back(i);
    std::vector<uint64_t> dh(n, 0);
    std::vector<char> dok(n, 0);
    detail::ParallelFor(reps, pr, tick, [&](size_t i) { dok[i] = imgops::ComputeDHash(files[i].path, dh[i]); });
    if (pr.cancel) return {};
    for (size_t a = 0; a < reps.size(); ++a) {
        const size_t i = reps[a];
        if (!dok[i] || dh[i] == 0 || dh[i] == ~0ull) continue;  // einfarbige Bilder tragen keine Information
        for (size_t b = a + 1; b < reps.size(); ++b) {
            const size_t j = reps[b];
            if (dok[j] && imgops::HammingDistance(dh[i], dh[j]) <= kSimilarMaxDistance) unite(i, j);
        }
    }

    std::map<size_t, Group> groups;
    for (size_t i = 0; i < n; ++i) groups[root(i)].members.push_back(i);
    std::vector<Group> out;
    for (auto& kv : groups) {
        Group& g = kv.second;
        if (g.members.size() < 2) continue;
        g.exact = std::all_of(g.members.begin(), g.members.end(),
                              [&](size_t m) { return exactClass[m] == exactClass[g.members[0]]; });
        out.push_back(std::move(g));
    }
    return out;
}

}  // namespace dup
