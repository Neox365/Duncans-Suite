// Logik-Tests der Verbindungsuebersicht (Formatierung, Heimnetz-Erkennung, Zusammenfassen). Baut mit MinGW:
//   g++ -std=c++17 -DUNICODE -D_UNICODE ConnTest.cpp ../Connections.cpp -o ConnTest.exe -liphlpapi -lws2_32
#include "../Connections.h"

#include <cstdio>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

int main()
{
    using namespace conn;
    const unsigned char a[4] = { 142, 250, 185, 46 }, lo[4] = { 127, 0, 0, 1 }, l1[4] = { 192, 168, 1, 5 }, l2[4] = { 172, 20, 0, 1 }, l3[4] = { 172, 32, 0, 1 }, l4[4] = { 10, 0, 0, 2 }, l5[4] = { 169, 254, 3, 4 };
    CHECK(FormatV4(a) == L"142.250.185.46");
    CHECK(IsLoopbackV4(lo) && !IsLoopbackV4(a));
    CHECK(IsLanV4(l1) && IsLanV4(l2) && IsLanV4(l4) && IsLanV4(l5) && !IsLanV4(l3) && !IsLanV4(a));

    const unsigned char v6[16] = { 0x26, 0x07, 0xf8, 0xb0, 0x40, 0x04, 0x08, 0x0c, 0, 0, 0, 0, 0, 0, 0x20, 0x0e };
    CHECK(FormatV6(v6) == L"2607:f8b0:4004:80c::200e");
    const unsigned char loop6[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
    CHECK(IsLoopbackV6(loop6) && !IsLoopbackV6(v6));
    CHECK(FormatV6(loop6) == L"::1");
    const unsigned char ula[16] = { 0xfd, 0x12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 }, ll[16] = { 0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2 };
    CHECK(IsLanV6(ula) && IsLanV6(ll) && !IsLanV6(v6));

    std::vector<Entry> es;
    auto add = [&](const wchar_t* p, unsigned pid, const wchar_t* r, unsigned port, bool lan) { Entry e; e.process = p; e.pid = pid; e.remote = r; e.port = port; e.lan = lan; es.push_back(e); };
    add(L"chrome.exe", 10, L"142.250.1.1", 443, false);
    add(L"chrome.exe", 10, L"142.250.1.2", 443, false);
    add(L"chrome.exe", 10, L"142.250.1.1", 443, false);       // gleiche Gegenstelle zweimal
    add(L"chrome.exe", 10, L"192.168.1.1", 80, true);
    add(L"chrome.exe", 10, L"142.250.1.9", 443, false);
    add(L"chrome.exe", 10, L"142.250.1.10", 443, false);
    add(L"drucker.exe", 20, L"192.168.1.50", 9100, true);
    add(L"spiel.exe", 30, L"2001:db8::1", 27015, false);
    const std::vector<Group> gs = GroupByProcess(es);
    CHECK(gs.size() == 3);
    CHECK(gs[0].process == L"chrome.exe" && gs[0].total == 6 && gs[0].internet == 5);
    CHECK(gs[0].samples.size() == kMaxSamples && gs[0].samples[0] == L"142.250.1.1:443" && gs[0].samples[1] == L"142.250.1.2:443");   // ohne Doppelte, Internet zuerst
    CHECK(gs[1].process == L"spiel.exe" && gs[1].samples[0] == L"[2001:db8::1]:27015");
    CHECK(gs[2].process == L"drucker.exe" && gs[2].internet == 0 && gs[2].total == 1);
    CHECK(GroupByProcess({}).empty());

    const std::vector<Entry> now = Snapshot();                 // je nach Rechner leer oder voll; darf nicht abstuerzen
    for (const Entry& e : now) CHECK(!e.process.empty() && e.remote != L"127.0.0.1");
    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
