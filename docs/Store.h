#pragma once
// Oggys Docs-Files: Datenmodell der Rechnungen, Index-Datei, Ordnerstruktur und Suche (ohne UI, ohne Win32,
// damit es getrennt testbar bleibt).
//
// Ordnerstruktur:  <Wurzel>\<Jahr>\<Glaeubiger>\<Datei>      (z. B. Rechnungen\2026\Netzanbieter\Strom_Oktober.pdf)
// Zu jeder Datei kennt der Index Betrag, Faelligkeit, Status, Schlagworte und Notiz. Er liegt als Textdatei
// ".oggys-index.tsv" in der Wurzel, damit er mit dem Ordner mitwandert; Schluessel ist der relative Pfad.
#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../common/Changelog.h"   // changelog::Utf8ToWide

namespace docs {

enum class Status { Open, Paid, Installment, Fixed };

inline const wchar_t* StatusName(Status s)
{
    switch (s) {
        case Status::Paid: return L"Bezahlt";
        case Status::Installment: return L"Ratenzahlung";
        case Status::Fixed: return L"Fixkosten";
        default: return L"Offen";
    }
}

inline bool StatusFromName(const std::wstring& n, Status& out)
{
    for (Status s : {Status::Open, Status::Paid, Status::Installment, Status::Fixed})
        if (n == StatusName(s)) { out = s; return true; }
    return false;
}

// ---- Text, Betraege, Datum ----------------------------------------------------------------------

inline std::wstring Lower(std::wstring s)
{
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

inline std::wstring Trim(const std::wstring& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::iswspace(s[a])) ++a;
    while (b > a && std::iswspace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

inline std::string WideToUtf8(const std::wstring& w)
{
    std::string out;
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t cp = static_cast<uint32_t>(w[i]);
        if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size() && w[i + 1] >= 0xDC00 && w[i + 1] <= 0xDFFF)
            cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<uint32_t>(w[++i]) - 0xDC00);
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) { out.push_back(static_cast<char>(0xE0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        else { out.push_back(static_cast<char>(0xF0 | (cp >> 18))); out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    }
    return out;
}

// Ordner-/Dateiname aus Freitext: keine verbotenen Zeichen, keine Punkte/Leerzeichen am Ende, begrenzte Laenge.
inline std::wstring SanitizeName(const std::wstring& in, const std::wstring& fallback)
{
    std::wstring out;
    for (wchar_t c : in) {
        if (c < 32 || std::wstring(L"<>:\"/\\|?*").find(c) != std::wstring::npos) c = L' ';
        out.push_back(c);
    }
    std::wstring t;   // Mehrfach-Leerzeichen zusammenfassen
    for (wchar_t c : out) {
        if (c == L' ' && !t.empty() && t.back() == L' ') continue;
        t.push_back(c);
    }
    t = Trim(t);
    while (!t.empty() && (t.back() == L'.' || t.back() == L' ')) t.pop_back();
    if (t.size() > 60) t.resize(60);
    while (!t.empty() && (t.back() == L'.' || t.back() == L' ')) t.pop_back();
    static const wchar_t* reserved[] = {L"CON", L"PRN", L"AUX", L"NUL", L"COM1", L"COM2", L"COM3", L"LPT1", L"LPT2", L"LPT3"};
    for (auto r : reserved)
        if (Lower(t) == Lower(r)) { t += L"_"; break; }
    return t.empty() ? fallback : t;
}

// Dateiname: wie SanitizeName, aber die Endung bleibt erhalten (der Name wird gekuerzt, nicht die Endung).
inline std::wstring SanitizeFileName(const std::wstring& in, const std::wstring& fallback)
{
    const size_t dot = in.find_last_of(L'.');
    std::wstring stem = dot == std::wstring::npos || dot == 0 ? in : in.substr(0, dot);
    std::wstring ext = dot == std::wstring::npos || dot == 0 ? L"" : in.substr(dot);
    if (ext.size() > 8) ext.clear();
    std::wstring e;
    for (wchar_t c : ext)
        if (std::iswalnum(c) || c == L'.') e.push_back(c);
    return SanitizeName(stem, fallback) + e;
}

// "1.234,56", "1234.56", "12,5 EUR", "99" -> Cent. Mehr als zwei Nachkommastellen oder Buchstaben (ausser Waehrung) = Fehler.
inline long long ParseAmountCents(const std::wstring& in, bool& ok)
{
    ok = false;
    std::wstring s;
    for (wchar_t c : in)
        if (!std::iswspace(c)) s.push_back(c);
    for (const wchar_t* cur : {L"€", L"EUR", L"eur", L"Eur"}) {
        const std::wstring cs = cur;
        if (s.size() >= cs.size() && s.compare(s.size() - cs.size(), cs.size(), cs) == 0) { s.resize(s.size() - cs.size()); break; }
    }
    if (s.empty()) { ok = true; return 0; }   // leer = 0 (der Aufrufer entscheidet, ob das "nicht erfasst" heisst)
    const size_t comma = s.find_last_of(L','), dot = s.find_last_of(L'.');
    size_t decPos = std::wstring::npos;
    if (comma != std::wstring::npos && (dot == std::wstring::npos || comma > dot)) decPos = comma;
    else if (dot != std::wstring::npos) {
        // Punkt nur als Dezimaltrenner, wenn er einmal vorkommt und nicht wie ein Tausenderpunkt (genau drei Ziffern danach) aussieht
        const bool single = s.find(L'.') == dot;
        if (single && (s.size() - dot - 1 != 3 || comma != std::wstring::npos || dot == 0)) decPos = dot;
    }
    long long whole = 0, frac = 0;
    int fracDigits = 0;
    bool any = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const wchar_t c = s[i];
        if (i == decPos) continue;
        if (c == L'.' || c == L',') {   // das Zeichen, das der Dezimaltrenner ist, darf nicht noch einmal vorkommen; das andere ist Tausendertrenner
            const wchar_t decChar = decPos == std::wstring::npos ? L',' : s[decPos];
            if (c == decChar) return 0;
            continue;
        }
        if (c == L'-' && i == 0) continue;   // negative Betraege (Gutschriften) zulassen
        if (c < L'0' || c > L'9') return 0;
        any = true;
        if (decPos != std::wstring::npos && i > decPos) {
            if (++fracDigits > 2) return 0;
            frac = frac * 10 + (c - L'0');
        } else {
            if (whole > 9000000000000LL) return 0;
            whole = whole * 10 + (c - L'0');
        }
    }
    if (!any) return 0;
    if (fracDigits == 1) frac *= 10;
    long long cents = whole * 100 + frac;
    if (s[0] == L'-') cents = -cents;
    ok = true;
    return cents;
}

// 123456 -> "1.234,56 EUR-Zeichen"; Cent < 0 ist hier ein echter Negativbetrag, kUnknown steht fuer "kein Betrag"
constexpr long long kUnknown = INT64_MIN;
inline std::wstring FormatAmount(long long cents, bool withEuro = true)
{
    if (cents == kUnknown) return L"–";
    const bool neg = cents < 0;
    unsigned long long a = static_cast<unsigned long long>(neg ? -cents : cents);
    std::wstring whole = std::to_wstring(a / 100), grouped;
    for (size_t i = 0; i < whole.size(); ++i) {
        if (i && (whole.size() - i) % 3 == 0) grouped.push_back(L'.');
        grouped.push_back(whole[i]);
    }
    const unsigned c = static_cast<unsigned>(a % 100);
    std::wstring out = (neg ? L"-" : L"") + grouped + L"," + (c < 10 ? L"0" : L"") + std::to_wstring(c);
    return withEuro ? out + L" €" : out;
}

// Kalender: Tage seit 1970-01-01 (proleptisch gregorianisch)
inline long long DaysFromCivil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}
inline void CivilFromDays(long long z, int& y, unsigned& m, unsigned& d)
{
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = static_cast<int>(yoe) + static_cast<int>(era) * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y += m <= 2;
}
inline bool ValidDate(int y, int m, int d)
{
    if (y < 1900 || y > 2999 || m < 1 || m > 12 || d < 1) return false;
    int yy; unsigned mm, dd;
    CivilFromDays(DaysFromCivil(y, static_cast<unsigned>(m), static_cast<unsigned>(d)), yy, mm, dd);
    return yy == y && static_cast<int>(mm) == m && static_cast<int>(dd) == d;   // 31.02. faellt durch
}
inline std::wstring IsoFrom(int y, int m, int d)
{
    wchar_t buf[16];
    swprintf(buf, 16, L"%04d-%02d-%02d", y, m, d);
    return buf;
}

// "24.12.2026", "24.12.26", "2026-12-24" -> ISO "2026-12-24". Leer ist erlaubt (ok = true, iso leer).
inline bool ParseDate(const std::wstring& in, std::wstring& iso)
{
    const std::wstring s = Trim(in);
    iso.clear();
    if (s.empty()) return true;
    int a = 0, b = 0, c = 0, n = 0;
    std::vector<int> nums;
    std::wstring cur;
    wchar_t sep = 0;
    for (wchar_t ch : s) {
        if (ch >= L'0' && ch <= L'9') { cur.push_back(ch); }
        else if (ch == L'.' || ch == L'-' || ch == L'/') {
            if (cur.empty() || (sep && ch != sep)) return false;
            sep = ch;
            nums.push_back(std::stoi(cur));
            cur.clear();
        } else return false;
        if (cur.size() > 4) return false;
    }
    if (cur.empty()) return false;
    nums.push_back(std::stoi(cur));
    n = static_cast<int>(nums.size());
    if (n != 3) return false;
    a = nums[0]; b = nums[1]; c = nums[2];
    int y, m, d;
    if (sep == L'-' && s.find(L'-') == 4) { y = a; m = b; d = c; }     // 2026-12-24
    else { d = a; m = b; y = c; if (y < 100) y += 2000; }              // 24.12.2026 / 24.12.26
    if (!ValidDate(y, m, d)) return false;
    iso = IsoFrom(y, m, d);
    return true;
}

inline std::wstring FormatDate(const std::wstring& iso)   // "2026-12-24" -> "24.12.2026"
{
    if (iso.size() != 10) return L"";
    return iso.substr(8, 2) + L"." + iso.substr(5, 2) + L"." + iso.substr(0, 4);
}

inline bool IsoToDays(const std::wstring& iso, long long& days)
{
    if (iso.size() != 10) return false;
    try {
        const int y = std::stoi(iso.substr(0, 4)), m = std::stoi(iso.substr(5, 2)), d = std::stoi(iso.substr(8, 2));
        if (!ValidDate(y, m, d)) return false;
        days = DaysFromCivil(y, static_cast<unsigned>(m), static_cast<unsigned>(d));
        return true;
    } catch (...) { return false; }
}

inline std::wstring AddDaysIso(const std::wstring& iso, int add)
{
    long long days = 0;
    if (!IsoToDays(iso, days)) return L"";
    int y; unsigned m, d;
    CivilFromDays(days + add, y, m, d);
    return IsoFrom(y, static_cast<int>(m), static_cast<int>(d));
}

// ---- Datenmodell ----------------------------------------------------------------------------------

struct Meta {
    long long cents = kUnknown;   // kUnknown = nicht erfasst
    std::wstring due;             // ISO oder leer
    Status status = Status::Open;
    std::wstring tags, note;
    std::wstring rate;            // Ratenzahlung: "Rate 3 von 12" / "50 € / Monat" (nur bei Status Ratenzahlung angezeigt)
};
using MetaMap = std::map<std::wstring, Meta>;   // Schluessel: relativer Pfad, Trenner '\'

struct Doc {
    std::wstring rel, name, year, creditor;
    Meta m;
    unsigned long long size = 0, mtime = 0;
    bool known = false;   // im Index vorhanden (sonst frisch gefunden)
};

inline std::wstring CleanField(std::wstring s)   // Tab/Zeilenumbruch haetten den Index kaputt gemacht
{
    for (auto& c : s)
        if (c == L'\t' || c == L'\n' || c == L'\r') c = L' ';
    return Trim(s);
}

inline std::string SerializeIndex(const MetaMap& map)
{
    std::wstring out = L"#OggysDocsFiles-Index 1\n";
    for (const auto& kv : map) {
        const Meta& m = kv.second;
        out += CleanField(kv.first) + L"\t" + (m.cents == kUnknown ? L"" : std::to_wstring(m.cents)) + L"\t" + m.due + L"\t" +
               StatusName(m.status) + L"\t" + CleanField(m.tags) + L"\t" + CleanField(m.note) + L"\t" + CleanField(m.rate) + L"\n";
    }
    return WideToUtf8(out);
}

inline MetaMap ParseIndex(const std::string& utf8)
{
    MetaMap map;
    std::string text = utf8.size() > 8u * 1024 * 1024 ? utf8.substr(0, 8u * 1024 * 1024) : utf8;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    const std::wstring all = changelog::Utf8ToWide(text);
    size_t pos = 0;
    while (pos < all.size()) {
        size_t nl = all.find(L'\n', pos);
        if (nl == std::wstring::npos) nl = all.size();
        std::wstring line = all.substr(pos, nl - pos);
        pos = nl + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty() || line[0] == L'#') continue;
        std::vector<std::wstring> f;
        size_t p = 0;
        for (;;) {
            const size_t t = line.find(L'\t', p);
            if (t == std::wstring::npos) { f.push_back(line.substr(p)); break; }
            f.push_back(line.substr(p, t - p));
            p = t + 1;
        }
        if (f.size() < 4 || f[0].empty()) continue;
        Meta m;
        if (!f[1].empty()) {
            try { m.cents = std::stoll(f[1]); } catch (...) { m.cents = kUnknown; }
        }
        std::wstring iso;
        if (ParseDate(f[2], iso)) m.due = iso;
        if (!StatusFromName(f[3], m.status)) m.status = Status::Open;
        if (f.size() > 4) m.tags = f[4];
        if (f.size() > 5) m.note = f[5];
        if (f.size() > 6) m.rate = f[6];
        map[f[0]] = m;
    }
    return map;
}

