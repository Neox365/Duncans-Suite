// Tests fuer vault/Vault.h und Crypto.h (Windows, CNG):
//   cl /nologo /std:c++17 /EHsc /W4 /utf-8 vault\tests\VaultTest.cpp /Fe:build\VaultTest.exe
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "../Vault.h"

namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static std::vector<vault::Entry> sample() {
    std::vector<vault::Entry> v;
    vault::Entry a;
    a.id = L"a1"; a.cat = vault::Cat::Password; a.title = L"GitHub"; a.user = L"duncan@example.org"; a.secret = L"p\tä\\ss\nwort"; a.url = L"https://github.com";
    a.tags = L"dev code"; a.notes = L"Zeile 1\nZeile 2\r\nTab\there"; a.created = 1000; a.modified = 2000;
    vault::Entry b;
    b.id = L"b2"; b.cat = vault::Cat::Wifi; b.title = L"Heimnetz"; b.user = L"FRITZ!Box 7590"; b.secret = L"s3cret-Key"; b.tags = L"zuhause";
    vault::Entry c;
    c.id = L"c3"; c.cat = vault::Cat::Note; c.title = L"Notiz"; c.notes = L"nur Text";
    v.push_back(a); v.push_back(b); v.push_back(c);
    return v;
}

static void serialization() {
    const auto in = sample();
    const std::string s = vault::Serialize(in);
    std::vector<vault::Entry> out;
    CHECK(vault::Parse(s, out) && out.size() == 3);
    CHECK(out[0].secret == in[0].secret && out[0].notes == in[0].notes && out[0].title == L"GitHub" && out[0].created == 1000 && out[0].modified == 2000);
    CHECK(out[1].cat == vault::Cat::Wifi && out[1].user == L"FRITZ!Box 7590");
    CHECK(out[2].cat == vault::Cat::Note && out[2].notes == L"nur Text");
    CHECK(!vault::Parse("kein Tresor", out));
    CHECK(vault::Parse("#DuncansVault 1\nzu\tkurz\n", out) && out.empty());
}

static void merge() {
    auto base = sample();
    auto backup = sample();
    backup[0].modified = 5000; backup[0].title = L"GitHub (neu)";   // neuer als lokal (2000)
    backup[1].modified = 0; backup[1].title = L"alt";                // aelter: bleibt lokal
    vault::Entry n; n.id = L"zz9"; n.title = L"Neu"; n.modified = 1;
    backup.push_back(n);
    int added = 0, updated = 0;
    vault::Merge(base, backup, added, updated);
    CHECK(added == 1 && updated == 1 && base.size() == 4);
    CHECK(base[0].title == L"GitHub (neu)" && base[1].title == L"Heimnetz" && base[3].title == L"Neu");
    vault::Merge(base, backup, added, updated);   // zweiter Import aendert nichts mehr
    CHECK(added == 0 && updated == 0 && base.size() == 4);
    CHECK(std::wstring(vault::SchemaOf(vault::Cat::License).secret) == L"Lizenzschlüssel / Key" && std::wstring(vault::SchemaOf(vault::Cat::Wifi).user) == L"WLAN-Name (SSID)");
    CHECK(std::wstring(vault::SecretWord(vault::Cat::Password)) == L"Passwort" && std::wstring(vault::SecretWord(vault::Cat::License)) == L"Lizenzschlüssel");
}

static void search() {
    const auto v = sample();
    CHECK(vault::Matches(v[0], L"github"));
    CHECK(vault::Matches(v[0], L"DUNCAN@"));            // Benutzername
    CHECK(vault::Matches(v[0], L"code dev"));           // Tags, beliebige Reihenfolge
    CHECK(!vault::Matches(v[0], L"github gibtsnicht"));
    CHECK(!vault::Matches(v[0], L"p\tä"));              // Passwort wird nicht durchsucht
    CHECK(vault::Matches(v[1], L"fritz zuhause"));
    CHECK(vault::Matches(v[2], L""));
}

