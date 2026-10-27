#pragma once
// Datenmodell, Serialisierung, Suche, Passwort-Generator, Staerke-Bewertung und verschluesselte Tresor-Datei von Duncan's Vault.
// Reine Logik ohne Oberflaeche (testbar). Alles bleibt lokal: eine einzige Datei, keine Netzwerkzugriffe.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "Crypto.h"
#include "UsbKey.h"

namespace vault {

enum class Cat { Password = 0, License = 1, Wifi = 2, Note = 3 };
constexpr int kCatCount = 4;
inline const wchar_t* CatName(Cat c)
{
    static const wchar_t* const n[kCatCount] = {L"Passwörter & Accounts", L"Software-Lizenzen", L"WLAN-Schlüssel", L"Vertrauliche Notizen"};
    return n[static_cast<int>(c)];
}

// Welche Felder eine Kategorie hat und wie sie heissen (leer/nullptr = Feld entfaellt)
struct Schema {
    const wchar_t* user;     // Benutzername / Lizenznehmer / Netzwerkname
    const wchar_t* secret;   // Passwort / Lizenzschluessel / WLAN-Schluessel
    const wchar_t* url;
    const wchar_t* notes;
};
inline const Schema& SchemaOf(Cat c)
{
    static const Schema s[kCatCount] = {
        {L"Benutzername", L"Passwort", L"Webseite / URL", L"Notiz"},
        {L"Lizenznehmer / E-Mail", L"Lizenzschlüssel / Key", L"Produkt-Webseite", L"Notiz"},
        {L"WLAN-Name (SSID)", L"WLAN-Schlüssel", nullptr, L"Notiz"},
        {nullptr, nullptr, nullptr, L"Inhalt der Notiz"},
    };
    return s[static_cast<int>(c)];
}

// Wort fuer das Geheimnis in Meldungen ("Passwort kopiert!")
inline const wchar_t* SecretWord(Cat c)
{
    return c == Cat::License ? L"Lizenzschlüssel" : c == Cat::Wifi ? L"WLAN-Schlüssel" : L"Passwort";
}

struct Entry {
    std::wstring id;
    Cat cat = Cat::Password;
    std::wstring title, user, secret, url, tags, notes;
    long long created = 0, modified = 0;   // Unix-Sekunden
};

inline void Wipe(Entry& e)
{
    Wipe(e.id); Wipe(e.title); Wipe(e.user); Wipe(e.secret); Wipe(e.url); Wipe(e.tags); Wipe(e.notes);
}
inline void Wipe(std::vector<Entry>& v)
{
    for (Entry& e : v) Wipe(e);
    v.clear();
}

inline std::wstring NewId()
{
    unsigned char b[8]{};
    RandomBytes(b, sizeof b);
    static const wchar_t* hex = L"0123456789abcdef";
    std::wstring s;
    for (unsigned char c : b) { s.push_back(hex[c >> 4]); s.push_back(hex[c & 15]); }
    return s;
}

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

// ---- Serialisierung: eine Zeile je Eintrag, Felder durch Tab getrennt, \\ \t \n \r maskiert ----

inline std::wstring Esc(const std::wstring& s)
{
    std::wstring o;
    for (wchar_t c : s) {
        switch (c) {
            case L'\\': o += L"\\\\"; break;
            case L'\t': o += L"\\t"; break;
            case L'\n': o += L"\\n"; break;
            case L'\r': o += L"\\r"; break;
            default: o.push_back(c);
        }
    }
    return o;
}
inline std::wstring Unesc(const std::wstring& s)
{
    std::wstring o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            const wchar_t n = s[++i];
            o.push_back(n == L't' ? L'\t' : n == L'n' ? L'\n' : n == L'r' ? L'\r' : n);
        } else o.push_back(s[i]);
    }
    return o;
}

