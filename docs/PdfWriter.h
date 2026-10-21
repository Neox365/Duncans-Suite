#pragma once
// Minimaler PDF-Schreiber fuer einfache Dokumente (Rechnungen): A4-Seiten, Text in Helvetica/Helvetica-Bold (Standard-
// schriften, keine Einbettung noetig), Linien und graue Flaechen. Zeichensatz WinAnsi (Windows-1252): Umlaute, ss, Euro.
// Reine Logik ohne Win32, damit sie getrennt testbar bleibt.
#include <cstdio>
#include <string>
#include <vector>

namespace pdfw {

constexpr double kPageW = 595.28, kPageH = 841.89;

// Unicode -> Windows-1252 (Latin-1-Bereich 1:1, 0x80..0x9F per Tabelle); alles andere wird zu '?'.
inline std::string ToWinAnsi(const std::wstring& s)
{
    static const struct { wchar_t u; unsigned char b; } special[] = {
        {0x20AC, 0x80}, {0x201A, 0x82}, {0x0192, 0x83}, {0x201E, 0x84}, {0x2026, 0x85}, {0x2020, 0x86}, {0x2021, 0x87},
        {0x02C6, 0x88}, {0x2030, 0x89}, {0x0160, 0x8A}, {0x2039, 0x8B}, {0x0152, 0x8C}, {0x017D, 0x8E}, {0x2018, 0x91},
        {0x2019, 0x92}, {0x201C, 0x93}, {0x201D, 0x94}, {0x2022, 0x95}, {0x2013, 0x96}, {0x2014, 0x97}, {0x02DC, 0x98},
        {0x2122, 0x99}, {0x0161, 0x9A}, {0x203A, 0x9B}, {0x0153, 0x9C}, {0x017E, 0x9E}, {0x0178, 0x9F}};
    std::string out;
    for (wchar_t c : s) {
        if (c == L'\t') { out += "    "; continue; }
        if (c < 0x20) continue;
        if (c < 0x80 || (c >= 0xA0 && c <= 0xFF)) { out.push_back(static_cast<char>(static_cast<unsigned char>(c))); continue; }
        bool found = false;
        for (const auto& sp : special)
            if (sp.u == c) { out.push_back(static_cast<char>(sp.b)); found = true; break; }
        if (!found) out.push_back('?');
    }
    return out;
}

// Zeichenbreiten (1/1000 Schriftgroesse) von Helvetica und Helvetica-Bold fuer ASCII 32..126
inline int GlyphWidth(wchar_t c, bool bold)
{
    static const short reg[95] = {
        278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,                    // ' ' .. '/'
        556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556,                    // '0' .. '?'
        1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,                   // '@' .. 'O'
        667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556,                    // 'P' .. '_'
        333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,                    // '`' .. 'o'
        556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584};                        // 'p' .. '~'
    static const short bld[95] = {
        278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278,
        556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 333, 333, 584, 584, 584, 611,
        975, 722, 722, 722, 722, 667, 611, 778, 722, 278, 556, 722, 611, 833, 722, 778,
        667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 333, 278, 333, 584, 556,
        333, 556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556, 278, 889, 611, 611,
        611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584};
    if (c >= 32 && c <= 126) return (bold ? bld : reg)[c - 32];
    switch (c) {   // Umlaute wie ihr Grundbuchstabe, ss/Euro grob
        case 0xE4: return GlyphWidth(L'a', bold);
        case 0xF6: return GlyphWidth(L'o', bold);
        case 0xFC: return GlyphWidth(L'u', bold);
        case 0xC4: return GlyphWidth(L'A', bold);
        case 0xD6: return GlyphWidth(L'O', bold);
        case 0xDC: return GlyphWidth(L'U', bold);
        case 0xDF: return 611;
        default: return 556;
    }
}

class Document {
public:
    Document() { NewPage(); }

    void NewPage() { m_pages.emplace_back(); m_target = static_cast<size_t>(-1); }
    // Folgende Text/Line/FillRect-Aufrufe gehen auf Seite i (0-basiert); SelectLast() kehrt zur letzten Seite zurueck
    void SelectPage(size_t i) { m_target = i < m_pages.size() ? i : static_cast<size_t>(-1); }
    void SelectLast() { m_target = static_cast<size_t>(-1); }
    size_t PageCount() const { return m_pages.size(); }

    static double Width(const std::wstring& s, double size, bool bold = false)
    {
        double w = 0;
        for (wchar_t c : s) w += GlyphWidth(c, bold);
        return w * size / 1000.0;
    }