static void generator() {
    for (int i = 0; i < 50; ++i) {
        vault::GenOptions o;
        o.length = 8 + i % 40;
        const std::wstring p = vault::Generate(o);
        CHECK(static_cast<int>(p.size()) == o.length);
        bool up = false, lo = false, di = false, sy = false;
        for (wchar_t c : p) {
            if (c >= L'A' && c <= L'Z') up = true;
            else if (c >= L'a' && c <= L'z') lo = true;
            else if (c >= L'0' && c <= L'9') di = true;
            else sy = true;
        }
        CHECK(up && lo && di && sy);
    }
    vault::GenOptions d;
    d.upper = false; d.lower = false; d.symbols = false;   // nur Ziffern
    for (wchar_t c : vault::Generate(d)) CHECK(c >= L'0' && c <= L'9');
    vault::GenOptions none;
    none.upper = none.lower = none.digits = none.symbols = false;   // nichts gewaehlt -> Kleinbuchstaben
    for (wchar_t c : vault::Generate(none)) CHECK(c >= L'a' && c <= L'z');
    vault::GenOptions big;
    big.length = 1000;
    CHECK(static_cast<int>(vault::Generate(big).size()) == vault::kGenMax);
    vault::GenOptions tiny;
    tiny.length = 1;
    CHECK(static_cast<int>(vault::Generate(tiny).size()) == vault::kGenMin);
    CHECK(vault::Generate(vault::GenOptions{}) != vault::Generate(vault::GenOptions{}));   // zwei Aufrufe liefern verschiedene Passwoerter
}

