#pragma once
// Tabellen-Engine fuer Duncan's Office (Sheets): 26 Spalten (A-Z) x 100 Zeilen, Formeln und CSV.
// Formeln: + - * / ^, Klammern, Zellbezuege (A1), Bereiche (A1:B5) und die Funktionen
// SUMME, MITTELWERT, MIN, MAX, ANZAHL (englisch: SUM, AVERAGE, MIN, MAX, COUNT), ZAEHLENWENN (COUNTIF) und WENN (IF) mit
// Vergleichen (= <> < > <= >=) und Text in Anfuehrungszeichen.
// Argumente trennt ";" (oder ","; steht "," zwischen zwei Ziffern, ist es ein Dezimalkomma).
// Reine Logik ohne Win32, damit sie getrennt testbar bleibt.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

namespace sheet {

constexpr int kCols = 26, kRows = 100;

struct Value {
    enum Kind { Empty, Number, Text, Error } kind = Empty;
    double num = 0;
    std::wstring text;   // Text oder Fehlercode (z. B. "#DIV/0!")
};

inline std::wstring ColName(int c) { return std::wstring(1, static_cast<wchar_t>(L'A' + c)); }
inline std::wstring CellName(int r, int c) { return ColName(c) + std::to_wstring(r + 1); }

inline bool IsLetter(wchar_t c) { return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z'); }
inline bool IsDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }

// 0 = keine Zellangabe, 1 = gueltig, 2 = sieht aus wie eine Zellangabe, liegt aber ausserhalb des Rasters
inline int ParseRef(const std::wstring& s, int& r, int& c)
{
    size_t i = 0;
    while (i < s.size() && IsLetter(s[i])) ++i;
    const size_t letters = i;
    if (letters == 0 || letters > 3 || i == s.size()) return 0;
    for (size_t k = i; k < s.size(); ++k)
        if (!IsDigit(s[k])) return 0;
    if (s.size() - i > 6) return 2;
    const long row = std::wcstol(s.c_str() + i, nullptr, 10);
    if (letters > 1 || row < 1 || row > kRows) return 2;
    c = std::towupper(s[0]) - L'A';
    r = static_cast<int>(row) - 1;
    return 1;
}

// "12,5", "12.5", "-3", "1e5" -> Zahl; alles andere (Tausenderpunkte, Buchstaben) bleibt Text
inline bool ParseNumber(const std::wstring& in, double& out)
{
    const size_t a = in.find_first_not_of(L" \t");
    if (a == std::wstring::npos) return false;
    const size_t b = in.find_last_not_of(L" \t");
    std::string n;
    bool digit = false, sep = false, exp = false;
    for (size_t i = a; i <= b; ++i) {
        const wchar_t ch = in[i];
        if (IsDigit(ch)) { digit = true; n.push_back(static_cast<char>(ch)); }
        else if ((ch == L'.' || ch == L',') && !sep && !exp) { sep = true; n.push_back('.'); }
        else if ((ch == L'e' || ch == L'E') && digit && !exp) { exp = true; n.push_back('e'); }
        else if ((ch == L'-' || ch == L'+') && (i == a || in[i - 1] == L'e' || in[i - 1] == L'E')) n.push_back(static_cast<char>(ch));
        else return false;
    }
    if (!digit) return false;
    char* end = nullptr;
    out = std::strtod(n.c_str(), &end);
    return end && *end == 0 && std::isfinite(out);
}

inline std::wstring FormatNumber(double d)
{
    if (!std::isfinite(d)) return L"#ZAHL!";
    if (d == 0) return L"0";
    wchar_t buf[48];
    if (std::fabs(d) < 1e15 && d == std::floor(d)) std::swprintf(buf, 48, L"%.0f", d);
    else std::swprintf(buf, 48, L"%.10g", d);
    for (wchar_t* p = buf; *p; ++p)
        if (*p == L'.') *p = L',';
    return buf;
}

// Waehrung "1.234,50 €" und Prozent "12,5 %" (deutsche Schreibweise)
inline std::wstring FormatCurrency(double d)
{
    if (!std::isfinite(d)) return L"#ZAHL!";
    const bool neg = d < 0 && std::fabs(d) >= 0.005;
    wchar_t buf[64];
    std::swprintf(buf, 64, L"%.2f", std::fabs(d));
    std::wstring t = buf;
    const size_t dot = t.find(L'.');
    std::wstring ip = t.substr(0, dot), frac = t.substr(dot + 1), out;
    for (size_t k = 0; k < ip.size(); ++k) {
        if (k && (ip.size() - k) % 3 == 0) out.push_back(L'.');
        out.push_back(ip[k]);
    }
    return (neg ? L"-" : L"") + out + L"," + frac + L" \u20AC";
}

inline std::wstring FormatPercent(double d)
{
    if (!std::isfinite(d)) return L"#ZAHL!";
    wchar_t buf[64];
    std::swprintf(buf, 64, L"%.2f", d * 100.0);
    std::wstring t = buf;
    while (!t.empty() && t.back() == L'0') t.pop_back();
    if (!t.empty() && t.back() == L'.') t.pop_back();
    if (t == L"-0" || t.empty()) t = L"0";
    for (auto& ch : t)
        if (ch == L'.') ch = L',';
    return t + L" %";
}

enum Format : unsigned char { FmtGeneral = 0, FmtCurrency = 1, FmtPercent = 2 };
constexpr std::uint32_t kNoFill = 0xFFFFFFFFu;   // keine Hintergrundfarbe

class Sheet {
public:
    std::wstring raw[kRows][kCols];   // Eingabe wie getippt (Formeln beginnen mit "=")
    unsigned char fmt[kRows][kCols] = {};   // Zahlenformat je Zelle (Format)
    std::uint32_t fill[kRows][kCols];       // Hintergrundfarbe (COLORREF) oder kNoFill