inline std::string Serialize(const std::vector<Entry>& entries)
{
    std::wstring out = L"#DuncansVault 1\n";
    for (const Entry& e : entries) {
        out += Esc(e.id) + L"\t" + std::to_wstring(static_cast<int>(e.cat)) + L"\t" + Esc(e.title) + L"\t" + Esc(e.user) + L"\t" + Esc(e.secret) + L"\t" +
               Esc(e.url) + L"\t" + Esc(e.tags) + L"\t" + Esc(e.notes) + L"\t" + std::to_wstring(e.created) + L"\t" + std::to_wstring(e.modified) + L"\n";
    }
    std::string utf8 = ToUtf8(out);
    Wipe(out);
    return utf8;
}

inline bool Parse(const std::string& utf8, std::vector<Entry>& entries)
{
    entries.clear();
    std::wstring all = FromUtf8(utf8);
    if (all.compare(0, 15, L"#DuncansVault 1") != 0) { Wipe(all); return false; }
    size_t pos = 0;
    while (pos < all.size()) {
        size_t nl = all.find(L'\n', pos);
        if (nl == std::wstring::npos) nl = all.size();
        const std::wstring line = all.substr(pos, nl - pos);
        pos = nl + 1;
        if (line.empty() || line[0] == L'#') continue;
        std::vector<std::wstring> f;
        size_t p = 0;
        for (;;) {
            const size_t t = line.find(L'\t', p);
            if (t == std::wstring::npos) { f.push_back(line.substr(p)); break; }
            f.push_back(line.substr(p, t - p));
            p = t + 1;
        }
        if (f.size() < 10) continue;
        Entry e;
        e.id = Unesc(f[0]);
        int c = 0;
        try { c = std::stoi(f[1]); } catch (...) {}
        e.cat = static_cast<Cat>(std::clamp(c, 0, kCatCount - 1));
        e.title = Unesc(f[2]); e.user = Unesc(f[3]); e.secret = Unesc(f[4]); e.url = Unesc(f[5]); e.tags = Unesc(f[6]); e.notes = Unesc(f[7]);
        try { e.created = std::stoll(f[8]); e.modified = std::stoll(f[9]); } catch (...) {}
        if (e.id.empty()) e.id = NewId();
        entries.push_back(std::move(e));
    }
    Wipe(all);
    return true;
}

// Backup importieren: unbekannte Eintraege werden hinzugefuegt, bekannte (gleiche Id) nur ersetzt, wenn das Backup neuer ist.
// Nichts wird geloescht; das Master-Passwort des Tresors bleibt unveraendert.
inline void Merge(std::vector<Entry>& into, const std::vector<Entry>& from, int& added, int& updated)
{
    added = updated = 0;
    for (const Entry& e : from) {
        Entry* cur = nullptr;
        for (Entry& x : into)
            if (x.id == e.id) { cur = &x; break; }
        if (!cur) { into.push_back(e); ++added; }
        else if (e.modified > cur->modified) { *cur = e; ++updated; }
    }
}

// ---- Suche: jedes Wort muss in Titel, Benutzername oder Tags vorkommen ----
inline bool Matches(const Entry& e, const std::wstring& query)
{
    const std::wstring hay = Lower(e.title + L" " + e.user + L" " + e.tags);
    const std::wstring q = Lower(query);
    size_t p = 0;
    while (p < q.size()) {
        while (p < q.size() && std::iswspace(q[p])) ++p;
        size_t e2 = p;
        while (e2 < q.size() && !std::iswspace(q[e2])) ++e2;
        if (e2 > p && hay.find(q.substr(p, e2 - p)) == std::wstring::npos) return false;
        p = e2;
    }
    return true;
}

// ---- Passwort-Generator ----
struct GenOptions {
    int length = 20;
    bool upper = true, lower = true, digits = true, symbols = true;
};
constexpr int kGenMin = 8, kGenMax = 64;

inline unsigned RandBelow(unsigned n)
{
    if (n <= 1) return 0;
    const unsigned limit = 0xFFFFFFFFu - (0xFFFFFFFFu % n);   // Ablehnungsverfahren: keine Verzerrung durch Modulo
    for (;;) {
        unsigned r = 0;
        if (!RandomBytes(reinterpret_cast<unsigned char*>(&r), sizeof r)) return 0;
        if (r < limit) return r % n;
    }
}

