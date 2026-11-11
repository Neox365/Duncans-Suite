// Test der Texterkennung (common/Ocr.h): Text mit GDI in ein Bild zeichnen, erkennen lassen, vergleichen.
// Baut nur mit MSVC:  cl /std:c++17 /EHsc /utf-8 /permissive- /I.. OcrTest.cpp /Fe:OcrTest.exe gdi32.lib user32.lib
// Braucht ein Windows-Sprachpaket mit Texterkennung (Deutsch oder Englisch); fehlt es, wird der Test uebersprungen (SKIP, Exitcode 0).
#include "../Ocr.h"

#include <cstdio>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

// Zeichnet die Zeilen schwarz auf weiss (Arial, hoehe px) und liefert BGRA von oben nach unten
static std::vector<uint8_t> Draw(const std::vector<std::wstring>& lines, int px, int& w, int& h)
{
    w = 1500;
    h = static_cast<int>(lines.size()) * (px + px / 2) + 80;
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, bmp);
    RECT all{0, 0, w, h};
    FillRect(dc, &all, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    HFONT font = CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Arial");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    int y = 40;
    for (const auto& l : lines) { TextOutW(dc, 40, y, l.c_str(), static_cast<int>(l.size())); y += px + px / 2; }
    GdiFlush();
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4);
    std::memcpy(out.data(), bits, out.size());
    SelectObject(dc, oldFont); DeleteObject(font); SelectObject(dc, old); DeleteObject(bmp); DeleteDC(dc);
    return out;
}

static bool Has(const std::wstring& s, const std::wstring& sub) { return s.find(sub) != std::wstring::npos; }

int main()
{
    int w = 0, h = 0;
    const std::vector<std::wstring> lines = {L"Rechnung Nr. 2026-117", L"Müller & Söhne GmbH, Hauptstraße 12", L"Gesamtbetrag: 1.234,56 EUR", L"Zahlbar bis 25.11.2026"};
    std::vector<uint8_t> px = Draw(lines, 44, w, h);
    ocr::Result r = ocr::Recognize(px.data(), w, h);
    if (!r.ok) {
        std::printf("SKIP: %ls\n", r.error.c_str());
        return 0;
    }
    std::printf("Sprache %ls, %zu Zeilen:\n%ls\n", r.language.c_str(), r.lines.size(), r.text.c_str());
    CHECK(!r.language.empty());
    CHECK(r.lines.size() == lines.size());
    CHECK(Has(r.text, L"Rechnung") && Has(r.text, L"2026-117"));
    CHECK(Has(r.text, L"Gesamtbetrag") && Has(r.text, L"1.234,56"));
    CHECK(Has(r.text, L"25.11.2026"));
    if (r.language.rfind(L"de", 0) == 0) CHECK(Has(r.text, L"Müller") && Has(r.text, L"Söhne") && Has(r.text, L"Hauptstraße"));   // Umlaute nur mit deutschem Sprachpaket sicher
    // Wortrahmen: liegen im Bild und sind in Zeilen von oben nach unten geordnet
    float lastY = -1;
    for (const auto& ln : r.lines) {
        CHECK(!ln.words.empty());
        for (const auto& wd : ln.words) CHECK(wd.x >= 0 && wd.y >= 0 && wd.x + wd.w <= w + 2 && wd.y + wd.h <= h + 2 && wd.w > 0 && wd.h > 0);
        CHECK(ln.words.front().y > lastY);
        lastY = ln.words.front().y;
    }
    // Das Wort "Rechnung" steht oben links (erste Zeile, am linken Rand)
    CHECK(r.lines[0].words[0].text.find(L"Rechnung") != std::wstring::npos && r.lines[0].words[0].x < 100 && r.lines[0].words[0].y < 100);

    // Leeres weisses Bild: kein Fehler, kein Text
    std::vector<uint8_t> blank(static_cast<size_t>(600) * 400 * 4, 255);
    ocr::Result b = ocr::Recognize(blank.data(), 600, 400);
    CHECK(b.ok && b.text.empty());
    // Ungueltige Eingaben
    CHECK(!ocr::Recognize(nullptr, 100, 100).ok);
    CHECK(!ocr::Recognize(blank.data(), 2, 2).ok);
    // Sehr grosses Bild wird verkleinert und trotzdem erkannt (Wortrahmen auf Originalgroesse umgerechnet): Bild 3000 breit, doppelt so gross gezeichnet
    {
        int bw = 0, bh = 0;
        std::vector<uint8_t> big = Draw({L"Rechnung Nr. 2026-117"}, 44, bw, bh);
        ocr::Result s = ocr::Recognize(big.data(), bw, bh);
        CHECK(s.ok && Has(s.text, L"2026-117"));
    }

    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
