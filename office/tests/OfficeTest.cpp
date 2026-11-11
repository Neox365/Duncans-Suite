// Tests fuer Duncan's Office: Tabellen-Engine (Formeln, Fehler, CSV) und Textkodierung. Ohne Win32, reines C++17.
#include <cstdio>
#include <string>

#include <vector>

#include "../Chart.h"
#include "../Codec.h"
#include "../Docx.h"
#include "../Sheet.h"
#include "../Xlsx.h"

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

// ---- DOCX ----------------------------------------------------------------------------------------------------------------------
static bool Contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

// Ist das XML wohlgeformt (Start-/End-Tags passen zusammen)?
static bool WellFormed(const std::string& xmlText)
{
    docx::xml::Tokenizer tk(xmlText);
    docx::xml::Tag t;
    std::vector<std::string> stack;
    while (tk.Next(t)) {
        if (t.kind == docx::xml::Tag::Start) stack.push_back(t.name);
        else if (t.kind == docx::xml::Tag::End) { if (stack.empty() || stack.back() != t.name) return false; stack.pop_back(); }
    }
    return stack.empty();
}

static std::string MakeDocx(const std::string& body, const std::string& styles = "", const std::string& numbering = "")
{
    const std::string ns = "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"";
    std::vector<std::pair<std::string, std::string>> f = {
        { "[Content_Types].xml", "<Types/>" },
        { "word/document.xml", "<?xml version=\"1.0\"?><w:document " + ns + "><w:body>" + body + "</w:body></w:document>" } };
    if (!styles.empty()) f.push_back({ "word/styles.xml", "<w:styles " + ns + ">" + styles + "</w:styles>" });
    if (!numbering.empty()) f.push_back({ "word/numbering.xml", "<w:numbering " + ns + ">" + numbering + "</w:numbering>" });
    return zip::Write(f);
}

static void DocxTests()
{
    const std::string styles = R"(<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii="Cambria" w:hAnsi="Cambria"/><w:sz w:val="22"/></w:rPr></w:rPrDefault></w:docDefaults>
<w:style w:type="paragraph" w:styleId="berschrift1"><w:name w:val="heading 1"/><w:rPr><w:b/><w:sz w:val="32"/><w:color w:val="1F3864"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Untertitel"><w:name w:val="Sub"/><w:basedOn w:val="berschrift1"/><w:rPr><w:i/></w:rPr></w:style>)";
    const std::string numbering = R"(<w:abstractNum w:abstractNumId="0"><w:lvl w:ilvl="0"><w:numFmt w:val="bullet"/></w:lvl></w:abstractNum>
<w:abstractNum w:abstractNumId="1"><w:lvl w:ilvl="0"><w:numFmt w:val="decimal"/></w:lvl></w:abstractNum>
<w:num w:numId="5"><w:abstractNumId w:val="0"/></w:num><w:num w:numId="6"><w:abstractNumId w:val="1"/></w:num>)";
    const std::string body = R"(
<w:p><w:pPr><w:pStyle w:val="berschrift1"/></w:pPr><w:r><w:t>Titel</w:t></w:r></w:p>
<w:p><w:pPr><w:jc w:val="center"/><w:tabs><w:tab w:val="left" w:pos="1000"/></w:tabs><w:rPr><w:b/></w:rPr></w:pPr>
  <w:r><w:rPr><w:b/><w:i/></w:rPr><w:t>Fett &amp; kursiv</w:t></w:r>
  <w:r><w:rPr><w:color w:val="FF0000"/><w:sz w:val="28"/><w:u w:val="single"/><w:strike/></w:rPr><w:t xml:space="preserve"> rot Gr&#246;&#223;e {x}\</w:t></w:r></w:p>
<w:p><w:r><w:t>a</w:t></w:r><w:r><w:tab/></w:r><w:r><w:t>b</w:t><w:br/><w:t>c</w:t></w:r><w:r><w:br w:type="page"/></w:r></w:p>
<w:p><w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="5"/></w:numPr></w:pPr><w:r><w:t>Punkt</w:t></w:r></w:p>
<w:p><w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="6"/></w:numPr></w:pPr><w:r><w:t>eins</w:t></w:r></w:p>
<w:p><w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="6"/></w:numPr></w:pPr><w:r><w:t>zwei</w:t></w:r></w:p>
<w:tbl><w:tblGrid><w:gridCol w:w="2000"/><w:gridCol w:w="3000"/></w:tblGrid>
  <w:tr><w:tc><w:p><w:r><w:t>A1</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>B1</w:t></w:r></w:p><w:p><w:r><w:t>B1 zweiter</w:t></w:r></w:p></w:tc></w:tr>
  <w:tr><w:tc><w:p/></w:tc><w:tc><w:p><w:r><w:t>B2</w:t></w:r></w:p></w:tc></w:tr></w:tbl>
