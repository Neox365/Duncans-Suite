// Test fuer common/CardSearch.h:  g++ -std=c++17 -I../../common SearchTest.cpp -o t && ./t
#include <cstdio>
#include "../../common/CardSearch.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    using namespace cardsearch;
    const std::wstring boostT = L"Duncan's Boost", boostD = L"Dein Zock-Werkzeug: Hintergrund aufr\u00E4umen, Overlay, Clips speichern und Fadenkreuz.";
    const std::wstring boostK = L"spiel spiele zocken gaming fps screenshot ping";
    CHECK(Fold(L"\u00C4rger \u00DCber Stra\u00DFe \u00F6l") == L"aerger ueber strasse oel");
    CHECK(Words(L"  Zocken   fps ").size() == 2 && Words(L"").empty() && Words(L"   ").empty());
    CHECK(Matches(L"", boostT, boostD, boostK));                               // leer: alles
    CHECK(Matches(L"boost", boostT, boostD, boostK) && Matches(L"BOOST", boostT, boostD, boostK));
    CHECK(Matches(L"zocken", boostT, boostD, boostK) && Matches(L"fps", boostT, boostD, boostK));   // Stichwort
    CHECK(Matches(L"aufraeumen", boostT, boostD, boostK) && Matches(L"aufr\u00E4umen", boostT, boostD, boostK));   // Umlaut in der Beschreibung
    CHECK(Matches(L"zocken ping", boostT, boostD, boostK));                    // alle Woerter
    CHECK(!Matches(L"zocken pdf", boostT, boostD, boostK));                    // ein Wort fehlt
    CHECK(!Matches(L"tresor", boostT, boostD, boostK));
    CHECK(Matches(L"  fadenkreuz  ", boostT, boostD, boostK));                 // Leerzeichen am Rand
    std::printf(failures ? "%d Fehler\n" : "alle Pruefungen bestanden\n", failures);
    return failures ? 1 : 0;
}
