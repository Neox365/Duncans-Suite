// Tests fuer docs/Store.h, TextEdit.h, PdfWriter.h und Invoice.h (laufen auch unter Linux):
//   g++ -std=c++17 -I.. DocsTest.cpp -o t && ./t
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "../Invoice.h"
#include "../Store.h"
#include "../TextEdit.h"

namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static void amounts() {
    using namespace docs;
    bool ok = false;
    CHECK(ParseAmountCents(L"1.234,56", ok) == 123456 && ok);
    CHECK(ParseAmountCents(L"1234.56", ok) == 123456 && ok);
    CHECK(ParseAmountCents(L"12,5 €", ok) == 1250 && ok);
    CHECK(ParseAmountCents(L"99", ok) == 9900 && ok);
    CHECK(ParseAmountCents(L"1.234", ok) == 123400 && ok);       // Tausenderpunkt
    CHECK(ParseAmountCents(L"0,05", ok) == 5 && ok);
    CHECK(ParseAmountCents(L"-20,00 EUR", ok) == -2000 && ok);
    CHECK(ParseAmountCents(L"", ok) == 0 && ok);                // leer = 0 (Aufrufer entscheidet)
    ParseAmountCents(L"abc", ok); CHECK(!ok);
    ParseAmountCents(L"1,234,5", ok); CHECK(!ok);
    ParseAmountCents(L"1,555", ok); CHECK(!ok);                  // drei Nachkommastellen
    CHECK(FormatAmount(123456) == L"1.234,56 €");
    CHECK(FormatAmount(5, false) == L"0,05");
    CHECK(FormatAmount(-250000) == L"-2.500,00 €");
    CHECK(FormatAmount(kUnknown) == L"–");
}

static void dates() {
    using namespace docs;
    std::wstring iso;
    CHECK(ParseDate(L"24.12.2026", iso) && iso == L"2026-12-24");
    CHECK(ParseDate(L"1.2.26", iso) && iso == L"2026-02-01");
    CHECK(ParseDate(L"2026-12-24", iso) && iso == L"2026-12-24");
    CHECK(ParseDate(L"", iso) && iso.empty());
    CHECK(!ParseDate(L"31.02.2026", iso));
    CHECK(!ParseDate(L"Montag", iso));
    CHECK(!ParseDate(L"12.2026", iso));
    CHECK(FormatDate(L"2026-12-24") == L"24.12.2026" && FormatDate(L"").empty());
    CHECK(AddDaysIso(L"2026-12-24", 14) == L"2027-01-07");
    CHECK(AddDaysIso(L"2028-02-28", 1) == L"2028-02-29");       // Schaltjahr
    long long a = 0, b = 0;
    CHECK(IsoToDays(L"2026-10-20", a) && IsoToDays(L"2026-10-21", b) && b == a + 1);
}

static void names() {
    using namespace docs;
    CHECK(SanitizeName(L"Netz/anbieter: AG?", L"X") == L"Netz anbieter AG");
    CHECK(SanitizeName(L"  ...  ", L"Sonstige") == L"Sonstige");
    CHECK(SanitizeName(L"CON", L"X") == L"CON_");
    CHECK(SanitizeName(std::wstring(200, L'a'), L"X").size() == 60);
    CHECK(SanitizeFileName(std::wstring(200, L'a') + L".pdf", L"X").size() == 64);   // Endung bleibt
    CHECK(SanitizeFileName(L"a:b.pdf", L"X") == L"a b.pdf");
}