    Sheet() { ResetStyles(); Invalidate(); }
    void ResetStyles()
    {
        std::memset(fmt, 0, sizeof fmt);
        for (auto& row : fill)
            for (auto& f : row) f = kNoFill;
    }

    void Set(int r, int c, const std::wstring& s) { raw[r][c] = s; Invalidate(); }
    void Clear()
    {
        for (auto& row : raw)
            for (auto& cell : row) cell.clear();
        ResetStyles();
        Invalidate();
    }
    // Berechnete Werte verwerfen (nach jeder Aenderung der Eingaben noetig)
    void Invalidate() const
    {
        std::memset(m_state, 0, sizeof m_state);
        m_depth = 0;
    }

    const Value& Get(int r, int c) const
    {
        static const Value circular{ Value::Error, 0, L"#ZIRKEL!" };
        static const Value tooDeep{ Value::Error, 0, L"#TIEFE!" };
        unsigned char& st = m_state[r][c];
        if (st == 2) return m_val[r][c];
        if (st == 1) return circular;
        if (m_depth > 200) return tooDeep;   // schuetzt den Stack bei sehr langen Bezugsketten
        st = 1;
        ++m_depth;
        m_val[r][c] = Compute(r, c);
        --m_depth;
        st = 2;
        return m_val[r][c];
    }

    std::wstring Display(int r, int c) const
    {
        const Value& v = Get(r, c);
        switch (v.kind) {
            case Value::Number:
                return fmt[r][c] == FmtCurrency ? FormatCurrency(v.num) : fmt[r][c] == FmtPercent ? FormatPercent(v.num) : FormatNumber(v.num);
            case Value::Text:
            case Value::Error: return v.text;
            default: return std::wstring();
        }
    }

    // Groesse des benutzten Bereichs (letzte nicht leere Zeile/Spalte + 1); false, wenn alles leer ist
    bool Used(int& rows, int& cols) const
    {
        rows = cols = 0;
        for (int r = 0; r < kRows; ++r)
            for (int c = 0; c < kCols; ++c)
                if (!raw[r][c].empty()) { rows = r + 1 > rows ? r + 1 : rows; cols = c + 1 > cols ? c + 1 : cols; }
        return rows > 0;
    }

private:
    mutable unsigned char m_state[kRows][kCols];   // 0 offen, 1 wird berechnet, 2 fertig
    mutable Value m_val[kRows][kCols];
    mutable int m_depth = 0;
    Value Compute(int r, int c) const;
};

namespace detail {

struct Fail { std::wstring code; };

// Zwischenergebnis, das auch Text sein kann (fuer Vergleiche, WENN und Kriterien)
struct Operand {
    bool text = false, empty = false;
    double n = 0;
    std::wstring s;
};

inline bool IsUmlaut(wchar_t c) { return c == 0xC4 || c == 0xD6 || c == 0xDC || c == 0xE4 || c == 0xF6 || c == 0xFC || c == 0xDF; }
inline wchar_t UpperName(wchar_t c)
{
    if (c == 0xE4) return 0xC4;
    if (c == 0xF6) return 0xD6;
    if (c == 0xFC) return 0xDC;
    return c < 128 ? static_cast<wchar_t>(std::towupper(c)) : c;
}
inline int CompareText(const std::wstring& a, const std::wstring& b)   // ohne Beachtung der Gross-/Kleinschreibung
{
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t k = 0; k < n; ++k) {
        const wchar_t x = UpperName(a[k]), y = UpperName(b[k]);
        if (x != y) return x < y ? -1 : 1;
    }
    return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}
// Platzhalter * (beliebig viele Zeichen) und ? (ein Zeichen), ohne Beachtung der Gross-/Kleinschreibung
inline bool WildMatch(const std::wstring& p, size_t pi, const std::wstring& t, size_t ti)
{
    while (pi < p.size()) {
        if (p[pi] == L'*') {
            for (size_t k = ti; k <= t.size(); ++k)
                if (WildMatch(p, pi + 1, t, k)) return true;
            return false;
        }
        if (ti >= t.size()) return false;
        if (p[pi] != L'?' && UpperName(p[pi]) != UpperName(t[ti])) return false;
        ++pi;
        ++ti;
    }
    return ti == t.size();
}

struct Parser {
    const Sheet& sh;
    const std::wstring& s;
    size_t i;

