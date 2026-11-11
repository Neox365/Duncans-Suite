#pragma once
// Texterkennung (OCR) mit der in Windows eingebauten Engine (Windows.Media.Ocr). Nichts wird hochgeladen, es braucht keine Fremdsoftware.
// Die Engine nutzt Windows-Sprachpakete: Deutsch wird bevorzugt, sonst Englisch, sonst die Sprachen des Benutzerprofils.
// Nur mit MSVC (C++/WinRT); mit anderen Compilern meldet Recognize() "nicht verfuegbar".
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#if defined(_MSC_VER)
#include <unknwn.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#pragma comment(lib, "windowsapp.lib")
#endif

namespace ocr {

struct Word {
    std::wstring text;
    float x = 0, y = 0, w = 0, h = 0;   // Rahmen in Pixeln des uebergebenen Bildes (links oben = 0,0)
};

struct Line {
    std::wstring text;
    std::vector<Word> words;
};

struct Result {
    bool ok = false;
    std::wstring error;      // verstaendliche Meldung, wenn ok == false
    std::wstring language;   // z. B. "de-DE"
    std::wstring text;       // alle Zeilen, durch Zeilenumbruch getrennt
    std::vector<Line> lines;
};

namespace detail {

// Mittelt das Bild auf die Haelfte, bis es in die Engine passt (Engine-Grenze meist 10 000 Pixel je Seite)
inline void Shrink(std::vector<uint8_t>& px, int& w, int& h, int maxDim, int& factor)
{
    factor = 1;
    while (w > maxDim || h > maxDim) {
        const int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
        std::vector<uint8_t> out(static_cast<size_t>(nw) * nh * 4);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
                for (int c = 0; c < 4; ++c) {
                    int sum = 0, n = 0;
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx) {
                            const int sx = std::min(w - 1, x * 2 + dx), sy = std::min(h - 1, y * 2 + dy);
                            sum += px[(static_cast<size_t>(sy) * w + sx) * 4 + c];
                            ++n;
                        }
                    out[(static_cast<size_t>(y) * nw + x) * 4 + c] = static_cast<uint8_t>(sum / n);
                }
        px.swap(out);
        w = nw;
        h = nh;
        factor *= 2;
    }
}

#if defined(_MSC_VER)
inline Result Run(std::vector<uint8_t> px, int w, int h)
{
    using namespace winrt;
    using namespace winrt::Windows::Globalization;
    using namespace winrt::Windows::Graphics::Imaging;
    using namespace winrt::Windows::Media::Ocr;
    using namespace winrt::Windows::Storage::Streams;
    Result r;
    try {
        try { winrt::init_apartment(winrt::apartment_type::multi_threaded); } catch (...) {}
        OcrEngine engine{nullptr};
        for (const wchar_t* tag : {L"de-DE", L"de", L"en-US", L"en-GB", L"en"}) {
            Language lang{tag};
            if (OcrEngine::IsLanguageSupported(lang)) { engine = OcrEngine::TryCreateFromLanguage(lang); if (engine) break; }
        }
        if (!engine) engine = OcrEngine::TryCreateFromUserProfileLanguages();
        if (!engine) {
            r.error = L"Windows hat kein Sprachpaket für die Texterkennung. Unter Einstellungen > Zeit und Sprache > Sprache und Region kann man Deutsch (mit „Texterkennung“) hinzufügen.";
            return r;
        }
        r.language = engine.RecognizerLanguage().LanguageTag().c_str();
        int factor = 1;
        Shrink(px, w, h, static_cast<int>(OcrEngine::MaxImageDimension()), factor);
        DataWriter dw;
        dw.WriteBytes(array_view<const uint8_t>(px.data(), px.data() + px.size()));
        SoftwareBitmap bmp = SoftwareBitmap::CreateCopyFromBuffer(dw.DetachBuffer(), BitmapPixelFormat::Bgra8, w, h, BitmapAlphaMode::Ignore);
        OcrResult res = engine.RecognizeAsync(bmp).get();
        for (OcrLine ln : res.Lines()) {
            Line line;
            line.text = ln.Text().c_str();
            for (OcrWord wd : ln.Words()) {
                Word word;
                word.text = wd.Text().c_str();
                const auto rc = wd.BoundingRect();
                word.x = rc.X * factor; word.y = rc.Y * factor; word.w = rc.Width * factor; word.h = rc.Height * factor;
                line.words.push_back(std::move(word));
            }
            if (!r.text.empty()) r.text += L"\n";
            r.text += line.text;
            r.lines.push_back(std::move(line));
        }
        r.ok = true;
    } catch (const winrt::hresult_error& e) {
        r.ok = false;
        r.error = L"Texterkennung fehlgeschlagen: " + std::wstring(e.message().c_str());
    } catch (...) {
        r.ok = false;
        r.error = L"Texterkennung fehlgeschlagen.";
    }
    return r;
}
#endif

}  // namespace detail

// Erkennt den Text in einem Bild (BGRA, 4 Byte je Pixel, Zeilen von oben nach unten, ohne Zeilenfuellbytes). Blockiert, bis die Engine fertig ist
// (typisch 0,3 bis 2 Sekunden je Seite); laeuft auf einem eigenen Thread, damit der aufrufende Thread (auch STA) nicht blockiert.
inline Result Recognize(const uint8_t* bgra, int w, int h)
{
    Result r;
#if defined(_MSC_VER)
    if (!bgra || w < 8 || h < 8 || static_cast<unsigned long long>(w) * h > 400000000ull) { r.error = L"Das Bild ist zu klein oder zu groß für die Texterkennung."; return r; }
    std::vector<uint8_t> copy(bgra, bgra + static_cast<size_t>(w) * h * 4);
    std::thread t([&] { r = detail::Run(std::move(copy), w, h); });
    t.join();
#else
    (void)bgra; (void)w; (void)h;
    r.error = L"Texterkennung ist in dieser Programmversion nicht verfügbar.";
#endif
    return r;
}

}  // namespace ocr