static void indexAndScan() {
    using namespace docs;
    const fs::path root = fs::temp_directory_path() / ("oggysdocs_" + std::to_string(std::rand()));
    fs::create_directories(root / "2026" / "Netzanbieter");
    fs::create_directories(root / "2026" / "Versicherung");
    fs::create_directories(root / ".versteckt");
    auto touch = [](const fs::path& p) { std::ofstream(p, std::ios::binary) << "x"; };
    touch(root / "2026" / "Netzanbieter" / "Strom Oktober.pdf");
    touch(root / "2026" / "Netzanbieter" / "notiz.txt");               // kein Beleg
    touch(root / "2026" / "Versicherung" / "Haftpflicht.PDF");
    touch(root / "2026" / "lose.jpg");                                // nur Jahr
    touch(root / "ohne.png");                                         // keine Ebene
    touch(root / ".versteckt" / "geheim.pdf");                        // versteckt

    MetaMap meta;
    Meta m;
    m.cents = 8950; m.due = L"2026-11-05"; m.status = Status::Paid; m.tags = L"strom abschlag"; m.note = L"Tab\there\nneu";
    meta[L"2026\\Netzanbieter\\Strom Oktober.pdf"] = m;
    const std::string text = SerializeIndex(meta);
    const MetaMap back = ParseIndex(text);
    CHECK(back.size() == 1);
    const Meta& b = back.at(L"2026\\Netzanbieter\\Strom Oktober.pdf");
    CHECK(b.cents == 8950 && b.due == L"2026-11-05" && b.status == Status::Paid && b.tags == L"strom abschlag" && b.note == L"Tab here neu");
    CHECK(ParseIndex("garbage ohne tabs\n\xFF\xFE\n").empty());

    auto docsFound = Scan(root, back);
    CHECK(docsFound.size() == 4);
    int known = 0;
    for (const auto& d : docsFound) {
        if (d.name == L"Strom Oktober.pdf") {
            CHECK(d.year == L"2026" && d.creditor == L"Netzanbieter" && d.known && d.m.cents == 8950);
            ++known;
        }
        if (d.name == L"lose.jpg") CHECK(d.year == L"2026" && d.creditor == L"Sonstige" && !d.known);
        if (d.name == L"ohne.png") CHECK(d.year.empty() && d.creditor == L"Sonstige");
        CHECK(d.name != L"geheim.pdf" && d.name != L"notiz.txt");
    }
    CHECK(known == 1);

    // Suche
    for (const auto& d : docsFound)
        if (d.name == L"Strom Oktober.pdf") {
            CHECK(Matches(d, L"strom"));
            CHECK(Matches(d, L"NETZ strom"));          // jedes Wort, beliebige Gross-/Kleinschreibung
            CHECK(Matches(d, L"abschlag"));            // Schlagwort
            CHECK(Matches(d, L"bezahlt"));             // Status
            CHECK(Matches(d, L"89,50"));               // Betrag
            CHECK(Matches(d, L"05.11.2026"));          // Faelligkeit
            CHECK(!Matches(d, L"gas"));
            CHECK(!Matches(d, L"strom gas"));
            CHECK(Matches(d, L"   "));
        }

    // Zielpfad: kollisionsfrei
    const fs::path t1 = TargetPath(root, L"2026", L"Netzanbieter", L"Strom Oktober.pdf");
    CHECK(t1.filename().wstring() == L"Strom Oktober (2).pdf");
    const fs::path t2 = TargetPath(root, L"2027", L"Neu/Firma", L"x.pdf");
    CHECK(t2.wstring().find(L"Neu Firma") != std::wstring::npos);
    fs::remove_all(root);
}

static void textEdit() {
    using namespace docs;
    TextEdit e;
    e.Insert(L"Hallo");
    CHECK(e.text == L"Hallo" && e.caret == 5);
    e.Move(-1, false, false); e.Backspace();
    CHECK(e.text == L"Halo" && e.caret == 3);
    e.Home(true);
    CHECK(e.HasSelection() && e.Selected() == L"Hal");
    e.Insert(L"X");                                      // ersetzt die Markierung
    CHECK(e.text == L"Xo" && e.caret == 1);
    e.SelectAll(); e.Insert(L"a\tb\x01" L"c");
    CHECK(e.text == L"a bc");
    e.maxLen = 5; e.End(false); e.Insert(L"123456");
    CHECK(e.text == L"a bc1");
    e.maxLen = 120;
    e.SetText(L"eins zwei drei");
    e.Move(-1, false, true);
    CHECK(e.caret == 10);                                // Anfang von "drei"
    e.Move(-1, false, true);
    CHECK(e.caret == 5);
    e.Delete(); CHECK(e.text == L"eins wei drei");
    e.SetCaret(0, false); e.Move(1, true, true);
    CHECK(e.Selected() == L"eins ");
    e.Move(1, false, false);                             // ohne Umschalt: Markierung zusammenziehen
    CHECK(!e.HasSelection() && e.caret == 5);
}

static std::string readAll(const fs::path& p) { std::ifstream f(p, std::ios::binary); return std::string(std::istreambuf_iterator<char>(f), {}); }