    Parser(const Sheet& sheet, const std::wstring& text, size_t start) : sh(sheet), s(text), i(start) {}

    void Ws() { while (i < s.size() && (s[i] == L' ' || s[i] == L'\t')) ++i; }
    bool Eat(wchar_t ch)
    {
        Ws();
        if (i < s.size() && s[i] == ch) { ++i; return true; }
        return false;
    }
    [[noreturn]] static void Throw(const wchar_t* code) { throw Fail{ code }; }

    double Expr()
    {
        double v = Term();
        for (;;) {
            if (Eat(L'+')) v += Term();
            else if (Eat(L'-')) v -= Term();
            else break;
        }
        return Check(v);
    }
    double Term()
    {
        double v = Power();
        for (;;) {
            if (Eat(L'*')) v *= Power();
            else if (Eat(L'/')) {
                const double d = Power();
                if (d == 0) Throw(L"#DIV/0!");
                v /= d;
            }
            else break;
        }
        return Check(v);
    }
    double Unary()
    {
        if (Eat(L'-')) return -Unary();
        if (Eat(L'+')) return Unary();
        return Primary();
    }
    // Wie in Excel: Vorzeichen binden staerker als ^ (-2^2 = 4), ^ wird von links nach rechts ausgewertet (2^2^3 = 64)
    double Power()
    {
        double b = Unary();
        while (Eat(L'^')) b = Check(std::pow(b, Unary()));
        return b;
    }
    static double Check(double v)
    {
        if (!std::isfinite(v)) Throw(L"#ZAHL!");
        return v;
    }

    double Primary()
    {
        Ws();
        if (i >= s.size()) Throw(L"#SYNTAX!");
        const wchar_t ch = s[i];
        if (ch == L'(') {
            ++i;
            const double v = Expr();
            if (!Eat(L')')) Throw(L"#SYNTAX!");
            return v;
        }
        if (IsDigit(ch) || ch == L'.') return Number();
        if (IsLetter(ch)) return Name();
        Throw(L"#SYNTAX!");
    }

    double Number()
    {
        std::string n;
        bool digits = false;
        while (i < s.size() && IsDigit(s[i])) { n.push_back(static_cast<char>(s[i++])); digits = true; }
        if (i + 1 < s.size() && (s[i] == L'.' || s[i] == L',') && IsDigit(s[i + 1])) {
            n.push_back('.');
            ++i;
            while (i < s.size() && IsDigit(s[i])) { n.push_back(static_cast<char>(s[i++])); digits = true; }
        }
        if (!digits) Throw(L"#SYNTAX!");
        if (i < s.size() && (s[i] == L'e' || s[i] == L'E')) {
            size_t j = i + 1;
            if (j < s.size() && (s[j] == L'+' || s[j] == L'-')) ++j;
            if (j < s.size() && IsDigit(s[j])) {
                n.push_back('e');
                for (size_t k = i + 1; k < j; ++k) n.push_back(static_cast<char>(s[k]));
                while (j < s.size() && IsDigit(s[j])) n.push_back(static_cast<char>(s[j++]));
                i = j;
            }
        }
        return std::strtod(n.c_str(), nullptr);
    }

