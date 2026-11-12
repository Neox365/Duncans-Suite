// Test des Meldungsfensters (common/ThemedBox.h): Rueckgabewerte, Tasten, Mausklick, Besitzerfenster, Umbruch.
// Baut mit MinGW:  g++ -std=c++17 -I.. ThemedBoxTest.cpp -o ThemedBoxTest.exe -lgdi32 -luser32 -ldwmapi
// Mit einem Dateinamen als Argument wird zusaetzlich ein Bild des Fensters als BMP gespeichert.
#include "../ThemedBox.h"

#include <cstdio>
#include <functional>
#include <thread>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

static HWND WaitBox()
{
    for (int i = 0; i < 300; ++i) {
        HWND h = FindWindowW(L"DuncansThemedBox", nullptr);
        if (h && IsWindowVisible(h)) return h;
        Sleep(10);
    }
    return nullptr;
}

static void Press(HWND h, WPARAM vk) { PostMessageW(h, WM_KEYDOWN, vk, 0); }

static void SaveBmp(HWND h, const char* path)
{
    RECT r;
    GetWindowRect(h, &r);
    const int w = r.right - r.left, hh = r.bottom - r.top;
    HDC sdc = GetDC(nullptr), mdc = CreateCompatibleDC(sdc);
    HBITMAP bmp = CreateCompatibleBitmap(sdc, w, hh);
    SelectObject(mdc, bmp);
    PrintWindow(h, mdc, 0);
    BITMAPINFOHEADER bi{sizeof bi, w, -hh, 1, 32, BI_RGB};
    std::vector<unsigned char> px(static_cast<size_t>(w) * hh * 4);
    GetDIBits(mdc, bmp, 0, hh, px.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
    FILE* f = std::fopen(path, "wb");
    if (f) {
        BITMAPFILEHEADER fh{0x4D42, static_cast<DWORD>(54 + px.size()), 0, 0, 54};
        bi.biHeight = -hh;
        std::fwrite(&fh, sizeof fh, 1, f); std::fwrite(&bi, sizeof bi, 1, f); std::fwrite(px.data(), 1, px.size(), f); std::fclose(f);
    }
    DeleteObject(bmp); DeleteDC(mdc); ReleaseDC(nullptr, sdc);
}

// Zeigt das Fenster, laesst "act" (in einem Hilfsthread) darauf los und liefert die Rueckgabe
static int Run(HWND owner, const wchar_t* text, const wchar_t* title, UINT flags, std::function<void(HWND)> act)
{
    std::thread t([&] { HWND h = WaitBox(); if (h) act(h); });
    const int r = themedbox::Show(owner, text, title, flags);
    t.join();
    return r;
}

int main(int argc, char** argv)
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TestOwner";
    RegisterClassW(&wc);
    HWND owner = CreateWindowW(L"TestOwner", L"Besitzer", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 800, 600, nullptr, nullptr, wc.hInstance, nullptr);

    CHECK(Run(owner, L"Hinweis", L"Titel", MB_OK | MB_ICONINFORMATION, [](HWND h) { Press(h, VK_RETURN); }) == IDOK);
    CHECK(Run(owner, L"Hinweis", L"Titel", MB_OK, [](HWND h) { Press(h, VK_ESCAPE); }) == IDOK);                          // Esc bei nur OK: OK
    CHECK(Run(owner, L"Frage", L"T", MB_YESNO | MB_ICONQUESTION, [](HWND h) { Press(h, VK_RETURN); }) == IDYES);            // erste Schaltflaeche vorgewaehlt
    CHECK(Run(owner, L"Frage", L"T", MB_YESNO | MB_DEFBUTTON2, [](HWND h) { Press(h, VK_RETURN); }) == IDNO);               // Enter waehlt die markierte
    CHECK(Run(owner, L"Frage", L"T", MB_YESNO, [](HWND h) { Press(h, 'J'); }) == IDYES);
    CHECK(Run(owner, L"Frage", L"T", MB_YESNO, [](HWND h) { Press(h, 'Y'); }) == IDYES);
    CHECK(Run(owner, L"Frage", L"T", MB_YESNO | MB_DEFBUTTON1, [](HWND h) { Press(h, 'N'); }) == IDNO);
    CHECK(Run(owner, L"Frage", L"T", MB_YESNO, [](HWND h) { Press(h, VK_ESCAPE); }) == IDNO);                              // Esc: Nein
    CHECK(Run(owner, L"Frage", L"T", MB_YESNOCANCEL, [](HWND h) { Press(h, VK_ESCAPE); }) == IDCANCEL);
    CHECK(Run(owner, L"Frage", L"T", MB_YESNOCANCEL | MB_DEFBUTTON3, [](HWND h) { Press(h, VK_RETURN); }) == IDCANCEL);
    CHECK(Run(owner, L"Frage", L"T", MB_YESNOCANCEL, [](HWND h) { Press(h, VK_TAB); Press(h, VK_RETURN); }) == IDNO);       // Tab weiter, Enter
    CHECK(Run(owner, L"Frage", L"T", MB_YESNOCANCEL, [](HWND h) { Press(h, VK_LEFT); Press(h, VK_RETURN); }) == IDCANCEL);   // Pfeil links umlaufend
    CHECK(Run(owner, L"Frage", L"T", MB_OKCANCEL, [](HWND h) { Press(h, VK_ESCAPE); }) == IDCANCEL);
    CHECK(Run(owner, L"Frage", L"T", MB_OKCANCEL, [](HWND h) { Press(h, 'J'); Press(h, VK_RETURN); }) == IDOK);              // J gibt es hier nicht: ignoriert
    CHECK(Run(owner, L"Frage", L"T", MB_YESNO, [](HWND h) { PostMessageW(h, WM_CLOSE, 0, 0); }) == IDNO);                   // Fenster schliessen

    // Mausklick auf die rechte Schaltflaeche (Nein) und auf die linke (Ja)
    auto click = [](HWND h, bool right) {
        RECT cr;
        GetClientRect(h, &cr);
        const int dpi = static_cast<int>(GetDpiForWindow(h)), pad = MulDiv(24, dpi, 96), bw = MulDiv(120, dpi, 96), bh = MulDiv(36, dpi, 96), gap = MulDiv(10, dpi, 96);
        const int cx = right ? cr.right - pad - bw / 2 : cr.right - pad - bw - gap - bw / 2, cy = cr.bottom - pad - bh / 2;
        const LPARAM lp = MAKELPARAM(cx, cy);
        SendMessageW(h, WM_MOUSEMOVE, 0, lp);
        PostMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, lp);
        PostMessageW(h, WM_LBUTTONUP, 0, lp);
    };
    CHECK(Run(owner, L"Frage", L"T", MB_YESNO, [&](HWND h) { click(h, true); }) == IDNO);
    CHECK(Run(owner, L"Frage", L"T", MB_YESNO | MB_DEFBUTTON2, [&](HWND h) { click(h, false); }) == IDYES);
    // Klick ausserhalb der Schaltflaechen tut nichts; danach Enter
    CHECK(Run(owner, L"Frage", L"T", MB_YESNO | MB_DEFBUTTON2, [&](HWND h) { PostMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5)); PostMessageW(h, WM_LBUTTONUP, 0, MAKELPARAM(5, 5)); Press(h, VK_RETURN); }) == IDNO);

    // Besitzer ist waehrenddessen gesperrt und danach wieder frei
    bool wasDisabled = false;
    CHECK(Run(owner, L"x", L"T", MB_OK, [&](HWND h) { wasDisabled = !IsWindowEnabled(owner); Press(h, VK_RETURN); }) == IDOK);
    CHECK(wasDisabled && IsWindowEnabled(owner));

    // Langer Text macht das Fenster hoeher; ohne Besitzer geht es auch
    LONG hShort = 0, hLong = 0;
    Run(owner, L"kurz", L"T", MB_OK, [&](HWND h) { RECT r; GetWindowRect(h, &r); hShort = r.bottom - r.top; Press(h, VK_RETURN); });
    std::wstring longText;
    for (int i = 0; i < 12; ++i) longText += L"Dies ist ein langer Satz, der umbrochen werden muss. ";
    Run(owner, longText.c_str(), L"T", MB_OK, [&](HWND h) { RECT r; GetWindowRect(h, &r); hLong = r.bottom - r.top; if (argc > 1) SaveBmp(h, argv[1]); Press(h, VK_RETURN); });
    CHECK(hLong > hShort + 40);
    CHECK(Run(nullptr, L"ohne Besitzer", nullptr, MB_OK, [](HWND h) { Press(h, VK_RETURN); }) == IDOK);

    DestroyWindow(owner);
    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
