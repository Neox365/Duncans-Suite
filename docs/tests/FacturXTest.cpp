// Test der E-Rechnungs-Erzeugung (docs/FacturX.h): Pflichtangaben, Summen, Einbettung ins PDF und Gegenprobe mit dem eigenen Einleser.
// Baut mit MinGW:  g++ -std=c++17 -I.. FacturXTest.cpp -o FacturXTest.exe
#include "../FacturX.h"
#include "../EInvoice.h"

#include <cstdio>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)
static bool Has(const std::string& h, const char* n) { return h.find(n) != std::string::npos; }

static invoice::Data Sample()
{
    invoice::Data d;
    d.issuer = L"Muster & Söhne GmbH"; d.issuerAddress = L"Hauptstraße 1|10115 Berlin";
    d.recipient = L"Kunde <AG>"; d.recipientAddress = L"Zweitweg 2|80331 München";
    d.number = L"RE-2026-001"; d.dateIso = L"2026-11-11"; d.dueIso = L"2026-11-25";
    d.taxNo = L"DE123456789"; d.iban = L"DE02120300000000202051"; d.bic = L"BYLADEM1001"; d.email = L"info@muster.de";
    d.items = { {L"Beratung", 2.5, 8000, -1}, {L"Buch", 1, 1999, 7}, {L"Rabatt", 1, -500, -1} };
    d.vatPercent = 19;
    return d;
}