    double CellNumber(int r, int c)
    {
        const Value& v = sh.Get(r, c);
        switch (v.kind) {
            case Value::Empty: return 0;
            case Value::Number: return v.num;
            case Value::Text: Throw(L"#WERT!");
            default: throw Fail{ v.text };
        }
    }

    // Name: Funktion "SUMME(" oder Zellbezug "A1"
    double Name()
    {
        const size_t st = i;
        while (i < s.size() && (IsLetter(s[i]) || IsUmlaut(s[i]))) ++i;
        const size_t letterEnd = i;
        while (i < s.size() && IsDigit(s[i])) ++i;
        const std::wstring tok = s.substr(st, i - st);
        size_t j = i;
        while (j < s.size() && s[j] == L' ') ++j;
        if (letterEnd == i) {
            if (j < s.size() && s[j] == L'(') {
                if (IsIfName(tok)) {   // WENN auch mitten in einer Rechnung, aber nur mit Zahlen als Ergebnis
                    i = st;
                    const Operand o = WennOp();
                    if (o.text) Throw(L"#WERT!");
                    return o.n;
                }
                i = j;
                return Func(tok);
            }
            Throw(L"#NAME?");
        }
        int r = 0, c = 0;
        const int k = ParseRef(tok, r, c);
        if (k == 0) Throw(L"#NAME?");
        if (k == 2) Throw(L"#BEZUG!");
        if (Eat(L':')) Throw(L"#WERT!");   // Bereiche nur innerhalb von Funktionen
        return CellNumber(r, c);
    }

    void AddCells(std::vector<double>& vals, int r1, int c1, int r2, int c2)
    {
        for (int r = r1 < r2 ? r1 : r2; r <= (r1 < r2 ? r2 : r1); ++r)
            for (int c = c1 < c2 ? c1 : c2; c <= (c1 < c2 ? c2 : c1); ++c) {
                const Value& v = sh.Get(r, c);
                if (v.kind == Value::Error) throw Fail{ v.text };
                if (v.kind == Value::Number) vals.push_back(v.num);   // leere Zellen und Text zaehlen nicht mit
            }
    }

    // Steht hier ein Bereich "A1:B5" oder eine einzelne Zelle (gefolgt von ; , oder ) ), kommen die Koordinaten zurueck und i steht dahinter
    bool RangeCoords(int& r1, int& c1, int& r2, int& c2)
    {
        Ws();
        size_t p = i;
        while (p < s.size() && IsLetter(s[p])) ++p;
        const size_t letterEnd = p;
        while (p < s.size() && IsDigit(s[p])) ++p;
        if (letterEnd == i || p == letterEnd) return false;
        const int k1 = ParseRef(s.substr(i, p - i), r1, c1);
        if (k1 == 0) return false;
        size_t q = p;
        while (q < s.size() && s[q] == L' ') ++q;
        if (q < s.size() && s[q] == L':') {
            ++q;
            while (q < s.size() && s[q] == L' ') ++q;
            size_t e = q;
            while (e < s.size() && IsLetter(s[e])) ++e;
            while (e < s.size() && IsDigit(s[e])) ++e;
            const int k2 = ParseRef(s.substr(q, e - q), r2, c2);
            if (k2 == 0) Throw(L"#SYNTAX!");
            if (k1 == 2 || k2 == 2) Throw(L"#BEZUG!");
            i = e;
            return true;
        }
        if (q < s.size() && (s[q] == L';' || s[q] == L',' || s[q] == L')')) {
            if (k1 == 2) Throw(L"#BEZUG!");
            i = q;
            r2 = r1;
            c2 = c1;
            return true;
        }
        return false;
    }

    // Eine Zelle oder einen Bereich als Zellmenge lesen; sonst false (dann ist ein normaler Ausdruck dran)
    bool RefOrRange(std::vector<double>& vals)
    {
        int r1 = 0, c1 = 0, r2 = 0, c2 = 0;
        if (!RangeCoords(r1, c1, r2, c2)) return false;
        AddCells(vals, r1, c1, r2, c2);
        return true;
    }

    // ---- Text, Vergleiche, WENN, ZAEHLENWENN ----
    static bool IsIfName(const std::wstring& tok)
    {
        std::wstring n;
        for (wchar_t ch : tok) n.push_back(UpperName(ch));
        return n == L"WENN" || n == L"IF";
    }
    static bool IsCountIfName(const std::wstring& n)
    {
        return n == L"Z\u00C4HLENWENN" || n == L"ZAEHLENWENN" || n == L"COUNTIF";
    }

