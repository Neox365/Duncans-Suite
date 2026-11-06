// Tests fuer die Duplikat-Erkennung von Duncan's Tidy (Dupes.h). Nur Windows.
// g++ -std=c++17 -DUNICODE -D_UNICODE -I../src DupeTest.cpp -o DupeTest.exe -lbcrypt -static
#include "../src/Dupes.h"

#include <cstdio>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

using dupes::File;

static void Put(const std::wstring& p, const std::string& data) {
    HANDLE f = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD w = 0;
    WriteFile(f, data.data(), (DWORD)data.size(), &w, nullptr);
    CloseHandle(f);
}

int main()
{
    // Reine Logik mit eingesetzter Hash-Funktion
    std::vector<File> v = {
        {L"a.bin", 10, 300}, {L"a (1).bin", 10, 200}, {L"b.bin", 10, 100},   // a und a (1) gleich, b anders
        {L"c.bin", 20, 50},                                                   // einzigartige Groesse
        {L"d.bin", 30, 10}, {L"e.bin", 30, 10}, {L"f.bin", 30, 10},           // gleich alt: kuerzester Name bleibt
        {L"leer1", 0, 1}, {L"leer2", 0, 2},                                   // leere Dateien nie doppelt
        {L"x.bin", 40, 5}, {L"y.bin", 40, 6},                                 // gleiche Groesse, Hash nicht lesbar
    };
    int hashed = 0;
    auto fake = [&](const std::wstring& p) -> std::string {
        ++hashed;
        if (p == L"a.bin" || p == L"a (1).bin") return "AA";
        if (p == L"b.bin") return "BB";
        if (p == L"d.bin" || p == L"e.bin" || p == L"f.bin") return "DD";
        return "";
    };
    const std::vector<size_t> r = dupes::FindRedundant(v, fake);
    CHECK((r == std::vector<size_t>{0, 5, 6}));            // a.bin (neuer als "a (1).bin"), e.bin, f.bin; d.bin bleibt (kuerzester Name)
    CHECK(hashed == 8);                                     // c.bin und die leeren Dateien wurden nie gelesen
    CHECK(dupes::FindRedundant({}, fake).empty());
    bool cancelled = true;
    CHECK(dupes::FindRedundant(v, fake, [&] { return cancelled; }).empty());   // Abbruch liefert nichts

    // Echte Dateien
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring dir = std::wstring(tmp) + L"tidy_dupe_test_" + std::to_wstring(GetCurrentProcessId()) + L"\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::string big(3 * 1024 * 1024 + 17, 'x');                       // groesser als ein Lesestueck
    std::string big2 = big;
    big2.back() = 'y';                                                      // gleiche Groesse, letzter Wert anders
    Put(dir + L"1.dat", big); Put(dir + L"2.dat", big); Put(dir + L"3.dat", big2); Put(dir + L"4.dat", "hello"); Put(dir + L"5.dat", "hello");
    const std::string h1 = dupes::HashFile(dir + L"1.dat");
    CHECK(h1.size() == 64 && h1 == dupes::HashFile(dir + L"2.dat") && h1 != dupes::HashFile(dir + L"3.dat"));
    CHECK(dupes::HashFile(dir + L"4.dat") == "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");   // SHA-256("hello")
    CHECK(dupes::HashFile(dir + L"fehlt.dat").empty());
    std::vector<File> real;
    for (const wchar_t* n : {L"1.dat", L"2.dat", L"3.dat", L"4.dat", L"5.dat"}) {
        File f;
        f.path = dir + n;
        WIN32_FILE_ATTRIBUTE_DATA a;
        GetFileAttributesExW(f.path.c_str(), GetFileExInfoStandard, &a);
        f.size = ((unsigned long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
        f.mtime = ((unsigned long long)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
        real.push_back(f);
    }
    real[0].mtime = 1000; real[1].mtime = 2000; real[3].mtime = 500; real[4].mtime = 400;   // 1.dat und 4.dat sind je die aelteren? 4.dat ist juenger als 5.dat
    const std::vector<size_t> rr = dupes::FindRedundant(real, [](const std::wstring& p) { return dupes::HashFile(p); });
    CHECK((rr == std::vector<size_t>{1, 3}));               // 2.dat (neuer als 1.dat) und 4.dat (neuer als 5.dat); 3.dat ist einzigartig
    for (const wchar_t* n : {L"1.dat", L"2.dat", L"3.dat", L"4.dat", L"5.dat"}) DeleteFileW((dir + n).c_str());
    RemoveDirectoryW(dir.c_str());

    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