inline std::wstring Generate(GenOptions o)
{
    static const std::wstring kUpper = L"ABCDEFGHJKLMNPQRSTUVWXYZ", kLower = L"abcdefghijkmnopqrstuvwxyz", kDigits = L"23456789",
                              kSymbols = L"!@#$%&*?-_+=.:;/";   // ohne leicht verwechselbare Zeichen (I l 1 O 0)
    if (!o.upper && !o.lower && !o.digits && !o.symbols) o.lower = true;
    const int len = std::clamp(o.length, kGenMin, kGenMax);
    std::vector<const std::wstring*> pools;
    if (o.upper) pools.push_back(&kUpper);
    if (o.lower) pools.push_back(&kLower);
    if (o.digits) pools.push_back(&kDigits);
    if (o.symbols) pools.push_back(&kSymbols);
    std::wstring all;
    for (const auto* p : pools) all += *p;
    std::wstring pw;
    for (const auto* p : pools) pw.push_back((*p)[RandBelow(static_cast<unsigned>(p->size()))]);   // von jeder gewaehlten Klasse mindestens eins
    while (static_cast<int>(pw.size()) < len) pw.push_back(all[RandBelow(static_cast<unsigned>(all.size()))]);
    for (size_t i = pw.size(); i > 1; --i) std::swap(pw[i - 1], pw[RandBelow(static_cast<unsigned>(i))]);   // Fisher-Yates
    return pw;
}

// ---- Staerke-Bewertung: grobe Entropie-Schaetzung (Zeichenvorrat x effektive Laenge) ----
struct Strength {
    int level = 0;       // 0 sehr schwach .. 4 sehr stark
    double bits = 0;
    const wchar_t* label = L"";
};
inline Strength Rate(const std::wstring& pw)
{
    Strength s;
    if (pw.empty()) { s.label = L"–"; return s; }
    bool lo = false, up = false, di = false, sy = false;
    std::set<wchar_t> uniq;
    for (wchar_t c : pw) {
        uniq.insert(c);
        if (c >= L'a' && c <= L'z') lo = true;
        else if (c >= L'A' && c <= L'Z') up = true;
        else if (c >= L'0' && c <= L'9') di = true;
        else sy = true;
    }
    const double pool = (lo ? 26 : 0) + (up ? 26 : 0) + (di ? 10 : 0) + (sy ? 33 : 0);
    const double eff = std::min<double>(static_cast<double>(pw.size()), static_cast<double>(uniq.size()) * 2.5);   // Wiederholungen zaehlen kaum
    s.bits = eff * std::log2(std::max(2.0, pool));
    s.level = s.bits < 28 ? 0 : s.bits < 40 ? 1 : s.bits < 60 ? 2 : s.bits < 80 ? 3 : 4;
    static const wchar_t* const names[5] = {L"Sehr schwach", L"Schwach", L"Mittel", L"Stark", L"Sehr stark"};
    s.label = names[s.level];
    return s;
}

// ---- Tresor-Datei ----
// Version 1 (mit Master-Passwort): "DVLT" | 1 | Iterationen (4, LE) | Salz (16) | Nonce (12) | Chiffretext | GCM-Tag (16).
// Version 2 (ohne Master-Passwort): "DVLT" | 2 | Laenge (4, LE) | DPAPI-geschuetzter Zufallsschluessel | Nonce (12) | Chiffretext | GCM-Tag (16);
//   der Schluessel ist an das Windows-Konto gebunden, die Datei ist trotzdem AES-256-GCM-verschluesselt.
// Alles vor dem Chiffretext wird als zusaetzliche authentifizierte Daten mitgeprueft.
constexpr unsigned kDefaultIterations = 600000;
inline unsigned& MinIterations() { static unsigned n = 100000; return n; }   // Dateien mit weniger Runden werden abgelehnt (Tests senken den Wert)
constexpr size_t kHeaderLen = 4 + 1 + 4 + 16 + 12;   // Version 1