<w:p><w:r><w:drawing/></w:r><w:r><w:fldChar/><w:instrText>PAGE</w:instrText></w:r><w:r><w:t>Ende</w:t></w:r></w:p>
<w:p/>)";
    docx::ReadResult r = docx::DocxToRtf(MakeDocx(body, styles, numbering));
    CHECK(r.ok && r.images == 1);
    const std::string& rtf = r.rtf;
    CHECK(rtf.compare(0, 6, "{\\rtf1") == 0 && rtf.back() == '}');
    CHECK(Contains(rtf, "{\\f0\\fnil\\fcharset0 Cambria;}"));                                  // Standardschrift aus den Dokumentvorgaben
    CHECK(Contains(rtf, "\\b ") && Contains(rtf, "\\i") && Contains(rtf, "\\ul") && Contains(rtf, "\\strike"));
    CHECK(Contains(rtf, "Titel") && Contains(rtf, "\\fs32"));                                 // Ueberschrift: Groesse und fett aus der Formatvorlage
    CHECK(Contains(rtf, "\\red31\\green56\\blue100;") && Contains(rtf, "\\red255\\green0\\blue0;"));
    CHECK(Contains(rtf, "\\qc") && Contains(rtf, "\\fs28"));
    CHECK(Contains(rtf, "Fett & kursiv"));
    CHECK(Contains(rtf, "Gr\\u246?\\u223?e \\{x\\}\\\\"));                                      // Umlaute als \uN, {, }, \ maskiert
    CHECK(Contains(rtf, "a}") && Contains(rtf, "\\tab ") && Contains(rtf, "\\line ") && Contains(rtf, "\\page "));
    CHECK(Contains(rtf, "\\pnlvlblt") && Contains(rtf, "Punkt"));                              // echte Aufzaehlung
    CHECK(Contains(rtf, "1.\\tab}") && Contains(rtf, "2.\\tab}") && Contains(rtf, "eins") && Contains(rtf, "zwei"));
    CHECK(Contains(rtf, "\\trowd") && Contains(rtf, "\\cellx2000") && Contains(rtf, "\\cellx5000") && Contains(rtf, "\\cell") && Contains(rtf, "\\row"));
    CHECK(Contains(rtf, "A1") && Contains(rtf, "B1 zweiter") && Contains(rtf, "B2"));
    CHECK(Contains(rtf, "B1}") && Contains(rtf, "\\par B1 zweiter") == false);                // Absatz in der Zelle mit \par getrennt
    CHECK(Contains(rtf, "Ende") && !Contains(rtf, "PAGE"));                                    // Feldanweisung wird nicht als Text uebernommen

    CHECK(r.lossy == L"Bilder und Zeichnungen");                                                // das <w:drawing/> im Beispiel
    {
        const std::string ns = "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"";
        const std::string withParts = zip::Write({ { "word/document.xml", "<w:document " + ns + "><w:body><w:p><w:r><w:footnoteReference w:id=\"2\"/></w:r></w:p></w:body></w:document>" },
                                                   { "word/header1.xml", "<w:hdr/>" }, { "word/media/image1.png", "x" }, { "word/comments.xml", "<w:comments/>" } });
        const docx::ReadResult lr = docx::DocxToRtf(withParts);
        CHECK(lr.ok && lr.lossy == L"Bilder und Zeichnungen, Kopf- und Fu\u00DFzeilen, Fu\u00DFnoten, Kommentare");
        CHECK(docx::DocxToRtf(MakeDocx("<w:p/>")).lossy.empty());
    }
    CHECK(!docx::DocxToRtf("kein zip").ok && !docx::DocxToRtf("").ok);
    CHECK(!docx::DocxToRtf(zip::Write({ { "a.txt", "x" } })).ok);                              // ZIP ohne word/document.xml
    const docx::ReadResult e = docx::DocxToRtf("nope");
    CHECK(!e.error.empty());
    const std::string good = MakeDocx("<w:p><w:r><w:t>x</w:t></w:r></w:p>");
    std::string cut = good.substr(0, good.size() - 30);                                        // abgeschnitten
    CHECK(!docx::DocxToRtf(cut).ok);

    // RTF -> DOCX
    const std::string rtfIn = R"({\rtf1\ansi\ansicpg1252\deff0\uc1{\fonttbl{\f0\fnil\fcharset0 Segoe UI;}{\f1\fnil\fcharset2 Symbol;}{\f2\fswiss\fcharset0 Arial;}}
{\colortbl ;\red255\green0\blue0;\red0\green0\blue255;}
\viewkind4\pard\ql\f0\fs24 Hallo {\b Welt}\par
\pard\qc{\i\ul schief} und {\cf1\fs36\strike rot}{\super 2}{\sub 3}\par
\pard\qr\f2 rechts: Gr\'fc\'dfe )" "\\" R"(u8364?\u233? {\\ \{ \} \~ \_}\tab Tab\line Zeile\par
\pard\qj Blocksatz\par
\pard{\pntext\f1\'B7\tab}{\*\pn\pnlvlblt\pnf1\pnindent0{\pntxtb\'B7}}\fi-360\li720 Punkt eins\par
{\pntext\f1\'B7\tab}Punkt zwei\par
\pard Normal danach\par
\trowd\trgaph108\trleft-108\cellx2000\cellx5000
\pard\intbl Z1\cell
\pard\intbl Z2a\par Z2b\cell
\row
\pard\intbl\row
\pard Schluss\par
})";
    // (der Tabellenrest "\pard\intbl\row" ohne Zellen darf nichts kaputt machen)
    const docx::WriteResult w = docx::RtfToDocx(rtfIn);
    CHECK(w.ok && !w.bytes.empty());
    std::vector<zip::Entry> es;
    CHECK(zip::ReadDirectory(w.bytes, es) && es.size() == 6);
    std::string doc, sty, num, ct, rel;
    CHECK(zip::ReadEntry(w.bytes, es, "word/document.xml", doc) && zip::ReadEntry(w.bytes, es, "word/styles.xml", sty) && zip::ReadEntry(w.bytes, es, "word/numbering.xml", num) &&
          zip::ReadEntry(w.bytes, es, "[Content_Types].xml", ct) && zip::ReadEntry(w.bytes, es, "_rels/.rels", rel) && zip::ReadEntry(w.bytes, es, "word/_rels/document.xml.rels", rel));
    CHECK(WellFormed(doc) && WellFormed(sty) && WellFormed(num) && WellFormed(ct));
    CHECK(Contains(doc, "<w:b/>") && Contains(doc, ">Welt<") && Contains(doc, "Hallo "));
    CHECK(Contains(doc, "<w:jc w:val=\"center\"/>") && Contains(doc, "<w:jc w:val=\"right\"/>") && Contains(doc, "<w:jc w:val=\"both\"/>"));
    CHECK(Contains(doc, "<w:i/>") && Contains(doc, "<w:u w:val=\"single\"/>") && Contains(doc, "<w:strike/>") && Contains(doc, "<w:color w:val=\"FF0000\"/>") && Contains(doc, "<w:sz w:val=\"36\"/>"));
    CHECK(Contains(doc, "w:val=\"superscript\"") && Contains(doc, "w:val=\"subscript\""));
    CHECK(Contains(doc, "Gr\xC3\xBC\xC3\x9F" "e \xE2\x82\xAC\xC3\xA9 \\ { } \xC2\xA0 \xE2\x80\x91"));                  // ue, ss, Euro, e-akut, maskierte Zeichen, geschuetztes Leerzeichen
    CHECK(Contains(doc, "<w:tab/>") && Contains(doc, "<w:br/>"));
    CHECK(Contains(doc, "<w:rFonts w:ascii=\"Arial\"") && !Contains(doc, "Symbol"));
    CHECK(Contains(doc, "<w:numPr><w:ilvl w:val=\"0\"/><w:numId w:val=\"1\"/></w:numPr>") && Contains(doc, "Punkt eins") && Contains(doc, "Punkt zwei"));
    CHECK(!Contains(doc, "\xC2\xB7"));                                                                                      // das Aufzaehlungszeichen steckt nicht im Text
    {
        size_t first = doc.find("w:numId w:val=\"1\""), second = doc.find("w:numId w:val=\"1\"", first + 1);
        CHECK(first != std::string::npos && second != std::string::npos);                                                    // beide Punkte sind Aufzaehlung (Fortsetzung ohne \pard)
    }
    CHECK(Contains(doc, "<w:tbl>") && Contains(doc, "<w:gridCol w:w=\"2000\"/>") && Contains(doc, "<w:gridCol w:w=\"3000\"/>") && Contains(doc, ">Z1<") && Contains(doc, ">Z2a<") && Contains(doc, ">Z2b<"));
    CHECK(Contains(doc, "Schluss") && Contains(doc, "Normal danach") && Contains(doc, "<w:sectPr>"));
    CHECK(Contains(num, "w:numFmt w:val=\"bullet\"") && Contains(sty, "Normal") && Contains(ct, "numbering+xml"));

    // Rundlauf: DOCX (von uns) -> RTF -> Inhalt bleibt erhalten
    const docx::ReadResult back = docx::DocxToRtf(w.bytes);
    CHECK(back.ok && Contains(back.rtf, "Welt") && Contains(back.rtf, "Punkt eins") && Contains(back.rtf, "\\pnlvlblt") && Contains(back.rtf, "Z2a") && Contains(back.rtf, "\\trowd") &&
          Contains(back.rtf, "Gr\\u252?\\u223?e"));

    // Fehlerfaelle
    CHECK(!docx::RtfToDocx("kein rtf").ok && !docx::RtfToDocx("").ok);
    const docx::WriteResult empty = docx::RtfToDocx("{\\rtf1\\ansi }");
    CHECK(empty.ok);
    std::vector<zip::Entry> es2;
    std::string doc2;
    CHECK(zip::ReadDirectory(empty.bytes, es2) && zip::ReadEntry(empty.bytes, es2, "word/document.xml", doc2) && Contains(doc2, "<w:p/>") && WellFormed(doc2));
    const docx::WriteResult trunc = docx::RtfToDocx("{\\rtf1\\ansi {\\b unvollstaendig");    // abgeschnittene Datei: keine Endlosschleife, nimmt was da ist
    CHECK(trunc.ok);
    // XML-Sonderzeichen im Text werden maskiert
    const docx::WriteResult sp = docx::RtfToDocx("{\\rtf1\\ansi <a> & \"b\"\\par}");
    std::vector<zip::Entry> es3;
    std::string doc3;
    CHECK(sp.ok && zip::ReadDirectory(sp.bytes, es3) && zip::ReadEntry(sp.bytes, es3, "word/document.xml", doc3) && Contains(doc3, "&lt;a&gt; &amp; &quot;b&quot;") && WellFormed(doc3));
}

