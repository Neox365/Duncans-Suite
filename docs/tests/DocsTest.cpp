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

    // Ordnerstruktur: auch leere Ordner, keine versteckten
    {
        fs::create_directories(root / "2026" / "Leer");
        fs::create_directories(root / "2024");
        const auto folders = ListFolders(root);
        auto has = [&](const wchar_t* y, const wchar_t* c) {
            return std::find(folders.begin(), folders.end(), std::make_pair(std::wstring(y), std::wstring(c))) != folders.end();
        };
        CHECK(has(L"2026", L"") && has(L"2026", L"Netzanbieter") && has(L"2026", L"Leer") && has(L"2024", L""));
        CHECK(!has(L".versteckt", L"") && folders.size() == 5);   // 2026 + 3 Glaeubiger, 2024
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

static void dateMask() {
    using namespace docs;
    TextEdit e;
    size_t prev = 0;
    auto type = [&](const wchar_t* s) {
        for (; *s; ++s) { e.Insert(std::wstring(1, *s)); MaskDate(e, prev); prev = e.text.size(); }
    };
    auto back = [&]() { e.Backspace(); MaskDate(e, prev); prev = e.text.size(); };
    type(L"01");   CHECK(e.text == L"01." && e.caret == 3);
    type(L"1");    CHECK(e.text == L"01.1");
    type(L"2");    CHECK(e.text == L"01.12.");
    type(L"2026"); CHECK(e.text == L"01.12.2026" && e.caret == 10);
    type(L"9");    CHECK(e.text == L"01.12.2026");                       // maximal acht Ziffern
    back();        CHECK(e.text == L"01.12.202");
    back(); back(); back(); CHECK(e.text == L"01.12");                    // Punkt entfernt sich ohne erneuten Schlusspunkt
    back();        CHECK(e.text == L"01.1");
    back();        CHECK(e.text == L"01");
    // selbst getippte Punkte bleiben unangetastet
    e.SetText(L""); prev = 0;
    type(L"1.12.2026"); CHECK(e.text == L"1.12.2026");
    // Ersetzen einer Markierung (alles markiert, neu tippen)
    e.SetText(L"20.10.2026"); prev = e.text.size(); e.SelectAll();
    type(L"05"); CHECK(e.text == L"05.");
    // Zwei-Ziffern-Jahr
    type(L"1226"); CHECK(e.text == L"05.12.26");
    std::wstring iso;
    CHECK(ParseDate(e.text, iso) && iso == L"2026-12-05");
    // Fremdzeichen: keine Maske
    e.SetText(L""); prev = 0; type(L"ab"); CHECK(e.text == L"ab");
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

static void invoiceNumbers() {
    CHECK(docs::FormatInvoiceNo(2026, 7) == L"RE-2026-007");
    CHECK(docs::FormatInvoiceNo(2026, 1234) == L"RE-2026-1234");
    int y = 0, n = 0;
    CHECK(docs::ParseInvoiceNo(L"RE-2026-007", y, n) && y == 2026 && n == 7);
    CHECK(docs::ParseInvoiceNo(L"2025-012", y, n) && y == 2025 && n == 12);   // aelteres Format ohne Praefix
    CHECK(!docs::ParseInvoiceNo(L"RE-2026-", y, n) && !docs::ParseInvoiceNo(L"RE-26-001", y, n) && !docs::ParseInvoiceNo(L"ABC", y, n));
    CHECK(!docs::ParseInvoiceNo(L"RE-2026-0x1", y, n) && !docs::ParseInvoiceNo(L"", y, n));

    const std::vector<std::wstring> files = {L"Rechnung_RE-2026-001_Mueller.pdf", L"Rechnung_RE-2026-004_Meier GmbH.pdf",
                                             L"Rechnung_2026-002_Alt.pdf", L"Rechnung_RE-2025-099_X.pdf", L"Strom.pdf"};
    CHECK(docs::MaxInvoiceNo(files, 2026) == 4);
    CHECK(docs::MaxInvoiceNo(files, 2025) == 99);
    CHECK(docs::MaxInvoiceNo(files, 2027) == 0);
    CHECK(docs::NextInvoiceNo(0, files, 2026) == 5);
    CHECK(docs::NextInvoiceNo(9, files, 2026) == 10);   // Zaehler gewinnt, wenn Rechnungen geloescht wurden (keine Wiederverwendung)
    CHECK(docs::NextInvoiceNo(0, {}, 2026) == 1);
    CHECK(docs::InvoiceNoTaken(files, L"RE-2026-004") && !docs::InvoiceNoTaken(files, L"RE-2026-005") && !docs::InvoiceNoTaken(files, L"RE-2026-00"));
}

static void installmentRate() {
    docs::MetaMap map;
    docs::Meta m;
    m.cents = 5000;
    m.status = docs::Status::Installment;
    m.rate = L"Rate 3 von 12";
    map[L"2026\\Bank\\Kredit.pdf"] = m;
    const docs::MetaMap back = docs::ParseIndex(docs::SerializeIndex(map));
    CHECK(back.size() == 1 && back.begin()->second.rate == L"Rate 3 von 12" && back.begin()->second.status == docs::Status::Installment);
    // aelterer Index ohne 7. Spalte bleibt lesbar
    const docs::MetaMap old = docs::ParseIndex("a.pdf\t100\t2026-01-02\tRatenzahlung\ttag\tnotiz\n");
    CHECK(old.size() == 1 && old.begin()->second.rate.empty() && old.begin()->second.note == L"notiz");
    docs::Doc d;
    d.name = L"Kredit.pdf";
    d.m = m;
    CHECK(docs::Matches(d, L"rate 3"));
}

static void closingAndFooter() {
    // Platzhalter im Schlusstext
    const std::wstring t = invoice::ExpandNote(invoice::DefaultNote(), 11900, L"2026-11-04");
    CHECK(t.find(L"119,00") != std::wstring::npos && t.find(L"04.11.2026") != std::wstring::npos);
    CHECK(t.find(L"[") == std::wstring::npos);
    const std::wstring noDue = invoice::ExpandNote(L"Zahlbar bis zum [Fälligkeitsdatum].", 100, L"");
    CHECK(noDue.find(L"bis zum") == std::wstring::npos && noDue.find(L"[") == std::wstring::npos);
    CHECK(invoice::ReplaceAll(L"aXbXc", L"X", L"--") == L"a--b--c" && invoice::ReplaceAll(L"abc", L"x", L"y") == L"abc");

    invoice::Data d;
    d.issuer = L"Mustermann Webdesign";
    d.recipient = L"Beispiel GmbH";
    d.number = L"BEISPIEL";
    d.dateIso = L"2026-10-21";
    d.dueIso = L"2026-11-04";
    for (int i = 0; i < 3; ++i) { invoice::Item it; it.desc = L"Leistung"; it.qty = 1; it.unitCents = 10000; d.items.push_back(it); }
    const std::string plain = invoice::BuildPdf(d);
    d.owner = L"Inhaber: Max Mustermann"; d.court = L"Amtsgericht Musterstadt, HRB 12345"; d.taxNo = L"DE123456789";
    d.phone = L"+49 123 456"; d.email = L"a@b.de"; d.web = L"www.b.de";
    d.bank = L"Musterbank"; d.iban = L"DE02 1203 0000 0000 2020 51"; d.bic = L"BYLADEM1001";
    const std::string full = invoice::BuildPdf(d);
    CHECK(full.size() > plain.size() && full.find("IBAN") != std::string::npos && plain.find("IBAN") == std::string::npos);
    CHECK(full.find("USt-IdNr.") != std::string::npos && full.find("E-Mail") != std::string::npos && full.find("Tel.") != std::string::npos);
    // viele Positionen: Fusszeile steht auf jeder Seite
    for (int i = 0; i < 30; ++i) { invoice::Item it; it.desc = L"Position"; it.qty = 1; it.unitCents = 100; d.items.push_back(it); }
    const std::string multi = invoice::BuildPdf(d);
    size_t ibans = 0;
    for (size_t p = 0; (p = multi.find("(IBAN", p)) != std::string::npos; ++p) ++ibans;
    CHECK(ibans >= 2);
}

static void smallBusiness() {
    invoice::Data d;
    d.issuer = L"Max Muster";
    d.recipient = L"Kunde";
    d.number = L"RE-2026-001";
    d.dateIso = L"2026-10-21";
    invoice::Item it;
    it.qty = 2;
    it.unitCents = 5000;
    d.items.push_back(it);
    d.vatPercent = 19;
    CHECK(invoice::Compute(d).vat == 1900 && invoice::Compute(d).gross == 11900);
    d.smallBusiness = true;   // Kleinunternehmer: keine USt, Brutto = Netto
    const invoice::Totals t = invoice::Compute(d);
    CHECK(t.vat == 0 && t.gross == 10000 && t.net == 10000);
    CHECK(std::wstring(invoice::TaxNoLabel(L"DE123456789")) == L"USt-IdNr." && std::wstring(invoice::TaxNoLabel(L"12/345/67890")) == L"Steuernr.");
    const std::string normal = [&] { invoice::Data n = d; n.smallBusiness = false; return invoice::BuildPdf(n); }();
    d.taxNo = L"12/345/67890";
    const std::string small = invoice::BuildPdf(d);
    CHECK(small.find("%PDF-") == 0 && small != normal);
    CHECK(normal.find("Umsatzsteuer") != std::string::npos);
    CHECK(small.find("Umsatzsteuer 19") == std::string::npos);
}

static void tagsStatsImport() {
    using namespace docs;
    // Tags: trennen, Dopplungen, Entfernen
    const auto t = SplitTags(L" Steuer, büro ;Software,, steuer ,");
    CHECK(t.size() == 3 && t[0] == L"Steuer" && t[1] == L"büro" && t[2] == L"Software");
    CHECK(EditTags(L"Steuer, Büro", L"büro, Software", L"STEUER") == L"Büro, Software");
    CHECK(EditTags(L"", L"a", L"") == L"a" && EditTags(L"a", L"", L"a").empty());
    Doc a, b, c;
    a.m.tags = L"Steuer, Büro"; b.m.tags = L"büro"; c.m.tags = L"";
    const auto all = std::vector<Doc>{a, b, c};
    const auto list = CollectTags(all);
    CHECK(list.size() == 2 && list[0].first == L"Büro" && list[0].second == 2 && list[1].first == L"Steuer");
    CHECK(MatchesTags(a, {L"büro", L"steuer"}) && !MatchesTags(b, {L"büro", L"steuer"}) && MatchesTags(c, {}));
    Meta m; m.tags = L"Steuer";
    CHECK(HasTag(m, L" steuer ") && !HasTag(m, L"büro"));

    // Index: Einnahme/Ausgabe, alte Dateien ohne Spalte gelten als Ausgabe
    MetaMap map;
    Meta in; in.cents = 100; in.income = true; map[L"2026\\X\\a.pdf"] = in;
    Meta out; out.cents = 50; map[L"2026\\X\\b.pdf"] = out;
    const MetaMap back = ParseIndex(SerializeIndex(map));
    CHECK(back.at(L"2026\\X\\a.pdf").income && !back.at(L"2026\\X\\b.pdf").income);

    // Finanz-Uebersicht
    std::vector<Doc> docsv(5);
    docsv[0].year = L"2026"; docsv[0].m.cents = 100000; docsv[0].m.income = true; docsv[0].m.status = Status::Paid;
    docsv[1].year = L"2026"; docsv[1].m.cents = 30000; docsv[1].m.status = Status::Open; docsv[1].m.due = L"2026-01-01";
    docsv[2].year = L"2026"; docsv[2].m.cents = 20000; docsv[2].m.status = Status::Paid;
    docsv[3].year = L"2025"; docsv[3].m.cents = 7000; docsv[3].m.status = Status::Open;      // altes Jahr: nur bei "offen"
    docsv[4].year = L"2026";                                                                  // ohne Betrag: ignoriert
    const Stats s = ComputeStats(docsv, L"2026", L"2026-06-01");
    CHECK(s.income == 100000 && s.expense == 50000 && s.open == 37000 && s.openCount == 2 && s.overdue == 1);

    // Import: Jahr und Glaeubiger aus dem Dateinamen
    CHECK(GuessYear(L"Rechnung_2025-03_Vodafone.pdf", L"2026") == L"2025");
    CHECK(GuessYear(L"scan12345.pdf", L"2026") == L"2026");        // Teil einer laengeren Zahl
    CHECK(GuessYear(L"RE-1850.pdf", L"2026") == L"2026");          // ausserhalb des Bereichs
    CHECK(GuessCreditor(L"Rechnung_Vodafone_Oktober.pdf", {}) == L"Vodafone");
    CHECK(GuessCreditor(L"telekom-2026-04.pdf", {}) == L"Telekom");
    CHECK(GuessCreditor(L"IMG_2024.jpg", {}) == L"Sonstige");
    CHECK(GuessCreditor(L"Stromrechnung_Stadtwerke.pdf", {L"Stadtwerke", L"Strom"}) == L"Stadtwerke");   // laengster bekannter Name
}

int main() {
    amounts(); dates(); names(); indexAndScan(); textEdit(); dateMask(); pdf(); invoiceNumbers(); smallBusiness(); installmentRate(); closingAndFooter();
    tagsStatsImport();
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures ? 1 : 0;
}