struct Session {
    SecBytes key;         // Hauptschluessel: gesperrter Speicher, wird beim Freigeben ueberschrieben
    Bytes salt;
    unsigned iterations = 0;
    bool dpapi = false;   // Tresor ohne Master-Passwort (Schluessel per Windows-Konto geschuetzt)
    bool usb = false;     // Tresor mit USB-Vault-Key (Dateiversion 3)
    unsigned char usbFlags = 0;   // usb: kUsbNeedsPw = zusaetzlich zum Stick ist ein Master-Passwort noetig
    Bytes blob;           // dpapi: geschuetzter Schluessel; usb: die beiden Schluesselfaecher (Stick, Wiederherstellung) samt Stick-Id, wie in der Datei
    bool Open() const { return key.size() == 32; }
    void Close() { Wipe(key); salt.clear(); blob.clear(); iterations = 0; dpapi = false; usb = false; usbFlags = 0; }
};

// NeedUsb: der Tresor braucht einen USB-Vault-Key (OpenFile ohne Stick-Angabe); WrongStick: angeschlossener Stick gehoert nicht zu diesem Tresor
enum class Result { Ok, NotFound, BadFormat, WrongPassword, IoError, CryptoError, NeedUsb, WrongStick };
enum class FileMode { Missing, Password, Dpapi, Invalid, Usb };

inline bool NewSession(const std::wstring& password, unsigned iterations, Session& s)
{
    s.Close();
    s.salt.assign(16, 0);
    if (!RandomBytes(s.salt.data(), s.salt.size())) return false;
    s.iterations = iterations;
    return DeriveKey(password, s.salt, iterations, s.key);
}

// Neuer Zufallsschluessel, per DPAPI an das Windows-Konto gebunden (Tresor ohne Master-Passwort)
inline bool NewDpapiSession(Session& s)
{
    s.Close();
    s.key.assign(32, 0);
    if (!RandomBytes(s.key.data(), s.key.size()) || !DpapiProtect(s.key, s.blob)) { s.Close(); return false; }
    s.dpapi = true;
    return true;
}

// Art der Tresor-Datei am Dateikopf erkennen (ohne zu entschluesseln)
inline FileMode ReadMode(const std::filesystem::path& path)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return FileMode::Missing;
    std::ifstream in(path, std::ios::binary);
    unsigned char h[5]{};
    if (!in || !in.read(reinterpret_cast<char*>(h), 5) || h[0] != 'D' || h[1] != 'V' || h[2] != 'L' || h[3] != 'T') return FileMode::Invalid;
    return h[4] == 1 ? FileMode::Password : h[4] == 2 ? FileMode::Dpapi : h[4] == 3 ? FileMode::Usb : FileMode::Invalid;
}

inline void PutU32(Bytes& b, unsigned v)
{
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF));
}
inline unsigned GetU32(const Bytes& b, size_t at)
{
    unsigned v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<unsigned>(b[at + i]) << (8 * i);
    return v;
}

// ---- USB-Vault-Key (Dateiversion 3) ----
// "DVLT" | 3 | Flags (1) | Iterationen (4, LE) | Faecher (168) | Nonce (12) | Chiffretext | GCM-Tag (16).
// Die Daten sind mit einem zufaelligen Hauptschluessel (MK) verschluesselt. Der MK steht nie im Klartext in der Datei, sondern zweimal
// verpackt (AES-GCM) in je einem Fach:
//   Fach 1 (Stick):             Salz (16) | Nonce (12) | verpackter MK (32) | Tag (16); Schluessel = PBKDF2(Stick-Geheimnis [+ Master-Passwort])
//   Fach 2 (Wiederherstellung): gleicher Aufbau; Schluessel = PBKDF2(Wiederherstellungscode)
//   danach die Stick-Id (16): sie ordnet den Stick dem Tresor zu und verraet nichts Geheimes.
// Alles vor dem Chiffretext (inkl. Flags und Faecher) wird als zusaetzliche authentifizierte Daten geprueft; die Faecher binden Flags,
// Iterationen und Stick-Id zusaetzlich als eigene authentifizierte Daten.
constexpr unsigned char kUsbNeedsPw = 1;
constexpr size_t kSlotLen = 16 + 12 + 32 + 16;                 // Salz + Nonce + MK + Tag
constexpr size_t kUsbBlobLen = 2 * kSlotLen + 16;              // zwei Faecher + Stick-Id
constexpr size_t kUsbHeadLen = 5 + 1 + 4 + kUsbBlobLen + 12;

