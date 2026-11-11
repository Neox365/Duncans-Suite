// Tests: Reparatur typischer Texterkennungs-Fehler (CleanOcrText) und die Betrags-/Faelligkeitssuche auf echtem OCR-Text.
//   g++ -std=c++17 -DUNICODE -D_UNICODE -I.. ScanTextTest.cpp -o ScanTextTest.exe
#include <cstdio>
#include <string>

#include "../InvoiceText.h"

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

int main()
{
    using docs::CleanOcrText;
    // Buchstabe o/O in Zahlenwoertern
    CHECK(CleanOcrText(L"1.200,oo €") == L"1.200,00 €");
    CHECK(CleanOcrText(L"Summe 99,O5") == L"Summe 99,05");
    CHECK(CleanOcrText(L"oo") == L"oo");                                   // ohne Ziffer bleibt es ein Wort
    CHECK(CleanOcrText(L"Online Betrag Ostern") == L"Online Betrag Ostern");
    CHECK(CleanOcrText(L"BYLADEMIOOI") == L"BYLADEMIOOI");                 // Buchstaben dabei: keine Zahl, bleibt
    CHECK(CleanOcrText(L"Müller-Straße 12") == L"Müller-Straße 12");
    // Verschluckter Punkt im Datum
    CHECK(CleanOcrText(L"Fällig am 25.112026") == L"Fällig am 25.11.2026");
    CHECK(CleanOcrText(L"25.11.2026 1.234,56") == L"25.11.2026 1.234,56");   // richtige Daten und Betraege bleiben
    CHECK(CleanOcrText(L"12345.1234") == L"12345.1234");
    CHECK(CleanOcrText(L"") == L"");
    CHECK(CleanOcrText(L"a\n25.112026\nb") == L"a\n25.11.2026\nb");           // Zeilenumbrueche bleiben

    // Echter OCR-Text einer eingescannten Rechnung (Windows-Texterkennung, Deutsch); Spalten kommen getrennt, Zahlen teils falsch gelesen
    const std::wstring ocr =
        L"Mustermann Webdesign\nMusterstraße 12\n12345 Musterstadt\nBeispiel GmbH\nRECHNUNG\nRechnungsnr.\nDatum\nFällig am\nPos.\n1\n2\n"
        L"Beschreibung\nWebdesign: Gestaltung und Umsetzung der\nFirmenwebsite\nHosting und Wartung (Monate)\nSupport (Stunden)\nMenge\n12\n2\n"
        L"Einzelpreis\n1.200,oo €\n9,90 €\n45,00 €\nUSt\n19%\n7%\nBEISPIEL\n11.11.2026\n25.112026\nGesamt\n1.200,oo €\n118,80€\n90,00 €\n1.408,80 €\n8,32 €\n245,10 €\n1.662,22 €\n"
        L"Zwischensumme (netto)\nUmsatzsteuer 7 % auf 118,80 €\nUmsatzsteuer 19 % auf 1.290,00 €\nGesamtbetrag\n"
        L"Bitte überweisen Sie den Rechnungsbetrag von 1.662,22 € bis zum 25.11.2026 auf\ndas unten genannte Konto. Soweit nicht anders angegeben, entspricht das\n"
        L"Rechnungsdatum dem Leistungsdatum. Vielen Dank für Ihren Auftrag!\nGiroCode\nIBAN: DE02 1203 0000 0000 2020 51\nBIC: BYLADEMIOOI\nSeite 1 von 1";
    const docs::ScannedInvoice a = docs::ScanInvoiceText(docs::CleanOcrText(ocr));
    CHECK(a.hasAmount && a.cents == 166222);
    CHECK(a.hasDue && a.dueIso == L"2026-11-25");
    // Ohne Reparatur wuerde nur die Faelligkeit aus dem Satz "bis zum" kommen (Lookback 90 Zeichen)
    const docs::ScannedInvoice b = docs::ScanInvoiceText(ocr);
    CHECK(b.hasAmount && b.cents == 166222 && b.hasDue && b.dueIso == L"2026-11-25");

    // Der erweiterte Rueckblick darf keine fremden "bis zum" erfassen
    const docs::ScannedInvoice c = docs::ScanInvoiceText(L"Gesamtbetrag 10,00 EUR\nDas Angebot gilt bis zum 01.12.2026 und danach nicht mehr.");
    CHECK(c.hasAmount && !c.hasDue);
    const docs::ScannedInvoice d = docs::ScanInvoiceText(L"Gesamtbetrag 10,00 EUR. Bitte zahlen Sie bis zum 05.12.2026.");
    CHECK(d.hasDue && d.dueIso == L"2026-12-05");

    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