// ---- Ordner scannen ---------------------------------------------------------------------------------

inline bool IsDocFile(const std::filesystem::path& p)
{
    const std::wstring e = Lower(p.extension().wstring());
    return e == L".pdf" || e == L".jpg" || e == L".jpeg" || e == L".png";
}

// Zerlegt einen relativen Pfad "2026\Netzanbieter\x.pdf" in Jahr und Glaeubiger (fehlende Ebenen: "" bzw. "Sonstige").
inline void SplitRel(const std::wstring& rel, std::wstring& year, std::wstring& creditor, std::wstring& name)
{
    std::vector<std::wstring> parts;
    std::wstring cur;
    for (wchar_t c : rel) {
        if (c == L'\\' || c == L'/') { parts.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    parts.push_back(cur);
    name = parts.back();
    year.clear();
    creditor = L"Sonstige";
    if (parts.size() >= 3) { year = parts[0]; creditor = parts[1]; }
    else if (parts.size() == 2) { year = parts[0]; }
}

// Alle Belege unter root (Tiefe <= 6), mit Metadaten aus dem Index. Versteckte Ordner/Dateien (".") werden ignoriert.
inline std::vector<Doc> Scan(const std::filesystem::path& root, const MetaMap& meta)
{
    std::vector<Doc> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) return out;
    for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (it.depth() > 6) { it.disable_recursion_pending(); continue; }
        const std::filesystem::path& p = it->path();
        const std::wstring fname = p.filename().wstring();
        if (!fname.empty() && fname[0] == L'.') {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec) || !IsDocFile(p)) continue;
        Doc d;
        d.rel = std::filesystem::relative(p, root, ec).wstring();
        std::replace(d.rel.begin(), d.rel.end(), L'/', L'\\');
        SplitRel(d.rel, d.year, d.creditor, d.name);
        d.size = it->file_size(ec);
        d.mtime = static_cast<unsigned long long>(it->last_write_time(ec).time_since_epoch().count());
        auto f = meta.find(d.rel);
        if (f != meta.end()) { d.m = f->second; d.known = true; }
        out.push_back(std::move(d));
    }
    return out;
}

