#pragma once
// Excel-Dateien (.xlsx) fuer Sheets lesen und schreiben. Reine Logik ohne Win32 (nutzt Zip.h und Codec.h), damit sie getrennt testbar bleibt.
// Gelesen wird das erste Tabellenblatt: Zahlen, Text (auch Rich-Text), Wahrheitswerte, Fehler, Formeln samt Ergebnis, Datum/Uhrzeit (als Text),
// Prozent-/Waehrungsformat und einfarbige Hintergruenge. Was Sheets nicht kann (weitere Blaetter, Zellen ausserhalb A-Z x 1-100, Formeln mit
// unbekannten Funktionen oder anderen Blaettern) wird gezaehlt; Formeln, die Sheets nicht rechnen kann, behalten ihr gespeichertes Ergebnis als Wert.
// Geschrieben wird eine schlichte, von Excel und LibreOffice lesbare Datei mit einem Blatt, Formeln (englische Namen, Komma als Trenner) mit
// gespeichertem Ergebnis, Zahlen-, Prozent- und Waehrungsformat und Hintergrundfarben.
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Codec.h"
#include "Sheet.h"
#include "Zip.h"

namespace xlsx {

struct ReadResult {
    bool ok = false;
    std::wstring error;
    int droppedCells = 0;   // Zellen mit Inhalt ausserhalb des Rasters
    int extraSheets = 0;    // weitere Tabellenblaetter (nicht uebernommen)
    int replacedFormulas = 0;   // Formeln, die Sheets nicht rechnen kann: gespeichertes Ergebnis statt Formel
    int dateCells = 0;      // Datums-/Zeitzellen (als Text uebernommen)
};

namespace detail {

// ---------- kleiner, nachsichtiger XML-Leser ----------
struct Tag {
    std::string name;      // ohne Namensraum-Praefix
    std::string attrs;     // Rohtext der Attribute
    bool closing = false, selfClosing = false;
    size_t begin = 0, end = 0;   // Position von '<' und hinter '>'
};

inline bool NextTag(const std::string& x, size_t pos, Tag& t)
{
    for (;;) {
        const size_t lt = x.find('<', pos);
        if (lt == std::string::npos) return false;
        if (x.compare(lt, 4, "<!--") == 0) { const size_t e = x.find("-->", lt); if (e == std::string::npos) return false; pos = e + 3; continue; }
        if (x.compare(lt, 2, "<?") == 0) { const size_t e = x.find("?>", lt); if (e == std::string::npos) return false; pos = e + 2; continue; }
        if (x.compare(lt, 9, "<![CDATA[") == 0) { const size_t e = x.find("]]>", lt); if (e == std::string::npos) return false; pos = e + 3; continue; }
        // Ende des Tags: erstes '>' ausserhalb von Anfuehrungszeichen
        size_t i = lt + 1;
        char q = 0;
        for (; i < x.size(); ++i) {
            const char ch = x[i];
            if (q) { if (ch == q) q = 0; }
            else if (ch == '"' || ch == '\'') q = ch;
            else if (ch == '>') break;
        }
        if (i >= x.size()) return false;
        t = Tag{};
        t.begin = lt;
        t.end = i + 1;
        size_t a = lt + 1;
        if (a < i && x[a] == '/') { t.closing = true; ++a; }
        size_t b = a;
        while (b < i && x[b] != ' ' && x[b] != '\t' && x[b] != '\r' && x[b] != '\n' && x[b] != '/') ++b;
        std::string nm = x.substr(a, b - a);
        const size_t colon = nm.find(':');
        if (colon != std::string::npos) nm.erase(0, colon + 1);
        t.name = nm;
        t.selfClosing = i > lt + 1 && x[i - 1] == '/';
        t.attrs = x.substr(b, i - b);
        return true;
    }
}

inline std::string Attr(const std::string& attrs, const char* name)
{
    const std::string key = std::string(name) + "=";
    size_t p = 0;
    while ((p = attrs.find(key, p)) != std::string::npos) {
        const bool boundary = p == 0 || attrs[p - 1] == ' ' || attrs[p - 1] == '\t' || attrs[p - 1] == '\r' || attrs[p - 1] == '\n' || attrs[p - 1] == ':';
        const size_t v = p + key.size();
        if (boundary && v < attrs.size() && (attrs[v] == '"' || attrs[v] == '\'')) {
            const size_t e = attrs.find(attrs[v], v + 1);
            if (e != std::string::npos) return attrs.substr(v + 1, e - v - 1);
        }
        p += key.size();
    }
    return std::string();
}

inline void AppendUtf8(std::string& out, unsigned cp)
{
    if (cp < 0x80) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) { out.push_back(static_cast<char>(0xE0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    else { out.push_back(static_cast<char>(0xF0 | (cp >> 18))); out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
}

inline std::wstring Unescape(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { out.push_back(s[i]); continue; }
        const size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 10) { out.push_back('&'); continue; }
        const std::string ent = s.substr(i + 1, semi - i - 1);
        if (ent == "amp") out.push_back('&');
        else if (ent == "lt") out.push_back('<');
        else if (ent == "gt") out.push_back('>');
        else if (ent == "quot") out.push_back('"');
        else if (ent == "apos") out.push_back('\'');
        else if (!ent.empty() && ent[0] == '#') {
            const bool hex = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X');
            const unsigned cp = static_cast<unsigned>(std::strtoul(ent.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10));
            if (cp > 0 && cp < 0x110000) AppendUtf8(out, cp);
        }
        else { out.push_back('&'); continue; }
        i = semi;
    }
    return codec::Utf8ToWide(out);
}

inline std::string Escape(const std::wstring& w)
{
    const std::string u = codec::WideToUtf8(w);
    std::string out;
    for (char ch : u) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20 && ch != '\t' && ch != '\n' && ch != '\r') break;   // in XML 1.0 nicht erlaubt
                out.push_back(ch);
        }
    }
    return out;
}

