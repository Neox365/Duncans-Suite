// Test der Kundenliste (docs/Customers.h).  Baut mit MinGW:  g++ -std=c++17 -I.. CustomersTest.cpp -o CustomersTest.exe
#include "../Customers.h"

#include <cstdio>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

int main()
{
    std::vector<docs::Customer> v;
    CHECK(docs::UpsertCustomer(v, L"Müller GmbH", L"Hauptstraße 1 | 10115 Berlin"));
    CHECK(docs::UpsertCustomer(v, L"alpha AG", L"Weg 2 | 80331 München"));
    CHECK(docs::UpsertCustomer(v, L"Zeta KG", L""));                                        // ohne Adresse ist auch ok
    CHECK(v.size() == 3 && v[0].name == L"alpha AG" && v[1].name == L"Müller GmbH" && v[2].name == L"Zeta KG");   // sortiert ohne Beachtung der Schreibweise
    CHECK(!docs::UpsertCustomer(v, L"  MÜLLER gmbh ", L"Hauptstraße 1 | 10115 Berlin"));     // gleicher Kunde, gleiche Adresse: nichts zu tun
    CHECK(v.size() == 3);
    CHECK(docs::UpsertCustomer(v, L"müller GmbH", L"Neuweg 5 | 10117 Berlin") && v.size() == 3 && v[1].address == L"Neuweg 5 | 10117 Berlin" && v[1].name == L"Müller GmbH");   // Adresse aktualisiert, Name bleibt
    CHECK(!docs::UpsertCustomer(v, L"Müller GmbH", L"") && v[1].address == L"Neuweg 5 | 10117 Berlin");   // leere Adresse ueberschreibt nichts
    CHECK(!docs::UpsertCustomer(v, L"   ", L"x") && v.size() == 3);
    CHECK(docs::UpsertCustomer(v, L"Tab\tName\nX", L"A\tB") && v.back().name == L"Zeta KG" && v[2].name == L"Tab Name X" && v[2].address == L"A B");

    const std::vector<docs::Customer> back = docs::ParseCustomers(docs::SerializeCustomers(v));
    CHECK(back.size() == v.size() && back[0].name == L"alpha AG" && back[1].address == L"Neuweg 5 | 10117 Berlin" && back[3].name == L"Zeta KG" && back[3].address.empty());
    CHECK(docs::ParseCustomers("").empty() && docs::ParseCustomers("#Kopf\n\n").empty());
    const auto dup = docs::ParseCustomers("\xEF\xBB\xBF" "A\tadr1\na\t\nB\tx\n");                 // Dubletten zusammengefuehrt, Adresse bleibt
    CHECK(dup.size() == 2 && dup[0].name == L"A" && dup[0].address == L"adr1");
    CHECK(docs::ParseCustomers("Nur Name\n").size() == 1);

    CHECK(docs::RemoveCustomer(v, L"ALPHA ag") && v.size() == 3 && !docs::RemoveCustomer(v, L"gibt es nicht"));
    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