inline Bytes SlotAad(unsigned char flags, unsigned iterations, const Bytes& stickId, unsigned char slot)
{
    Bytes a = {'D', 'V', 'L', 'T', 3, flags};
    PutU32(a, iterations);
    a.insert(a.end(), stickId.begin(), stickId.end());
    a.push_back(slot);
    return a;
}

// MK mit kek verpacken: dst bekommt Nonce (12) | Chiffretext (32) | Tag (16)
inline bool WrapInto(const SecBytes& kek, const Bytes& aad, const SecBytes& mk, unsigned char* dst)
{
    Bytes nonce(12, 0), cipher;
    unsigned char tag[16]{};
    if (mk.size() != 32 || !RandomBytes(nonce.data(), 12) || !GcmCrypt(true, kek, nonce, aad, mk.data(), 32, cipher, tag)) return false;
    memcpy(dst, nonce.data(), 12);
    memcpy(dst + 12, cipher.data(), 32);
    memcpy(dst + 44, tag, 16);
    return true;
}

inline bool UnwrapFrom(const SecBytes& kek, const Bytes& aad, const unsigned char* src, SecBytes& mk)
{
    const Bytes nonce(src, src + 12);
    Bytes out;
    unsigned char tag[16];
    memcpy(tag, src + 44, 16);
    if (!GcmCrypt(false, kek, nonce, aad, src + 12, 32, out, tag)) return false;
    mk.assign(out.begin(), out.end());
    Wipe(out);
    return mk.size() == 32;
}

// Stick-Geheimnis (+ optional Master-Passwort) -> Schluessel fuer Fach 1
inline bool DeriveUsbKek(const SecBytes& secret, const std::wstring& password, const Bytes& salt, unsigned iterations, SecBytes& kek)
{
    SecBytes in(secret.begin(), secret.end());
    std::string pw = ToUtf8(password);
    in.insert(in.end(), pw.begin(), pw.end());
    Wipe(pw);
    const bool ok = DeriveKeyBytes(in.data(), in.size(), salt, iterations, kek);
    Wipe(in);
    return ok;
}

inline bool DeriveRecoveryKek(const std::wstring& code, const Bytes& salt, unsigned iterations, SecBytes& kek)
{
    std::string c = ToUtf8(NormalizeCode(code));
    if (c.size() != 32) { Wipe(c); return false; }
    const bool ok = DeriveKeyBytes(reinterpret_cast<const unsigned char*>(c.data()), c.size(), salt, iterations, kek);
    Wipe(c);
    return ok;
}

// Neuer Tresorschluessel (Zufall), verpackt fuer den Stick (mit optionalem Master-Passwort) und fuer einen frisch erzeugten Wiederherstellungscode
inline bool NewUsbSession(const StickKey& stick, const std::wstring& password, unsigned iterations, Session& s, std::wstring& recoveryCode)
{
    s.Close();
    recoveryCode.clear();
    if (!stick.Valid()) return false;
    SecBytes mk(32, 0);
    const unsigned char flags = password.empty() ? 0 : kUsbNeedsPw;
    Bytes blob(kUsbBlobLen, 0);
    unsigned char* p = blob.data();
    bool ok = RandomBytes(mk.data(), 32) && RandomBytes(p, 16) && RandomBytes(p + kSlotLen, 16);
    if (ok) memcpy(p + 2 * kSlotLen, stick.id.data(), 16);
    std::wstring code = NewRecoveryCode();
    SecBytes k1, k2;
    ok = ok && DeriveUsbKek(stick.secret, password, Bytes(p, p + 16), iterations, k1)
            && DeriveRecoveryKek(code, Bytes(p + kSlotLen, p + kSlotLen + 16), iterations, k2)
            && WrapInto(k1, SlotAad(flags, iterations, stick.id, 1), mk, p + 16)
            && WrapInto(k2, SlotAad(flags, iterations, stick.id, 2), mk, p + kSlotLen + 16);
    Wipe(k1);
    Wipe(k2);
    if (!ok) { Wipe(mk); Wipe(code); return false; }
    s.key = mk;
    Wipe(mk);
    s.usb = true;
    s.usbFlags = flags;
    s.iterations = iterations;
    s.blob = blob;
    recoveryCode = code;
    return true;
}

