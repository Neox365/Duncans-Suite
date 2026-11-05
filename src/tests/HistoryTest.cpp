// Logik-Tests fuer den Treiber-Verlauf des Driver Updaters (ohne Oberflaeche)
#include "../DriverHistory.h"

#include <cstdio>
#include <cstring>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

int main()
{
    using namespace drvhistory;
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    const std::wstring path = std::wstring(tmp) + L"drvhist" + std::to_wstring(GetCurrentProcessId()) + L".txt";
    DeleteFileW(path.c_str());

    CHECK(Load(path).empty());                       // Datei fehlt: leer, kein Fehler
    CHECK(!Append(path, {}));                        // nichts anzuhaengen
    CHECK(!Append(L"", { Entry{} }));

    Entry a; a.when = L"05.11.2026 18:30"; a.device = L"Realtek(R) Audio"; a.title = L"Realtek - Sound - 6.0.9\t999"; a.oldVersion = L"6.0.9500.1";
    a.newDate = L"01.10.2026"; a.ok = true; a.restorePoint = true;
    Entry b; b.when = L"05.11.2026 18:31"; b.device = L"Gerät mit Umlaut äöü"; b.title = L"Zeile1\nZeile2"; b.oldVersion = L""; b.ok = false;
    CHECK(Append(path, { a, b }));
    std::vector<Entry> l = Load(path);
    CHECK(l.size() == 2);
    CHECK(l[0].device == L"Realtek(R) Audio" && l[0].ok && l[0].restorePoint && l[0].newDate == L"01.10.2026");
    CHECK(l[0].title == L"Realtek - Sound - 6.0.9 999");            // Tab wird zu Leerzeichen
    CHECK(l[1].device == L"Gerät mit Umlaut äöü" && !l[1].ok && !l[1].restorePoint);
    CHECK(l[1].title == L"Zeile1 Zeile2" && l[1].oldVersion.empty());

    // Anhaengen behaelt die Reihenfolge, Beschnitt auf die neuesten
    std::vector<Entry> many;
    for (int i = 0; i < 250; ++i) { Entry e; e.when = std::to_wstring(i); e.device = L"d"; e.ok = true; many.push_back(e); }
    CHECK(Append(path, many));
    l = Load(path);
    CHECK(l.size() == kMaxEntries);
    CHECK(l.front().when == L"50" && l.back().when == L"249");

    // beschaedigte Zeilen werden uebersprungen
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        const char* t = "kaputt\r\n\r\n01.01.2026 10:00\tGeraet\tTitel\t1.0\t\t1\t0\r\nnur\tzwei\r\n";
        DWORD n; WriteFile(h, t, (DWORD)std::strlen(t), &n, nullptr); CloseHandle(h);
    }
    l = Load(path);
    CHECK(l.size() == 1 && l[0].device == L"Geraet" && l[0].ok && !l[0].restorePoint);
    DeleteFileW(path.c_str());

    CHECK(!NowText().empty() && NowText().size() == 16 && NowText()[2] == L'.' && NowText()[13] == L':');
    CHECK(!FilePath().empty());

    // Alter
    CHECK(IsOlderThan(20230101, 20260315, 2));
    CHECK(!IsOlderThan(20240315, 20260315, 2));      // genau 2 Jahre: noch nicht aelter
    CHECK(IsOlderThan(20240314, 20260315, 2));
    CHECK(!IsOlderThan(0, 20260315, 2));             // unbekanntes Datum
    CHECK(!IsOlderThan(20260101, 20260315, 2));
    CHECK(TodayKey() > 20250101);

    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
