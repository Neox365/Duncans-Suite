// Test der Rechtschreibpruefung (office/Spell.h) mit dem echten Windows-Woerterbuch.
// Baut nur mit MSVC:  cl /std:c++17 /EHsc /utf-8 /permissive- /I.. SpellTest.cpp /Fe:SpellTest.exe ole32.lib user32.lib
// Fehlt das deutsche Woerterbuch, werden die deutschen Pruefungen uebersprungen.
#include "../Spell.h"

#include <cstdio>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

static bool Contains(const std::vector<std::wstring>& v, const std::wstring& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

int main()
{
    // Filter fuer Zahlen und Kennungen (braucht kein Woerterbuch)
    CHECK(spell::IgnorableWord(L"2026-117") && spell::IgnorableWord(L"BYLADEM1001") && spell::IgnorableWord(L"mail@beispiel.de"));
    CHECK(spell::IgnorableWord(L"www.beispiel.de") && spell::IgnorableWord(L"C:\\x") && spell::IgnorableWord(L"---"));
    CHECK(!spell::IgnorableWord(L"Rechnugn") && !spell::IgnorableWord(L"Müller"));

    spell::Checker c(L"de-DE");
    if (!c.Ok()) { std::printf("SKIP: kein Woerterbuch verfuegbar\n"); return 0; }
    std::printf("Sprache: %ls\n", c.Language().c_str());
    if (c.Language().rfind(L"de", 0) != 0) { std::printf("SKIP: kein deutsches Woerterbuch (nur %ls)\n", c.Language().c_str()); return 0; }

    const std::wstring text = L"Ich habe eine Rechnugn gesehen und das ist sher wichtig. Straße 12, IBAN DE02120300000000202051.";
    const auto first = c.Find(text, 0, 1);
    CHECK(first.size() == 1 && first[0].word == L"Rechnugn" && first[0].start == static_cast<LONG>(text.find(L"Rechnugn")) && first[0].length == 8);
    const auto all = c.Find(text, 0, 10);
    CHECK(all.size() == 2 && all[1].word == L"sher");                                  // IBAN, Zahl und "Straße" werden nicht gemeldet
    CHECK(all[1].start == static_cast<LONG>(text.find(L"sher")));
    const auto from = c.Find(text, text.find(L"gesehen"), 10);                                             // ab hinter dem ersten Fehler: Positionen bleiben absolut
    CHECK(from.size() == 1 && from[0].word == L"sher" && from[0].start == static_cast<LONG>(text.find(L"sher")));
    CHECK(c.Find(text, text.size(), 5).empty() && c.Find(L"", 0, 5).empty());

    CHECK(Contains(c.Suggest(L"Rechnugn"), L"Rechnung"));
    CHECK(Contains(c.Suggest(L"sher"), L"sehr"));
    CHECK(c.Suggest(L"Rechnung").empty() || !Contains(c.Suggest(L"Rechnugn"), L"Rechnugn"));

    const auto rep = c.Find(L"Das ist das das Haus.", 0, 5);                             // doppeltes Wort
    std::printf("doppeltes Wort: %zu Treffer, erstes: '%ls'\n", rep.size(), rep.empty() ? L"" : rep[0].word.c_str());
    CHECK(rep.empty() || rep[0].repeated);                                              // das deutsche Windows-Woerterbuch meldet doppelte Woerter nicht; andere Sprachen schon

    CHECK(c.Find(L"Müller & Söhne GmbH, Hauptstraße 12, 10115 Berlin", 0, 5).empty());   // korrekter Text mit Umlauten
    c.Ignore(L"Rechnugn");                                                              // nur fuer diese Sitzung
    const auto after = c.Find(text, 0, 10);
    CHECK(after.size() == 1 && after[0].word == L"sher");

    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