// Stick-Id und Flags am Dateikopf lesen (ohne zu entschluesseln)
inline bool ReadUsbHeader(const std::filesystem::path& path, unsigned char& flags, Bytes& stickId)
{
    std::ifstream in(path, std::ios::binary);
    Bytes h(10 + kUsbBlobLen, 0);
    if (!in || !in.read(reinterpret_cast<char*>(h.data()), static_cast<std::streamsize>(h.size())) || h[0] != 'D' || h[1] != 'V' || h[2] != 'L' || h[3] != 'T' || h[4] != 3) return false;
    flags = h[5];
    stickId.assign(h.begin() + 10 + 2 * kSlotLen, h.end());
    return true;
}

// Womit ein USB-Tresor geoeffnet wird: mit dem Stick (und ggf. Master-Passwort) oder mit dem Wiederherstellungscode (dann steht er in 'password')
struct UsbUnlock {
    const StickKey* stick = nullptr;
    bool recovery = false;
};

// Speichert plain verschluesselt; die vorherige Datei bleibt als <Datei>.bak erhalten, geschrieben wird atomar ueber eine .tmp-Datei.
inline Result SaveFile(const std::filesystem::path& path, const Session& s, const std::string& plain)
{
    if (!s.Open() || plain.empty()) return Result::CryptoError;
    Bytes nonce(12, 0);
    if (!RandomBytes(nonce.data(), nonce.size())) return Result::CryptoError;
    Bytes head = {'D', 'V', 'L', 'T', static_cast<unsigned char>(s.usb ? 3 : s.dpapi ? 2 : 1)};
    if (s.usb) {
        if (s.blob.size() != kUsbBlobLen) return Result::CryptoError;
        head.push_back(s.usbFlags);
        PutU32(head, s.iterations);
        head.insert(head.end(), s.blob.begin(), s.blob.end());
    } else if (s.dpapi) {
        if (s.blob.empty()) return Result::CryptoError;
        PutU32(head, static_cast<unsigned>(s.blob.size()));
        head.insert(head.end(), s.blob.begin(), s.blob.end());
    } else {
        PutU32(head, s.iterations);
        head.insert(head.end(), s.salt.begin(), s.salt.end());
    }
    head.insert(head.end(), nonce.begin(), nonce.end());
    Bytes cipher;
    unsigned char tag[16]{};
    if (!GcmCrypt(true, s.key, nonce, head, reinterpret_cast<const unsigned char*>(plain.data()), plain.size(), cipher, tag)) return Result::CryptoError;

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path tmp = path.wstring() + L".tmp", bak = path.wstring() + L".bak";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) return Result::IoError;
        o.write(reinterpret_cast<const char*>(head.data()), static_cast<std::streamsize>(head.size()));
        o.write(reinterpret_cast<const char*>(cipher.data()), static_cast<std::streamsize>(cipher.size()));
        o.write(reinterpret_cast<const char*>(tag), 16);
        o.flush();
        if (!o) { o.close(); std::filesystem::remove(tmp, ec); return Result::IoError; }
    }
    if (std::filesystem::exists(path, ec)) std::filesystem::copy_file(path, bak, std::filesystem::copy_options::overwrite_existing, ec);
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(tmp, ec);
        return Result::IoError;
    }
    return Result::Ok;
}

