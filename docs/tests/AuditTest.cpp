// Test von SHA-256, Aenderungsprotokoll (Hash-Kette), Index-Feldern fuer gesperrte Rechnungen und Rechnungsdaten-Datei (Storno).
// Baut mit MinGW:  g++ -std=c++17 -I.. AuditTest.cpp -o AuditTest.exe
#include "../AuditLog.h"
#include "../Invoice.h"

#include <cstdio>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

int main()
{
    // SHA-256 Pruefwerte (FIPS 180-4)
    CHECK(sha256::Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256::Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256::Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(sha256::Hex(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    // Hash-Kette
    std::string log, prev;
    const wchar_t* acts[] = {L"erstellt", L"Status", L"Storno"};
    for (int i = 0; i < 3; ++i) {
        const std::string l = docs::LogLine(L"2026-11-11 10:0" + std::to_wstring(i), acts[i], L"Rechnung_RE-2026-00" + std::to_wstring(i + 1) + L".pdf", L"Müller\tTab", prev);
        log += l + "\n";
        prev = l;
    }
    CHECK(docs::LogVerify(log) == 0 && docs::LogSplit(log).size() == 3);
    CHECK(log.find("M\xC3\xBCller Tab") != std::string::npos);                       // Tabulator im Text bricht das Format nicht
    std::string t1 = log; t1.replace(t1.find("Status"), 6, "Statuz");
    CHECK(docs::LogVerify(t1) == 3);                                                  // Aenderung in Zeile 2 faellt in Zeile 3 auf (deren Kettenwert passt nicht mehr)
    std::vector<std::string> ls = docs::LogSplit(log);
    CHECK(docs::LogVerify(ls[0] + "\n" + ls[2] + "\n") == 2);                          // Zeile geloescht
    CHECK(docs::LogVerify(ls[1] + "\n" + ls[2] + "\n") == 1);                          // erste Zeile geloescht
    CHECK(docs::LogVerify(log + ls[0] + "\n") == 4);                                   // Zeile angehaengt, die nicht zur Kette passt
    CHECK(docs::LogVerify("") == 0);
    std::string t2 = ls[0] + "\n" + ls[1] + "\n";                                      // letzte Zeile entfernen merkt die Kette nicht (bekannte Grenze)
    CHECK(docs::LogVerify(t2) == 0);

    // Index: gesperrt, Hash, Bezug
    docs::MetaMap map;
    docs::Meta m;
    m.cents = 12345; m.locked = true; m.hash = sha256::HexW("x"); m.link = L"Storniert durch RE-2026-002"; m.income = true;
    map[L"2026/Firma/a.pdf"] = m;
    docs::Meta o; o.cents = 100;
    map[L"2026/Firma/b.pdf"] = o;
    const docs::MetaMap back = docs::ParseIndex(docs::SerializeIndex(map));
    CHECK(back.size() == 2 && back.at(L"2026/Firma/a.pdf").locked && back.at(L"2026/Firma/a.pdf").hash == sha256::HexW("x") && back.at(L"2026/Firma/a.pdf").link == m.link);
    CHECK(!back.at(L"2026/Firma/b.pdf").locked && back.at(L"2026/Firma/b.pdf").hash.empty() && back.at(L"2026/Firma/b.pdf").link.empty());
    // alter Index ohne die neuen Spalten
    const docs::MetaMap old = docs::ParseIndex("#OggysDocsFiles-Index 1\n2026/X/c.pdf\t500\t\tOffen\ttag\tnotiz\t\tEinnahme\n");
    CHECK(old.size() == 1 && old.begin()->second.cents == 500 && !old.begin()->second.locked && old.begin()->second.income);

    // Rechnungsdaten
    invoice::Data d;
    d.issuer = L"Muster GmbH"; d.issuerAddress = L"Weg 1|10115 Berlin"; d.recipient = L"Kunde"; d.recipientAddress = L"Str 2|80331 München";
    d.number = L"RE-2026-007"; d.dateIso = L"2026-11-11"; d.dueIso = L"2026-11-25"; d.note = L"Danke\nSchön"; d.taxNo = L"DE123456789"; d.iban = L"DE02120300000000202051";
    d.items = { {L"Beratung", 2.5, 8000, -1}, {L"Buch", 1, 1999, 7}, {L"Rabatt", 1, -500, 0} };
    invoice::Data e;
    CHECK(invoice::ParseData(invoice::SerializeData(d), e));
    CHECK(e.issuer == d.issuer && e.recipientAddress == d.recipientAddress && e.number == d.number && e.dueIso == d.dueIso && e.taxNo == d.taxNo && e.iban == d.iban);
    CHECK(e.items.size() == 3 && e.items[0].qty == 2.5 && e.items[0].unitCents == 8000 && e.items[0].vatPercent == -1 && e.items[2].unitCents == -500 && e.items[2].vatPercent == 0);
    CHECK(e.note == L"Danke Schön");                                                     // Zeilenumbruch wird zu Leerzeichen
    CHECK(invoice::Compute(e).gross == invoice::Compute(d).gross);
    invoice::Data bad;
    CHECK(!invoice::ParseData("", bad) && !invoice::ParseData("#Rechnungsdaten 1\nissuer\tX\n", bad) && !invoice::ParseData("item\tA\tx\ty\tz\n", bad));

    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