// Text zwischen dem Ende des Tags t und dem naechsten '<'
inline std::string TextAfter(const std::string& x, const Tag& t)
{
    const size_t lt = x.find('<', t.end);
    return x.substr(t.end, (lt == std::string::npos ? x.size() : lt) - t.end);
}

// ---------- Hilfen ----------
inline std::string Lower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32); return s; }

inline bool ParseCellRef(const std::string& ref, int& row, int& col, bool& inGrid)
{
    size_t i = 0;
    int colNum = 0;
    while (i < ref.size() && ((ref[i] >= 'A' && ref[i] <= 'Z') || (ref[i] >= 'a' && ref[i] <= 'z'))) {
        colNum = colNum * 26 + (ref[i] & 0x1F);
        if (colNum > 20000) return false;
        ++i;
    }
    if (i == 0 || i == ref.size()) return false;
    long rowNum = 0;
    for (size_t k = i; k < ref.size(); ++k) {
        if (ref[k] < '0' || ref[k] > '9') return false;
        rowNum = rowNum * 10 + (ref[k] - '0');
        if (rowNum > 10000000) return false;
    }
    if (rowNum < 1) return false;
    col = colNum - 1;
    row = static_cast<int>(rowNum) - 1;
    inGrid = col >= 0 && col < sheet::kCols && row >= 0 && row < sheet::kRows;
    return true;
}

inline std::wstring NumberText(double d)
{
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.15g", d);
    return std::wstring(buf, buf + std::char_traits<char>::length(buf));
}

// Seriennummer (Tage seit 1899-12-30) -> Text
inline std::wstring SerialToText(double serial, bool timeOnly, bool dateOnly)
{
    if (!(serial >= 0) || serial > 2958465) return NumberText(serial);
    long days = static_cast<long>(serial);
    double frac = serial - static_cast<double>(days);
    long secs = static_cast<long>(frac * 86400.0 + 0.5);
    if (secs >= 86400) { secs -= 86400; ++days; }
    wchar_t buf[48];
    if (timeOnly) { std::swprintf(buf, 48, L"%02ld:%02ld", secs / 3600, (secs / 60) % 60); return buf; }
    if (days > 0 && days < 61) ++days;        // vor dem 1.3.1900 zaehlt Excel einen 29.2.1900 mit, den es nie gab
    // Seriennummer (Tage seit 1899-12-30) -> Datum (Algorithmus von Howard Hinnant); 25569 = Seriennummer des 1.1.1970
    const long z = days - 25569 + 719468;     // 719468 = Tage von 0000-03-01 bis 1970-01-01
    const long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = static_cast<long>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) ++y;
    if (dateOnly || secs == 0) std::swprintf(buf, 48, L"%02u.%02u.%04ld", d, m, y);
    else std::swprintf(buf, 48, L"%02u.%02u.%04ld %02ld:%02ld", d, m, y, secs / 3600, (secs / 60) % 60);
    return buf;
}

