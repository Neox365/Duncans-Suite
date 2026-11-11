// Tests: Steuersaetze je Position, Rabatt (negative Position), GiroCode-Nutzdaten, GiroCode im Rechnungs-PDF.
//   g++ -std=c++17 -DUNICODE -D_UNICODE -I.. GiroTest.cpp -o GiroTest.exe
// Mit einem Ordner als Argument schreibt der Test Beispiel-PDFs dorthin; tools/CheckQr.py --pdf liest den GiroCode daraus
// mit einem unabhaengigen Decoder und vergleicht ihn mit den Nutzdaten.
#include <cstdio>
#include <fstream>
#include <string>

#include "../Invoice.h"

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

static bool Has(const std::string& s, const std::string& sub) { return s.find(sub) != std::string::npos; }

static size_t CountOf(const std::string& s, const std::string& sub)
{
    size_t n = 0;
    for (size_t p = 0; (p = s.find(sub, p)) != std::string::npos; p += sub.size()) ++n;
    return n;
}

static invoice::Data Sample()
{
    invoice::Data d;
    d.issuer = L"Mustermann Webdesign";
    d.issuerAddress = L"Musterstraße 12 | 12345 Musterstadt";
    d.recipient = L"Beispiel GmbH";
    d.recipientAddress = L"Hauptstraße 1 | 10115 Berlin";
    d.number = L"RE-2026-001";
    d.dateIso = L"2026-11-11";
    d.dueIso = L"2026-11-25";
    d.iban = L"DE02 1203 0000 0000 2020 51";
    d.bic = L"BYLADEM1001";
    d.bank = L"Musterbank eG";
    return d;
}