void ChartTests()
{
    sheet::Sheet s;
    // Kopfzeile + Beschriftungsspalte + zwei Reihen
    s.Set(0, 0, L"Monat"); s.Set(0, 1, L"Umsatz"); s.Set(0, 2, L"Kosten");
    s.Set(1, 0, L"Jan"); s.Set(1, 1, L"10"); s.Set(1, 2, L"4");
    s.Set(2, 0, L"Feb"); s.Set(2, 1, L"=B2*2"); s.Set(2, 2, L"6");
    chart::Data d = chart::Extract(s, 0, 0, 2, 2);
    CHECK(d.ok);
    CHECK(d.labels.size() == 2 && d.labels[0] == L"Jan" && d.labels[1] == L"Feb");
    CHECK(d.names.size() == 2 && d.names[0] == L"Umsatz" && d.names[1] == L"Kosten");
    CHECK(d.series.size() == 2 && d.series[0][1] == 20 && d.series[1][0] == 4);
    // nur Zahlen: Beschriftungen sind 1, 2, ...
    chart::Data e = chart::Extract(s, 1, 1, 2, 2);
    CHECK(e.ok && e.labels.size() == 2 && e.labels[0] == L"1" && e.names[0].empty() && e.series.size() == 2);
    // einzelne Zelle und Text ohne Zahlen sind kein Diagramm
    CHECK(!chart::Extract(s, 1, 1, 1, 1).ok);
    CHECK(!chart::Extract(s, 0, 0, 2, 0).ok);
    // schoene Achse
    double lo, hi, st;
    chart::NiceAxis(0, 87, lo, hi, st);
    CHECK(lo == 0 && hi >= 87 && st > 0 && (hi - lo) / st <= 10);
    chart::NiceAxis(-3, 4, lo, hi, st);
    CHECK(lo <= -3 && hi >= 4);
}

