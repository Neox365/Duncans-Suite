// Test fuer Duplicates.h und ReadQuickMeta/ParseExifDate (nur Windows, braucht WIC).
// g++ -std=c++17 -I.. DuplicatesTest.cpp ../ImageOps.cpp -lwindowscodecs -lole32 -loleaut32 -luuid -lpropsys -o t.exe
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

#include "../Duplicates.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static void WriteBmp(const std::wstring& path, int w, int h, unsigned char (*px)(int, int, int, int)) {
    const int row = (w * 3 + 3) & ~3;
    std::vector<unsigned char> d(54 + row * h, 0);
    auto put32 = [&](int o, unsigned v) { for (int i = 0; i < 4; ++i) d[o + i] = (unsigned char)(v >> (8 * i)); };
    d[0] = 'B'; d[1] = 'M';
    put32(2, (unsigned)d.size()); put32(10, 54); put32(14, 40); put32(18, w); put32(22, h);
    d[26] = 1; d[28] = 24; put32(34, row * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned char v = px(x, y, w, h);
            unsigned char* p = &d[54 + (h - 1 - y) * row + x * 3];
            p[0] = p[1] = p[2] = v;
        }
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD wr;
    WriteFile(f, d.data(), (DWORD)d.size(), &wr, nullptr);
    CloseHandle(f);
}

// 8x8 Bloecke mit festem, unregelmaessigem Muster (unabhaengig von der Bildgroesse)
static unsigned char Blocks(int x, int y, int w, int h) {
    const int bx = x * 8 / w, by = y * 8 / h;
    return (unsigned char)(((bx * 73 + by * 151 + (bx ^ by) * 29) * 37) % 256);
}
static unsigned char Checker(int x, int y, int, int) { return (unsigned char)((((x / 4) + (y / 4)) & 1) ? 255 : 0); }
static unsigned char Flat(int, int, int, int) { return 128; }

int main() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ULONGLONG t = 0;
    CHECK(imgops::ParseExifDate(L"2024:05:17 13:45:00", t) && t > 0);
    ULONGLONG t2 = 0;
    CHECK(imgops::ParseExifDate(L"2024:05:17 13:45:01", t2) && t2 == t + 10000000ull);
    CHECK(!imgops::ParseExifDate(L"0000:00:00 00:00:00", t2));
    CHECK(!imgops::ParseExifDate(L"kaputt", t2));
    CHECK(imgops::HammingDistance(0, 0) == 0 && imgops::HammingDistance(0xFF, 0) == 8 && imgops::HammingDistance(~0ull, 0) == 64);

    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"dupTest" + std::to_wstring(GetCurrentProcessId()) + L"\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    WriteBmp(dir + L"a.bmp", 64, 64, Blocks);
    CopyFileW((dir + L"a.bmp").c_str(), (dir + L"b.bmp").c_str(), FALSE);
    WriteBmp(dir + L"c.bmp", 128, 128, Blocks);   // gleiches Motiv, andere Groesse -> aehnlich
    WriteBmp(dir + L"d.bmp", 64, 64, Checker);    // anderes Bild
    WriteBmp(dir + L"e.bmp", 64, 64, Flat);       // einfarbig: nie als aehnlich werten
    WriteBmp(dir + L"f.bmp", 32, 32, Flat);

    std::vector<dup::File> files;
    for (const wchar_t* n : {L"a.bmp", L"b.bmp", L"c.bmp", L"d.bmp", L"e.bmp", L"f.bmp"}) {
        WIN32_FILE_ATTRIBUTE_DATA fa{};
        GetFileAttributesExW((dir + n).c_str(), GetFileExInfoStandard, &fa);
        files.push_back({dir + n, ((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow});
    }
    dup::Progress pr;
    auto groups = dup::Find(files, pr);
    CHECK(groups.size() == 1);
    if (groups.size() == 1) {
        auto& g = groups[0];
        CHECK(g.members.size() == 3 && g.members[0] == 0 && g.members[1] == 1 && g.members[2] == 2);
        CHECK(!g.exact);  // c ist nur aehnlich
    }

    // nur a und b: identisch
    std::vector<dup::File> two = {files[0], files[1], files[3]};
    dup::Progress pr2;
    auto g2 = dup::Find(two, pr2);
    CHECK(g2.size() == 1 && g2[0].exact && g2[0].members.size() == 2);

    // Abbruch liefert nichts
    dup::Progress pr3;
    pr3.cancel = true;
    CHECK(dup::Find(files, pr3).empty());

    imgops::QuickMeta qm;
    CHECK(imgops::ReadQuickMeta(dir + L"a.bmp", qm) && qm.taken == 0 && !qm.hasGps);

    for (const wchar_t* n : {L"a.bmp", L"b.bmp", L"c.bmp", L"d.bmp", L"e.bmp", L"f.bmp"}) DeleteFileW((dir + n).c_str());
    RemoveDirectoryW(dir.c_str());
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures ? 1 : 0;
}
