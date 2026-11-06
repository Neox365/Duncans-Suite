#pragma once
// DOCX lesen und schreiben fuer den Writer von Duncan's Office.
// Lesen:  DOCX -> RTF-Text (den das Rich-Edit-Steuerelement des Writers direkt laden kann): Absaetze, Ueberschriften, Fett/Kursiv/Unterstrichen/
//         Durchgestrichen, Schriftart, -groesse und -farbe, hoch-/tiefgestellt, Ausrichtung, Tabulatoren, Zeilenumbrueche, Aufzaehlungen,
//         nummerierte Listen (als Text "1."), einfache Tabellen. Bilder, Kopf-/Fusszeilen, Fussnoten und Kommentare werden nicht uebernommen.
// Schreiben: RTF-Text (so wie ihn Rich Edit ausgibt) -> DOCX mit denselben Elementen (Aufzaehlungen als echte Listen, Tabellen als Tabellen).
// Reine Logik ohne Win32, getrennt testbar.
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Codec.h"
#include "Zip.h"

namespace docx {

// ---------------------------------------------------------------------------------------------------------------------------------
// Kleiner XML-Zerleger (Namensraeume werden ignoriert)
// ---------------------------------------------------------------------------------------------------------------------------------
namespace xml {

struct Tag {
    enum Kind { Start, End, Empty, Text } kind = Text;
    std::string name;      // lokaler Name ohne Namensraum-Vorsatz
    std::string text;      // nur bei Text
    std::vector<std::pair<std::string, std::string>> attrs;
    std::string Attr(const std::string& local) const {
        for (const auto& a : attrs) if (a.first == local) return a.second;
        return std::string();
    }
    bool Has(const std::string& local) const {
        for (const auto& a : attrs) if (a.first == local) return true;
        return false;
    }
};

inline void AppendUtf8(std::string& out, unsigned long cp)
{
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
}

inline std::string Decode(const std::string& s)
{
    if (s.find('&') == std::string::npos) return s;
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { out += s[i]; continue; }
        const size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 10) { out += s[i]; continue; }
        const std::string ent = s.substr(i + 1, semi - i - 1);
        if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "amp") out += '&';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (ent.size() > 1 && ent[0] == '#') {
            const bool hex = ent[1] == 'x' || ent[1] == 'X';
            char* end = nullptr;
            const std::string num = ent.substr(hex ? 2 : 1);
            const unsigned long cp = num.empty() ? 0 : std::strtoul(num.c_str(), &end, hex ? 16 : 10);
            if (!num.empty() && end && *end == '\0' && cp > 0 && cp < 0x110000) AppendUtf8(out, cp); else { out += s[i]; continue; }
        } else { out += s[i]; continue; }
        i = semi;
    }
    return out;
}

inline std::string Local(const std::string& qn) { const size_t c = qn.find(':'); return c == std::string::npos ? qn : qn.substr(c + 1); }

class Tokenizer {
public:
    explicit Tokenizer(const std::string& s) : m_s(s) {}
    bool Next(Tag& t) {
        t = Tag{};
        while (m_pos < m_s.size()) {
            if (m_s[m_pos] != '<') {
                const size_t lt = m_s.find('<', m_pos);
                const size_t end = lt == std::string::npos ? m_s.size() : lt;
                t.kind = Tag::Text;
                t.text = Decode(m_s.substr(m_pos, end - m_pos));
                m_pos = end;
                return true;
            }
            if (m_s.compare(m_pos, 4, "<!--") == 0) { const size_t e = m_s.find("-->", m_pos + 4); if (e == std::string::npos) return false; m_pos = e + 3; continue; }
            if (m_s.compare(m_pos, 9, "<![CDATA[") == 0) {
                const size_t e = m_s.find("]]>", m_pos + 9);
                if (e == std::string::npos) return false;
                t.kind = Tag::Text;
                t.text = m_s.substr(m_pos + 9, e - m_pos - 9);
                m_pos = e + 3;
                return true;
            }
            if (m_s.compare(m_pos, 2, "<?") == 0) { const size_t e = m_s.find("?>", m_pos + 2); if (e == std::string::npos) return false; m_pos = e + 2; continue; }
            if (m_s.compare(m_pos, 2, "<!") == 0) { const size_t e = m_s.find('>', m_pos); if (e == std::string::npos) return false; m_pos = e + 1; continue; }
            if (m_s.compare(m_pos, 2, "</") == 0) {
                const size_t e = m_s.find('>', m_pos);
                if (e == std::string::npos) return false;
                t.kind = Tag::End;
                t.name = Local(Trim(m_s.substr(m_pos + 2, e - m_pos - 2)));
                m_pos = e + 1;
                return true;
            }
            return ParseStart(t);
        }
        return false;
    }
private:
    static std::string Trim(const std::string& s) {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
        return s.substr(a, b - a);
    }
    bool ParseStart(Tag& t) {
        size_t p = m_pos + 1;
        const size_t n = m_s.size();
        size_t ns = p;
        while (p < n && !std::isspace(static_cast<unsigned char>(m_s[p])) && m_s[p] != '>' && m_s[p] != '/') ++p;
        t.name = Local(m_s.substr(ns, p - ns));
        t.kind = Tag::Start;
        for (;;) {
            while (p < n && std::isspace(static_cast<unsigned char>(m_s[p]))) ++p;
            if (p >= n) return false;
            if (m_s[p] == '>') { m_pos = p + 1; return true; }
            if (m_s[p] == '/') { t.kind = Tag::Empty; ++p; continue; }
            const size_t as = p;
            while (p < n && m_s[p] != '=' && m_s[p] != '>' && m_s[p] != '/' && !std::isspace(static_cast<unsigned char>(m_s[p]))) ++p;
            const std::string an = Local(m_s.substr(as, p - as));
            while (p < n && std::isspace(static_cast<unsigned char>(m_s[p]))) ++p;
            std::string val;
            if (p < n && m_s[p] == '=') {
                ++p;
                while (p < n && std::isspace(static_cast<unsigned char>(m_s[p]))) ++p;
                if (p < n && (m_s[p] == '"' || m_s[p] == '\'')) {
                    const char q = m_s[p++];
                    const size_t ve = m_s.find(q, p);
                    if (ve == std::string::npos) return false;
                    val = Decode(m_s.substr(p, ve - p));
                    p = ve + 1;
                }
            }
            if (!an.empty()) t.attrs.emplace_back(an, val);
            else ++p;
        }
    }
    const std::string& m_s;
    size_t m_pos = 0;
};