void XlsxTests()
{
    using namespace sheet;
    auto Rgb = [](unsigned r, unsigned g, unsigned b) { return static_cast<std::uint32_t>(r | (g << 8) | (b << 16)); };
    // Eine handgebaute Excel-Datei: gemeinsame Zeichenfolgen, Datum, Wahrheitswert, Fehler, gemeinsame Formel, fremde Funktion, Zellen ausserhalb
    const std::string xml = "<?xml version=\"1.0\"?>";
    const std::string sst = xml + "<sst xmlns=\"x\" count=\"3\"><si><t>Name</t></si><si><r><t>Ger</t></r><r><t xml:space=\"preserve\">\xC3\xA4t &amp; Co</t></r><rPh><t>XXX</t></rPh></si><si><t>01234</t></si></sst>";
    const std::string styles = xml + "<styleSheet xmlns=\"x\"><numFmts count=\"2\"><numFmt numFmtId=\"164\" formatCode=\"dd\\.mm\\.yyyy\"/><numFmt numFmtId=\"165\" formatCode=\"#,##0.00\\ &quot;\xE2\x82\xAC&quot;\"/></numFmts>"
        "<fills count=\"3\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill><fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF00FF80\"/></patternFill></fill></fills>"
        "<cellXfs count=\"5\"><xf numFmtId=\"0\" fillId=\"0\"/><xf numFmtId=\"164\" fillId=\"0\"/><xf numFmtId=\"10\" fillId=\"0\"/><xf numFmtId=\"165\" fillId=\"2\"/><xf numFmtId=\"0\" fillId=\"2\"/></cellXfs></styleSheet>";
    const std::string sheet1 = xml + "<worksheet xmlns=\"x\"><sheetData>"
        "<row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"s\"><v>1</v></c><c r=\"C1\" t=\"s\"><v>2</v></c></row>"
        "<row r=\"2\"><c r=\"A2\"><v>12.5</v></c><c r=\"B2\" s=\"1\"><v>45000</v></c><c r=\"C2\" s=\"2\"><v>0.125</v></c><c r=\"D2\" s=\"3\"><v>7</v></c><c r=\"E2\" s=\"4\"/></row>"
        "<row r=\"3\"><c r=\"A3\"><f>SUM(A2:A2,2)</f><v>14.5</v></c><c r=\"B3\" t=\"b\"><v>1</v></c><c r=\"C3\" t=\"e\"><v>#DIV/0!</v></c>"
        "<c r=\"D3\" t=\"str\"><f>CONCATENATE(A1,B1)</f><v>Treffer</v></c><c r=\"E3\"><f>IF(A2&gt;10,\"gro\xC3\x9F\",\"klein\")</f><v>0</v></c><c r=\"F3\"><f>Tabelle2!A1+1</f><v>99</v></c></row>"
        "<row r=\"4\"><c r=\"A4\"><f t=\"shared\" ref=\"A4:A5\" si=\"0\">$A$2*2</f><v>25</v></c></row>"
        "<row r=\"5\"><c r=\"A5\"><f t=\"shared\" si=\"0\"/><v>26</v></c><c r=\"AA5\"><v>5</v></c></row>"
        "<row r=\"150\"><c r=\"A150\"><v>1</v></c></row>"
        "<row r=\"6\"><c r=\"A6\" t=\"inlineStr\"><is><t>direkt</t></is></c></row></sheetData></worksheet>";
    const std::string wb = xml + "<workbook xmlns=\"x\" xmlns:r=\"y\"><sheets><sheet name=\"Eins\" sheetId=\"1\" r:id=\"rId7\"/><sheet name=\"Zwei\" sheetId=\"2\" r:id=\"rId8\"/></sheets></workbook>";
    const std::string rels = xml + "<Relationships><Relationship Id=\"rId7\" Type=\"t\" Target=\"worksheets/sheet1.xml\"/><Relationship Id=\"rId8\" Type=\"t\" Target=\"worksheets/sheet2.xml\"/></Relationships>";
    const std::string file = zip::Write({ { "xl/workbook.xml", wb }, { "xl/_rels/workbook.xml.rels", rels }, { "xl/worksheets/sheet1.xml", sheet1 },
                                          { "xl/worksheets/sheet2.xml", xml + "<worksheet xmlns=\"x\"/>" }, { "xl/sharedStrings.xml", sst }, { "xl/styles.xml", styles } });
    Sheet s;
    const xlsx::ReadResult rr = xlsx::Read(file, s);
    CHECK(rr.ok);
    CHECK(rr.extraSheets == 1);
    CHECK(rr.droppedCells == 2);                                   // AA5 und A150
    CHECK(s.raw[0][0] == L"Name");
    CHECK(s.raw[0][1] == L"Ger\xE4t & Co");                        // Rich-Text zusammengesetzt, Lautschrift (rPh) ignoriert
    CHECK(s.raw[0][2] == L"01234");
    CHECK(s.raw[1][0] == L"12.5" && s.Display(1, 0) == L"12,5");
    CHECK(s.raw[1][1] == L"15.03.2023" && rr.dateCells == 1);      // Datumsformat: als Text
    CHECK(s.fmt[1][2] == FmtPercent && s.Display(1, 2) == L"12,5 %");
    CHECK(s.fmt[1][3] == FmtCurrency && s.fill[1][3] == Rgb(0x00, 0xFF, 0x80));
    CHECK(s.fill[1][4] == Rgb(0x00, 0xFF, 0x80) && s.raw[1][4].empty());   // leere Zelle mit Hintergrund
    CHECK(s.raw[2][0] == L"=SUM(A2:A2;2)" && s.Display(2, 0) == L"14,5");   // Formel bleibt Formel (Trenner , -> ;)
    CHECK(s.raw[2][1] == L"WAHR");
    CHECK(s.raw[2][2] == L"#DIV/0!");
    CHECK(s.raw[2][3] == L"Treffer");                              // CONCATENATE kann Sheets nicht: Ergebnis statt Formel
    CHECK(s.raw[2][4] == L"=IF(A2>10;\"gro\xDF\";\"klein\")" && s.Display(2, 4) == L"gro\xDF");
    CHECK(s.raw[2][5] == L"99");                                   // Bezug auf anderes Blatt: Ergebnis
    CHECK(rr.replacedFormulas == 2);
    CHECK(s.raw[3][0] == L"=A2*2" && s.Display(3, 0) == L"25");    // $ entfernt
    CHECK(s.raw[4][0] == L"26");                                   // gemeinsame Formel ohne Text: gespeicherter Wert
    CHECK(s.raw[5][0] == L"direkt");

    // Datum: Seriennummern
    CHECK(xlsx::detail::SerialToText(45000, false, true) == L"15.03.2023");
    CHECK(xlsx::detail::SerialToText(45292.5, false, false) == L"01.01.2024 12:00");
    CHECK(xlsx::detail::SerialToText(61, false, true) == L"01.03.1900");
    CHECK(xlsx::detail::SerialToText(1, false, true) == L"01.01.1900");
    CHECK(xlsx::detail::SerialToText(0.75, true, false) == L"18:00");

    // Kaputte Dateien
    Sheet bad;
    CHECK(!xlsx::Read("kein ZIP", bad).ok);
    CHECK(!xlsx::Read(zip::Write({ { "a.txt", "x" } }), bad).ok);
    CHECK(!xlsx::Read(zip::Write({ { "xl/workbook.xml", wb } }), bad).ok);          // Blatt fehlt

    // Schreiben und wieder lesen
    Sheet w;
    w.Set(0, 0, L"Text mit \"Anf\xFChrung\" & <Tag>");
    w.Set(0, 1, L"12,5");
    w.Set(0, 2, L"-3");
    w.Set(1, 0, L"=SUMME(B1:C1)");
    w.Set(1, 1, L"=WENN(B1>2;\"ja\";\"nein\")");
    w.Set(1, 2, L"=B1*1,5");
    w.Set(2, 0, L"=1/0");
    w.Set(2, 1, L"=MITTELWERT(B1:C1)");
    w.Set(2, 2, L"=ZAEHLENWENN(B1:C1;\">0\")");
    w.Set(4, 0, L"=RUNDEN(B1;0)");
    w.Set(3, 0, L"0,25");
    w.fmt[3][0] = FmtPercent;
    w.Set(3, 1, L"1234,5");
    w.fmt[3][1] = FmtCurrency;
    w.fill[3][1] = Rgb(255, 200, 0);
    w.fill[4][4] = Rgb(1, 2, 3);
    const std::string out = xlsx::Write(w);
    std::vector<zip::Entry> ents;
    CHECK(zip::ReadDirectory(out, ents) && ents.size() == 6);
    std::string sheetXml, stylesXml;
    CHECK(zip::ReadEntry(out, ents, "xl/worksheets/sheet1.xml", sheetXml) && zip::ReadEntry(out, ents, "xl/styles.xml", stylesXml));
    CHECK(sheetXml.find("<f>SUM(B1:C1)</f><v>9.5</v>") != std::string::npos);
    CHECK(sheetXml.find("<f>IF(B1&gt;2,&quot;ja&quot;,&quot;nein&quot;)</f>") != std::string::npos);
    CHECK(sheetXml.find("<f>ROUND(B1,0)</f>") != std::string::npos);
    CHECK(sheetXml.find("<f>B1*1.5</f><v>18.75</v>") != std::string::npos);         // Dezimalkomma -> Punkt
    CHECK(sheetXml.find("<f>COUNTIF(B1:C1,&quot;&gt;0&quot;)</f>") != std::string::npos);
    CHECK(sheetXml.find("t=\"e\"><f>1/0</f><v>#DIV/0!</v>") != std::string::npos);
    CHECK(sheetXml.find("Text mit &quot;Anf\xC3\xBC" "hrung&quot; &amp; &lt;Tag&gt;") != std::string::npos);
    CHECK(stylesXml.find("rgb=\"FFFFC800\"") != std::string::npos && stylesXml.find("numFmtId=\"10\"") != std::string::npos);
    Sheet back;
    const xlsx::ReadResult br = xlsx::Read(out, back);
    CHECK(br.ok && br.replacedFormulas == 0 && br.droppedCells == 0 && br.extraSheets == 0);
    for (int r = 0; r < 5; ++r)
        for (int c = 0; c < 5; ++c) {
            CHECK(back.Display(r, c) == w.Display(r, c));                           // gleiche Anzeige nach dem Hin und Zurueck
            CHECK(back.fmt[r][c] == w.fmt[r][c] && back.fill[r][c] == w.fill[r][c]);
        }
    CHECK(back.raw[0][0] == w.raw[0][0] && back.raw[0][1] == L"12.5");
    CHECK(back.raw[1][0] == L"=SUM(B1;C1)" || back.raw[1][0] == L"=SUM(B1:C1)");
    Sheet empty;
    CHECK(xlsx::Read(xlsx::Write(empty), back).ok && back.raw[0][0].empty());
}

