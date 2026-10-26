// Test fuer die Schnellkonvertierung in ImageOps (JPG, PNG, BMP, WebP). Nur Windows (WIC).
// g++ -std=c++17 -DUNICODE -D_UNICODE -I.. ConvertTest.cpp ../ImageOps.cpp -lwindowscodecs -lole32 -loleaut32 -luuid -lpropsys
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

#include "../ImageOps.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

// 24-Bit-BMP mit einfarbiger Flaeche
static void WriteBmp(const std::wstring& path, int w, int h, BYTE r, BYTE g, BYTE b) {
    const int row = (w * 3 + 3) & ~3;
    std::vector<BYTE> d(54 + row * h, 0);
    auto put32 = [&](int o, unsigned v) { for (int i = 0; i < 4; ++i) d[o + i] = (BYTE)(v >> (8 * i)); };
    d[0] = 'B'; d[1] = 'M';
    put32(2, (unsigned)d.size()); put32(10, 54); put32(14, 40); put32(18, w); put32(22, h);
    d[26] = 1; d[28] = 24; put32(34, row * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            BYTE* p = &d[54 + y * row + x * 3];
            p[0] = b; p[1] = g; p[2] = r;
        }
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD wr = 0;
    WriteFile(f, d.data(), (DWORD)d.size(), &wr, nullptr);
    CloseHandle(f);
}

static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

int main() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring dir = std::wstring(tmp) + L"pc_convert_test_" + std::to_wstring(GetCurrentProcessId()) + L"\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring src = dir + L"quelle.bmp";
    WriteBmp(src, 64, 48, 200, 40, 40);

    CHECK(std::wstring(imgops::OutExt(imgops::OutFormat::Jpeg)) == L"jpg");
    CHECK(std::wstring(imgops::OutExt(imgops::OutFormat::Webp)) == L"webp");
    CHECK(imgops::HasFormat(L"C:\\x\\Foto.JPEG", imgops::OutFormat::Jpeg));
    CHECK(imgops::HasFormat(L"a.png", imgops::OutFormat::Png));
    CHECK(!imgops::HasFormat(L"a.png", imgops::OutFormat::Bmp));

    const imgops::OutFormat all[] = {imgops::OutFormat::Jpeg, imgops::OutFormat::Png, imgops::OutFormat::Bmp, imgops::OutFormat::Webp};
    for (imgops::OutFormat f : all) {
        const std::wstring dst = dir + L"ziel." + imgops::OutExt(f);
        std::wstring err;
        const bool ok = imgops::ConvertImage(src, dst, f, &err);
        if (f == imgops::OutFormat::Webp && !ok) {   // ohne WebP-Encoder auf dem Rechner ist ein klarer Fehler richtig
            std::printf("Hinweis: WebP nicht verfuegbar (%ls)\n", err.c_str());
            CHECK(!Exists(dst) && !err.empty());
            continue;
        }
        CHECK(ok);
        CHECK(Exists(dst));
        CHECK(!Exists(dst + L".pctmp"));
        imgops::ImageInfo info;
        CHECK(imgops::ReadImageInfo(dst, info) && info.width == 64 && info.height == 48);
        CHECK(imgops::HasFormat(dst, f));
    }

    // Quelle gleich Ziel wird abgelehnt, nicht lesbare Quelle ebenfalls
    std::wstring err;
    CHECK(!imgops::ConvertImage(src, src, imgops::OutFormat::Png, &err));
    CHECK(!imgops::ConvertImage(dir + L"gibtsnicht.png", dir + L"x.jpg", imgops::OutFormat::Jpeg, &err));
    CHECK(!Exists(dir + L"x.jpg"));

    // aufraeumen
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((dir + fd.cFileName).c_str()); } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
    CoUninitialize();
    if (failures) std::printf("%d Fehler\n", failures);
    else std::printf("Alle Konvertierungstests bestanden\n");
    return failures ? 1 : 0;
}