    std::wstring StringLit()
    {
        std::wstring out;
        ++i;   // Anfuehrungszeichen
        while (i < s.size()) {
            if (s[i] == L'"') {
                if (i + 1 < s.size() && s[i + 1] == L'"') { out.push_back(L'"'); i += 2; continue; }
                ++i;
                return out;
            }
            out.push_back(s[i++]);
        }
        Throw(L"#SYNTAX!");
    }

    // Steht hier "WENN(" (mit Leerzeichen vor der Klammer erlaubt)?
    bool PeekIf() const
    {
        size_t p = i;
        std::wstring tok;
        while (p < s.size() && (IsLetter(s[p]) || IsUmlaut(s[p]))) tok.push_back(s[p++]);
        if (!IsIfName(tok)) return false;
        while (p < s.size() && s[p] == L' ') ++p;
        return p < s.size() && s[p] == L'(';
    }

    Operand Opnd()
    {
        Ws();
        Operand o;
        if (i < s.size() && s[i] == L'"') {
            o.text = true;
            o.s = StringLit();
            return o;
        }
        if (PeekIf()) {   // WENN liefert Zahl oder Text; folgt danach noch eine Rechenoperation, gilt das Ergebnis als Zahl innerhalb des Ausdrucks
            const size_t save = i;
            o = WennOp();
            Ws();
            if (i < s.size() && (s[i] == L'+' || s[i] == L'-' || s[i] == L'*' || s[i] == L'/' || s[i] == L'^')) {
                if (o.text) Throw(L"#WERT!");
                i = save;
                o = Operand();
                o.n = Expr();
            }
            return o;
        }
        size_t p = i;
        while (p < s.size() && IsLetter(s[p])) ++p;
        const size_t letterEnd = p;
        while (p < s.size() && IsDigit(s[p])) ++p;
        if (letterEnd > i && p > letterEnd && !(p < s.size() && (s[p] == L'(' || s[p] == L':'))) {
            int r = 0, c = 0;
            if (ParseRef(s.substr(i, p - i), r, c) == 1) {
                const Value& v = sh.Get(r, c);
                if (v.kind == Value::Text || v.kind == Value::Empty) {
                    const size_t save = i;
                    i = p;
                    Ws();
                    if (i < s.size() && (s[i] == L'+' || s[i] == L'-' || s[i] == L'*' || s[i] == L'/' || s[i] == L'^')) {   // Teil einer Rechnung: als Zahl auswerten
                        i = save;
                        o.n = Expr();
                        return o;
                    }
                    i = p;
                    if (v.kind == Value::Text) { o.text = true; o.s = v.text; }
                    else o.empty = true;
                    return o;
                }
            }
        }
        o.n = Expr();
        return o;
    }

    // Vergleich oder einzelner Wert; Ergebnis eines Vergleichs ist 1 (wahr) oder 0 (falsch)
    Operand CondOp()
    {
        Operand l = Opnd();
        Ws();
        int op = 0;   // 1 =, 2 <>, 3 <, 4 >, 5 <=, 6 >=
        if (i < s.size()) {
            const wchar_t ch = s[i];
            if (ch == L'=') { op = 1; ++i; }
            else if (ch == L'<') {
                ++i;
                if (i < s.size() && s[i] == L'>') { op = 2; ++i; }
                else if (i < s.size() && s[i] == L'=') { op = 5; ++i; }
                else op = 3;
            }
            else if (ch == L'>') {
                ++i;
                if (i < s.size() && s[i] == L'=') { op = 6; ++i; }
                else op = 4;
            }
        }
        if (!op) return l;
        Operand r = Opnd();
        if (l.empty && r.text) l.text = true;     // leere Zelle gegen Text: wie ""
        else if (r.empty && l.text) r.text = true;
        if (l.text != r.text) {
            if (op == 1) return Operand{ false, false, 0, L"" };
            if (op == 2) return Operand{ false, false, 1, L"" };
            Throw(L"#WERT!");
        }
        int cmp = 0;
        if (l.text) cmp = CompareText(l.s, r.s);
        else cmp = l.n < r.n ? -1 : (l.n > r.n ? 1 : 0);
        bool res = false;
        switch (op) {
            case 1: res = cmp == 0; break;
            case 2: res = cmp != 0; break;
            case 3: res = cmp < 0; break;
            case 4: res = cmp > 0; break;
            case 5: res = cmp <= 0; break;
            default: res = cmp >= 0; break;
        }
        Operand o;
        o.n = res ? 1 : 0;
        return o;
    }

