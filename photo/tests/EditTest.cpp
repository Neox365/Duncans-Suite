// Test fuer die Bildkorrektur in ImageOps (Farbe, Horizont ausrichten, Speichern). Nur Windows (WIC).
// g++ -std=c++17 -DUNICODE -D_UNICODE -I.. EditTest.cpp ../ImageOps.cpp -lwindowscodecs -lole32 -loleaut32 -luuid -lpropsys
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../ImageOps.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static std::vector<BYTE> Solid(UINT w, UINT h, BYTE b, BYTE g, BYTE r) {
    std::vector<BYTE> px((size_t)w * h * 4);
    for (size_t i = 0; i < px.size(); i += 4) { px[i] = b; px[i + 1] = g; px[i + 2] = r; px[i + 3] = 255; }
    return px;
}

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
    DWORD wr;
    WriteFile(f, d.data(), (DWORD)d.size(), &wr, nullptr);
    CloseHandle(f);
}

int main() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    using imgops::EditParams;

    CHECK(std::fabs(imgops::StraightenScale(100, 100, 0) - 1.0) < 1e-9);
    CHECK(std::fabs(imgops::StraightenScale(100, 100, 45) - std::sqrt(0.5)) < 1e-6);
    CHECK(imgops::StraightenScale(300, 100, 10) < imgops::StraightenScale(100, 100, 10));  // breite Bilder verlieren mehr
    CHECK(EditParams{}.IsIdentity());

    // Farbe
    {
        auto px = Solid(2, 2, 128, 128, 128);
        EditParams p; p.brightness = 100;
        imgops::ApplyColor(px.data(), 4, p);
        CHECK(px[0] == 255 && px[1] == 255 && px[2] == 255 && px[3] == 255);
        px = Solid(2, 2, 128, 128, 128);
        p = EditParams{}; p.brightness = -100;
        imgops::ApplyColor(px.data(), 4, p);
        CHECK(px[0] < 5);
        px = Solid(2, 2, 100, 150, 200);  // b, g, r
        p = EditParams{}; p.saturation = -100;
        imgops::ApplyColor(px.data(), 4, p);
        CHECK(px[0] == px[1] && px[1] == px[2]);
        px = Solid(2, 2, 100, 150, 200);
        p = EditParams{}; p.saturation = 50;
        imgops::ApplyColor(px.data(), 4, p);
        CHECK(px[2] > 200 && px[0] < 100);  // Farben werden kraeftiger
        px = Solid(2, 2, 100, 150, 200);
        p = EditParams{}; p.contrast = 50;
        imgops::ApplyColor(px.data(), 4, p);
        CHECK(px[2] > 200 && px[0] < 100);
        px = Solid(2, 2, 100, 150, 200);
        imgops::ApplyColor(px.data(), 4, EditParams{});
        CHECK(px[0] == 100 && px[1] == 150 && px[2] == 200);
    }

    // Horizont: Oberseite weiss, unten schwarz; im Uhrzeigersinn gedreht liegt der Horizont rechts tiefer als links
    {
        const UINT w = 300, h = 200;
        std::vector<BYTE> px((size_t)w * h * 4, 0);
        for (UINT y = 0; y < h; ++y)
            for (UINT x = 0; x < w; ++x) {
                BYTE v = y < h / 2 ? 255 : 0;
                BYTE* q = &px[((size_t)y * w + x) * 4];
                q[0] = q[1] = q[2] = v; q[3] = 255;
            }
        UINT ow = w, oh = h;
        EditParams p; p.angle = 8;
        std::vector<BYTE> cp = px;
        CHECK(imgops::ApplyEdits(cp, ow, oh, p));
        const double s = imgops::StraightenScale(w, h, 8);
        CHECK(ow == (UINT)std::lround(w * s) && oh == (UINT)std::lround(h * s));
        CHECK(cp.size() == (size_t)ow * oh * 4);
        auto edge = [&](UINT x) {
            for (UINT y = 0; y < oh; ++y)
                if (cp[((size_t)y * ow + x) * 4] < 128) return (int)y;
            return -1;
        };
        const int left = edge(10), right = edge(ow - 11);
        CHECK(left > 0 && right > 0 && right > left + 10);
        // Randlos: keine Randpixel ausserhalb des Bildes (wuerden schwarz/weiss gespiegelt, hier nur Kontrolle der Ecken)
        CHECK(cp[3] == 255);

        UINT w0 = w, h0 = h;
        std::vector<BYTE> same = px;
        EditParams none;
        CHECK(imgops::ApplyEdits(same, w0, h0, none) && w0 == w && h0 == h && same == px);
    }

    // Speichern ueber WIC (BMP): Groesse und Helligkeit
    {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        const std::wstring dir = std::wstring(tmp) + L"editTest" + std::to_wstring(GetCurrentProcessId()) + L"\\";
        CreateDirectoryW(dir.c_str(), nullptr);
        WriteBmp(dir + L"a.bmp", 120, 80, 100, 100, 100);
        EditParams p; p.brightness = 40; p.angle = 5;
        std::wstring err;
        CHECK(imgops::EditImage(dir + L"a.bmp", dir + L"b.bmp", p, &err));
        std::vector<BYTE> px;
        UINT w = 0, h = 0;
        CHECK(imgops::LoadPixels(dir + L"b.bmp", 0, px, w, h));
        const double s = imgops::StraightenScale(120, 80, 5);
        CHECK(w == (UINT)std::lround(120 * s) && h == (UINT)std::lround(80 * s));
        CHECK(!px.empty() && px[(size_t)(h / 2) * w * 4 + (w / 2) * 4 + 1] > 150);
        CHECK(imgops::LoadPixels(dir + L"a.bmp", 60, px, w, h) && w == 60 && h == 40);  // Vorschau verkleinert
        // In-place (dst == src)
        EditParams q; q.contrast = 30;
        CHECK(imgops::EditImage(dir + L"a.bmp", dir + L"a.bmp", q, &err));
        DeleteFileW((dir + L"a.bmp").c_str());
        DeleteFileW((dir + L"b.bmp").c_str());
        RemoveDirectoryW(dir.c_str());
    }

    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures ? 1 : 0;
}
