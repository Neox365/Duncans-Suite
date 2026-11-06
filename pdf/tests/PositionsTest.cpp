// Tests fuer das Merken der letzten Seite pro Datei (Positions.h). Nur Windows.
// g++ -std=c++17 -DUNICODE -D_UNICODE -I.. PositionsTest.cpp -o PositionsTest.exe -static
#include "../Positions.h"

#include <cstdio>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

int main()
{
    using namespace pdfpos;
    const std::string a = KeyFor(L"C:\\Users\\X\\Doku\\Buch.pdf");
    CHECK(a.size() == 16 && a == KeyFor(L"c:\\users\\x\\doku\\buch.PDF"));          // Gross-/Kleinschreibung egal wie bei Windows-Pfaden
    CHECK(a != KeyFor(L"C:\\Users\\X\\Doku\\Buch2.pdf") && KeyFor(L"") .size() == 16);
    CHECK(KeyFor(L"\x00E4.pdf") != KeyFor(L"a.pdf"));                                  // Umlaute zaehlen mit

    List l;
    CHECK(Get(l, a) == -1);
    Set(l, a, 12);
    CHECK(Get(l, a) == 12 && l.size() == 1);
    Set(l, a, 30);
    CHECK(Get(l, a) == 30 && l.size() == 1);                                           // ueberschrieben, nicht doppelt
    Set(l, KeyFor(L"b.pdf"), -5);
    CHECK(Get(l, KeyFor(L"b.pdf")) == 0);                                              // negative Seite wird 0
    Set(l, a, 31);
    CHECK(l.back().first == a);                                                        // zuletzt benutzt = hinten

    // Hin und zurueck
    const List r = Parse(Serialize(l));
    CHECK(r == l);
    // Kaputte Zeilen werden ignoriert
    const std::string bad = "xyz\n=5\n0123456789abcdef=\n0123456789abcdeg=3\n0123456789abcdef=12x\n0123456789abcdef=99999999999\n\n0123456789abcdef=7\r\n";
    const List b = Parse(bad);
    CHECK(b.size() == 1 && b[0].first == "0123456789abcdef" && b[0].second == 7);

    // Obergrenze: die aeltesten fliegen raus
    List many;
    for (int i = 0; i < 250; ++i) Set(many, KeyFor(L"f" + std::to_wstring(i)), i);
    CHECK(many.size() == kMaxEntries && Get(many, KeyFor(L"f0")) == -1 && Get(many, KeyFor(L"f249")) == 249 && Get(many, KeyFor(L"f50")) == 50);

    // Datei
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring file = std::wstring(tmp) + L"pdfpos_test_" + std::to_wstring(GetCurrentProcessId()) + L".txt";
    CHECK(Save(file, many) && Load(file) == many);
    DeleteFileW(file.c_str());
    CHECK(Load(file).empty());                                                         // fehlende Datei: leer, kein Absturz

    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