    double Cond()
    {
        const Operand o = CondOp();
        if (o.text) Throw(L"#WERT!");
        return o.n;
    }

    // Ende des Arguments (Position von ; , oder ) auf gleicher Klammerebene), ausgehend von start
    size_t SkipArg(size_t start) const
    {
        int depth = 0;
        bool quote = false;
        for (size_t j = start; j < s.size(); ++j) {
            const wchar_t ch = s[j];
            if (ch == L'"') { quote = !quote; continue; }
            if (quote) continue;
            if (ch == L'(') ++depth;
            else if (ch == L')') { if (depth == 0) return j; --depth; }
            else if ((ch == L';' || ch == L',') && depth == 0) return j;
        }
        return s.size();
    }

    // WENN(Bedingung; dann; sonst): nur der gewaehlte Zweig muss fehlerfrei sein
    Operand WennOp()
    {
        while (i < s.size() && (IsLetter(s[i]) || IsUmlaut(s[i]))) ++i;   // Name
        if (!Eat(L'(')) Throw(L"#SYNTAX!");
        const double cond = Cond();
        if (!(Eat(L';') || Eat(L','))) Throw(L"#SYNTAX!");
        Operand a, b;
        Fail fa, fb;
        bool failA = false, failB = false, hasB = false;
        auto branch = [&](Operand& out, Fail& err, bool& failed) {
            Ws();
            const size_t start = i;
            try { out = CondOp(); }
            catch (const Fail& f) {
                if (f.code == L"#SYNTAX!") throw;
                failed = true;
                err = f;
                i = SkipArg(start);
            }
        };
        branch(a, fa, failA);
        if (Eat(L';') || Eat(L',')) { hasB = true; branch(b, fb, failB); }
        if (!Eat(L')')) Throw(L"#SYNTAX!");
        if (cond != 0) {
            if (failA) throw fa;
            return a;
        }
        if (!hasB) return Operand();
        if (failB) throw fb;
        return b;
    }

    static bool CriterionMatches(const Value& v, const Operand& crit)
    {
        if (v.kind == Value::Error) return false;
        if (!crit.text) return crit.empty ? v.kind == Value::Empty : (v.kind == Value::Number && v.num == crit.n);
        std::wstring c = crit.s;
        int op = 1;
        auto starts = [&](const wchar_t* pre) { return c.compare(0, std::wcslen(pre), pre) == 0; };
        if (starts(L"<>")) { op = 2; c.erase(0, 2); }
        else if (starts(L"<=")) { op = 5; c.erase(0, 2); }
        else if (starts(L">=")) { op = 6; c.erase(0, 2); }
        else if (starts(L"<")) { op = 3; c.erase(0, 1); }
        else if (starts(L">")) { op = 4; c.erase(0, 1); }
        else if (starts(L"=")) { op = 1; c.erase(0, 1); }
        double num = 0;
        if (ParseNumber(c, num)) {
            if (v.kind != Value::Number) return op == 2;
            switch (op) {
                case 1: return v.num == num;
                case 2: return v.num != num;
                case 3: return v.num < num;
                case 4: return v.num > num;
                case 5: return v.num <= num;
                default: return v.num >= num;
            }
        }
        const std::wstring t = v.kind == Value::Text ? v.text : std::wstring();
        if (v.kind == Value::Number) return op == 2;
        if (v.kind == Value::Empty && !c.empty()) return op == 2;
        if (op == 1) return WildMatch(c, 0, t, 0);
        if (op == 2) return !WildMatch(c, 0, t, 0);
        if (v.kind != Value::Text) return false;
        const int cmp = CompareText(t, c);
        return op == 3 ? cmp < 0 : op == 4 ? cmp > 0 : op == 5 ? cmp <= 0 : cmp >= 0;
    }