    // x/y in Punkt, Ursprung links unten; gray 0 = schwarz .. 1 = weiss
    void Text(double x, double y, double size, bool bold, const std::wstring& s, double gray = 0.0)
    {
        TextTo(Cur(), x, y, size, bold, s, gray);
    }
    // Fusszeile auf jeder Seite: links Text, rechts "Seite i von n"
    void Footer(const std::wstring& left, double margin = 56, double y = 34)
    {
        for (size_t i = 0; i < m_pages.size(); ++i) {
            TextTo(m_pages[i], margin, y, 7, false, left, 0.55);
            const std::wstring pg = L"Seite " + std::to_wstring(i + 1) + L" von " + std::to_wstring(m_pages.size());
            TextTo(m_pages[i], kPageW - margin - Width(pg, 7), y, 7, false, pg, 0.55);
        }
    }

private:
    size_t m_target = static_cast<size_t>(-1);
    std::string& Cur() { return m_target < m_pages.size() ? m_pages[m_target] : m_pages.back(); }
    static void TextTo(std::string& c, double x, double y, double size, bool bold, const std::wstring& s, double gray)
    {
        char head[160];
        std::snprintf(head, sizeof head, "BT /F%d %.2f Tf %.3f g %.2f %.2f Td (", bold ? 2 : 1, size, gray, x, y);
        c += head;
        for (unsigned char ch : ToWinAnsi(s)) {
            if (ch == '(' || ch == ')' || ch == '\\') { c.push_back('\\'); c.push_back(static_cast<char>(ch)); }
            else if (ch < 0x20 || ch > 0x7E) { char oct[8]; std::snprintf(oct, sizeof oct, "\\%03o", ch); c += oct; }
            else c.push_back(static_cast<char>(ch));
        }
        c += ") Tj ET\n";
    }

public:
    void TextRight(double xRight, double y, double size, bool bold, const std::wstring& s, double gray = 0.0)
    {
        Text(xRight - Width(s, size, bold), y, size, bold, s, gray);
    }
    void Line(double x1, double y1, double x2, double y2, double width = 0.5, double gray = 0.6)
    {
        char buf[160];
        std::snprintf(buf, sizeof buf, "%.2f w %.3f G %.2f %.2f m %.2f %.2f l S\n", width, gray, x1, y1, x2, y2);
        Cur() += buf;
    }
    void FillRect(double x, double y, double w, double h, double gray)
    {
        char buf[160];
        std::snprintf(buf, sizeof buf, "%.3f g %.2f %.2f %.2f %.2f re f 0 g\n", gray, x, y, w, h);
        Cur() += buf;
    }

    // Komplette PDF-Datei. title: Dokumenttitel (Info-Dictionary, UTF-16).
    std::string Build(const std::wstring& title) const
    {
        std::string out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
        std::vector<size_t> offsets;   // Objekt n beginnt bei offsets[n-1]
        auto begin = [&](int n) {
            (void)n;
            offsets.push_back(out.size());
            out += std::to_string(offsets.size()) + " 0 obj\n";
        };
        const int pageBase = 6;   // Seite i: Objekt pageBase + 2i, Inhalt pageBase + 2i + 1
        begin(1);
        out += "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
        begin(2);
        out += "<< /Type /Pages /Count " + std::to_string(m_pages.size()) + " /Kids [";
        for (size_t i = 0; i < m_pages.size(); ++i) out += std::to_string(pageBase + 2 * i) + " 0 R ";
        out += "] >>\nendobj\n";
        begin(3);
        out += "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>\nendobj\n";
        begin(4);
        out += "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>\nendobj\n";
        begin(5);
        out += "<< /Title <FEFF";
        for (wchar_t c : title) {
            char hex[8];
            std::snprintf(hex, sizeof hex, "%04X", static_cast<unsigned>(c) & 0xFFFFu);
            out += hex;
        }
        out += "> /Producer (Oggys Docs-Files) /Creator (Oggys Docs-Files) >>\nendobj\n";
        for (size_t i = 0; i < m_pages.size(); ++i) {
            begin(0);
            char mb[200];
            std::snprintf(mb, sizeof mb, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.2f %.2f] ", kPageW, kPageH);
            out += mb;
            out += "/Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> /Contents " + std::to_string(pageBase + 2 * i + 1) + " 0 R >>\nendobj\n";
            begin(0);
            out += "<< /Length " + std::to_string(m_pages[i].size()) + " >>\nstream\n" + m_pages[i] + "endstream\nendobj\n";
        }
        const size_t xref = out.size();
        out += "xref\n0 " + std::to_string(offsets.size() + 1) + "\n0000000000 65535 f \n";
        for (size_t off : offsets) {
            char e[32];
            std::snprintf(e, sizeof e, "%010zu 00000 n \n", off);
            out += e;
        }
        out += "trailer\n<< /Size " + std::to_string(offsets.size() + 1) + " /Root 1 0 R /Info 5 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
        return out;
    }

private:
    std::vector<std::string> m_pages;   // Inhaltsstrom je Seite
};

}  // namespace pdfw
