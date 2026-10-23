#pragma once
// Tabellen-Engine fuer Duncan's Office (Sheets): 26 Spalten (A-Z) x 100 Zeilen, Formeln und CSV.
// Formeln: + - * / ^, Klammern, Zellbezuege (A1), Bereiche (A1:B5) und die Funktionen
// SUMME, MITTELWERT, MIN, MAX, ANZAHL (englisch: SUM, AVERAGE, MIN, MAX, COUNT).
// Argumente trennt ";" (oder ","; steht "," zwischen zwei Ziffern, ist es ein Dezimalkomma).
// Reine Logik ohne Win32, damit sie getrennt testbar bleibt.
#include <cmath>
#include <cstdio>
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

class Sheet {
public:
    std::wstring raw[kRows][kCols];   // Eingabe wie getippt (Formeln beginnen mit "=")

    Sheet() { Invalidate(); }

    void Set(int r, int c, const std::wstring& s) { raw[r][c] = s; Invalidate(); }
    void Clear()
    {
        for (auto& row : raw)
            for (auto& cell : row) cell.clear();
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
            case Value::Number: return FormatNumber(v.num);
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
        double v = Unary();
        for (;;) {
            if (Eat(L'*')) v *= Unary();
            else if (Eat(L'/')) {
                const double d = Unary();
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
        return Power();
    }
    double Power()
    {
        double b = Primary();
        if (Eat(L'^')) b = Check(std::pow(b, Unary()));
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
        while (i < s.size() && IsLetter(s[i])) ++i;
        const size_t letterEnd = i;
        while (i < s.size() && IsDigit(s[i])) ++i;
        const std::wstring tok = s.substr(st, i - st);
        size_t j = i;
        while (j < s.size() && s[j] == L' ') ++j;
        if (letterEnd == i) {
            if (j < s.size() && s[j] == L'(') {
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

    // Eine Zelle oder einen Bereich als Zellmenge lesen; sonst false (dann ist ein normaler Ausdruck dran)
    bool RefOrRange(std::vector<double>& vals)
    {
        Ws();
        size_t p = i;
        while (p < s.size() && IsLetter(s[p])) ++p;
        const size_t letterEnd = p;
        while (p < s.size() && IsDigit(s[p])) ++p;
        if (letterEnd == i || p == letterEnd) return false;
        int r1 = 0, c1 = 0;
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
            int r2 = 0, c2 = 0;
            const int k2 = ParseRef(s.substr(q, e - q), r2, c2);
            if (k2 == 0) Throw(L"#SYNTAX!");
            if (k1 == 2 || k2 == 2) Throw(L"#BEZUG!");
            i = e;
            AddCells(vals, r1, c1, r2, c2);
            return true;
        }
        if (q < s.size() && (s[q] == L';' || s[q] == L',' || s[q] == L')')) {
            if (k1 == 2) Throw(L"#BEZUG!");
            i = q;
            AddCells(vals, r1, c1, r1, c1);
            return true;
        }
        return false;
    }

    double Func(const std::wstring& nameIn)
    {
        std::wstring name;
        for (wchar_t ch : nameIn) name.push_back(static_cast<wchar_t>(std::towupper(ch)));
        enum { Sum, Avg, Min, Max, Count } fn;
        if (name == L"SUMME" || name == L"SUM") fn = Sum;
        else if (name == L"MITTELWERT" || name == L"AVERAGE") fn = Avg;
        else if (name == L"MIN") fn = Min;
        else if (name == L"MAX") fn = Max;
        else if (name == L"ANZAHL" || name == L"COUNT") fn = Count;
        else Throw(L"#NAME?");
        if (!Eat(L'(')) Throw(L"#SYNTAX!");
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
            const double d = p.Expr();
            p.Ws();
            if (p.i != t.size()) throw detail::Fail{ L"#SYNTAX!" };
            v.kind = Value::Number;
            v.num = d;
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

}  // namespace sheet