static void pdf() {
    using namespace pdfw;
    CHECK(ToWinAnsi(L"äöüß € „") == std::string("\xE4\xF6\xFC\xDF \x80 \x84"));
    CHECK(ToWinAnsi(L"中") == "?");
    CHECK(Document::Width(L"0123456789", 10) == 55.6);

    invoice::Data d;
    d.issuer = L"Müller & Söhne (GmbH)";
    d.issuerAddress = L"Hauptstr. 1|12345 Musterstadt";
    d.recipient = L"Erika Mustermann";
    d.recipientAddress = L"Weg 2|54321 Anderswo";
    d.number = L"2026-001"; d.dateIso = L"2026-10-20"; d.dueIso = L"2026-11-03";
    d.note = L"IBAN DE00 0000 0000 0000 0000 00 – vielen Dank für Ihren Auftrag!";
    d.vatPercent = 19;
    d.items = {{L"Beratung", 2.5, 8000}, {L"Fahrtkosten (pauschal)", 1, 4550}};
    const auto t = invoice::Compute(d);
    CHECK(t.net == 24550 && t.vat == 4665 && t.gross == 29215 && t.lines.size() == 2 && t.lines[0] == 20000);

    const std::string out = invoice::BuildPdf(d);
    CHECK(out.compare(0, 8, "%PDF-1.4") == 0);
    CHECK(out.size() > 600 && out.substr(out.size() - 6) == "%%EOF\n");
    // xref: jeder Eintrag zeigt exakt auf "<n> 0 obj"
    const size_t sx = out.rfind("startxref\n");
    const size_t xrefPos = std::stoul(out.substr(sx + 10));
    CHECK(out.compare(xrefPos, 4, "xref") == 0);
    size_t p = out.find('\n', xrefPos) + 1;                          // "0 N"
    const int count = std::stoi(out.substr(p + 2));
    p = out.find('\n', p) + 1;
    p = out.find('\n', p) + 1;                                       // Eintrag 0 (frei) ueberspringen
    for (int i = 1; i < count; ++i) {
        const size_t off = std::stoul(out.substr(p, 10));
        const std::string want = std::to_string(i) + " 0 obj\n";
        CHECK(out.compare(off, want.size(), want) == 0);
        p = out.find('\n', p) + 1;
    }
    CHECK(out.find("/Type /Pages /Count 1") != std::string::npos);
    CHECK(out.find("RECHNUNG") != std::string::npos);
    CHECK(out.find("Seite 1 von 1") != std::string::npos);
    CHECK(out.find("\\374") != std::string::npos);                   // u-Umlaut als Oktal-Escape im Text
    CHECK(out.find("(Hauptstr. 1)") != std::string::npos);

    // Klammern im Text werden maskiert
    Document doc;
    doc.Text(10, 10, 10, false, L"a(b)c\\d");
    CHECK(doc.Build(L"t").find("(a\\(b\\)c\\\\d)") != std::string::npos);

    // viele Positionen -> mehrere Seiten, Seitenzahl in der Fusszeile
    invoice::Data big = d;
    big.items.clear();
    for (int i = 0; i < 60; ++i) big.items.push_back({L"Position " + std::to_wstring(i + 1) + L" mit langer Beschreibung, die umgebrochen werden muss, weil sie breit ist", 1, 1000});
    const std::string bigOut = invoice::BuildPdf(big);
    CHECK(bigOut.find("/Count 1") == std::string::npos && bigOut.find("Seite 2 von") != std::string::npos);

    // Wrap
    const auto lines = invoice::Wrap(L"aaaa bbbb cccc dddd", Document::Width(L"aaaa bbbb", 10) + 1, 10);
    CHECK(lines.size() == 2 && lines[0] == L"aaaa bbbb" && lines[1] == L"cccc dddd");
    CHECK(invoice::Wrap(std::wstring(300, L'w'), 100, 10).size() > 3);          // langes Wort wird getrennt
    double q = 0;
    CHECK(invoice::ParseQuantity(L"1,5", q) && q == 1.5);
    CHECK(!invoice::ParseQuantity(L"1,5,2", q) && !invoice::ParseQuantity(L"x", q));
    CHECK(invoice::FormatQuantity(2) == L"2" && invoice::FormatQuantity(1.5) == L"1,5");

    // Datei schreiben (kommt auf der Platte unveraendert an)
    const fs::path f = fs::temp_directory_path() / "oggys_test.pdf";
    { std::ofstream o(f, std::ios::binary); o.write(out.data(), static_cast<std::streamsize>(out.size())); }
    CHECK(readAll(f) == out);
    fs::remove(f);
}

int main() {
    amounts(); dates(); names(); indexAndScan(); textEdit(); pdf();
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures ? 1 : 0;
}
