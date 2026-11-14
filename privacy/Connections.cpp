#include "Connections.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <algorithm>
#include <cstring>
#include <map>

namespace conn {

std::wstring FormatV4(const unsigned char a[4])
{
    return std::to_wstring(a[0]) + L"." + std::to_wstring(a[1]) + L"." + std::to_wstring(a[2]) + L"." + std::to_wstring(a[3]);
}

std::wstring FormatV6(const unsigned char a[16])
{
    // Kurzform ohne Nullfolgen-Kuerzung: acht Gruppen in Hex (ausreichend lesbar, eindeutig)
    static const wchar_t* hex = L"0123456789abcdef";
    std::wstring out;
    int zeroStart = -1, zeroLen = 0, bestStart = -1, bestLen = 0;
    unsigned short g[8];
    for (int i = 0; i < 8; ++i) g[i] = static_cast<unsigned short>((a[i * 2] << 8) | a[i * 2 + 1]);
    for (int i = 0; i < 8; ++i) {                      // laengste Nullfolge (mind. 2) als "::" kuerzen
        if (g[i] == 0) { if (zeroStart < 0) zeroStart = i; ++zeroLen; if (zeroLen > bestLen) { bestLen = zeroLen; bestStart = zeroStart; } }
        else { zeroStart = -1; zeroLen = 0; }
    }
    if (bestLen < 2) bestStart = -1;
    for (int i = 0; i < 8; ++i) {
        if (i == bestStart) { out += L"::"; i += bestLen - 1; continue; }
        if (!out.empty() && out.back() != L':') out += L":";
        std::wstring part;
        bool started = false;
        for (int s = 12; s >= 0; s -= 4) {
            const int d = (g[i] >> s) & 0xF;
            if (d || started || s == 0) { part += hex[d]; started = true; }
        }
        out += part;
    }
    return out;
}

bool IsLoopbackV4(const unsigned char a[4]) { return a[0] == 127; }

bool IsLanV4(const unsigned char a[4])
{
    return a[0] == 10 || (a[0] == 172 && a[1] >= 16 && a[1] <= 31) || (a[0] == 192 && a[1] == 168) || (a[0] == 169 && a[1] == 254);
}

bool IsLoopbackV6(const unsigned char a[16])
{
    for (int i = 0; i < 15; ++i) if (a[i] != 0) return false;
    return a[15] == 1;
}

bool IsLanV6(const unsigned char a[16]) { return (a[0] & 0xFE) == 0xFC || (a[0] == 0xFE && (a[1] & 0xC0) == 0x80); }

namespace {

std::wstring ProcessName(unsigned pid, std::map<unsigned, std::wstring>& cache)
{
    const auto it = cache.find(pid);
    if (it != cache.end()) return it->second;
    std::wstring name;
    if (pid == 0 || pid == 4) name = L"System";
    else if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t buf[MAX_PATH] = {};
        DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, buf, &n) && n > 0) {
            name.assign(buf, n);
            const size_t k = name.find_last_of(L"\\/");
            if (k != std::wstring::npos) name = name.substr(k + 1);
        }
        CloseHandle(h);
    }
    if (name.empty()) name = L"Prozess " + std::to_wstring(pid);
    cache[pid] = name;
    return name;
}

}  // namespace

std::vector<Entry> Snapshot()
{
    std::vector<Entry> out;
    std::map<unsigned, std::wstring> names;
    std::vector<unsigned char> buf;
    DWORD size = 0;
    for (int attempt = 0; attempt < 4; ++attempt) {          // die Tabelle kann zwischen den Aufrufen wachsen
        const DWORD r = GetExtendedTcpTable(buf.empty() ? nullptr : buf.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
        if (r == NO_ERROR) {
            const auto* t = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buf.data());
            for (DWORD i = 0; i < t->dwNumEntries; ++i) {
                const MIB_TCPROW_OWNER_PID& row = t->table[i];
                if (row.dwState != MIB_TCP_STATE_ESTAB) continue;
                unsigned char a[4];
                std::memcpy(a, &row.dwRemoteAddr, 4);
                if (IsLoopbackV4(a)) continue;
                Entry e;
                e.pid = row.dwOwningPid;
                e.process = ProcessName(e.pid, names);
                e.remote = FormatV4(a);
                e.port = ntohs(static_cast<u_short>(row.dwRemotePort));
                e.lan = IsLanV4(a);
                out.push_back(std::move(e));
            }
            break;
        }
        if (r != ERROR_INSUFFICIENT_BUFFER) break;
        buf.assign(size + 4096, 0);
    }
    buf.clear();
    size = 0;
    for (int attempt = 0; attempt < 4; ++attempt) {
        const DWORD r = GetExtendedTcpTable(buf.empty() ? nullptr : buf.data(), &size, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0);
        if (r == NO_ERROR) {
            const auto* t = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(buf.data());
            for (DWORD i = 0; i < t->dwNumEntries; ++i) {
                const MIB_TCP6ROW_OWNER_PID& row = t->table[i];
                if (row.dwState != MIB_TCP_STATE_ESTAB) continue;
                if (IsLoopbackV6(row.ucRemoteAddr)) continue;
                Entry e;
                e.pid = row.dwOwningPid;
                e.process = ProcessName(e.pid, names);
                e.remote = FormatV6(row.ucRemoteAddr);
                e.port = ntohs(static_cast<u_short>(row.dwRemotePort));
                e.lan = IsLanV6(row.ucRemoteAddr);
                out.push_back(std::move(e));
            }
            break;
        }
        if (r != ERROR_INSUFFICIENT_BUFFER) break;
        buf.assign(size + 4096, 0);
    }
    return out;
}

std::vector<Group> GroupByProcess(const std::vector<Entry>& entries)
{
    std::vector<Group> groups;
    for (const Entry& e : entries) {
        auto it = std::find_if(groups.begin(), groups.end(), [&](const Group& g) { return g.process == e.process; });
        if (it == groups.end()) { groups.emplace_back(); it = groups.end() - 1; it->process = e.process; it->pid = e.pid; }
        ++it->total;
        if (!e.lan) ++it->internet;
    }
    for (Group& g : groups) {                                   // Gegenstellen: erst Internet, dann Heimnetz, ohne Doppelte
        for (int pass = 0; pass < 2; ++pass)
            for (const Entry& e : entries) {
                if (e.process != g.process || (pass == 0) == e.lan) continue;
                const std::wstring s = (e.remote.find(L':') != std::wstring::npos ? L"[" + e.remote + L"]" : e.remote) + L":" + std::to_wstring(e.port);
                if (g.samples.size() < kMaxSamples && std::find(g.samples.begin(), g.samples.end(), s) == g.samples.end()) g.samples.push_back(s);
            }
    }
    std::sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) {
        if (a.internet != b.internet) return a.internet > b.internet;
        if (a.total != b.total) return a.total > b.total;
        return a.process < b.process;
    });
    return groups;
}

}  // namespace conn