enum class NumKind { General, Percent, Currency, Date, Time, DateTime };

inline NumKind KindOfFormat(int id, const std::string& code)
{
    if (id == 9 || id == 10) return NumKind::Percent;
    if ((id >= 5 && id <= 8) || (id >= 41 && id <= 44)) return NumKind::Currency;
    if ((id >= 14 && id <= 17) || id == 22 || (id >= 27 && id <= 31) || (id >= 34 && id <= 36) || (id >= 50 && id <= 58)) return id == 22 ? NumKind::DateTime : NumKind::Date;
    if ((id >= 18 && id <= 21) || (id >= 45 && id <= 47)) return NumKind::Time;
    if (code.empty()) return NumKind::General;
    // benutzerdefiniert: Text in Anfuehrungszeichen, [..] und Escapes ausblenden
    std::string c;
    bool q = false, br = false;
    for (size_t i = 0; i < code.size(); ++i) {
        const char ch = code[i];
        if (ch == '"') { q = !q; continue; }
        if (q) continue;
        if (ch == '[') { br = true; continue; }
        if (ch == ']') { br = false; continue; }
        if (br) continue;
        if (ch == '\\' || ch == '_' || ch == '*') { ++i; continue; }
        c.push_back(static_cast<char>(ch | 0x20));
    }
    const bool date = c.find('y') != std::string::npos || c.find('d') != std::string::npos;
    const bool time = c.find('h') != std::string::npos || c.find('s') != std::string::npos;
    if (date && time) return NumKind::DateTime;
    if (date) return NumKind::Date;
    if (time) return NumKind::Time;
    if (code.find('%') != std::string::npos) return NumKind::Percent;
    if (code.find("\xE2\x82\xAC") != std::string::npos || code.find('$') != std::string::npos || code.find("\xC2\xA3") != std::string::npos) return NumKind::Currency;
    return NumKind::General;
}

struct Xf { NumKind kind = NumKind::General; std::uint32_t fill = sheet::kNoFill; };

inline std::string ReadOr(const std::string& bytes, const std::vector<zip::Entry>& entries, const char* name, size_t maxSize = 64u * 1024 * 1024)
{
    std::string out;
    zip::ReadEntry(bytes, entries, name, out, maxSize);
    return out;
}

// Formel aus Excel fuer die Sheets-Engine: Trenner , -> ;  und $ entfernen (ausser in Text)
inline std::wstring FromExcelFormula(const std::wstring& f)
{
    std::wstring out;
    bool q = false;
    for (wchar_t ch : f) {
        if (ch == L'"') { q = !q; out.push_back(ch); continue; }
        if (q) { out.push_back(ch); continue; }
        if (ch == L',') out.push_back(L';');
        else if (ch == L'$') continue;
        else out.push_back(ch);
    }
    return out;
}

}  // namespace detail