int main(int argc, char** argv)
{
    invoice::Data d = Sample();
    CHECK(invoice::EInvoiceProblem(d).empty());
    const std::string x = invoice::BuildEInvoiceXml(d);
    CHECK(!x.empty() && Has(x, "urn:cen.eu:en16931:2017") && Has(x, "<ram:ID>RE-2026-001</ram:ID>") && Has(x, "Muster &amp; S\xC3\xB6hne GmbH") && Has(x, "Kunde &lt;AG&gt;"));
    CHECK(Has(x, "schemeID=\"VA\">DE123456789") && Has(x, "<ram:PostcodeCode>10115</ram:PostcodeCode>") && Has(x, "<ram:LineOne>Hauptstra\xC3\x9F" "e 1</ram:LineOne>"));
    // Summen: Beratung 200,00 (19 %) + Rabatt -5,00 (19 %) = 195,00 netto -> 37,05; Buch 19,99 (7 %) -> 1,40
    const invoice::Totals t = invoice::Compute(d);
    CHECK(t.net == 21499 && t.vat == 3705 + 140 && t.gross == 21499 + 3845);
    CHECK(Has(x, "<ram:LineTotalAmount>214.99</ram:LineTotalAmount>") && Has(x, "<ram:GrandTotalAmount>253.44</ram:GrandTotalAmount>") && Has(x, "<ram:TaxTotalAmount currencyID=\"EUR\">38.45</ram:TaxTotalAmount>"));
    CHECK(Has(x, "<ram:BasisAmount>194.99</ram:BasisAmount>") == false);
    CHECK(Has(x, "<ram:BasisAmount>195.00</ram:BasisAmount>") && Has(x, "<ram:CalculatedAmount>37.05</ram:CalculatedAmount>") && Has(x, "<ram:BasisAmount>19.99</ram:BasisAmount>"));
    CHECK(Has(x, "<ram:ChargeAmount>5.00</ram:ChargeAmount>") && Has(x, "<ram:BilledQuantity unitCode=\"C62\">-1</ram:BilledQuantity>"));   // Abzug: Preis positiv, Menge negativ
    CHECK(Has(x, "<ram:BilledQuantity unitCode=\"C62\">2.5</ram:BilledQuantity>") && Has(x, "<ram:IBANID>DE02120300000000202051</ram:IBANID>") && Has(x, "20261125"));

    // Gegenprobe: unser eigener Einleser liest die erzeugten Daten
    docs::EInvoice e;
    CHECK(docs::ParseEInvoice(x, e) && e.valid && e.number == L"RE-2026-001" && e.cents == 25344 && e.issueIso == L"2026-11-11" && e.dueIso == L"2026-11-25" && e.iban == L"DE02120300000000202051");

    // PDF mit eingebettetem XML
    const std::string pdf = invoice::BuildPdf(d, &x);
    CHECK(pdf.compare(0, 8, "%PDF-1.7") == 0 && Has(pdf, "/AFRelationship /Alternative") && Has(pdf, "(factur-x.xml)") && Has(pdf, "/Type /Metadata") && Has(pdf, "EN 16931"));
    const std::string found = docs::FindEmbeddedXml(pdf);
    docs::EInvoice e2;
    CHECK(!found.empty() && docs::ParseEInvoice(found, e2) && e2.cents == 25344);
    // xref-Tabelle: jeder Eintrag zeigt auf "n 0 obj"
    {
        const size_t sx = pdf.rfind("startxref");
        const size_t xr = static_cast<size_t>(std::stoull(pdf.substr(sx + 10)));
        CHECK(pdf.compare(xr, 4, "xref") == 0);
        const int n = std::stoi(pdf.substr(xr + 7));
        bool ok = true;
        size_t p = pdf.find('\n', xr + 5) + 1;   // erster Eintrag (Objekt 0)
        p = pdf.find('\n', p) + 1;
        for (int i = 1; i < n; ++i, p = pdf.find('\n', p) + 1) {
            const size_t off = static_cast<size_t>(std::stoull(pdf.substr(p, 10)));
            ok = ok && pdf.compare(off, std::to_string(i).size() + 6, std::to_string(i) + " 0 obj") == 0;
        }
        CHECK(ok);
    }
    CHECK(invoice::BuildPdf(d).compare(0, 8, "%PDF-1.4") == 0 && !Has(invoice::BuildPdf(d), "factur-x"));   // ohne XML unveraendert

    // Gutschrift / Storno: Typ 381, Bezug auf die Rechnung, Betraege positiv
    invoice::Data g = Sample();
    g.number = L"RE-2026-002"; g.cancels = L"RE-2026-001"; g.cancelsDateIso = L"2026-11-11"; g.dueIso.clear();
    const std::string gx = invoice::BuildEInvoiceXml(g);
    CHECK(Has(gx, "<ram:TypeCode>381</ram:TypeCode>") && !Has(gx, "<ram:TypeCode>380</ram:TypeCode>") && Has(gx, "<ram:IssuerAssignedID>RE-2026-001</ram:IssuerAssignedID>") && Has(gx, "<qdt:DateTimeString format=\"102\">20261111"));
    docs::EInvoice ge;
    CHECK(docs::ParseEInvoice(gx, ge) && ge.credit && ge.cents == -25344);
    const std::string gpdf = invoice::BuildPdf(g, &gx);
    CHECK(Has(gpdf, "GUTSCHRIFT") && Has(gpdf, "Storno zu") && Has(gpdf, "RE-2026-001") && !Has(invoice::BuildPdf(d), "GUTSCHRIFT"));
    // Kleinunternehmer: Kategorie E mit Begruendung, keine Steuer
    invoice::Data k = Sample();
    k.smallBusiness = true; k.taxNo = L"12/345/67890";
    const std::string kx = invoice::BuildEInvoiceXml(k);
    CHECK(Has(kx, "<ram:CategoryCode>E</ram:CategoryCode>") && Has(kx, "<ram:ExemptionReason>Gem\xC3\xA4\xC3\x9F \xC2\xA7 19 UStG") && Has(kx, "schemeID=\"FC\">12/345/67890") && !Has(kx, "<ram:CategoryCode>S</ram:CategoryCode>"));
    CHECK(Has(kx, "<ram:TaxTotalAmount currencyID=\"EUR\">0.00</ram:TaxTotalAmount>") && Has(kx, "<ram:DuePayableAmount>214.99</ram:DuePayableAmount>"));
    // 0-%-Position: Kategorie Z
    invoice::Data z = Sample();
    z.items = { {L"Foerderung", 1, 10000, 0} };
    CHECK(Has(invoice::BuildEInvoiceXml(z), "<ram:CategoryCode>Z</ram:CategoryCode>"));
    // keine Faelligkeit, kein IBAN
    invoice::Data n = Sample(); n.dueIso.clear(); n.iban.clear();
    const std::string nx = invoice::BuildEInvoiceXml(n);
    CHECK(Has(nx, "Zahlbar sofort") && !Has(nx, "IBANID") && !Has(nx, "PaymentMeans"));

    // Pflichtangaben
    auto bad = [&](auto fn) { invoice::Data b = Sample(); fn(b); return !invoice::EInvoiceProblem(b).empty() && invoice::BuildEInvoiceXml(b).empty(); };
    CHECK(bad([](invoice::Data& b) { b.taxNo.clear(); }));
    CHECK(bad([](invoice::Data& b) { b.issuerAddress = L"Nur eine Zeile"; }));
    CHECK(bad([](invoice::Data& b) { b.recipientAddress = L"Weg 1|Berlin"; }));            // keine PLZ
    CHECK(bad([](invoice::Data& b) { b.recipientAddress = L"Weg 1|1234 Berlin"; }));       // PLZ zu kurz
    CHECK(bad([](invoice::Data& b) { b.number.clear(); }));
    CHECK(bad([](invoice::Data& b) { b.items.clear(); }));
    CHECK(bad([](invoice::Data& b) { b.items[0].desc = L"  "; }));

    if (argc > 1) {   // Dateien fuer die Gegenprobe mit externen Pruefern (tools/CheckFacturX.py) ablegen
        const std::string dir = argv[1];
        invoice::Data kk = Sample(); kk.smallBusiness = true; kk.taxNo = L"12/345/67890";
        const std::string kxml = invoice::BuildEInvoiceXml(kk);
        struct { const char* name; const std::string* data; } files[] = {{"rechnung.xml", &x}, {"rechnung.pdf", &pdf}, {"klein.xml", &kxml}, {"gutschrift.xml", &gx}};
        for (auto& f : files) { FILE* o = std::fopen((dir + "/" + f.name).c_str(), "wb"); if (o) { std::fwrite(f.data->data(), 1, f.data->size(), o); std::fclose(o); } }
    }
    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