inline std::string Escape(const std::string& s)
{
    std::string o;
    o.reserve(s.size() + s.size() / 8);
    for (char c : s) {
        if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '"') o += "&quot;";
        else if (static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\n' && c != '\r') continue;     // in XML 1.0 nicht erlaubt
        else o += c;
    }
    return o;
}

}  // namespace xml

// ---------------------------------------------------------------------------------------------------------------------------------
// DOCX -> RTF
// ---------------------------------------------------------------------------------------------------------------------------------
namespace detail {

struct Fmt {
    int bold = -1, italic = -1, underline = -1, strike = -1;   // -1 = nicht gesetzt (erbt)
    int size = 0;                                               // Halbpunkt, 0 = nicht gesetzt
    std::string font, color;                                    // leer = nicht gesetzt
    int vert = 0;                                               // 1 hoch, 2 tief
};

inline bool OnOff(const xml::Tag& t)
{
    if (!t.Has("val")) return true;
    const std::string v = t.Attr("val");
    return !(v == "0" || v == "false" || v == "off" || v == "none");
}

// Eine Eigenschaft aus w:rPr uebernehmen
inline void ApplyRPr(const xml::Tag& t, Fmt& f)
{
    const std::string& n = t.name;
    if (n == "b") f.bold = OnOff(t) ? 1 : 0;
    else if (n == "i") f.italic = OnOff(t) ? 1 : 0;
    else if (n == "strike" || n == "dstrike") f.strike = OnOff(t) ? 1 : 0;
    else if (n == "u") f.underline = (t.Has("val") && t.Attr("val") == "none") ? 0 : 1;
    else if (n == "sz") { const int v = std::atoi(t.Attr("val").c_str()); if (v > 0 && v < 1000) f.size = v; }
    else if (n == "color") { const std::string v = t.Attr("val"); if (v.size() == 6) f.color = v; else if (v == "auto") f.color = "auto"; }
    else if (n == "rFonts") { std::string v = t.Attr("ascii"); if (v.empty()) v = t.Attr("hAnsi"); if (!v.empty()) f.font = v; }
    else if (n == "vertAlign") { const std::string v = t.Attr("val"); f.vert = v == "superscript" ? 1 : (v == "subscript" ? 2 : 0); }
}

inline Fmt Merge(const Fmt& base, const Fmt& over)
{
    Fmt r = base;
    if (over.bold >= 0) r.bold = over.bold;
    if (over.italic >= 0) r.italic = over.italic;
    if (over.underline >= 0) r.underline = over.underline;
    if (over.strike >= 0) r.strike = over.strike;
    if (over.size) r.size = over.size;
    if (!over.font.empty()) r.font = over.font;
    if (!over.color.empty()) r.color = over.color;
    if (over.vert) r.vert = over.vert;
    return r;
}

inline std::string RtfEscape(const std::string& utf8)
{
    const std::wstring w = codec::Utf8ToWide(utf8);
    std::string o;
    for (wchar_t c : w) {
        const unsigned u = static_cast<unsigned>(c);
        if (c == L'\\') o += "\\\\";
        else if (c == L'{') o += "\\{";
        else if (c == L'}') o += "\\}";
        else if (u == 0x00A0) o += "\\~";
        else if (u == 0x2011) o += "\\_";
        else if (u == 0x00AD) o += "\\-";
        else if (u < 0x20) continue;
        else if (u < 0x80) o += static_cast<char>(c);
        else o += "\\u" + std::to_string(static_cast<short>(u)) + "?";
    }
    return o;
}

struct StyleInfo { Fmt fmt; std::string basedOn; std::string name; };

}  // namespace detail

struct ReadResult {
    bool ok = false;
    std::string rtf;
    int images = 0;           // nicht uebernommene Bilder/Zeichnungen
    std::wstring lossy;       // was der Writer nicht kennt und beim Speichern verloren ginge, z. B. "Bilder und Zeichnungen, Kopf- und Fusszeilen" (leer = nichts)
    std::wstring error;
};