// ======================= Lesen =======================
inline ReadResult Read(const std::string& bytes, sheet::Sheet& sh)
{
    using namespace detail;
    ReadResult res;
    std::vector<zip::Entry> entries;
    if (!zip::ReadDirectory(bytes, entries)) { res.error = L"Das ist keine gültige Excel-Datei (kein ZIP-Archiv)."; return res; }
    const std::string workbook = ReadOr(bytes, entries, "xl/workbook.xml", 8u * 1024 * 1024);
    if (workbook.empty()) { res.error = L"Das ist keine gültige Excel-Datei (xl/workbook.xml fehlt)."; return res; }

    // erstes Blatt finden
    std::string sheetPath = "xl/worksheets/sheet1.xml";
    {
        std::string rid;
        int sheets = 0;
        Tag t;
        for (size_t p = 0; NextTag(workbook, p, t); p = t.end) {
            if (t.closing || t.name != "sheet") continue;
            if (sheets++ == 0) rid = Attr(t.attrs, "id");
        }
        res.extraSheets = sheets > 1 ? sheets - 1 : 0;
        const std::string rels = ReadOr(bytes, entries, "xl/_rels/workbook.xml.rels", 2u * 1024 * 1024);
        for (size_t p = 0; !rid.empty() && NextTag(rels, p, t); p = t.end) {
            if (t.closing || t.name != "Relationship" || Attr(t.attrs, "Id") != rid) continue;
            std::string target = Attr(t.attrs, "Target");
            if (!target.empty()) {
                if (target[0] == '/') target.erase(0, 1);
                else target = "xl/" + target;
                sheetPath = target;
            }
            break;
        }
    }
    const std::string sheetXml = ReadOr(bytes, entries, sheetPath.c_str(), 48u * 1024 * 1024);
    if (sheetXml.empty()) { res.error = L"Das erste Tabellenblatt konnte nicht gelesen werden."; return res; }

    // gemeinsame Zeichenfolgen
    std::vector<std::wstring> sst;
    {
        const std::string x = ReadOr(bytes, entries, "xl/sharedStrings.xml", 48u * 1024 * 1024);
        Tag t;
        std::string cur;
        bool inSi = false, inRPh = false;
        for (size_t p = 0; NextTag(x, p, t); p = t.end) {
            if (t.name == "si") {
                if (!t.closing) { inSi = !t.selfClosing; cur.clear(); if (t.selfClosing) sst.emplace_back(); }
                else { sst.push_back(Unescape(cur)); inSi = false; }
            }
            else if (t.name == "rPh") inRPh = !t.closing && !t.selfClosing;
            else if (inSi && !inRPh && t.name == "t" && !t.closing && !t.selfClosing) cur += TextAfter(x, t);
        }
    }

    // Formate und Fuellungen
    std::vector<Xf> xfs;
    {
        const std::string x = ReadOr(bytes, entries, "xl/styles.xml", 8u * 1024 * 1024);
        std::map<int, std::string> numFmts;
        std::vector<std::uint32_t> fills;
        std::vector<std::pair<int, int>> rawXfs;   // numFmtId, fillId
        Tag t;
        enum { None, NumFmts, Fills, CellXfs } sect = None;
        bool inFill = false, solid = false;
        std::uint32_t fillColor = sheet::kNoFill;
        for (size_t p = 0; NextTag(x, p, t); p = t.end) {
            if (t.name == "numFmts") sect = t.closing ? None : NumFmts;
            else if (t.name == "fills") sect = t.closing ? None : Fills;
            else if (t.name == "cellXfs") sect = t.closing ? None : CellXfs;
            else if (sect == NumFmts && t.name == "numFmt" && !t.closing) numFmts[std::atoi(Attr(t.attrs, "numFmtId").c_str())] = codec::WideToUtf8(Unescape(Attr(t.attrs, "formatCode")));
            else if (sect == Fills) {
                if (t.name == "fill") {
                    if (!t.closing) { inFill = !t.selfClosing; solid = false; fillColor = sheet::kNoFill; if (t.selfClosing) fills.push_back(sheet::kNoFill); }
                    else { fills.push_back(solid ? fillColor : sheet::kNoFill); inFill = false; }
                }
                else if (inFill && !t.closing && t.name == "patternFill") solid = Attr(t.attrs, "patternType") == "solid";
                else if (inFill && !t.closing && t.name == "fgColor") {
                    const std::string rgb = Attr(t.attrs, "rgb");
                    if (rgb.size() == 8) {
                        const unsigned v = static_cast<unsigned>(std::strtoul(rgb.c_str() + 2, nullptr, 16));
                        fillColor = ((v >> 16) & 0xFF) | (v & 0xFF00) | ((v & 0xFF) << 16);   // RRGGBB -> COLORREF
                    }
                }
            }
            else if (sect == CellXfs && t.name == "xf" && !t.closing) rawXfs.emplace_back(std::atoi(Attr(t.attrs, "numFmtId").c_str()), std::atoi(Attr(t.attrs, "fillId").c_str()));
        }
        for (const auto& rx : rawXfs) {
            Xf xf;
            const auto it = numFmts.find(rx.first);
            xf.kind = KindOfFormat(rx.first, it == numFmts.end() ? std::string() : it->second);
            if (rx.second >= 0 && rx.second < static_cast<int>(fills.size())) xf.fill = fills[static_cast<size_t>(rx.second)];
            xfs.push_back(xf);
        }
    }

    sh.Clear();
    struct Cached { int r, c; bool isNum; double num; std::wstring text; bool isError; };
    std::vector<Cached> formulas;
    Tag t;
    for (size_t p = 0; NextTag(sheetXml, p, t);) {
        if (t.closing || t.name != "c") { p = t.end; continue; }
        const std::string ref = Attr(t.attrs, "r"), type = Attr(t.attrs, "t");
        const int style = std::atoi(Attr(t.attrs, "s").c_str());
        std::string fText, vText, isText;
        bool hasF = false, hasV = false, hasIs = false;
        size_t next = t.end;
        if (!t.selfClosing) {
            Tag u;
            bool inIs = false;
            for (size_t q = t.end; NextTag(sheetXml, q, u); q = u.end) {
                next = u.end;
                if (u.closing && u.name == "c") break;
                if (u.name == "f" && !u.closing) { hasF = true; if (!u.selfClosing) fText = TextAfter(sheetXml, u); }
                else if (u.name == "v" && !u.closing && !u.selfClosing) { hasV = true; vText = TextAfter(sheetXml, u); }
                else if (u.name == "is") inIs = !u.closing && !u.selfClosing;
                else if (inIs && u.name == "t" && !u.closing && !u.selfClosing) { hasIs = true; isText += TextAfter(sheetXml, u); }
            }
        }
        p = next;
        int row = 0, col = 0;
        bool inGrid = false;
        if (!ParseCellRef(ref, row, col, inGrid)) continue;
        const Xf xf = style >= 0 && style < static_cast<int>(xfs.size()) ? xfs[static_cast<size_t>(style)] : Xf{};

        // Wert bestimmen
        bool isNum = false, isErr = false, hasValue = false;
        double num = 0;
        std::wstring text;
        if (type == "s" && hasV) {
            const size_t idx = static_cast<size_t>(std::strtoul(vText.c_str(), nullptr, 10));
            if (idx < sst.size()) { text = sst[idx]; hasValue = true; }
        }
        else if (hasIs) { text = Unescape(isText); hasValue = true; }
        else if (type == "str" && hasV) { text = Unescape(vText); hasValue = true; }
        else if (type == "b" && hasV) { text = vText == "1" ? L"WAHR" : L"FALSCH"; hasValue = true; }
        else if (type == "e" && hasV) { text = Unescape(vText); isErr = true; hasValue = true; }
        else if (hasV) {
            char* end = nullptr;
            num = std::strtod(vText.c_str(), &end);
            if (end != vText.c_str()) { isNum = true; hasValue = true; }
        }

        std::wstring raw;
        if (hasValue && isNum) {
            if (xf.kind == NumKind::Date || xf.kind == NumKind::DateTime || xf.kind == NumKind::Time) {
                raw = SerialToText(num, xf.kind == NumKind::Time, xf.kind == NumKind::Date);
                if (inGrid) ++res.dateCells;
                isNum = false;
                text = raw;
            }
            else raw = NumberText(num);
        }
        else if (hasValue) raw = text;

        const bool formula = hasF && !fText.empty();
        if (!inGrid) {
            if (hasValue || hasF) ++res.droppedCells;
            continue;
        }
        if (formula) {
            std::wstring f = FromExcelFormula(Unescape(fText));
            sh.raw[row][col] = L"=" + f;
            formulas.push_back({ row, col, isNum, num, text, isErr });
        }
        else sh.raw[row][col] = raw;
        sh.fmt[row][col] = xf.kind == NumKind::Percent ? sheet::FmtPercent : xf.kind == NumKind::Currency ? sheet::FmtCurrency : sheet::FmtGeneral;
        sh.fill[row][col] = xf.fill;
    }
    sh.Invalidate();

    // Formeln, die Sheets nicht rechnen kann, durch das gespeicherte Ergebnis ersetzen
    for (const Cached& c : formulas) {
        const sheet::Value& v = sh.Get(c.r, c.c);
        if (v.kind == sheet::Value::Error && !c.isError) {
            sh.raw[c.r][c.c] = c.isNum ? NumberText(c.num) : c.text;
            ++res.replacedFormulas;
            sh.Invalidate();
        }
    }
    res.ok = true;
    return res;
}