    double Func(const std::wstring& nameIn)
    {
        std::wstring name;
        for (wchar_t ch : nameIn) name.push_back(UpperName(ch));
        enum { Sum, Avg, Min, Max, Count, CountIf } fn;
        if (name == L"SUMME" || name == L"SUM") fn = Sum;
        else if (name == L"MITTELWERT" || name == L"AVERAGE") fn = Avg;
        else if (name == L"MIN") fn = Min;
        else if (name == L"MAX") fn = Max;
        else if (name == L"ANZAHL" || name == L"COUNT") fn = Count;
        else if (IsCountIfName(name)) fn = CountIf;
        else Throw(L"#NAME?");
        if (!Eat(L'(')) Throw(L"#SYNTAX!");
        if (fn == CountIf) {   // ZAEHLENWENN(Bereich; Kriterium)
            int r1 = 0, c1 = 0, r2 = 0, c2 = 0;
            Ws();
            if (!RangeCoords(r1, c1, r2, c2)) Throw(L"#SYNTAX!");
            if (!(Eat(L';') || Eat(L','))) Throw(L"#SYNTAX!");
            const Operand crit = CondOp();
            if (!Eat(L')')) Throw(L"#SYNTAX!");
            double cnt = 0;
            for (int r = r1 < r2 ? r1 : r2; r <= (r1 < r2 ? r2 : r1); ++r)
                for (int c = c1 < c2 ? c1 : c2; c <= (c1 < c2 ? c2 : c1); ++c)
                    if (CriterionMatches(sh.Get(r, c), crit)) ++cnt;
            return cnt;
        }
        std::vector<double> vals;
        if (!Eat(L')')) {
            for (;;) {
                if (!RefOrRange(vals)) vals.push_back(Expr());
                if (Eat(L';') || Eat(L',')) continue;
                if (Eat(L')')) break;
                Throw(L"#SYNTAX!");
            }
        }
        double r = 0;
        switch (fn) {
            case Sum:
                for (double v : vals) r += v;
                break;
            case Avg:
                if (vals.empty()) Throw(L"#DIV/0!");
                for (double v : vals) r += v;
                r /= static_cast<double>(vals.size());
                break;
            case Min:
                if (!vals.empty()) { r = vals[0]; for (double v : vals) if (v < r) r = v; }
                break;
            case Max:
                if (!vals.empty()) { r = vals[0]; for (double v : vals) if (v > r) r = v; }
                break;
            case Count:
                r = static_cast<double>(vals.size());
                break;
            case CountIf:
                break;   // oben behandelt
        }
        return Check(r);
    }
};

}  // namespace detail

inline Value Sheet::Compute(int r, int c) const
{
    const std::wstring& t = raw[r][c];
    const size_t a = t.find_first_not_of(L" \t");
    Value v;
    if (a == std::wstring::npos) return v;
    if (t[a] == L'=') {
        try {
            detail::Parser p(*this, t, a + 1);
            const detail::Operand o = p.CondOp();
            p.Ws();
            if (p.i != t.size()) throw detail::Fail{ L"#SYNTAX!" };
            if (o.text) {
                v.kind = Value::Text;
                v.text = o.s;
            }
            else {
                v.kind = Value::Number;
                v.num = o.n;
            }
        }
        catch (const detail::Fail& f) {
            v.kind = Value::Error;
            v.text = f.code;
        }
        return v;
    }
    double d = 0;
    if (ParseNumber(t, d)) {
        v.kind = Value::Number;
        v.num = d;
    }
    else {
        v.kind = Value::Text;
        v.text = t;
    }
    return v;
}

// ---- CSV (Rohinhalt der Zellen, damit Formeln beim Speichern und Laden erhalten bleiben) ----

inline std::wstring CsvQuote(const std::wstring& f, wchar_t delim)
{
    const std::wstring special = std::wstring(L"\"\r\n") + delim;
    if (f.find_first_of(special) == std::wstring::npos && (f.empty() || (f.front() != L' ' && f.back() != L' '))) return f;
    std::wstring out = L"\"";
    for (wchar_t ch : f) {
        if (ch == L'"') out += L"\"\"";
        else out.push_back(ch);
    }
    out.push_back(L'"');
    return out;
}

inline std::wstring ToCsv(const Sheet& sh, wchar_t delim = L';')
{
    int rows = 0, cols = 0;
    std::wstring out;
    if (!sh.Used(rows, cols)) return out;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (c) out.push_back(delim);
            out += CsvQuote(sh.raw[r][c], delim);
        }
        out += L"\r\n";
    }
    return out;
}

