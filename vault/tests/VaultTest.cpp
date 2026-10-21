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

static void kdf() {
    vault::Bytes salt(16, 7), k1, k2, k3;
    CHECK(vault::DeriveKey(L"abc", salt, 1000, k1) && k1.size() == 32);
    CHECK(vault::DeriveKey(L"abc", salt, 1000, k2) && k1 == k2);                    // deterministisch
    CHECK(vault::DeriveKey(L"abd", salt, 1000, k3) && k1 != k3);                    // anderes Passwort
    salt[0] = 8;
    CHECK(vault::DeriveKey(L"abc", salt, 1000, k3) && k1 != k3);                    // anderes Salz
    // bekannter Testvektor (RFC 7914, PBKDF2-HMAC-SHA256, "passwd"/"salt", c=1): 55 ac 04 6e 56 e3 08 9f
    vault::Bytes s2 = {'s', 'a', 'l', 't'}, k4;
    CHECK(vault::DeriveKey(L"passwd", s2, 1, k4) && k4[0] == 0x55 && k4[1] == 0xac && k4[2] == 0x04 && k4[3] == 0x6e && k4[4] == 0x56 && k4[5] == 0xe3);
}

int main() {
    serialization(); merge(); search(); generator(); strength(); kdf(); file();
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures ? 1 : 0;
}