// Jahr- und Glaeubiger-Ordner der Wurzel (auch leere): {Jahr, ""} fuer einen Jahresordner, {Jahr, Glaeubiger} darunter.
// Versteckte Ordner (".") werden ignoriert. Damit zeigt der Baum die Ordnerstruktur, nicht nur die Ordner mit Belegen.
inline std::vector<std::pair<std::wstring, std::wstring>> ListFolders(const std::filesystem::path& root)
{
    std::vector<std::pair<std::wstring, std::wstring>> out;
    std::error_code ec;
    auto visible = [](const std::filesystem::path& p) {
        const std::wstring n = p.filename().wstring();
        return !n.empty() && n[0] != L'.';
    };
    for (std::filesystem::directory_iterator y(root, std::filesystem::directory_options::skip_permission_denied, ec), end; !ec && y != end; y.increment(ec)) {
        if (!y->is_directory(ec) || !visible(y->path())) continue;
        const std::wstring year = y->path().filename().wstring();
        out.push_back({year, L""});
        std::error_code ec2;
        for (std::filesystem::directory_iterator c(y->path(), std::filesystem::directory_options::skip_permission_denied, ec2), end2; !ec2 && c != end2; c.increment(ec2))
            if (c->is_directory(ec2) && visible(c->path())) out.push_back({year, c->path().filename().wstring()});
    }
    return out;
}