inline ReadResult DocxToRtf(const std::string& bytes)
{
    using namespace detail;
    ReadResult res;
    std::vector<zip::Entry> entries;
    std::string docXml, stylesXml, numberingXml;
    if (!zip::ReadDirectory(bytes, entries)) { res.error = L"Das ist keine gültige DOCX-Datei (kein ZIP-Archiv)."; return res; }
    if (!zip::ReadEntry(bytes, entries, "word/document.xml", docXml, 32u * 1024 * 1024)) { res.error = L"Das ist keine gültige DOCX-Datei (word/document.xml fehlt oder ist beschädigt)."; return res; }
    zip::ReadEntry(bytes, entries, "word/styles.xml", stylesXml, 8u * 1024 * 1024);
    zip::ReadEntry(bytes, entries, "word/numbering.xml", numberingXml, 4u * 1024 * 1024);

    // Formatvorlagen: Standardwerte und je Vorlage (ohne Vererbung ueber mehrere Ebenen ausser basedOn)
    Fmt docDefault;
    std::map<std::string, StyleInfo> styles;
    {
        xml::Tokenizer tk(stylesXml);
        xml::Tag t;
        bool inDefaults = false, inStyle = false, inRPr = false;
        std::string id;
        while (tk.Next(t)) {
            if (t.kind == xml::Tag::Start && t.name == "rPrDefault") inDefaults = true;
            else if (t.kind == xml::Tag::End && t.name == "rPrDefault") inDefaults = false;
            else if (t.kind == xml::Tag::Start && t.name == "style") { inStyle = true; id = t.Attr("styleId"); styles[id].name = id; }
            else if (t.kind == xml::Tag::End && t.name == "style") inStyle = false;
            else if (t.kind == xml::Tag::Start && t.name == "rPr") inRPr = true;
            else if (t.kind == xml::Tag::End && t.name == "rPr") inRPr = false;
            else if (t.kind == xml::Tag::Empty || t.kind == xml::Tag::Start) {
                if (inDefaults && inRPr) ApplyRPr(t, docDefault);
                else if (inStyle && inRPr) ApplyRPr(t, styles[id].fmt);
                else if (inStyle && t.name == "basedOn") styles[id].basedOn = t.Attr("val");
                else if (inStyle && t.name == "name") styles[id].name = t.Attr("val");
            }
        }
    }
    auto styleFmt = [&](const std::string& id) {
        Fmt f;
        std::vector<std::string> chain;
        std::string cur = id;
        for (int depth = 0; depth < 8 && !cur.empty() && styles.count(cur); ++depth) { chain.push_back(cur); cur = styles[cur].basedOn; }
        for (size_t i = chain.size(); i-- > 0;) f = Merge(f, styles[chain[i]].fmt);
        return f;
    };

    // Nummerierung: numId -> Aufzaehlung (true) oder nummeriert (false), je Ebene 0
    std::map<std::string, std::string> numToAbstract;
    std::map<std::string, bool> abstractBullet;
    {
        xml::Tokenizer tk(numberingXml);
        xml::Tag t;
        std::string absId, numId;
        bool inLvl0 = false;
        while (tk.Next(t)) {
            if (t.kind == xml::Tag::Start && t.name == "abstractNum") { absId = t.Attr("abstractNumId"); inLvl0 = false; }
            else if (t.kind == xml::Tag::Start && t.name == "lvl") inLvl0 = t.Attr("ilvl") == "0";
            else if ((t.kind == xml::Tag::Empty || t.kind == xml::Tag::Start) && t.name == "numFmt" && inLvl0) abstractBullet[absId] = t.Attr("val") == "bullet";
            else if (t.kind == xml::Tag::Start && t.name == "num") numId = t.Attr("numId");
            else if ((t.kind == xml::Tag::Empty || t.kind == xml::Tag::Start) && t.name == "abstractNumId" && !numId.empty()) numToAbstract[numId] = t.Attr("val");
        }
    }

    // Tabellen der RTF-Datei
    std::vector<std::string> fonts = { "Calibri" };
    std::vector<std::string> colors;                      // "RRGGBB", Index in RTF = Position + 1
    auto fontIdx = [&](const std::string& name) -> int {
        for (size_t i = 0; i < fonts.size(); ++i) if (fonts[i] == name) return static_cast<int>(i);
        fonts.push_back(name);
        return static_cast<int>(fonts.size() - 1);
    };
    auto colorIdx = [&](const std::string& hex) -> int {
        for (size_t i = 0; i < colors.size(); ++i) if (colors[i] == hex) return static_cast<int>(i + 1);
        colors.push_back(hex);
        return static_cast<int>(colors.size());
    };
    if (!docDefault.font.empty()) fonts[0] = docDefault.font;
    bool usedSymbol = false;

    // Durchlauf
    struct Cell { std::string text; int width = 0; };
    struct Row { std::vector<Cell> cells; };
    std::string body;                                      // Hauptteil
    std::string* out = &body;
    std::vector<int> gridCols;
    std::vector<Row> rows;                                 // Zeilen der aktuellen Tabelle
    int tblDepth = 0;
    bool inRPr = false, inPPr = false, inRun = false, inT = false, inTabs = false, inTc = false, inTcPr = false, skipText = false;
    Fmt runFmt, paraFmtOverride;
    std::string paraStyle, runBody, paraBody;
    std::string align;
    bool listPara = false, numberedPara = false;
    int listLevel = 0;
    std::string listNumId;
    std::map<std::string, int> counters;
    bool pendingPar = false;                               // in einer Zelle: vor dem naechsten Absatz ein \par
    int tcWidth = 0;
    std::string tcText;

    auto emitRun = [&]() {
        if (runBody.empty()) return;
        Fmt f = Merge(Merge(docDefault, styleFmt(paraStyle)), runFmt);
        std::string g = "{";
        if (!f.font.empty()) g += "\\f" + std::to_string(fontIdx(f.font));
        if (f.size) g += "\\fs" + std::to_string(f.size);
        if (!f.color.empty() && f.color != "auto") g += "\\cf" + std::to_string(colorIdx(f.color));
        if (f.bold == 1) g += "\\b";
        if (f.italic == 1) g += "\\i";
        if (f.underline == 1) g += "\\ul";
        if (f.strike == 1) g += "\\strike";
        if (f.vert == 1) g += "\\super"; else if (f.vert == 2) g += "\\sub";
        g += " " + runBody + "}";
        paraBody += g;
        runBody.clear();
    };

    auto beginPara = [&]() {
        paraBody.clear();
        runBody.clear();
        paraStyle.clear();
        align.clear();
        listPara = numberedPara = false;
        listLevel = 0;
        listNumId.clear();
    };

    auto endPara = [&]() {
        emitRun();
        std::string p = "\\pard";
        if (inTc) p += "\\intbl";
        if (align == "center") p += "\\qc"; else if (align == "right" || align == "end") p += "\\qr";
        else if (align == "both" || align == "distribute") p += "\\qj"; else p += "\\ql";
        std::string prefix;
        if (listPara) {
            const int indent = 720 + 360 * listLevel;
            if (numberedPara) {
                int& c = counters[listNumId + ":" + std::to_string(listLevel)];
                ++c;
                prefix = "{" + std::string("\\f0 ") + std::to_string(c) + ".\\tab}";
                p += "\\fi-360\\li" + std::to_string(indent);
            } else {
                usedSymbol = true;
                prefix = "{\\pntext\\f__SYM__\\'B7\\tab}{\\*\\pn\\pnlvlblt\\pnf__SYM__\\pnindent0{\\pntxtb\\'B7}}";
                p += "\\fi-360\\li" + std::to_string(indent);
            }
        }
        // Ueberschrift ohne eigene Formatierung: fett, wenn die Vorlage so heisst und keine Groesse bekannt ist
        p += " " + prefix + paraBody;
        if (inTc) {
            if (pendingPar) *out += "\\par ";
            *out += p;
            pendingPar = true;
        } else {
            *out += p + "\\par\n";
        }
    };

    xml::Tokenizer tk(docXml);
    xml::Tag t;
    bool started = false;
    while (tk.Next(t)) {
        const std::string& n = t.name;
        if (t.kind == xml::Tag::Text) {
            if (inT && !skipText) runBody += RtfEscape(t.text);
            continue;
        }
        const bool start = t.kind == xml::Tag::Start, end = t.kind == xml::Tag::End, empty = t.kind == xml::Tag::Empty;
        if (n == "body" && start) started = true;
        if (!started) continue;
        if (n == "tbl" && start) { ++tblDepth; if (tblDepth == 1) { rows.clear(); gridCols.clear(); } }
        else if (n == "tbl" && end) {
            if (tblDepth == 1 && !rows.empty()) {
                for (const Row& r : rows) {
                    std::string row = "\\trowd\\trgaph108\\trleft0";
                    int x = 0;
                    for (size_t ci = 0; ci < r.cells.size(); ++ci) {
                        int w = r.cells[ci].width;
                        if (w <= 0) w = ci < gridCols.size() ? gridCols[ci] : 2000;
                        x += w;
                        row += "\\clbrdrt\\brdrs\\brdrw10\\clbrdrl\\brdrs\\brdrw10\\clbrdrb\\brdrs\\brdrw10\\clbrdrr\\brdrs\\brdrw10\\cellx" + std::to_string(x);
                    }
                    row += "\n";
                    for (const Cell& c : r.cells) row += c.text + "\\cell\n";
                    row += "\\row\n";
                    *out += row;
                }
                *out += "\\pard\\par\n";
            }
            if (tblDepth > 0) --tblDepth;
        }
        else if (n == "gridCol" && tblDepth == 1) gridCols.push_back(std::atoi(t.Attr("w").c_str()));
        else if (n == "tr" && start && tblDepth == 1) rows.emplace_back();
        else if (n == "tc" && start && tblDepth == 1) { inTc = true; tcText.clear(); tcWidth = 0; pendingPar = false; out = &tcText; }
        else if (n == "tc" && end && tblDepth == 1) {
            inTc = false;
            out = &body;
            if (!rows.empty()) { Cell c; c.text = tcText; c.width = tcWidth; rows.back().cells.push_back(c); }
            if (tcText.empty() && !rows.empty() && !rows.back().cells.empty()) rows.back().cells.back().text = "\\pard\\intbl\\ql ";
        }
        else if (n == "tcPr" && start) inTcPr = true;
        else if (n == "tcPr" && end) inTcPr = false;
        else if (n == "tcW" && inTcPr && tblDepth == 1) tcWidth = std::atoi(t.Attr("w").c_str());
        else if (n == "p" && (start || empty)) {
            beginPara();
            if (empty) endPara();
        }
        else if (n == "p" && end) endPara();
        else if (n == "pPr" && start) inPPr = true;
        else if (n == "pPr" && end) inPPr = false;
        else if (inPPr && !inRPr && (empty || start) && n == "jc") align = t.Attr("val");
        else if (inPPr && !inRPr && (empty || start) && n == "pStyle") paraStyle = t.Attr("val");
        else if (inPPr && (empty || start) && n == "numId") {
            listNumId = t.Attr("val");
            if (listNumId != "0" && !listNumId.empty()) {
                listPara = true;
                const auto a = numToAbstract.find(listNumId);
                numberedPara = !(a != numToAbstract.end() && abstractBullet[a->second]);
            }
        }
        else if (inPPr && (empty || start) && n == "ilvl") listLevel = std::clamp(std::atoi(t.Attr("val").c_str()), 0, 8);
        else if (n == "tabs" && start) inTabs = true;
        else if (n == "tabs" && end) inTabs = false;
        else if (n == "r" && start) { inRun = true; runFmt = Fmt{}; runBody.clear(); }
        else if (n == "r" && end) { emitRun(); inRun = false; }
        else if (n == "rPr" && start) inRPr = true;
        else if (n == "rPr" && end) inRPr = false;
        else if (inRPr && inRun && (empty || start)) ApplyRPr(t, runFmt);
        else if (inRun && !inRPr && n == "t" && start) { inT = true; skipText = false; }
        else if (inRun && !inRPr && n == "t" && end) inT = false;
        else if (inRun && !inRPr && n == "instrText") skipText = true;
        else if (inRun && !inRPr && !inTabs && !inPPr && n == "tab" && (empty || start)) runBody += "\\tab ";
        else if (inRun && !inRPr && n == "br" && (empty || start)) runBody += t.Attr("type") == "page" ? "\\page " : "\\line ";
        else if (inRun && !inRPr && n == "cr" && (empty || start)) runBody += "\\line ";
        else if (n == "drawing" || n == "pict") { if (start || empty) ++res.images; }
    }

    // Kopf zusammenbauen
    std::string head = "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1{\\fonttbl";
    for (size_t i = 0; i < fonts.size(); ++i) head += "{\\f" + std::to_string(i) + "\\fnil\\fcharset0 " + fonts[i] + ";}";
    size_t symIdx = fonts.size();
    if (usedSymbol) head += "{\\f" + std::to_string(symIdx) + "\\fnil\\fcharset2 Symbol;}";
    head += "}{\\colortbl ;";
    for (const std::string& c : colors) {
        auto hexv = [&](size_t i) { return std::to_string(std::strtol(c.substr(i, 2).c_str(), nullptr, 16)); };
        head += "\\red" + hexv(0) + "\\green" + hexv(2) + "\\blue" + hexv(4) + ";";
    }
    head += "}\\viewkind4\\uc1 ";
    std::string all = head + body;
    for (size_t p; (p = all.find("__SYM__")) != std::string::npos;) all.replace(p, 7, std::to_string(symIdx));
    all += "}";
    res.rtf = all;
    res.ok = true;
    {
        bool media = false, hdr = false, cmt = false;
        for (const zip::Entry& e : entries) {
            std::string n = e.name;
            for (char& ch : n) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
            if (n.compare(0, 11, "word/media/") == 0) media = true;
            else if (n.compare(0, 11, "word/header") == 0 || n.compare(0, 11, "word/footer") == 0) hdr = true;
            else if (n.compare(0, 13, "word/comments") == 0) cmt = true;
        }
        std::vector<std::wstring> lost;
        if (res.images > 0 || media) lost.push_back(L"Bilder und Zeichnungen");
        if (hdr) lost.push_back(L"Kopf- und Fu\u00DFzeilen");
        if (docXml.find("footnoteReference") != std::string::npos || docXml.find("endnoteReference") != std::string::npos) lost.push_back(L"Fu\u00DFnoten");
        if (cmt) lost.push_back(L"Kommentare");
        for (size_t i = 0; i < lost.size(); ++i) res.lossy += (i ? L", " : L"") + lost[i];
    }
    return res;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// RTF -> DOCX
// ---------------------------------------------------------------------------------------------------------------------------------
namespace detail {

inline unsigned Cp1252(unsigned char c)
{
    static const unsigned hi[32] = { 0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
                                     0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178 };
    return (c >= 0x80 && c < 0xA0) ? hi[c - 0x80] : c;
}

struct RunF {
    bool b = false, i = false, u = false, s = false;
    int size = 0;                 // Halbpunkt (0 = Standard)
    int font = -1;                // Index in der RTF-Schriftart-Tabelle
    int color = 0;                // Index in der Farbtabelle (0 = automatisch)
    int vert = 0;
    bool operator==(const RunF& o) const { return b == o.b && i == o.i && u == o.u && s == o.s && size == o.size && font == o.font && color == o.color && vert == o.vert; }
};

struct Run { RunF f; std::string text; };      // text: UTF-8 mit \t fuer Tabulator, \n fuer Zeilenumbruch, \f fuer Seitenumbruch

struct Para {
    std::vector<Run> runs;
    char align = 'l';
    int list = 0;                 // 0 keine, 1 Aufzaehlung, 2 nummeriert
    int indentLeft = 0, firstLine = 0;
};

struct CellDoc { std::vector<Para> paras; };
struct RowDoc { std::vector<CellDoc> cells; std::vector<int> cellx; };
struct Block { bool table = false; Para para; std::vector<RowDoc> rows; };

}  // namespace detail

struct WriteResult {
    bool ok = false;
    std::string bytes;
    std::wstring error;
};

inline WriteResult RtfToDocx(const std::string& rtf)
{
    using namespace detail;
    WriteResult res;
    if (rtf.compare(0, 5, "{\\rtf") != 0) { res.error = L"Das ist kein RTF-Text."; return res; }

    std::vector<std::string> fonts;
    std::vector<std::string> colors = { "" };       // 0 = automatisch
    std::vector<Block> blocks;

    struct G { RunF f; bool skip = false; int uc = 1; };
    std::vector<G> stack;
    G cur;
    Para para;                                      // laufender Absatz
    bool paraHasContent = false;
    bool inCell = false;                            // \intbl gesehen
    std::vector<CellDoc> rowCells;
    CellDoc cellDoc;
    std::vector<int> rowCellx;
    bool rowOpen = false;
    std::string text;                               // laufender Text der laufenden Run
    RunF textFmt;
    // Tabellen mit Farben und Schriftarten: Zustand der Tabellen
    enum class Dest { None, FontTbl, ColorTbl } dest = Dest::None;
    int fontEntry = -1;
    std::string fontName;
    int colRed = -1, colGreen = -1, colBlue = -1;
    int depthAtDest = 0;
    bool bulletPending = false, numberPending = false;
    int skipUc = 0;                                 // nach \uN folgende Ersatzzeichen ueberspringen

    auto flushText = [&]() {
        if (text.empty()) return;
        if (!para.runs.empty() && para.runs.back().f == textFmt) para.runs.back().text += text;
        else para.runs.push_back({ textFmt, text });
        paraHasContent = true;
        text.clear();
    };
    auto addText = [&](unsigned cp) {
        if (cur.skip || dest != Dest::None) return;
        if (!(textFmt == cur.f)) { flushText(); textFmt = cur.f; }
        xml::AppendUtf8(text, cp);
    };
    auto addSpecial = [&](char c) {
        if (cur.skip || dest != Dest::None) return;
        if (!(textFmt == cur.f)) { flushText(); textFmt = cur.f; }
        text += c;
    };
    auto endParagraph = [&](bool force) {
        flushText();
        if (paraHasContent || force) {
            if (inCell) cellDoc.paras.push_back(para);
            else { Block b; b.para = para; blocks.push_back(std::move(b)); }
        }
        const char al = para.align;
        para = Para{};
        para.align = al;
        paraHasContent = false;
    };

    size_t i = 0;
    const size_t n = rtf.size();
    while (i < n) {
        const char c = rtf[i];
        if (c == '{') {
            stack.push_back(cur);
            ++i;
            // Ignorierbares Ziel {\*\...} ueberspringen, ausser Aufzaehlungs-Angaben
            if (i + 1 < n && rtf[i] == '\\' && rtf[i + 1] == '*') {
                if (rtf.compare(i, 5, "\\*\\pn") == 0) {
                    const std::string seg = rtf.substr(i, std::min<size_t>(120, n - i));
                    if (seg.find("\\pnlvlblt") != std::string::npos) { bulletPending = true; numberPending = false; para.list = 1; }
                    else if (seg.find("\\pnlvlbody") != std::string::npos || seg.find("\\pndec") != std::string::npos) { numberPending = true; bulletPending = false; para.list = 2; }
                }
                cur.skip = true;
            }
            continue;
        }
        if (c == '}') {
            flushText();
            if (dest != Dest::None && static_cast<int>(stack.size()) <= depthAtDest) {
                if (dest == Dest::FontTbl && fontEntry >= 0) {}
                dest = Dest::None;
            }
            if (!stack.empty()) { cur = stack.back(); stack.pop_back(); }
            ++i;
            continue;
        }
        if (c == '\\') {
            ++i;
            if (i >= n) break;
            const char d = rtf[i];
            if (d == '\\' || d == '{' || d == '}') { addText(static_cast<unsigned char>(d)); ++i; continue; }
            if (d == '~') { addText(0x00A0); ++i; continue; }
            if (d == '_') { addText(0x2011); ++i; continue; }
            if (d == '-') { ++i; continue; }
            if (d == '*') { cur.skip = true; ++i; continue; }
            if (d == '\'') {
                if (i + 2 < n) {
                    const unsigned v = static_cast<unsigned>(std::strtoul(rtf.substr(i + 1, 2).c_str(), nullptr, 16));
                    if (skipUc > 0) --skipUc; else addText(Cp1252(static_cast<unsigned char>(v)));
                }
                i += 3;
                continue;
            }
            if (d == '\n' || d == '\r') { ++i; continue; }
            if (!std::isalpha(static_cast<unsigned char>(d))) { ++i; continue; }
            size_t ws = i;
            while (i < n && std::isalpha(static_cast<unsigned char>(rtf[i]))) ++i;
            const std::string word = rtf.substr(ws, i - ws);
            bool hasNum = false;
            long num = 0;
            if (i < n && (rtf[i] == '-' || std::isdigit(static_cast<unsigned char>(rtf[i])))) {
                const size_t ns = i;
                if (rtf[i] == '-') ++i;
                while (i < n && std::isdigit(static_cast<unsigned char>(rtf[i]))) ++i;
                num = std::strtol(rtf.substr(ns, i - ns).c_str(), nullptr, 10);
                hasNum = true;
            }
            if (i < n && rtf[i] == ' ') ++i;                              // Leerzeichen hinter dem Steuerwort gehoert dazu

            // Schriftart- und Farbtabelle
            if (word == "fonttbl") { dest = Dest::FontTbl; depthAtDest = static_cast<int>(stack.size()); continue; }
            if (word == "colortbl") { dest = Dest::ColorTbl; depthAtDest = static_cast<int>(stack.size()); colRed = colGreen = colBlue = -1; continue; }
            if (word == "stylesheet" || word == "info" || word == "pict" || word == "header" || word == "footer" || word == "headerl" || word == "headerr" ||
                word == "footerl" || word == "footerr" || word == "footnote" || word == "object" || word == "fldinst" || word == "listtable" || word == "listoverridetable" ||
                word == "revtbl" || word == "generator" || word == "datafield" || word == "private") { cur.skip = true; continue; }
            if (word == "pntext") {                                       // Aufzaehlungszeichen steht schon in \pn: Absatz als Liste merken
                if (!cur.skip) { if (bulletPending) para.list = 1; else if (numberPending) para.list = 2; }
                cur.skip = true;
                continue;
            }
            if (cur.skip) continue;
            if (dest == Dest::FontTbl) {
                if (word == "f" && hasNum) { fontEntry = static_cast<int>(num); fontName.clear(); if (static_cast<int>(fonts.size()) <= fontEntry) fonts.resize(static_cast<size_t>(fontEntry) + 1); }
                continue;
            }
            if (dest == Dest::ColorTbl) {
                if (word == "red") colRed = static_cast<int>(num); else if (word == "green") colGreen = static_cast<int>(num); else if (word == "blue") colBlue = static_cast<int>(num);
                continue;
            }
            if (word == "par" || word == "sect") { endParagraph(true); }
            else if (word == "pard") { para.align = 'l'; para.list = 0; para.indentLeft = para.firstLine = 0; inCell = inCell && rowOpen; bulletPending = numberPending = false; }
            else if (word == "plain") { cur.f = RunF{}; }
            else if (word == "line") addSpecial('\n');
            else if (word == "tab") addSpecial('\t');
            else if (word == "page") addSpecial('\f');
            else if (word == "emdash") addText(0x2014);
            else if (word == "endash") addText(0x2013);
            else if (word == "bullet") addText(0x2022);
            else if (word == "lquote") addText(0x2018);
            else if (word == "rquote") addText(0x2019);
            else if (word == "ldblquote") addText(0x201C);
            else if (word == "rdblquote") addText(0x201D);
            else if (word == "u" && hasNum) {
                addText(static_cast<unsigned>(static_cast<unsigned short>(num)));
                skipUc = cur.uc;
            }
            else if (word == "uc" && hasNum) cur.uc = static_cast<int>(num);
            else if (word == "b") cur.f.b = !hasNum || num != 0;
            else if (word == "i") cur.f.i = !hasNum || num != 0;
            else if (word == "ul" || word == "uld" || word == "uldb" || word == "ulw") cur.f.u = !hasNum || num != 0;
            else if (word == "ulnone") cur.f.u = false;
            else if (word == "strike" || word == "striked") cur.f.s = !hasNum || num != 0;
            else if (word == "super") cur.f.vert = 1;
            else if (word == "sub") cur.f.vert = 2;
            else if (word == "nosupersub") cur.f.vert = 0;
            else if (word == "fs" && hasNum) cur.f.size = static_cast<int>(num);
            else if (word == "f" && hasNum) cur.f.font = static_cast<int>(num);
            else if (word == "cf" && hasNum) cur.f.color = static_cast<int>(num);
            else if (word == "ql") para.align = 'l';
            else if (word == "qc") para.align = 'c';
            else if (word == "qr") para.align = 'r';
            else if (word == "qj") para.align = 'j';
            else if (word == "li" && hasNum) para.indentLeft = static_cast<int>(num);
            else if (word == "fi" && hasNum) para.firstLine = static_cast<int>(num);
            else if (word == "intbl") { inCell = true; }
            else if (word == "trowd") { rowOpen = true; inCell = true; rowCells.clear(); rowCellx.clear(); cellDoc = CellDoc{}; }
            else if (word == "cellx" && hasNum) rowCellx.push_back(static_cast<int>(num));
            else if (word == "cell") {
                if (!paraHasContent && cellDoc.paras.empty() && text.empty()) { /* leere Zelle */ }
                flushText();
                if (paraHasContent || cellDoc.paras.empty()) { cellDoc.paras.push_back(para); }
                para = Para{}; paraHasContent = false;
                rowCells.push_back(cellDoc);
                cellDoc = CellDoc{};
            }
            else if (word == "row") {
                flushText();
                RowDoc r;
                r.cells = rowCells;
                r.cellx = rowCellx;
                if (!blocks.empty() && blocks.back().table) blocks.back().rows.push_back(r);
                else { Block b; b.table = true; b.rows.push_back(r); blocks.push_back(std::move(b)); }
                rowCells.clear(); rowCellx.clear(); cellDoc = CellDoc{};
                rowOpen = false; inCell = false;
                para = Para{}; paraHasContent = false;
            }
            continue;
        }
        if (c == '\r' || c == '\n') { ++i; continue; }
        // Klartext
        if (dest == Dest::FontTbl && !cur.skip) {
            if (c == ';') { if (fontEntry >= 0 && fontEntry < static_cast<int>(fonts.size())) { std::string nm = fontName; while (!nm.empty() && nm.back() == ' ') nm.pop_back(); while (!nm.empty() && nm.front() == ' ') nm.erase(nm.begin()); if (!nm.empty()) fonts[static_cast<size_t>(fontEntry)] = nm; } fontName.clear(); }
            else fontName += c;
            ++i;
            continue;
        }
        if (dest == Dest::ColorTbl && !cur.skip) {
            if (c == ';') {
                if (colRed >= 0) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "%02X%02X%02X", std::clamp(colRed, 0, 255), std::clamp(colGreen, 0, 255), std::clamp(colBlue, 0, 255));
                    colors.push_back(buf);
                } else if (colors.size() == 1 && false) {}
                colRed = colGreen = colBlue = -1;
            }
            ++i;
            continue;
        }
        if (cur.skip) { ++i; continue; }
        if (skipUc > 0) { --skipUc; ++i; continue; }
        // UTF-8-Bytes im Klartext sind in RTF nicht vorgesehen; Bytes ueber Latin-1 uebernehmen
        addText(Cp1252(static_cast<unsigned char>(c)));
        ++i;
    }
    endParagraph(false);

    // --- DOCX erzeugen ---
    auto rfont = [&](int idx) -> std::string {
        if (idx >= 0 && idx < static_cast<int>(fonts.size()) && !fonts[static_cast<size_t>(idx)].empty()) return fonts[static_cast<size_t>(idx)];
        return std::string();
    };
    bool usesBullets = false, usesNumbers = false;
    auto runXml = [&](const Run& r) {
        std::string x;
        const RunF& f = r.f;
        std::string pr;
        const std::string fn = rfont(f.font);
        if (!fn.empty() && fn != "Symbol") { const std::string e = xml::Escape(fn); pr += "<w:rFonts w:ascii=\"" + e + "\" w:hAnsi=\"" + e + "\" w:cs=\"" + e + "\"/>"; }
        if (f.b) pr += "<w:b/><w:bCs/>";
        if (f.i) pr += "<w:i/><w:iCs/>";
        if (f.s) pr += "<w:strike/>";
        if (f.color > 0 && f.color < static_cast<int>(colors.size())) pr += "<w:color w:val=\"" + colors[static_cast<size_t>(f.color)] + "\"/>";
        if (f.size > 0) pr += "<w:sz w:val=\"" + std::to_string(f.size) + "\"/><w:szCs w:val=\"" + std::to_string(f.size) + "\"/>";
        if (f.u) pr += "<w:u w:val=\"single\"/>";
        if (f.vert == 1) pr += "<w:vertAlign w:val=\"superscript\"/>"; else if (f.vert == 2) pr += "<w:vertAlign w:val=\"subscript\"/>";
        // Text in Stuecke zerlegen: Tabulator, Zeilenumbruch, Seitenumbruch
        std::string seg;
        auto flush = [&]() {
            if (seg.empty()) return;
            x += "<w:r>" + (pr.empty() ? std::string() : "<w:rPr>" + pr + "</w:rPr>") + "<w:t xml:space=\"preserve\">" + xml::Escape(seg) + "</w:t></w:r>";
            seg.clear();
        };
        for (char ch : r.text) {
            if (ch == '\t' || ch == '\n' || ch == '\f') {
                flush();
                x += "<w:r>" + (pr.empty() ? std::string() : "<w:rPr>" + pr + "</w:rPr>") +
                     (ch == '\t' ? "<w:tab/>" : (ch == '\n' ? "<w:br/>" : "<w:br w:type=\"page\"/>")) + "</w:r>";
            } else seg += ch;
        }
        flush();
        return x;
    };
    auto paraXml = [&](const Para& p) {
        std::string ppr;
        if (p.list == 1) { ppr += "<w:numPr><w:ilvl w:val=\"0\"/><w:numId w:val=\"1\"/></w:numPr>"; usesBullets = true; }
        else if (p.list == 2) { ppr += "<w:numPr><w:ilvl w:val=\"0\"/><w:numId w:val=\"2\"/></w:numPr>"; usesNumbers = true; }
        if (p.list == 0 && (p.indentLeft || p.firstLine)) {
            ppr += "<w:ind w:left=\"" + std::to_string(std::max(0, p.indentLeft)) + "\"" + (p.firstLine < 0 ? " w:hanging=\"" + std::to_string(-p.firstLine) + "\"" : p.firstLine > 0 ? " w:firstLine=\"" + std::to_string(p.firstLine) + "\"" : "") + "/>";
        }
        if (p.align == 'c') ppr += "<w:jc w:val=\"center\"/>"; else if (p.align == 'r') ppr += "<w:jc w:val=\"right\"/>"; else if (p.align == 'j') ppr += "<w:jc w:val=\"both\"/>";
        std::string x = "<w:p>" + (ppr.empty() ? std::string() : "<w:pPr>" + ppr + "</w:pPr>");
        for (const Run& r : p.runs) x += runXml(r);
        return x + "</w:p>";
    };

    std::string bodyXml;
    for (const Block& b : blocks) {
        if (!b.table) { bodyXml += paraXml(b.para); continue; }
        size_t maxCells = 0;
        for (const RowDoc& r : b.rows) maxCells = std::max(maxCells, r.cells.size());
        if (maxCells == 0) continue;
        std::vector<int> widths(maxCells, 2000);
        for (const RowDoc& r : b.rows)
            if (r.cellx.size() == r.cells.size()) {
                int prev = 0;
                for (size_t ci = 0; ci < r.cellx.size(); ++ci) { widths[ci] = std::max(300, r.cellx[ci] - prev); prev = r.cellx[ci]; }
                break;
            }
        std::string tbl = "<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/><w:tblBorders>";
        for (const char* e : { "top", "left", "bottom", "right", "insideH", "insideV" }) tbl += std::string("<w:") + e + " w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"808080\"/>";
        tbl += "</w:tblBorders></w:tblPr><w:tblGrid>";
        for (int w : widths) tbl += "<w:gridCol w:w=\"" + std::to_string(w) + "\"/>";
        tbl += "</w:tblGrid>";
        for (const RowDoc& r : b.rows) {
            tbl += "<w:tr>";
            for (size_t ci = 0; ci < maxCells; ++ci) {
                tbl += "<w:tc><w:tcPr><w:tcW w:w=\"" + std::to_string(widths[ci]) + "\" w:type=\"dxa\"/></w:tcPr>";
                if (ci < r.cells.size() && !r.cells[ci].paras.empty()) for (const Para& p : r.cells[ci].paras) tbl += paraXml(p);
                else tbl += "<w:p/>";
                tbl += "</w:tc>";
            }
            tbl += "</w:tr>";
        }
        bodyXml += tbl + "</w:tbl><w:p/>";
    }
    if (bodyXml.empty()) bodyXml = "<w:p/>";

    const std::string W = "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"";
    const std::string decl = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    const std::string document = decl + "<w:document " + W + "><w:body>" + bodyXml +
        "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/><w:pgMar w:top=\"1134\" w:right=\"1134\" w:bottom=\"1134\" w:left=\"1134\" w:header=\"708\" w:footer=\"708\" w:gutter=\"0\"/></w:sectPr></w:body></w:document>";
    const std::string styles = decl + "<w:styles " + W + "><w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\" w:cs=\"Calibri\"/><w:sz w:val=\"22\"/><w:szCs w:val=\"22\"/>"
        "<w:lang w:val=\"de-DE\"/></w:rPr></w:rPrDefault><w:pPrDefault><w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/></w:pPr></w:pPrDefault></w:docDefaults>"
        "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/><w:qFormat/></w:style></w:styles>";
    std::string numbering = decl + "<w:numbering " + W + ">"
        "<w:abstractNum w:abstractNumId=\"0\"><w:multiLevelType w:val=\"hybridMultilevel\"/><w:lvl w:ilvl=\"0\"><w:start w:val=\"1\"/><w:numFmt w:val=\"bullet\"/><w:lvlText w:val=\"&#8226;\"/><w:lvlJc w:val=\"left\"/>"
        "<w:pPr><w:ind w:left=\"720\" w:hanging=\"360\"/></w:pPr><w:rPr><w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\" w:hint=\"default\"/></w:rPr></w:lvl></w:abstractNum>"
        "<w:abstractNum w:abstractNumId=\"1\"><w:multiLevelType w:val=\"hybridMultilevel\"/><w:lvl w:ilvl=\"0\"><w:start w:val=\"1\"/><w:numFmt w:val=\"decimal\"/><w:lvlText w:val=\"%1.\"/><w:lvlJc w:val=\"left\"/>"
        "<w:pPr><w:ind w:left=\"720\" w:hanging=\"360\"/></w:pPr></w:lvl></w:abstractNum>"
        "<w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num><w:num w:numId=\"2\"><w:abstractNumId w:val=\"1\"/></w:num></w:numbering>";
    (void)usesBullets; (void)usesNumbers;
    const std::string contentTypes = decl + "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
        "<Override PartName=\"/word/numbering.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml\"/></Types>";
    const std::string rels = decl + "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/></Relationships>";
    const std::string docRels = decl + "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering\" Target=\"numbering.xml\"/></Relationships>";
    res.bytes = zip::Write({ { "[Content_Types].xml", contentTypes }, { "_rels/.rels", rels }, { "word/document.xml", document },
                             { "word/_rels/document.xml.rels", docRels }, { "word/styles.xml", styles }, { "word/numbering.xml", numbering } });
    res.ok = true;
    return res;
}

}  // namespace docx