int main(int argc, char** argv)
{
    // ---- IBAN ----
    CHECK(giro::IbanValid(L"DE02 1203 0000 0000 2020 51"));
    CHECK(giro::IbanValid(L"de89370400440532013000"));                  // Kleinbuchstaben und ohne Leerzeichen
    CHECK(giro::IbanValid(L"GB82 WEST 1234 5698 7654 32"));              // Buchstaben im Konto
    CHECK(!giro::IbanValid(L"DE02 1203 0000 0000 2020 52"));             // falsche Pruefziffer
    CHECK(!giro::IbanValid(L"") && !giro::IbanValid(L"DE02") && !giro::IbanValid(L"1234567890123456") && !giro::IbanValid(L"DE02 1203 0000 0000 2020 5!"));
    CHECK(giro::NormalizeIban(L" de02 1203\t0000 ") == L"DE0212030000");

    // ---- EPC-Nutzdaten ----
    const std::string p = giro::EpcPayload(L"Mustermann Webdesign", L"DE02 1203 0000 0000 2020 51", L"BYLADEM1001", 142800, L"Rechnung RE-2026-001");
    CHECK(p == "BCD\n002\n1\nSCT\nBYLADEM1001\nMustermann Webdesign\nDE02120300000000202051\nEUR1428.00\n\n\nRechnung RE-2026-001");
    CHECK(giro::EpcPayload(L"A", L"DE02120300000000202051", L"", 5, L"x") == "BCD\n002\n1\nSCT\n\nA\nDE02120300000000202051\nEUR0.05\n\n\nx");   // ohne BIC, 5 Cent
    CHECK(Has(giro::EpcPayload(L"A", L"DE02120300000000202051", L"kaputt", 100, L"x"), "SCT\n\nA\n"));    // ungueltige BIC entfaellt
    CHECK(Has(giro::EpcPayload(L"Müller & Söhne", L"DE02120300000000202051", L"", 100, L"x"), "M\xC3\xBCller & S\xC3\xB6hne"));   // UTF-8
    CHECK(giro::EpcPayload(L"A", L"DE02120300000000202052", L"", 100, L"x").empty());                   // falsche IBAN
    CHECK(giro::EpcPayload(L"A", L"DE02120300000000202051", L"", 0, L"x").empty());                     // Betrag 0
    CHECK(giro::EpcPayload(L"A", L"DE02120300000000202051", L"", -100, L"x").empty());                  // negativ (Gutschrift)
    CHECK(giro::EpcPayload(L"A", L"DE02120300000000202051", L"", 100000000000LL, L"x").empty());        // zu gross
    CHECK(giro::EpcPayload(L"  ", L"DE02120300000000202051", L"", 100, L"x").empty());                  // kein Empfaenger
    {
        const std::string n = giro::EpcPayload(std::wstring(100, L'N'), L"DE02120300000000202051", L"BYLADEM1001", 100, std::wstring(300, L'T'));
        CHECK(!n.empty() && n.size() <= giro::kMaxBytes);                                                // Name auf 70, Text gekuerzt
        CHECK(CountOf(n, "N") == 70);
        const std::string z = giro::EpcPayload(L"Zeile1\nZeile2", L"DE02120300000000202051", L"", 100, L"a\r\nb");
        CHECK(Has(z, "\nZeile1 Zeile2\n") && !Has(z, "\r") && CountOf(z, "\n") == 10);                   // keine Umbrueche in Feldern
    }

    // ---- Steuersaetze ----
    {
        invoice::Data d = Sample();
        d.vatPercent = 19;
        d.items = {{L"Webdesign", 1, 100000, -1}, {L"Fachbuch", 2, 2500, 7}, {L"Beratung", 1, 20000, -1}, {L"Hotel", 1, 10000, 0}};
        const invoice::Totals t = invoice::Compute(d);
        CHECK(t.net == 100000 + 5000 + 20000 + 10000);
        CHECK(t.vatLines.size() == 3);
        CHECK(t.vatLines[0].percent == 0 && t.vatLines[0].net == 10000 && t.vatLines[0].vat == 0);       // aufsteigend sortiert
        CHECK(t.vatLines[1].percent == 7 && t.vatLines[1].net == 5000 && t.vatLines[1].vat == 350);
        CHECK(t.vatLines[2].percent == 19 && t.vatLines[2].net == 120000 && t.vatLines[2].vat == 22800);
        CHECK(t.vat == 23150 && t.gross == 135000 + 23150);
    }
    {   // Rundung je Steuersatz, nicht je Position: 3 x 0,33 EUR zu 19 %
        invoice::Data d = Sample();
        d.items = {{L"a", 1, 33}, {L"b", 1, 33}, {L"c", 1, 33}};
        const invoice::Totals t = invoice::Compute(d);
        CHECK(t.vatLines.size() == 1 && t.vatLines[0].vat == 19 && t.vat == 19);                         // 0,99 * 19 % = 0,1881 -> 0,19 (je Position waeren es 3 x 0,06 = 0,18)
    }
    {   // Rabatt als negative Position, mit eigenem Satz
        invoice::Data d = Sample();
        d.items = {{L"Leistung", 1, 100000}, {L"Rabatt 10 %", 1, -10000}};
        const invoice::Totals t = invoice::Compute(d);
        CHECK(t.net == 90000 && t.vat == 17100 && t.gross == 107100);
    }
    {   // Kleinunternehmer: keine Steuerzeilen, auch bei gesetzten Positionssaetzen
        invoice::Data d = Sample();
        d.smallBusiness = true;
        d.items = {{L"a", 1, 10000, 7}, {L"b", 1, 5000, 19}};
        const invoice::Totals t = invoice::Compute(d);
        CHECK(t.vatLines.empty() && t.vat == 0 && t.gross == 15000);
    }
    {   // Altes Verhalten: ein Satz fuer alles
        invoice::Data d = Sample();
        d.vatPercent = 7;
        d.items = {{L"a", 1, 10000}, {L"b", 3, 1000}};
        const invoice::Totals t = invoice::Compute(d);
        CHECK(t.vatLines.size() == 1 && t.vatLines[0].percent == 7 && t.vat == 910 && t.gross == 13910);
    }

    // ---- PDF ----
    {
        invoice::Data d = Sample();
        d.items = {{L"Webdesign", 1, 100000}, {L"Hosting", 12, 990}};
        const std::string pdf = invoice::BuildPdf(d);
        CHECK(Has(pdf, "GiroCode") && Has(pdf, "mit der Banking-App scannen"));
        CHECK(CountOf(pdf, " re f") > 150);                                                               // QR-Rechtecke (zusaetzlich zu Kopf- und Summenflaechen)
        CHECK(Has(pdf, "Umsatzsteuer 19 %") && !Has(pdf, " auf "));                                        // ein Satz: wie bisher
        CHECK(!Has(pdf, "(USt)"));
        invoice::Data noIban = d;
        noIban.iban.clear();
        const std::string a = invoice::BuildPdf(noIban);
        CHECK(!Has(a, "GiroCode") && CountOf(a, " re f") < 10);                                            // ohne IBAN kein GiroCode
        invoice::Data badIban = d;
        badIban.iban = L"DE02 1203 0000 0000 2020 52";
        CHECK(!Has(invoice::BuildPdf(badIban), "GiroCode"));
        invoice::Data credit = d;
        credit.items = {{L"Gutschrift", 1, -5000}};
        CHECK(!Has(invoice::BuildPdf(credit), "GiroCode"));                                                // Gutschrift: nichts zu zahlen
        invoice::Data small = d;
        small.smallBusiness = true;
        CHECK(Has(invoice::BuildPdf(small), "GiroCode"));                                                  // Kleinunternehmer: Betrag brutto = netto, GiroCode bleibt
        invoice::Data multi = d;
        multi.items = {{L"Webdesign", 1, 100000, -1}, {L"Fachbuch", 2, 2500, 7}};
        const std::string m = invoice::BuildPdf(multi);
        CHECK(Has(m, "(USt)") && Has(m, "Umsatzsteuer 7 % auf ") && Has(m, "Umsatzsteuer 19 % auf ") && Has(m, "(7 %)") && Has(m, "(19 %)"));
        // Sehr viele Positionen: der GiroCode landet auf einer eigenen Seite statt zu fehlen
        invoice::Data big = d;
        big.items.clear();
        for (int i = 0; i < 30; ++i) big.items.push_back({L"Position " + std::to_wstring(i + 1), 1, 1000 + i});
        CHECK(Has(invoice::BuildPdf(big), "GiroCode"));
        if (argc > 1) {
            auto save = [&](const char* name, const invoice::Data& x) {
                std::ofstream f(std::string(argv[1]) + "/" + name, std::ios::binary);
                const std::string b = invoice::BuildPdf(x);
                f.write(b.data(), static_cast<std::streamsize>(b.size()));
            };
            save("giro_einfach.pdf", d);
            save("giro_mehrere_saetze.pdf", multi);
            save("giro_viele_positionen.pdf", big);
            invoice::Data nobic = d;
            nobic.bic.clear();
            nobic.number = L"RE-2026-002";
            nobic.recipient = L"Müller & Söhne";
            save("giro_ohne_bic.pdf", nobic);
        }
    }

    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