// ---- Suche -------------------------------------------------------------------------------------------

// Jedes Wort der Suche muss in Dateiname, Glaeubiger, Jahr, Schlagworten, Notiz, Status oder Betrag vorkommen.
inline bool Matches(const Doc& d, const std::wstring& query)
{
    const std::wstring q = Trim(query);
    if (q.empty()) return true;
    const std::wstring hay = Lower(d.name + L" " + d.creditor + L" " + d.year + L" " + d.m.tags + L" " + d.m.note + L" " + d.m.rate + L" " +
                                   StatusName(d.m.status) + L" " + FormatAmount(d.m.cents, false) + L" " + FormatDate(d.m.due));
    size_t p = 0;
    const std::wstring lq = Lower(q);
    while (p < lq.size()) {
        while (p < lq.size() && std::iswspace(lq[p])) ++p;
        size_t e = p;
        while (e < lq.size() && !std::iswspace(lq[e])) ++e;
        if (e > p && hay.find(lq.substr(p, e - p)) == std::wstring::npos) return false;
        p = e;
    }
    return true;
}

// Zielpfad fuer eine neue Datei: <root>\<Jahr>\<Glaeubiger>\<Dateiname>; vorhandene Dateien werden nicht ueberschrieben.
inline std::filesystem::path TargetPath(const std::filesystem::path& root, const std::wstring& year, const std::wstring& creditor,
                                        const std::wstring& fileName)
{
    const std::wstring y = SanitizeName(year, L"Unbekannt"), c = SanitizeName(creditor, L"Sonstige");
    const std::filesystem::path dir = root / y / c;
    const std::wstring clean = SanitizeFileName(fileName, L"Datei");
    std::filesystem::path p = dir / clean;
    std::error_code ec;
    for (int i = 2; std::filesystem::exists(p, ec) && i < 1000; ++i) {
        const std::filesystem::path base = std::filesystem::path(clean).stem(), ext = std::filesystem::path(clean).extension();
        p = dir / (base.wstring() + L" (" + std::to_wstring(i) + L")" + ext.wstring());
    }
    return p;
}