// ======================= Schreiben =======================
namespace detail {

// Sheets-Formel -> Excel: deutsche Funktionsnamen, ; -> , und Dezimalkomma -> Punkt
inline std::string ToExcelFormula(const std::wstring& f)
{
    std::wstring out;
    bool q = false;
    for (size_t i = 0; i < f.size(); ++i) {
        const wchar_t ch = f[i];
        if (ch == L'"') { q = !q; out.push_back(ch); continue; }
        if (q) { out.push_back(ch); continue; }
        if (sheet::IsLetter(ch) || ch == 0xC4 || ch == 0xE4) {
            size_t j = i;
            std::wstring name;
            while (j < f.size() && (sheet::IsLetter(f[j]) || f[j] == 0xC4 || f[j] == 0xE4 || f[j] == L'_' || sheet::IsDigit(f[j]))) name.push_back(f[j++]);
            if (j < f.size() && f[j] == L'(') {
                std::wstring up;
                for (wchar_t c : name) up.push_back(c == 0xE4 ? static_cast<wchar_t>(0xC4) : static_cast<wchar_t>(std::towupper(c)));
                if (up == L"SUMME") up = L"SUM";
                else if (up == L"MITTELWERT") up = L"AVERAGE";
                else if (up == L"ANZAHL") up = L"COUNT";
                else if (up == L"ZAEHLENWENN" || up == L"Z\xC4HLENWENN") up = L"COUNTIF";
                else if (up == L"WENN") up = L"IF";
                else if (up == L"SUMMEWENN") up = L"SUMIF";
                else if (up == L"RUNDEN") up = L"ROUND";
                else if (up == L"WURZEL") up = L"SQRT";
                else if (up == L"GANZZAHL") up = L"INT";
                else if (up == L"SVERWEIS") up = L"VLOOKUP";
                else if (up == L"AUFRUNDEN") up = L"ROUNDUP";
                else if (up == L"ABRUNDEN") up = L"ROUNDDOWN";
                else if (up == L"REST") up = L"MOD";
                else if (up == L"POTENZ") up = L"POWER";
                else if (up == L"PRODUKT") up = L"PRODUCT";
                else if (up == L"VORZEICHEN") up = L"SIGN";
                out += up;
            }
            else out += name;
            i = j - 1;
            continue;
        }
        if (ch == L';') out.push_back(L',');
        else if (ch == L',' && i > 0 && i + 1 < f.size() && sheet::IsDigit(f[i - 1]) && sheet::IsDigit(f[i + 1])) out.push_back(L'.');   // Dezimalkomma
        else out.push_back(ch);
    }
    return Escape(out);
}

inline std::string ColLetter(int c) { return std::string(1, static_cast<char>('A' + c)); }

inline std::string NumStr(double d)
{
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.17g", d);
    return buf;
}

}  // namespace detail