// Liest CSV (Trennzeichen ; , oder Tab wird erkannt, Felder in Anfuehrungszeichen, "" als Anfuehrungszeichen).
// Gibt die Zahl der nicht leeren Felder zurueck, die ausserhalb des Rasters lagen und verworfen wurden.
inline int FromCsv(Sheet& sh, std::wstring text)
{
    sh.Clear();
    if (!text.empty() && text[0] == 0xFEFF) text.erase(0, 1);

    int cnt[3] = { 0, 0, 0 };   // ; Tab ,
    bool q = false;
    for (wchar_t ch : text) {
        if (ch == L'"') q = !q;
        else if (!q) {
            if (ch == L'\n') break;
            if (ch == L';') ++cnt[0];
            else if (ch == L'\t') ++cnt[1];
            else if (ch == L',') ++cnt[2];
        }
    }
    wchar_t delim = L';';
    int best = cnt[0];
    if (cnt[1] > best) { delim = L'\t'; best = cnt[1]; }
    if (cnt[2] > best) delim = L',';

    int row = 0, col = 0, dropped = 0;
    std::wstring field;
    bool inQuotes = false, quoted = false, pending = false;   // pending: in dieser Zeile wurde schon etwas gelesen
    auto endField = [&]() {
        if (row < kRows && col < kCols) sh.raw[row][col] = field;
        else if (!field.empty()) ++dropped;
        field.clear();
        quoted = false;
        ++col;
    };
    auto endRow = [&]() {
        endField();
        ++row;
        col = 0;
        pending = false;
    };
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        if (inQuotes) {
            if (ch == L'"') {
                if (i + 1 < text.size() && text[i + 1] == L'"') { field.push_back(L'"'); ++i; }
                else inQuotes = false;
            }
            else field.push_back(ch);
            continue;
        }
        if (ch == L'"' && field.empty() && !quoted) { inQuotes = true; quoted = true; pending = true; }
        else if (ch == delim) { endField(); pending = true; }
        else if (ch == L'\r') {
            if (i + 1 < text.size() && text[i + 1] == L'\n') continue;
            endRow();
        }
        else if (ch == L'\n') endRow();
        else { field.push_back(ch); pending = true; }
    }
    if (pending || !field.empty()) endRow();
    sh.Invalidate();
    return dropped;
}

// ---- Auswahl: Statistik und Zwischenablage-Text ----

struct RangeStats {
    double sum = 0;
    int count = 0;   // Zahl der Zellen mit Zahlenwert
    double Average() const { return count ? sum / count : 0; }
};

inline RangeStats Stats(const Sheet& sh, int r1, int c1, int r2, int c2)
{
    RangeStats st;
    for (int r = r1; r <= r2; ++r)
        for (int c = c1; c <= c2; ++c) {
            const Value& v = sh.Get(r, c);
            if (v.kind == Value::Number) { st.sum += v.num; ++st.count; }
        }
    return st;
}

// Bereich als Text mit Tabulatoren (Zeilen mit CRLF); raw = eingegebener Text (Formeln), sonst angezeigte Werte
inline std::wstring RangeToTsv(const Sheet& sh, int r1, int c1, int r2, int c2, bool raw)
{
    std::wstring out;
    for (int r = r1; r <= r2; ++r) {
        for (int c = c1; c <= c2; ++c) {
            if (c > c1) out.push_back(L'\t');
            out += CsvQuote(raw ? sh.raw[r][c] : sh.Display(r, c), L'\t');
        }
        out += L"\r\n";
    }
    return out;
}

// Zerlegt Text mit Trennzeichen (Tab beim Einfuegen aus Excel) in Zeilen und Felder; Felder in Anfuehrungszeichen duerfen Tab und Zeilenumbruch enthalten
inline std::vector<std::vector<std::wstring>> ParseDelimited(const std::wstring& text, wchar_t delim)
{
    std::vector<std::vector<std::wstring>> rows;
    std::vector<std::wstring> row;
    std::wstring field;
    bool inQuotes = false, quoted = false, pending = false;
    auto endField = [&]() { row.push_back(field); field.clear(); quoted = false; };
    auto endRow = [&]() { endField(); rows.push_back(row); row.clear(); pending = false; };
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        if (inQuotes) {
            if (ch == L'"') {
                if (i + 1 < text.size() && text[i + 1] == L'"') { field.push_back(L'"'); ++i; }
                else inQuotes = false;
            }
            else field.push_back(ch);
            continue;
        }
        if (ch == L'"' && field.empty() && !quoted) { inQuotes = true; quoted = true; pending = true; }
        else if (ch == delim) { endField(); pending = true; }
        else if (ch == L'\r') {
            if (i + 1 < text.size() && text[i + 1] == L'\n') continue;
            endRow();
        }
        else if (ch == L'\n') endRow();
        else { field.push_back(ch); pending = true; }
    }
    if (pending || !field.empty()) endRow();
    return rows;
}

}  // namespace sheet
