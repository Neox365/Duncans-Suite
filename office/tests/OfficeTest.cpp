// Tests fuer Duncan's Office: Tabellen-Engine (Formeln, Fehler, CSV) und Textkodierung. Ohne Win32, reines C++17.
#include <cstdio>
#include <string>

#include "../Codec.h"
#include "../Sheet.h"

namespace {
int g_fail = 0, g_count = 0;

void Check(bool ok, const char* what, int line)
{
    ++g_count;
    if (!ok) {
        ++g_fail;
        std::printf("FEHLER Zeile %d: %s\n", line, what);
    }
}
#define CHECK(x) Check((x), #x, __LINE__)

std::wstring Show(sheet::Sheet& s, const wchar_t* cell, const wchar_t* raw)
{
    int r = 0, c = 0;
    sheet::ParseRef(cell, r, c);
    s.Set(r, c, raw);
    return s.Display(r, c);
}
}  // namespace

int main()
{
    using namespace sheet;

    // Zellbezuege
    int r = -1, c = -1;
    CHECK(ParseRef(L"A1", r, c) == 1 && r == 0 && c == 0);
    CHECK(ParseRef(L"z100", r, c) == 1 && r == 99 && c == 25);
    CHECK(ParseRef(L"A101", r, c) == 2);
    CHECK(ParseRef(L"AA1", r, c) == 2);
    CHECK(ParseRef(L"A0", r, c) == 2);
    CHECK(ParseRef(L"SUMME", r, c) == 0);
    CHECK(ParseRef(L"1A", r, c) == 0);
    CHECK(CellName(4, 2) == L"C5");

    // Zahlen
    double d = 0;
    CHECK(ParseNumber(L"12,5", d) && d == 12.5);
    CHECK(ParseNumber(L" -3.25 ", d) && d == -3.25);
    CHECK(ParseNumber(L"1e3", d) && d == 1000);
    CHECK(!ParseNumber(L"1.234,5", d));
    CHECK(!ParseNumber(L"abc", d));
    CHECK(!ParseNumber(L"", d));
    CHECK(!ParseNumber(L"-", d));
    CHECK(FormatNumber(3) == L"3");
    CHECK(FormatNumber(0.1 + 0.2) == L"0,3");
    CHECK(FormatNumber(-2.5) == L"-2,5");
    CHECK(FormatNumber(1.0 / 3.0) == L"0,3333333333");

    // Grundrechenarten, Punkt vor Strich, Klammern, Potenz, Vorzeichen
    Sheet s;
    CHECK(Show(s, L"A1", L"=1+2*3") == L"7");
    CHECK(Show(s, L"A1", L"=(1+2)*3") == L"9");
    CHECK(Show(s, L"A1", L"=10/4") == L"2,5");
    CHECK(Show(s, L"A1", L"=2^3^2") == L"512");
    CHECK(Show(s, L"A1", L"=-2+5") == L"3");
    CHECK(Show(s, L"A1", L"= 1,5 + 2,5 ") == L"4");
    CHECK(Show(s, L"A1", L"=2*-3") == L"-6");

    // Zellbezuege in Formeln, Neuberechnung nach Aenderung
    s.Clear();
    s.Set(0, 0, L"10");
    s.Set(1, 0, L"20,5");
    CHECK(Show(s, L"A3", L"=A1+A2") == L"30,5");
    CHECK(Show(s, L"B1", L"=a1*2") == L"20");
    s.Set(0, 0, L"4");
    CHECK(s.Display(2, 0) == L"24,5");
    CHECK(Show(s, L"B2", L"=C9+1") == L"1");   // leere Zelle zaehlt als 0

    // Funktionen
    s.Clear();
    for (int i = 0; i < 5; ++i) s.Set(i, 0, std::to_wstring(i + 1));   // A1..A5 = 1..5
    s.Set(5, 0, L"Text");
    CHECK(Show(s, L"B1", L"=SUMME(A1:A5)") == L"15");
    CHECK(Show(s, L"B2", L"=summe(A1:A6)") == L"15");   // Text im Bereich wird ignoriert
    CHECK(Show(s, L"B3", L"=MITTELWERT(A1:A5)") == L"3");
    CHECK(Show(s, L"B4", L"=MIN(A1:A5)") == L"1");
    CHECK(Show(s, L"B5", L"=MAX(A1:A5; 99)") == L"99");
    CHECK(Show(s, L"B6", L"=SUM(A1;A2;A3)") == L"6");
    CHECK(Show(s, L"B7", L"=AVERAGE(A1,A2)") == L"1,5");
    CHECK(Show(s, L"B8", L"=ANZAHL(A1:A6)") == L"5");
    CHECK(Show(s, L"B9", L"=SUMME(A1:A2)*2+MAX(A3:A5)") == L"11");
    CHECK(Show(s, L"B10", L"=SUMME(A5:A1)") == L"15");   // Bereich rueckwaerts
    CHECK(Show(s, L"B11", L"=SUMME(1,5)") == L"1,5");     // Komma zwischen Ziffern = Dezimalkomma
    CHECK(Show(s, L"B12", L"=SUMME(1;5)") == L"6");
    CHECK(Show(s, L"B13", L"=SUMME()") == L"0");
    CHECK(Show(s, L"B14", L"=MITTELWERT(C1:C3)") == L"#DIV/0!");   // nichts zu mitteln
    CHECK(Show(s, L"B15", L"=MAX(C1:C3)") == L"0");

    // Fehler und ihre Weitergabe
    CHECK(Show(s, L"C1", L"=1/0") == L"#DIV/0!");
    CHECK(Show(s, L"C2", L"=C1+1") == L"#DIV/0!");
    CHECK(Show(s, L"C3", L"=SUMME(C1:C2)") == L"#DIV/0!");
    CHECK(Show(s, L"C4", L"=A6+1") == L"#WERT!");        // Text in Rechnung
    CHECK(Show(s, L"C5", L"=FOO(1)") == L"#NAME?");
    CHECK(Show(s, L"C6", L"=FOO") == L"#NAME?");
    CHECK(Show(s, L"C7", L"=A101") == L"#BEZUG!");
    CHECK(Show(s, L"C8", L"=SUMME(A1:A101)") == L"#BEZUG!");
    CHECK(Show(s, L"C9", L"=1+") == L"#SYNTAX!");
    CHECK(Show(s, L"C10", L"=(1+2") == L"#SYNTAX!");
    CHECK(Show(s, L"C11", L"=1 2") == L"#SYNTAX!");
    CHECK(Show(s, L"C12", L"=") == L"#SYNTAX!");
    CHECK(Show(s, L"C13", L"=A1:A2") == L"#WERT!");
    CHECK(Show(s, L"C14", L"=10^1000") == L"#ZAHL!");

    // Zirkelbezug
    s.Clear();
    s.Set(0, 0, L"=B1+1");
    s.Set(0, 1, L"=A1+1");
    CHECK(s.Display(0, 0) == L"#ZIRKEL!");
    CHECK(s.Display(0, 1) == L"#ZIRKEL!");
    s.Set(1, 1, L"=B2");
    CHECK(s.Display(1, 1) == L"#ZIRKEL!");

    // lange Bezugskette ueberlaeuft den Stack nicht
    s.Clear();
    s.Set(0, 0, L"1");
    for (int i = 1; i < kRows; ++i) s.Set(i, 0, L"=A" + std::to_wstring(i) + L"+1");
    CHECK(s.Display(kRows - 1, 0) == L"100");

    // Text bleibt Text, auch mit Gleichheitszeichen mittendrin
    s.Clear();
    CHECK(Show(s, L"A1", L"Hallo Welt") == L"Hallo Welt");
    CHECK(Show(s, L"A2", L"1.234,5") == L"1.234,5");
    CHECK(Show(s, L"A3", L"a=b") == L"a=b");
    CHECK(Show(s, L"A4", L"   ") == L"");

    // CSV: Export
    s.Clear();
    s.Set(0, 0, L"Name");
    s.Set(0, 1, L"Betrag");
    s.Set(1, 0, L"Miete; Strom");
    s.Set(1, 1, L"12,5");
    s.Set(2, 0, L"Sagt \"Hi\"");
    s.Set(2, 1, L"=SUMME(B2:B2;1)");
    s.Set(3, 0, L"Zeile1\nZeile2");
    CHECK(ToCsv(s) == L"Name;Betrag\r\n\"Miete; Strom\";12,5\r\n\"Sagt \"\"Hi\"\"\";\"=SUMME(B2:B2;1)\"\r\n\"Zeile1\nZeile2\";\r\n"
          || ToCsv(s) == L"Name;Betrag\r\n\"Miete; Strom\";12,5\r\n\"Sagt \"\"Hi\"\"\";=SUMME(B2:B2;1)\r\n\"Zeile1\nZeile2\";\r\n");
    Sheet empty;
    CHECK(ToCsv(empty).empty());

    // CSV: Rundreise inklusive Formeln, Anfuehrungszeichen und Zeilenumbruch
    Sheet back;
    CHECK(FromCsv(back, ToCsv(s)) == 0);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 2; ++j) CHECK(back.raw[i][j] == s.raw[i][j]);
    CHECK(back.Display(2, 1) == L"13,5");

    // CSV: Trennzeichen-Erkennung und Sonderfaelle
    CHECK(FromCsv(back, L"a,b,c\n1,2,3\n") == 0 && back.raw[1][2] == L"3" && back.raw[0][1] == L"b");
    CHECK(FromCsv(back, L"a\tb\r\n1\t2") == 0 && back.raw[1][1] == L"2");
    CHECK(FromCsv(back, L"\xFEFFx;y\r\n") == 0 && back.raw[0][0] == L"x");
    CHECK(FromCsv(back, L"\"a;b\";c") == 0 && back.raw[0][0] == L"a;b" && back.raw[0][1] == L"c");
    CHECK(FromCsv(back, L"1;;3\n") == 0 && back.raw[0][1].empty() && back.raw[0][2] == L"3");
    CHECK(FromCsv(back, L"") == 0 && !back.Used(r, c));
    CHECK(FromCsv(back, L"a;b\r\n\r\nc;d") == 0 && back.raw[1][0].empty() && back.raw[2][0] == L"c");

    // CSV: zu grosse Tabellen werden gekuerzt und gemeldet
    std::wstring wide;
    for (int i = 0; i < kCols + 3; ++i) wide += (i ? L";" : L"") + std::to_wstring(i + 1);
    CHECK(FromCsv(back, wide) == 3);
    std::wstring tall;
    for (int i = 0; i < kRows + 2; ++i) tall += std::to_wstring(i + 1) + L"\n";
    CHECK(FromCsv(back, tall) == 2 && back.raw[kRows - 1][0] == std::to_wstring(kRows));

    // Textkodierung
    CHECK(codec::WideToUtf8(L"ä€") == "\xC3\xA4\xE2\x82\xAC");
    CHECK(codec::Utf8ToWide("\xC3\xA4\xE2\x82\xAC") == L"ä€");
    CHECK(codec::WideToUtf8(L"\xD83D\xDE00") == "\xF0\x9F\x98\x80");   // Emoji (Surrogatpaar)
    CHECK(codec::Utf8ToWide("\xF0\x9F\x98\x80") == L"\xD83D\xDE00");
    bool ok = true;
    codec::Utf8ToWide("\xE4", &ok);
    CHECK(!ok);                                   // einzelnes Latin-1-Byte ist kein UTF-8
    codec::Utf8ToWide("\xC3", &ok);
    CHECK(!ok);                                   // abgeschnittene Folge
    codec::Utf8ToWide("\xC0\x80", &ok);
    CHECK(!ok);                                   // Overlong
    CHECK(codec::DecodeAuto("K\xE4se \x80") == L"Käse €");   // Rueckfall Windows-1252
    CHECK(codec::DecodeAuto("\xEF\xBB\xBF" "ab") == L"ab");
    CHECK(codec::DecodeAuto(std::string("\xFF\xFE" "a\0b\0", 6)) == L"ab");
    CHECK(codec::DecodeAuto(codec::EncodeUtf8Bom(L"über")) == L"über");

    std::printf("%d Pruefungen, %d Fehler\n", g_count, g_fail);
    return g_fail ? 1 : 0;
}