// Entschluesselt; bei Erfolg ist s fuer weitere Speichervorgaenge (gleiches Salz/Schluessel) bereit.
// Version 2 braucht kein Passwort (das Windows-Konto genuegt); passt das Konto nicht, kommt WrongPassword.
inline Result OpenFile(const std::filesystem::path& path, const std::wstring& password, Session& s, std::string& plain, const UsbUnlock* usb = nullptr)
{
    plain.clear();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return Result::NotFound;
    std::ifstream in(path, std::ios::binary);
    if (!in) return Result::IoError;
    Bytes data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < 5 + 4 + 12 + 16 + 1 || data.size() > 64u * 1024 * 1024 || data[0] != 'D' || data[1] != 'V' || data[2] != 'L' || data[3] != 'T') return Result::BadFormat;
    s.Close();
    size_t headLen = 0;
    Bytes nonce;
    if (data[4] == 1) {
        if (data.size() < kHeaderLen + 16 + 1) return Result::BadFormat;
        const unsigned iter = GetU32(data, 5);
        if (iter < MinIterations() || iter > 20000000u) return Result::BadFormat;
        s.iterations = iter;
        s.salt.assign(data.begin() + 9, data.begin() + 25);
        nonce.assign(data.begin() + 25, data.begin() + 37);
        headLen = kHeaderLen;
        if (!DeriveKey(password, s.salt, iter, s.key)) return Result::CryptoError;
    } else if (data[4] == 2) {
        const unsigned bl = GetU32(data, 5);
        if (bl == 0 || bl > 4096 || data.size() < 9 + static_cast<size_t>(bl) + 12 + 16 + 1) return Result::BadFormat;
        s.blob.assign(data.begin() + 9, data.begin() + 9 + bl);
        nonce.assign(data.begin() + 9 + bl, data.begin() + 9 + bl + 12);
        headLen = 9 + bl + 12;
        s.dpapi = true;
        if (!DpapiUnprotect(s.blob, s.key) || s.key.size() != 32) { s.Close(); return Result::WrongPassword; }
    } else if (data[4] == 3) {
        if (data.size() < kUsbHeadLen + 16 + 1) return Result::BadFormat;
        const unsigned char flags = data[5];
        const unsigned iter = GetU32(data, 6);
        if (iter < MinIterations() || iter > 20000000u) return Result::BadFormat;
        const Bytes blob(data.begin() + 10, data.begin() + 10 + kUsbBlobLen);
        const Bytes id(blob.begin() + 2 * kSlotLen, blob.end());
        nonce.assign(data.begin() + 10 + kUsbBlobLen, data.begin() + kUsbHeadLen);
        headLen = kUsbHeadLen;
        if (!usb) return Result::NeedUsb;
        SecBytes kek;
        size_t slot = 0;
        if (usb->recovery) {
            slot = kSlotLen;
            if (!DeriveRecoveryKek(password, Bytes(blob.begin() + slot, blob.begin() + slot + 16), iter, kek)) return Result::WrongPassword;
        } else {
            if (!usb->stick || !usb->stick->Valid() || usb->stick->id != id) return Result::WrongStick;
            if ((flags & kUsbNeedsPw) && password.empty()) return Result::WrongPassword;
            if (!DeriveUsbKek(usb->stick->secret, (flags & kUsbNeedsPw) ? password : std::wstring(), Bytes(blob.begin(), blob.begin() + 16), iter, kek)) return Result::CryptoError;
        }
        const bool unwrapped = UnwrapFrom(kek, SlotAad(flags, iter, id, usb->recovery ? 2 : 1), blob.data() + slot + 16, s.key);
        Wipe(kek);
        if (!unwrapped) { s.Close(); return Result::WrongPassword; }
        s.usb = true;
        s.usbFlags = flags;
        s.iterations = iter;
        s.blob = blob;
    } else {
        return Result::BadFormat;
    }
    const Bytes head(data.begin(), data.begin() + headLen);
    const size_t cl = data.size() - headLen - 16;
    Bytes out;
    unsigned char tag[16];
    std::copy(data.end() - 16, data.end(), tag);
    if (!GcmCrypt(false, s.key, nonce, head, data.data() + headLen, cl, out, tag)) {
        s.Close();
        return Result::WrongPassword;
    }
    plain.assign(reinterpret_cast<const char*>(out.data()), out.size());
    Wipe(out);
    return Result::Ok;
}

}  // namespace vault
