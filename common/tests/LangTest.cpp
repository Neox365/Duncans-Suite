// Test der Sprachumschaltung (common/Lang.h).
// Baut mit MinGW:  g++ -std=c++17 -I.. LangTest.cpp -o LangTest.exe
#include "../Lang.h"

#include <cstdio>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

int main()
{
    static const lang::Entry kT[] = {
        {L"Speichern unter", L"Save as"},
        {L"{*} Rechnungen", L"{1} invoices"},
        {L"{*} Rechnung", L"{1} invoice"},
        {L"Position {*}: Die Menge ist ungültig.", L"Line {1}: The quantity is invalid."},
        {L"{*} von {*} fertig", L"{2} of {1} done"},
        {L"Rechnung {*} stornieren?", L"Cancel invoice {1}?"},
        {L"Status: {*}", L"Status: {1}"},
        {L"Bezahlt", L"Paid"},
    };

    // Die Sprachdatei landet in einem Wegwerf-Ordner, nicht im echten Profil
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    SetEnvironmentVariableW(L"LOCALAPPDATA", tmp);

    // Deutsch: alles bleibt, wie es ist
    SetEnvironmentVariableW(L"DUNCANS_LANG", L"de");
    lang::Init();
    lang::Add(kT);
    CHECK(!lang::English());
    CHECK(lang::Tr(L"Speichern unter") == L"Speichern unter");
    CHECK(lang::Tr(L"3 Rechnungen") == L"3 Rechnungen");

    // Englisch
    lang::SetMode(lang::Mode::English);
    CHECK(lang::English());
    CHECK(lang::Tr(L"Speichern unter") == L"Save as");
    CHECK(lang::Tr(L"Hinweis") == L"Notice");                                  // gemeinsame Tabelle
    CHECK(lang::Tr(L"Schließen") == L"Close");
    CHECK(lang::Tr(L"12 Rechnungen") == L"12 invoices");
    CHECK(lang::Tr(L"1 Rechnung") == L"1 invoice");
    CHECK(lang::Tr(L"Position 4: Die Menge ist ungültig.") == L"Line 4: The quantity is invalid.");
    CHECK(lang::Tr(L"2 von 9 fertig") == L"9 of 2 done");                      // Reihenfolge der Platzhalter
    CHECK(lang::Tr(L"Rechnung RE-2026-0001 stornieren?") == L"Cancel invoice RE-2026-0001?");
    CHECK(lang::Tr(L"Status: Bezahlt") == L"Status: Paid");                    // erfasster Teil wird mituebersetzt
    CHECK(lang::Tr(L"Status: Unbekannt") == L"Status: Unbekannt");
    CHECK(lang::Tr(L"Mein Text, nicht in der Tabelle") == L"Mein Text, nicht in der Tabelle");
    CHECK(lang::Tr(L"") == L"");
    CHECK(lang::Tr(L"Telekom_Rechnung.pdf") == L"Telekom_Rechnung.pdf");       // Dateinamen bleiben
    CHECK(lang::Tr(std::wstring(L"Bezahlt")) == L"Paid");

    // Zeilen aus mehreren Teilen mit Mittelpunkt
    CHECK(lang::Tr(L"3 Rechnungen   ·   Status: Bezahlt   ·   x:/Ordner") == L"3 invoices   ·   Status: Paid   ·   x:/Ordner");

    // zurueck auf Deutsch (und die Wahl in der Datei aufraeumen)
    lang::SetMode(lang::Mode::German);
    CHECK(lang::Tr(L"Bezahlt") == L"Bezahlt");
    lang::SetMode(lang::Mode::Auto);

    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