int main()
{
    DocxTests();
    ChartTests();
    XlsxTests();
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
    CHECK(Show(s, L"A1", L"=2^3^2") == L"64");   // wie Excel: von links nach rechts
    CHECK(Show(s, L"A1", L"=-2^2") == L"4");
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

    // WENN, Vergleiche, ZAEHLENWENN
    s.Clear();
    for (int i = 0; i < 5; ++i) s.Set(i, 0, std::to_wstring(i + 1));   // A1..A5 = 1..5
    s.Set(0, 2, L"ja");
    s.Set(1, 2, L"nein");
    s.Set(2, 2, L"Ja");
    CHECK(Show(s, L"B1", L"=WENN(A5>3;\"hoch\";\"niedrig\")") == L"hoch");
    CHECK(Show(s, L"B2", L"=WENN(A1>3;\"hoch\";\"niedrig\")") == L"niedrig");
    CHECK(Show(s, L"B3", L"=WENN(A5>3;10;20)*2") == L"20");
    CHECK(Show(s, L"B4", L"=WENN(A1>3;10;20)+1") == L"21");
    CHECK(Show(s, L"B5", L"=WENN(C4=0;0;A5/C4)") == L"0");               // leere Zelle = 0, der andere Zweig (Division durch 0) wird nicht ausgewertet
    CHECK(Show(s, L"B6", L"=WENN(A1=0;1;1/0)") == L"#DIV/0!");           // der gewaehlte Zweig darf nicht fehlerhaft sein
    CHECK(Show(s, L"B7", L"=WENN(C1=\"ja\";1;0)") == L"1");
    CHECK(Show(s, L"B8", L"=WENN(C3=\"ja\";1;0)") == L"1");               // ohne Beachtung der Gross-/Kleinschreibung
    CHECK(Show(s, L"B9", L"=WENN(C2<>\"ja\";\"anders\";\"gleich\")") == L"anders");
    CHECK(Show(s, L"B10", L"=WENN(C9=\"\";\"leer\";\"voll\")") == L"leer");
    CHECK(Show(s, L"B11", L"=WENN(A1>0;WENN(A5>9;\"a\";\"b\");\"c\")") == L"b");   // verschachtelt, Text als Ergebnis
    CHECK(Show(s, L"B12", L"=IF(A5>=5;1;2)") == L"1");
    CHECK(Show(s, L"B13", L"=WENN(A1>0;\"x\")") == L"x");
    CHECK(Show(s, L"B14", L"=WENN(A1>9;\"x\")") == L"0");                 // ohne Sonst-Zweig: 0
    CHECK(Show(s, L"B15", L"=A5>3") == L"1");
    CHECK(Show(s, L"B16", L"=A5<=3") == L"0");
    CHECK(Show(s, L"B17", L"=C1") == L"ja");                              // Textzelle direkt als Ergebnis
    CHECK(Show(s, L"B18", L"=WENN(A1;1;2)") == L"1");
    CHECK(Show(s, L"B19", L"=WENN(A1>0;\"x\";\"y\")+1") == L"#WERT!");    // Text in Rechnung
    CHECK(Show(s, L"B20", L"=WENN(A1>0;1") == L"#SYNTAX!");
    CHECK(Show(s, L"D1", L"=ZÄHLENWENN(A1:A5;\">2\")") == L"3");
    CHECK(Show(s, L"D2", L"=ZAEHLENWENN(A1:A5;3)") == L"1");
    CHECK(Show(s, L"D3", L"=COUNTIF(A1:A5;\"<>3\")") == L"4");
    CHECK(Show(s, L"D4", L"=zählenwenn(C1:C5;\"ja\")") == L"2");           // Text, ohne Beachtung der Gross-/Kleinschreibung
    CHECK(Show(s, L"D5", L"=ZÄHLENWENN(C1:C5;\"n*\")") == L"1");          // Platzhalter
    CHECK(Show(s, L"D6", L"=ZÄHLENWENN(A1:A5;\">=\"&1)") == L"#SYNTAX!");  // & wird nicht unterstuetzt
    CHECK(Show(s, L"D7", L"=ZÄHLENWENN(A1:A5;A2)") == L"1");              // Kriterium aus einer Zelle
    CHECK(Show(s, L"D8", L"=ZÄHLENWENN(A1:A5;\"<=2\")+ANZAHL(A1:A5)") == L"7");
    CHECK(Show(s, L"E1", L"=SUMMEWENN(A1:A5;\">2\")") == L"12");           // ohne Summenbereich: der Bereich selbst
    CHECK(Show(s, L"E2", L"=SUMIF(A1:A5;3)") == L"3");
    CHECK(Show(s, L"E3", L"=SUMMEWENN(C1:C5;\"ja\";A1:A5)") == L"4");      // Kriterium in C, summiert wird A (1 + 3)
    CHECK(Show(s, L"E4", L"=summewenn(C1:C5;\"n*\";A1:A5)") == L"2");      // Platzhalter, Kleinschreibung
    CHECK(Show(s, L"E5", L"=SUMMEWENN(A1:A5;\"<=2\")+1") == L"4");
    CHECK(Show(s, L"E6", L"=SUMMEWENN(A1:A5;\">2\"") == L"#SYNTAX!");

    // SVERWEIS und Rechenfunktionen
    s.Clear();
    s.Set(0, 0, L"Apfel"); s.Set(0, 1, L"1,5"); s.Set(0, 2, L"rot");
    s.Set(1, 0, L"Birne"); s.Set(1, 1, L"2");   s.Set(1, 2, L"gelb");
    s.Set(2, 0, L"Kiwi");  s.Set(2, 1, L"3");   s.Set(2, 2, L"gr\u00FCn");
    s.Set(0, 4, L"1");  s.Set(0, 5, L"eins");
    s.Set(1, 4, L"5");  s.Set(1, 5, L"f\u00FCnf");
    s.Set(2, 4, L"10"); s.Set(2, 5, L"zehn");
    CHECK(Show(s, L"H1", L"=SVERWEIS(\"Birne\";A1:C3;2;FALSCH)") == L"2");
    CHECK(Show(s, L"H2", L"=SVERWEIS(\"kiwi\";A1:C3;3;0)") == L"gr\u00FCn");             // ohne Beachtung der Gross-/Kleinschreibung, Text als Ergebnis
    CHECK(Show(s, L"H3", L"=VLOOKUP(\"Ki*\";A1:C3;2;FALSE)") == L"3");                  // Platzhalter
    CHECK(Show(s, L"H4", L"=SVERWEIS(\"Zitrone\";A1:C3;2;FALSCH)") == L"#NV");
    CHECK(Show(s, L"H5", L"=SVERWEIS(\"Birne\";A1:C3;4;FALSCH)") == L"#BEZUG!");
    CHECK(Show(s, L"H6", L"=SVERWEIS(\"Birne\";A1:C3;2;FALSCH)*2") == L"4");              // mitten in einer Rechnung
    CHECK(Show(s, L"H7", L"=SVERWEIS(7;E1:F3;2)") == L"f\u00FCnf");                       // ungefaehr: groesster Wert <= 7
    CHECK(Show(s, L"H8", L"=SVERWEIS(0;E1:F3;2)") == L"#NV");
    CHECK(Show(s, L"H9", L"=SVERWEIS(10;E1:F3;2;WAHR)") == L"zehn");
    CHECK(Show(s, L"H10", L"=WENN(SVERWEIS(\"Kiwi\";A1:C3;2;0)>2;\"teuer\";\"billig\")") == L"teuer");
    CHECK(Show(s, L"H11", L"=SVERWEIS(B2;E1:F3;2;0)") == L"#NV");                         // 2 kommt in E nicht vor
    CHECK(Show(s, L"J1", L"=RUNDEN(1234,5678;-2)") == L"1200");
    CHECK(Show(s, L"J2", L"=ROUND(2,5;0)") == L"3");
    CHECK(Show(s, L"J3", L"=ABS(-3)") == L"3");
    CHECK(Show(s, L"J4", L"=WURZEL(16)+1") == L"5");
    CHECK(Show(s, L"J5", L"=GANZZAHL(-2,5)") == L"-3");
    CHECK(Show(s, L"J6", L"=WURZEL(-1)") == L"#ZAHL!");
    CHECK(Show(s, L"J7", L"=RUNDEN(2)") == L"#SYNTAX!");
    // MEDIAN, PRODUKT, REST, POTENZ, AUFRUNDEN, ABRUNDEN, VORZEICHEN
    s.Clear();
    s.Set(0, 0, L"5"); s.Set(1, 0, L"1"); s.Set(2, 0, L"9"); s.Set(3, 0, L"3");
    CHECK(Show(s, L"C1", L"=MEDIAN(A1:A4)") == L"4");                      // gerade Anzahl: Mittel der beiden mittleren
    CHECK(Show(s, L"C2", L"=MEDIAN(A1:A3)") == L"5");
    CHECK(Show(s, L"C3", L"=PRODUKT(A1:A4)") == L"135");
    CHECK(Show(s, L"C4", L"=PRODUCT(2;3;4)") == L"24");
    CHECK(Show(s, L"C5", L"=REST(10;3)") == L"1");
    CHECK(Show(s, L"C6", L"=MOD(-1;3)") == L"2");                          // Vorzeichen des Teilers
    CHECK(Show(s, L"C7", L"=REST(5;0)") == L"#DIV/0!");
    CHECK(Show(s, L"C8", L"=POTENZ(2;10)") == L"1024");
    CHECK(Show(s, L"C9", L"=POWER(9;0,5)+1") == L"4");
    CHECK(Show(s, L"C10", L"=AUFRUNDEN(2,341;2)") == L"2,35");
    CHECK(Show(s, L"C11", L"=AUFRUNDEN(-2,341;2)") == L"-2,35");           // von null weg
    CHECK(Show(s, L"C12", L"=ABRUNDEN(2,349;2)") == L"2,34");
    CHECK(Show(s, L"C13", L"=ROUNDDOWN(-2,349;2)") == L"-2,34");
    CHECK(Show(s, L"C14", L"=AUFRUNDEN(2,5;0)") == L"3");
    CHECK(Show(s, L"C15", L"=AUFRUNDEN(2;0)") == L"2");                    // schon glatt
    CHECK(Show(s, L"C16", L"=VORZEICHEN(-7)") == L"-1");
    CHECK(Show(s, L"C17", L"=SIGN(0)") == L"0");
    CHECK(Show(s, L"C18", L"=MEDIAN(B1:B2)") == L"#ZAHL!");                // leer
    CHECK(Show(s, L"C19", L"=REST(5)") == L"#SYNTAX!");

    // Sortieren
    s.Clear();
    s.Set(0, 0, L"Name"); s.Set(0, 1, L"Preis");
    s.Set(1, 0, L"Kiwi"); s.Set(1, 1, L"3");
    s.Set(2, 0, L"apfel"); s.Set(2, 1, L"12");
    s.Set(3, 0, L"Birne"); s.Set(3, 1, L"1,5");
    s.Set(4, 0, L"Dattel");
    s.fill[1][0] = 0x112233; s.fmt[2][1] = FmtCurrency;
    CHECK(s.SortRows(1, 0, 4, 1, 0, true));                                // nach Name aufsteigend (ohne Schreibweise), leere Preis-Zelle wandert mit
    CHECK(s.raw[1][0] == L"apfel" && s.raw[1][1] == L"12" && s.raw[2][0] == L"Birne" && s.raw[3][0] == L"Dattel" && s.raw[3][1].empty() && s.raw[4][0] == L"Kiwi");
    CHECK(s.fmt[1][1] == FmtCurrency && s.fill[4][0] == 0x112233 && s.fill[1][0] == kNoFill);   // Formate und Farben wandern mit
    CHECK(s.raw[0][0] == L"Name");                                          // Kopfzeile ausserhalb des Bereichs bleibt
    CHECK(s.SortRows(1, 0, 4, 1, 1, false));                                // Preis absteigend; leere Zelle zuletzt
    CHECK(s.raw[1][1] == L"12" && s.raw[2][1] == L"3" && s.raw[3][1] == L"1,5" && s.raw[4][0] == L"Dattel");
    CHECK(s.SortRows(1, 0, 4, 1, 1, true) && s.raw[1][1] == L"1,5" && s.raw[4][0] == L"Dattel");   // leer bleibt auch aufsteigend hinten
    s.Set(1, 1, L"=A1");
    const std::wstring before = s.raw[2][0];
    CHECK(!s.SortRows(1, 0, 4, 1, 0, true) && s.raw[2][0] == before);       // Formeln: nichts veraendern
    CHECK(!s.SortRows(1, 0, 4, 1, 3, true) && !s.SortRows(3, 0, 1, 1, 0, true));
    s.Clear();
    s.Set(0, 0, L"b"); s.Set(1, 0, L"2"); s.Set(2, 0, L"a"); s.Set(3, 0, L"1");
    CHECK(s.SortRows(0, 0, 3, 0, 0, true) && s.raw[0][0] == L"1" && s.raw[1][0] == L"2" && s.raw[2][0] == L"a" && s.raw[3][0] == L"b");   // Zahlen vor Text
    CHECK(s.SortRows(0, 0, 3, 0, 0, false) && s.raw[0][0] == L"b" && s.raw[3][0] == L"1");
    // Zahlenformate und Hintergrundfarbe
    CHECK(FormatCurrency(1234.5) == L"1.234,50 \u20AC");
    CHECK(FormatCurrency(-0.5) == L"-0,50 \u20AC");
    CHECK(FormatCurrency(1234567.891) == L"1.234.567,89 \u20AC");
    CHECK(FormatCurrency(0) == L"0,00 \u20AC");
    CHECK(FormatPercent(0.125) == L"12,5 %");
    CHECK(FormatPercent(1) == L"100 %");
    CHECK(FormatPercent(0) == L"0 %");
    s.Clear();
    s.Set(0, 0, L"0,25");
    s.fmt[0][0] = FmtPercent;
    s.Set(1, 0, L"=A1*4000");
    s.fmt[1][0] = FmtCurrency;
    CHECK(s.Display(0, 0) == L"25 %");
    CHECK(s.Display(1, 0) == L"1.000,00 \u20AC");
    CHECK(s.fill[3][3] == kNoFill);
    s.fill[3][3] = 0x123456;
    s.Clear();
    CHECK(s.fill[3][3] == kNoFill && s.fmt[0][0] == FmtGeneral);        // Clear setzt auch Formate und Farben zurueck

    // Auswahl: Statistik und Zwischenablage-Text
    s.Clear();
    s.Set(0, 0, L"10");
    s.Set(0, 1, L"20,5");
    s.Set(1, 0, L"Text");
    s.Set(1, 1, L"=A1+B1");
    const RangeStats st = Stats(s, 0, 0, 1, 1);
    CHECK(st.count == 3 && st.sum == 61 && std::fabs(st.Average() - 61.0 / 3.0) < 1e-9);
    CHECK(RangeToTsv(s, 0, 0, 1, 1, false) == L"10\t20,5\r\nText\t30,5\r\n");
    CHECK(RangeToTsv(s, 0, 0, 1, 1, true) == L"10\t20,5\r\nText\t=A1+B1\r\n");
    s.Set(2, 0, L"a\tb");
    s.Set(2, 1, L"x\"y");
    const auto rows = ParseDelimited(RangeToTsv(s, 2, 0, 2, 1, true), L'\t');
    CHECK(rows.size() == 1 && rows[0].size() == 2 && rows[0][0] == L"a\tb" && rows[0][1] == L"x\"y");
    const auto excel = ParseDelimited(L"1\t2\t3\r\n4\t5\t6\r\n", L'\t');
    CHECK(excel.size() == 2 && excel[1].size() == 3 && excel[1][2] == L"6");
    CHECK(ParseDelimited(L"", L'\t').empty());
    CHECK(ParseDelimited(L"nur Text", L'\t').size() == 1);

    std::printf("%d Pruefungen, %d Fehler\n", g_count, g_fail);
    return g_fail ? 1 : 0;
}