inline std::string Write(const sheet::Sheet& sh)
{
    using namespace detail;
    // Stile: (Format, Fuellung) -> Index in cellXfs; 0 = Standard
    std::map<std::pair<int, std::uint32_t>, int> xfIndex;
    std::vector<std::pair<int, std::uint32_t>> xfList;
    std::map<std::uint32_t, int> fillIndex;
    std::vector<std::uint32_t> fillList;
    auto styleOf = [&](int r, int c) -> int {
        const int f = sh.fmt[r][c];
        const std::uint32_t fl = sh.fill[r][c];
        if (f == 0 && fl == sheet::kNoFill) return 0;
        const auto key = std::make_pair(f, fl);
        auto it = xfIndex.find(key);
        if (it != xfIndex.end()) return it->second;
        if (fl != sheet::kNoFill && !fillIndex.count(fl)) { fillIndex[fl] = static_cast<int>(fillList.size()) + 2; fillList.push_back(fl); }
        const int idx = static_cast<int>(xfList.size()) + 1;
        xfList.push_back(key);
        xfIndex[key] = idx;
        return idx;
    };

    std::string rows;
    for (int r = 0; r < sheet::kRows; ++r) {
        std::string cells;
        for (int c = 0; c < sheet::kCols; ++c) {
            const std::wstring& raw = sh.raw[r][c];
            const int st = styleOf(r, c);
            if (raw.empty() && st == 0) continue;
            const std::string ref = ColLetter(c) + std::to_string(r + 1);
            std::string cell = "<c r=\"" + ref + "\"";
            if (st) cell += " s=\"" + std::to_string(st) + "\"";
            if (raw.empty()) { cells += cell + "/>"; continue; }
            double d = 0;
            if (raw[0] == L'=') {
                const sheet::Value& v = sh.Get(r, c);
                const std::string f = ToExcelFormula(raw.substr(1));
                if (v.kind == sheet::Value::Number) cell += "><f>" + f + "</f><v>" + NumStr(v.num) + "</v></c>";
                else if (v.kind == sheet::Value::Text) cell += " t=\"str\"><f>" + f + "</f><v>" + Escape(v.text) + "</v></c>";
                else if (v.kind == sheet::Value::Error) cell += " t=\"e\"><f>" + f + "</f><v>" + Escape(v.text) + "</v></c>";
                else cell += "><f>" + f + "</f></c>";
            }
            else if (sheet::ParseNumber(raw, d)) cell += "><v>" + NumStr(d) + "</v></c>";
            else cell += " t=\"inlineStr\"><is><t xml:space=\"preserve\">" + Escape(raw) + "</t></is></c>";
            cells += cell;
        }
        if (!cells.empty()) rows += "<row r=\"" + std::to_string(r + 1) + "\">" + cells + "</row>";
    }

    const std::string xmlHead = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    const std::string ns = "xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"";
    const std::string worksheet = xmlHead + "<worksheet " + ns + "><cols><col min=\"1\" max=\"26\" width=\"14\" customWidth=\"1\"/></cols><sheetData>" + rows + "</sheetData></worksheet>";

    std::string fills = "<fills count=\"" + std::to_string(fillList.size() + 2) + "\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill>";
    for (std::uint32_t c : fillList) {
        char rgb[16];
        std::snprintf(rgb, sizeof rgb, "FF%02X%02X%02X", c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
        fills += std::string("<fill><patternFill patternType=\"solid\"><fgColor rgb=\"") + rgb + "\"/><bgColor indexed=\"64\"/></patternFill></fill>";
    }
    fills += "</fills>";
    std::string cellXfs = "<cellXfs count=\"" + std::to_string(xfList.size() + 1) + "\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>";
    for (const auto& x : xfList) {
        const int numFmt = x.first == sheet::FmtCurrency ? 164 : x.first == sheet::FmtPercent ? 10 : 0;
        const int fillId = x.second == sheet::kNoFill ? 0 : fillIndex[x.second];
        cellXfs += "<xf numFmtId=\"" + std::to_string(numFmt) + "\" fontId=\"0\" fillId=\"" + std::to_string(fillId) + "\" borderId=\"0\" xfId=\"0\"" +
                   (numFmt ? " applyNumberFormat=\"1\"" : "") + (fillId ? " applyFill=\"1\"" : "") + "/>";
    }
    cellXfs += "</cellXfs>";
    const std::string styles = xmlHead + "<styleSheet " + ns + "><numFmts count=\"1\"><numFmt numFmtId=\"164\" formatCode=\"#,##0.00\\ &quot;\xE2\x82\xAC&quot;\"/></numFmts>"
        "<fonts count=\"1\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font></fonts>" + fills +
        "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>" + cellXfs +
        "<cellStyles count=\"1\"><cellStyle name=\"Standard\" xfId=\"0\" builtinId=\"0\"/></cellStyles></styleSheet>";

    const std::string contentTypes = xmlHead +
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
        "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/></Types>";
    const std::string rels = xmlHead +
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>";
    const std::string workbook = xmlHead + "<workbook " + ns + " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<sheets><sheet name=\"Tabelle1\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>";
    const std::string wbRels = xmlHead +
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/></Relationships>";

    return zip::Write({ { "[Content_Types].xml", contentTypes }, { "_rels/.rels", rels }, { "xl/workbook.xml", workbook },
                        { "xl/_rels/workbook.xml.rels", wbRels }, { "xl/worksheets/sheet1.xml", worksheet }, { "xl/styles.xml", styles } });
}

}  // namespace xlsx
