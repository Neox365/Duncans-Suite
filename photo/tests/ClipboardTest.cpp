// Tests fuer ClipboardCopy.h (Strg+C im Photo Commander). Nur Windows. Veraendert kurz die Zwischenablage und stellt sie danach wieder her (Text).
// g++ -std=c++17 -DUNICODE -D_UNICODE -I.. ClipboardTest.cpp -o ClipboardTest.exe -lole32 -luser32 -lshell32 -static
#include "../ClipboardCopy.h"

#include <shellapi.h>

#include <cstdio>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

static std::wstring GetText()
{
    std::wstring t;
    if (!OpenClipboard(nullptr)) return t;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) { if (auto* p = static_cast<const wchar_t*>(GlobalLock(h))) { t = p; GlobalUnlock(h); } }
    CloseClipboard();
    return t;
}
static void SetText(const std::wstring& t)
{
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    if (!t.empty()) {
        HGLOBAL g = photoclip::ToGlobal(t.c_str(), (t.size() + 1) * sizeof(wchar_t));
        if (g && !SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
    }
    CloseClipboard();
}

int main()
{
    // Dateiliste
    const std::vector<BYTE> d = photoclip::BuildDropFiles({L"C:\\a\\x.jpg", L"D:\\b c\\\xE4.png"});
    const auto* df = reinterpret_cast<const DROPFILES*>(d.data());
    CHECK(df->pFiles == sizeof(DROPFILES) && df->fWide);
    const wchar_t* w = reinterpret_cast<const wchar_t*>(d.data() + df->pFiles);
    CHECK(std::wstring(w) == L"C:\\a\\x.jpg");
    w += std::wstring(w).size() + 1;
    CHECK(std::wstring(w) == L"D:\\b c\\\xE4.png");
    w += std::wstring(w).size() + 1;
    CHECK(*w == 0);                                                       // doppelte Null am Ende
    CHECK(d.size() == sizeof(DROPFILES) + (10 + 1 + 12 + 1 + 1) * sizeof(wchar_t));

    // Bitmap: 2x2, oben links rot (voll), oben rechts blau halbdurchsichtig, unten links gruen, unten rechts durchsichtig
    const BYTE px[16] = { 0,0,255,255,   255,0,0,128,   0,255,0,255,   0,0,0,0 };   // BGRA, von oben nach unten
    const std::vector<BYTE> dib = photoclip::BuildDib(px, 2, 2);
    const auto* bi = reinterpret_cast<const BITMAPINFOHEADER*>(dib.data());
    CHECK(bi->biWidth == 2 && bi->biHeight == 2 && bi->biBitCount == 32 && dib.size() == sizeof(BITMAPINFOHEADER) + 16);
    const BYTE* r0 = dib.data() + sizeof(BITMAPINFOHEADER);               // unterste Bildzeile zuerst
    CHECK(r0[0] == 0 && r0[1] == 255 && r0[2] == 0);                      // gruen
    CHECK(r0[4] == 255 && r0[5] == 255 && r0[6] == 255);                  // durchsichtig -> weiss
    const BYTE* r1 = r0 + 8;
    CHECK(r1[0] == 0 && r1[1] == 0 && r1[2] == 255);                      // rot
    CHECK(r1[4] == 255 && r1[5] == 127 && r1[6] == 127);                  // blau 50 % auf weiss: B voll, G/R etwa halb (127)

    // Echte Zwischenablage
    const std::wstring backup = GetText();
    CHECK(photoclip::CopyToClipboard(nullptr, {L"C:\\test\\eins.jpg"}, px, 2, 2));
    CHECK(IsClipboardFormatAvailable(CF_HDROP) && IsClipboardFormatAvailable(CF_DIB));
    {
        OpenClipboard(nullptr);
        HDROP h = static_cast<HDROP>(GetClipboardData(CF_HDROP));
        CHECK(h && DragQueryFileW(h, 0xFFFFFFFF, nullptr, 0) == 1);
        wchar_t buf[MAX_PATH] = {};
        if (h) DragQueryFileW(h, 0, buf, MAX_PATH);
        CHECK(std::wstring(buf) == L"C:\\test\\eins.jpg");
        const UINT fmt = RegisterClipboardFormatW(L"Preferred DropEffect");
        HANDLE e = GetClipboardData(fmt);
        CHECK(e && *static_cast<DWORD*>(GlobalLock(e)) == DROPEFFECT_COPY);
        if (e) GlobalUnlock(e);
        CloseClipboard();
    }
    CHECK(photoclip::CopyToClipboard(nullptr, {L"C:\\a.jpg", L"C:\\b.jpg"}, nullptr, 0, 0));   // mehrere Dateien: kein Bitmap
    CHECK(IsClipboardFormatAvailable(CF_HDROP) && !IsClipboardFormatAvailable(CF_DIB));
    CHECK(!photoclip::CopyToClipboard(nullptr, {}, nullptr, 0, 0));
    SetText(backup);

    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