static void strength() {
    CHECK(vault::Rate(L"").level == 0);
    CHECK(vault::Rate(L"aaaaaaaa").level == 0);
    CHECK(vault::Rate(L"passwort").level <= 1);
    CHECK(vault::Rate(L"Tr0ub4dor&3x").level >= 2);
    CHECK(vault::Rate(vault::Generate(vault::GenOptions{})).level >= 3);
    CHECK(vault::Rate(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJ0123456789!?").level == 4);
    CHECK(vault::Rate(L"Aa1!Aa1!Aa1!Aa1!").bits < vault::Rate(L"Xq7!mZ2#Lp9$Wv4%").bits);   // Wiederholungsmuster schlechter
}

static void file() {
    const fs::path dir = fs::temp_directory_path() / L"duncans_vault_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    const fs::path f = dir / L"vault.dat";
    vault::Session s;
    CHECK(vault::NewSession(L"Meister#Passwort 1", 2000, s) && s.Open() && s.key.size() == 32 && s.salt.size() == 16);
    const std::string plain = vault::Serialize(sample());
    CHECK(vault::SaveFile(f, s, plain) == vault::Result::Ok && fs::exists(f));
    // Datei enthaelt keinen Klartext
    std::ifstream in(f, std::ios::binary);
    const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    CHECK(raw.find("GitHub") == std::string::npos && raw.find("s3cret-Key") == std::string::npos && raw.compare(0, 4, "DVLT") == 0);

    vault::Session s2;
    std::string back;
    CHECK(vault::OpenFile(f, L"Meister#Passwort 1", s2, back) == vault::Result::Ok && back == plain);
    CHECK(vault::OpenFile(f, L"falsch", s2, back) == vault::Result::WrongPassword && back.empty() && !s2.Open());
    CHECK(vault::OpenFile(f, L"meister#passwort 1", s2, back) == vault::Result::WrongPassword);   // Gross/Klein zaehlt
    CHECK(vault::OpenFile(dir / L"gibtsnicht.dat", L"x", s2, back) == vault::Result::NotFound);

    // zweites Speichern: neuer Nonce (andere Datei), Sicherung der alten
    CHECK(vault::SaveFile(f, s, plain) == vault::Result::Ok && fs::exists(dir / L"vault.dat.bak"));
    std::ifstream in2(f, std::ios::binary);
    const std::string raw2((std::istreambuf_iterator<char>(in2)), std::istreambuf_iterator<char>());
    in2.close();
    CHECK(raw2 != raw && raw2.size() == raw.size());
    CHECK(vault::OpenFile(f, L"Meister#Passwort 1", s2, back) == vault::Result::Ok && back == plain);

    // Manipulation (ein Bit im Chiffretext, ein Byte im Kopf) wird erkannt
    for (size_t pos : {raw2.size() / 2, size_t(10)}) {
        std::string bad = raw2;
        bad[pos] = static_cast<char>(bad[pos] ^ 1);
        { std::ofstream o(f, std::ios::binary | std::ios::trunc); o.write(bad.data(), static_cast<std::streamsize>(bad.size())); }
        vault::Result r = vault::OpenFile(f, L"Meister#Passwort 1", s2, back);
        CHECK(r == vault::Result::WrongPassword || r == vault::Result::BadFormat);
        CHECK(back.empty());
    }
    // abgeschnittene / fremde Datei
    { std::ofstream o(f, std::ios::binary | std::ios::trunc); o << "DVLT"; }
    CHECK(vault::OpenFile(f, L"x", s2, back) == vault::Result::BadFormat);
    { std::ofstream o(f, std::ios::binary | std::ios::trunc); o << "irgendein anderer Dateiinhalt, der kein Tresor ist, aber lang genug ist"; }
    CHECK(vault::OpenFile(f, L"x", s2, back) == vault::Result::BadFormat);

    // Master-Passwort aendern: neues Salz, altes Passwort passt nicht mehr
    vault::Session s3;
    CHECK(vault::NewSession(L"Neues Passwort 22", 2000, s3) && s3.salt != s.salt);
    CHECK(vault::SaveFile(f, s3, plain) == vault::Result::Ok);
    CHECK(vault::OpenFile(f, L"Meister#Passwort 1", s2, back) == vault::Result::WrongPassword);
    CHECK(vault::OpenFile(f, L"Neues Passwort 22", s2, back) == vault::Result::Ok && back == plain);
    // Umlaute im Master-Passwort
    CHECK(vault::NewSession(L"Pässwörd-€-日本", 2000, s3) && vault::SaveFile(f, s3, plain) == vault::Result::Ok);
    CHECK(vault::OpenFile(f, L"Pässwörd-€-日本", s2, back) == vault::Result::Ok && back == plain);
    fs::remove_all(dir, ec);
}

static void noPassword() {
    const fs::path dir = fs::temp_directory_path() / L"duncans_vault_test_dpapi";
    std::error_code ec;
    fs::remove_all(dir, ec);
    const fs::path f = dir / L"vault.dat";
    CHECK(vault::ReadMode(f) == vault::FileMode::Missing);
    vault::Session s;
    CHECK(vault::NewDpapiSession(s) && s.Open() && s.dpapi && !s.blob.empty() && s.blob.size() != 32);   // geschuetzter Blob ist nicht der Klartextschluessel
    const std::string plain = vault::Serialize(sample());
    CHECK(vault::SaveFile(f, s, plain) == vault::Result::Ok && vault::ReadMode(f) == vault::FileMode::Dpapi);
    std::ifstream in(f, std::ios::binary);
    const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    CHECK(raw.find("GitHub") == std::string::npos && raw.compare(0, 4, "DVLT") == 0 && raw[4] == 2);
    vault::Bytes keyCopy(s.key.begin(), s.key.end());
    CHECK(raw.find(std::string(keyCopy.begin(), keyCopy.end())) == std::string::npos);   // der Schluessel steht nicht im Klartext in der Datei

    vault::Session s2;
    std::string back;
    CHECK(vault::OpenFile(f, L"", s2, back) == vault::Result::Ok && back == plain && s2.dpapi);   // ohne Passwort
    CHECK(vault::OpenFile(f, L"egal", s2, back) == vault::Result::Ok && back == plain);          // Passwort wird ignoriert
    CHECK(vault::SaveFile(f, s2, plain) == vault::Result::Ok && vault::OpenFile(f, L"", s2, back) == vault::Result::Ok);

    // Manipulation (Chiffretext und Kopf) wird erkannt
    for (size_t pos : {raw.size() - 20, size_t(12)}) {
        std::string bad = raw;
        bad[pos] = static_cast<char>(bad[pos] ^ 1);
        { std::ofstream o(f, std::ios::binary | std::ios::trunc); o.write(bad.data(), static_cast<std::streamsize>(bad.size())); }
        const vault::Result r = vault::OpenFile(f, L"", s2, back);
        CHECK(r == vault::Result::WrongPassword || r == vault::Result::BadFormat);
        CHECK(back.empty());
    }
    // Wechsel zwischen den Arten: ohne Passwort -> mit Passwort -> ohne Passwort
    vault::Session sp;
    CHECK(vault::NewSession(L"Neues Passwort 22", 2000, sp) && vault::SaveFile(f, sp, plain) == vault::Result::Ok && vault::ReadMode(f) == vault::FileMode::Password);
    CHECK(vault::OpenFile(f, L"", s2, back) == vault::Result::WrongPassword);   // jetzt wird das Passwort gebraucht
    CHECK(vault::OpenFile(f, L"Neues Passwort 22", s2, back) == vault::Result::Ok && back == plain && !s2.dpapi);
    CHECK(vault::NewDpapiSession(s) && vault::SaveFile(f, s, plain) == vault::Result::Ok && vault::ReadMode(f) == vault::FileMode::Dpapi);
    CHECK(vault::OpenFile(f, L"", s2, back) == vault::Result::Ok && back == plain);
    // fremde Datei / abgeschnitten
    { std::ofstream o(f, std::ios::binary | std::ios::trunc); o << "DVLT\x02"; }
    CHECK(vault::ReadMode(f) == vault::FileMode::Dpapi && vault::OpenFile(f, L"", s2, back) == vault::Result::BadFormat);
    { std::ofstream o(f, std::ios::binary | std::ios::trunc); o << "kein Tresor"; }
    CHECK(vault::ReadMode(f) == vault::FileMode::Invalid);
    fs::remove_all(dir, ec);
}

static void kdf() {
    vault::Bytes salt(16, 7);
    vault::SecBytes k1, k2, k3;
    CHECK(vault::DeriveKey(L"abc", salt, 1000, k1) && k1.size() == 32);
    CHECK(vault::DeriveKey(L"abc", salt, 1000, k2) && k1 == k2);                    // deterministisch
    CHECK(vault::DeriveKey(L"abd", salt, 1000, k3) && k1 != k3);                    // anderes Passwort
    salt[0] = 8;
    CHECK(vault::DeriveKey(L"abc", salt, 1000, k3) && k1 != k3);                    // anderes Salz
    // bekannter Testvektor (RFC 7914, PBKDF2-HMAC-SHA256, "passwd"/"salt", c=1): 55 ac 04 6e 56 e3 08 9f
    vault::Bytes s2 = {'s', 'a', 'l', 't'};
    vault::SecBytes k4;
    CHECK(vault::DeriveKey(L"passwd", s2, 1, k4) && k4[0] == 0x55 && k4[1] == 0xac && k4[2] == 0x04 && k4[3] == 0x6e && k4[4] == 0x56 && k4[5] == 0xe3);
}

static void memoryProtection() {
    // Schluessel liegen in gesperrtem Speicher (VirtualLock) und werden beim Freigeben ueberschrieben
    const int before = vault::LockedAllocs();
    const unsigned char* raw = nullptr;
    {
        vault::SecBytes k(32, 0xAB);
        raw = k.data();
        CHECK(vault::LockedAllocs() == before + 1 && vault::LockFailures() == 0);
        MEMORY_BASIC_INFORMATION mbi{};
        CHECK(VirtualQuery(raw, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT);
        vault::Wipe(k);   // sofort ueberschreiben und freigeben
        CHECK(k.empty());
    }
    vault::SecBytes a(32, 1), b(a);   // Kopien bekommen eigenen gesperrten Speicher
    CHECK(a.data() != b.data() && a == b && vault::LockFailures() == 0);
    vault::Session s;
    CHECK(vault::NewDpapiSession(s) && s.key.size() == 32);
    s.Close();
    CHECK(s.key.empty() && !s.Open());
    // Mindestzahl an PBKDF2-Runden: Dateien mit zu wenigen Runden werden abgelehnt
    const unsigned saved = vault::MinIterations();
    vault::MinIterations() = 100000;
    const fs::path dir = fs::temp_directory_path() / L"duncans_vault_test_iter";
    std::error_code ec;
    fs::remove_all(dir, ec);
    vault::Session weak, back;
    std::string plain = vault::Serialize(sample()), got;
    CHECK(vault::NewSession(L"pw-pw-pw-pw", 2000, weak) && vault::SaveFile(dir / L"v.dat", weak, plain) == vault::Result::Ok);
    CHECK(vault::OpenFile(dir / L"v.dat", L"pw-pw-pw-pw", back, got) == vault::Result::BadFormat);
    vault::MinIterations() = saved;
    CHECK(vault::OpenFile(dir / L"v.dat", L"pw-pw-pw-pw", back, got) == vault::Result::Ok);
    CHECK(vault::kDefaultIterations >= 100000);
    fs::remove_all(dir, ec);
}

static void usbKey() {
    // Ein Ordner dient als "Stick": die Funktionen arbeiten auf einem Laufwerkswurzel-Pfad
    const fs::path dir = fs::temp_directory_path() / L"duncans_vault_test_usb";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::wstring root = dir.wstring() + L"\\";

    // Schluesseldatei: Roundtrip, versteckt, Pruefsumme, nicht ueberschreiben ohne Erlaubnis
    vault::StickKey a, b, c;
    CHECK(vault::NewStickKey(a) && a.Valid() && vault::NewStickKey(b) && a.id != b.id && a.secret != b.secret);
    CHECK(vault::WriteKeyFile(root, a, false));
    const DWORD attr = GetFileAttributesW(vault::KeyFilePath(root).c_str());
    CHECK(attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_HIDDEN) && (attr & FILE_ATTRIBUTE_SYSTEM));
    CHECK(!vault::WriteKeyFile(root, b, false));
    CHECK(vault::ReadKeyFile(root, c) && c.id == a.id && c.secret == a.secret);
    CHECK(vault::WriteKeyFile(root, b, true) && vault::ReadKeyFile(root, c) && c.id == b.id);   // bewusstes Ueberschreiben (versteckte Systemdatei)
    CHECK(vault::WriteKeyFile(root, a, true));
    {   // beschaedigte Datei (ein Bit gekippt) wird abgelehnt
        std::fstream f(vault::KeyFilePath(root), std::ios::in | std::ios::out | std::ios::binary);
        CHECK(f.is_open());
        f.seekp(30);
        f.put('X');
    }
    CHECK(!vault::ReadKeyFile(root, c));
    CHECK(vault::WriteKeyFile(root, a, true) && vault::ReadKeyFile(root, c));
    CHECK(!vault::ReadKeyFile(root + L"gibt-es-nicht\\", c));

    // Wiederherstellungscode: 32 Zeichen Base32 in Vierergruppen, Normalisierung
    const std::wstring code = vault::NewRecoveryCode();
    CHECK(code.size() == 39 && code[4] == L'-' && vault::NormalizeCode(code).size() == 32);
    CHECK(vault::NormalizeCode(L"ab-c d 0189!") == L"ABCD");
    CHECK(vault::NewRecoveryCode() != code);

    // Tresor mit Stick, ohne Master-Passwort
    const std::string plain = vault::Serialize(sample());
    const fs::path vf = dir / L"v.dat";
    vault::Session s, back;
    std::wstring rec;
    CHECK(vault::NewUsbSession(a, L"", 2000, s, rec) && s.usb && s.usbFlags == 0 && s.Open() && s.blob.size() == vault::kUsbBlobLen && rec.size() == 39);
    CHECK(vault::SaveFile(vf, s, plain) == vault::Result::Ok);
    CHECK(vault::ReadMode(vf) == vault::FileMode::Usb);
    unsigned char flags = 9;
    vault::Bytes id;
    CHECK(vault::ReadUsbHeader(vf, flags, id) && flags == 0 && id == a.id);
    std::string got;
    CHECK(vault::OpenFile(vf, L"", back, got) == vault::Result::NeedUsb && !back.Open());                // ohne Stick-Angabe: kein Zugriff
    vault::UsbUnlock withStick;
    withStick.stick = &a;
    CHECK(vault::OpenFile(vf, L"", back, got, &withStick) == vault::Result::Ok && got == plain && back.usb && back.key == s.key);
    CHECK(vault::SaveFile(vf, back, plain) == vault::Result::Ok);                                          // weiter speichern: gleiche Faecher
    vault::UsbUnlock wrong;
    wrong.stick = &b;                                                                                       // fremder Stick
    CHECK(vault::OpenFile(vf, L"", back, got, &wrong) == vault::Result::WrongStick && !back.Open());
    vault::StickKey forged = a;                                                                             // gleiche Id, anderes Geheimnis
    forged.secret[0] ^= 1;
    vault::UsbUnlock forgedUnlock;
    forgedUnlock.stick = &forged;
    CHECK(vault::OpenFile(vf, L"", back, got, &forgedUnlock) == vault::Result::WrongPassword && !back.Open());
    vault::UsbUnlock viaCode;
    viaCode.recovery = true;                                                                                // Wiederherstellung ohne Stick (auch klein geschrieben, ohne Striche)
    std::wstring lower = rec;
    for (auto& ch : lower) ch = static_cast<wchar_t>(towlower(ch));
    CHECK(vault::OpenFile(vf, lower, back, got, &viaCode) == vault::Result::Ok && got == plain && back.usb);
    CHECK(vault::OpenFile(vf, L"AAAA-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA-AAAA", back, got, &viaCode) == vault::Result::WrongPassword);
    CHECK(vault::OpenFile(vf, L"zu kurz", back, got, &viaCode) == vault::Result::WrongPassword);
    CHECK(got.empty() || got == plain);

    // Manipulierte Flags im Kopf (Master-Passwort-Pflicht abschalten) fallen auf
    {
        std::fstream f(vf, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(5);
        f.put(static_cast<char>(vault::kUsbNeedsPw));
    }
    CHECK(vault::OpenFile(vf, L"", back, got, &withStick) == vault::Result::WrongPassword);
    {
        std::fstream f(vf, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(5);
        f.put(0);
    }
    CHECK(vault::OpenFile(vf, L"", back, got, &withStick) == vault::Result::Ok);

    // Stick plus Master-Passwort
    vault::Session sp;
    std::wstring rec2;
    CHECK(vault::NewUsbSession(a, L"geheim-123", 2000, sp, rec2) && sp.usbFlags == vault::kUsbNeedsPw);
    CHECK(vault::SaveFile(dir / L"p.dat", sp, plain) == vault::Result::Ok);
    CHECK(vault::ReadUsbHeader(dir / L"p.dat", flags, id) && flags == vault::kUsbNeedsPw);
    CHECK(vault::OpenFile(dir / L"p.dat", L"", back, got, &withStick) == vault::Result::WrongPassword);
    CHECK(vault::OpenFile(dir / L"p.dat", L"falsch-123", back, got, &withStick) == vault::Result::WrongPassword);
    CHECK(vault::OpenFile(dir / L"p.dat", L"geheim-123", back, got, &withStick) == vault::Result::Ok && got == plain);
    CHECK(vault::OpenFile(dir / L"p.dat", rec2, back, got, &viaCode) == vault::Result::Ok && got == plain);      // Code umgeht das Passwort (Notfall)
    CHECK(vault::OpenFile(dir / L"p.dat", rec, back, got, &viaCode) == vault::Result::WrongPassword);          // Code eines anderen Tresors passt nicht

    // Datei enthaelt weder Klartext noch Schluesselmaterial des Sticks
    {
        std::ifstream in(dir / L"p.dat", std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(bytes.find("GitHub") == std::string::npos);
        const std::string sec(reinterpret_cast<const char*>(a.secret.data()), 32);
        CHECK(bytes.find(sec) == std::string::npos);
    }
    SetFileAttributesW(vault::KeyFilePath(root).c_str(), FILE_ATTRIBUTE_NORMAL);
    fs::remove_all(dir, ec);
    CHECK(!vault::DeleteKeyFile(root));
}

int main() {
    vault::MinIterations() = 1000;   // die Tests arbeiten mit wenigen Runden, damit sie schnell laufen
    serialization(); merge(); search(); generator(); strength(); kdf(); file(); noPassword(); memoryProtection(); usbKey();
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures ? 1 : 0;
}