// ---- Fortlaufende Rechnungsnummern: RE-JJJJ-NNN ----
inline std::wstring FormatInvoiceNo(int year, int n)
{
    wchar_t buf[40];
    swprintf(buf, 40, L"RE-%04d-%03d", year, n);
    return buf;
}

// "RE-2026-007" (oder aelteres "2026-007") -> year/n; false bei anderem Format
inline bool ParseInvoiceNo(const std::wstring& s, int& year, int& n)
{
    size_t p = 0;
    if (s.compare(0, 3, L"RE-") == 0) p = 3;
    if (s.size() < p + 6 || s[p + 4] != L'-') return false;
    for (size_t i = p; i < p + 4; ++i)
        if (s[i] < L'0' || s[i] > L'9') return false;
    size_t e = p + 5;
    if (e >= s.size()) return false;
    for (size_t i = e; i < s.size(); ++i)
        if (s[i] < L'0' || s[i] > L'9') return false;
    if (s.size() - e > 6) return false;
    year = std::stoi(s.substr(p, 4));
    n = std::stoi(s.substr(e));
    return true;
}

// Hoechste Nummer des Jahres unter Dateinamen der Form "Rechnung_<Nummer>_<Empfaenger>.pdf"
inline int MaxInvoiceNo(const std::vector<std::wstring>& fileNames, int year)
{
    int best = 0;
    for (const std::wstring& name : fileNames) {
        if (name.compare(0, 9, L"Rechnung_") != 0) continue;
        const size_t e = name.find(L'_', 9);
        int y = 0, n = 0;
        if (ParseInvoiceNo(name.substr(9, e == std::wstring::npos ? std::wstring::npos : e - 9), y, n) && y == year) best = std::max(best, n);
    }
    return best;
}

// Naechste freie Nummer: Zaehler (nie zurueckgesetzt, auch wenn eine Rechnung geloescht wurde) und vorhandene Dateien zaehlen
inline int NextInvoiceNo(int storedCounter, const std::vector<std::wstring>& fileNames, int year)
{
    return std::max(storedCounter, MaxInvoiceNo(fileNames, year)) + 1;
}

// Gibt es schon eine Rechnungsdatei mit genau dieser Nummer?
inline bool InvoiceNoTaken(const std::vector<std::wstring>& fileNames, const std::wstring& number)
{
    const std::wstring prefix = L"Rechnung_" + number + L"_";
    for (const std::wstring& name : fileNames)
        if (name.compare(0, prefix.size(), prefix) == 0) return true;
    return false;
}

}  // namespace docs
