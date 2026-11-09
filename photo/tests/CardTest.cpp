// Test fuer photo/Card.cpp: Layouts, Zeichnen (Hintergrund, Fotos, Text) und Speichern in Druckgroesse.
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cstdio>
#include <string>

#include "../Card.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

static std::wstring Tmp(const wchar_t* name) {
    wchar_t t[MAX_PATH]{};
    GetTempPathW(MAX_PATH, t);
    return std::wstring(t) + L"duncans_card_" + name;
}

// einfarbiges Testfoto als PNG
static bool MakePhoto(const std::wstring& path, UINT w, UINT h, Gdiplus::Color c) {
    Gdiplus::Bitmap b(w, h, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&b);
        Gdiplus::SolidBrush br(c);
        g.FillRectangle(&br, 0, 0, static_cast<INT>(w), static_cast<INT>(h));
    }
    UINT n = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&n, &size);
    std::vector<BYTE> buf(size);
    auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    Gdiplus::GetImageEncoders(n, size, codecs);
    for (UINT i = 0; i < n; ++i)
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0) return b.Save(path.c_str(), &codecs[i].Clsid, nullptr) == Gdiplus::Ok;
    return false;
}

static Gdiplus::Color Px(Gdiplus::Bitmap& b, int x, int y) {
    Gdiplus::Color c;
    b.GetPixel(x, y, &c);
    return c;
}

static bool Near(Gdiplus::Color c, int r, int g, int b, int tol = 6) {
    return abs(c.GetR() - r) <= tol && abs(c.GetG() - g) <= tol && abs(c.GetB() - b) <= tol;
}

int main() {
    card::Session gdi;
    CHECK(gdi.Ok());

    // Layouts: jede Zelle liegt im Einheitsquadrat, Zellenzahl stimmt mit dem Namen ueberein
    for (int i = 0; i < card::LayoutCount(); ++i) {
        for (float aspect : {0.7f, 1.0f, 1.5f}) {
            const auto cells = card::Cells(i, aspect);
            CHECK(static_cast<int>(cells.size()) == card::GetLayout(i).cells);
            for (const auto& c : cells) CHECK(c.x >= 0 && c.y >= 0 && c.w > 0 && c.h > 0 && c.x + c.w <= 1.0001f && c.y + c.h <= 1.0001f);
        }
    }

    // Vorlagen: Eigene Einstellungen aendert nichts, die uebrigen setzen Stil und Text, Fotos/Papier/Layout bleiben
    CHECK(card::PresetCount() >= 7);
    {
        card::Spec p;
        p.photos = {L"a.jpg", L"b.jpg"};
        p.layout = 3; p.paper = 2; p.landscape = false; p.text = L"mein Text";
        card::Spec before = p;
        card::ApplyPreset(p, 0);
        CHECK(p.text == before.text && p.bgMode == before.bgMode);
        for (int i = 1; i < card::PresetCount(); ++i) {
            card::Spec q = before;
            card::ApplyPreset(q, i);
            CHECK(q.photos == before.photos && q.layout == 3 && q.paper == 2 && !q.landscape);
            CHECK(!q.text.empty() && q.textSizePct >= 2 && q.textSizePct <= 25 && q.marginPct <= 15 && q.gapPct <= 10 && q.radiusPct <= 50 && q.borderPct <= 10);
            CHECK(q.textY < 0);
            auto bmp = card::Render(q, 400, 600, [](const std::wstring&) { return std::shared_ptr<Gdiplus::Bitmap>(); });
            CHECK(bmp != nullptr);
        }
    }

    card::Spec s;
    s.paper = 0;       // 10x15
    s.landscape = true;
    UINT w = 0, h = 0;
    card::CanvasSize(s, 300, w, h);
    CHECK(w == 1772 && h == 1181);
    s.landscape = false;
    card::CanvasSize(s, 300, w, h);
    CHECK(w == 1181 && h == 1772);
    s.paper = 5;
    card::CanvasSize(s, 150, w, h);
    CHECK(w == h);

    const std::wstring red = Tmp(L"red.png"), blue = Tmp(L"blue.png");
    CHECK(MakePhoto(red, 800, 600, Gdiplus::Color(255, 220, 20, 20)));
    CHECK(MakePhoto(blue, 600, 800, Gdiplus::Color(255, 20, 40, 220)));
    CHECK(card::LoadPhoto(red, 400) && card::LoadPhoto(red, 400)->GetWidth() == 400 && card::LoadPhoto(red, 400)->GetHeight() == 300);
    CHECK(!card::LoadPhoto(L"C:\\gibt\\es\\nicht.jpg", 400));

    card::PhotoLoader load = [](const std::wstring& p) { return card::LoadPhoto(p, 1000); };

    // ein Foto, Rand: Ecke zeigt die Hintergrundfarbe, Mitte das Foto
    card::Spec a;
    a.photos = {red};
    a.layout = 0;
    a.marginPct = 10;
    a.bg1 = RGB(255, 255, 255);
    auto bmp = card::Render(a, 600, 400, load);
    CHECK(bmp && bmp->GetWidth() == 600 && bmp->GetHeight() == 400);
    if (bmp) {
        CHECK(Near(Px(*bmp, 3, 3), 255, 255, 255));
        CHECK(Near(Px(*bmp, 300, 200), 220, 20, 20));
    }

    // zwei Fotos nebeneinander: links rot, rechts blau; ohne Rand und Abstand
    card::Spec b;
    b.photos = {red, blue};
    b.layout = 1;
    b.marginPct = 0;
    b.gapPct = 0;
    bmp = card::Render(b, 600, 400, load);
    if (bmp) {
        CHECK(Near(Px(*bmp, 100, 200), 220, 20, 20));
        CHECK(Near(Px(*bmp, 500, 200), 20, 40, 220));
    }

    // Schreibfeld: unten bleibt frei, Foto liegt darueber; Text zeichnet Pixel in Textfarbe
    card::Spec c;
    c.photos = {red};
    c.layout = 0;
    c.marginPct = 0;
    c.textBand = true;
    c.text = L"Frohe Weihnachten";
    c.textSizePct = 14;
    c.textColor = RGB(0, 0, 0);
    c.bg1 = RGB(255, 255, 255);
    bmp = card::Render(c, 600, 400, load);
    if (bmp) {
        CHECK(Near(Px(*bmp, 300, 100), 220, 20, 20));      // Foto
        CHECK(Near(Px(*bmp, 5, 395), 255, 255, 255));      // Schreibfeld bleibt Hintergrund
        int dark = 0;
        for (int y = 320; y < 400; ++y)
            for (int x = 100; x < 500; ++x)
                if (Px(*bmp, x, y).GetR() < 90) ++dark;
        CHECK(dark > 100);                                  // Schrift ist zu sehen
    }

    // runde Ecken: die Zellenecke bleibt Hintergrund
    card::Spec d;
    d.photos = {red};
    d.layout = 0;
    d.marginPct = 0;
    d.radiusPct = 50;
    d.bg1 = RGB(255, 255, 255);
    bmp = card::Render(d, 400, 400, load);
    if (bmp) {
        CHECK(Near(Px(*bmp, 2, 2), 255, 255, 255));
        CHECK(Near(Px(*bmp, 200, 200), 220, 20, 20));
    }

    // weisser Fotorahmen
    card::Spec e;
    e.photos = {blue};
    e.layout = 0;
    e.marginPct = 0;
    e.borderPct = 10;
    e.bg1 = RGB(0, 0, 0);
    bmp = card::Render(e, 400, 400, load);
    if (bmp) {
        CHECK(Near(Px(*bmp, 8, 200), 255, 255, 255));
        CHECK(Near(Px(*bmp, 200, 200), 20, 40, 220));
    }

    // Foto-Hintergrund und Verlauf liefern ohne Absturz Bilder; ohne Fotos bleiben Zellen grau
    card::Spec f;
    f.photos = {red};
    f.layout = 5;
    f.bgMode = 2;
    CHECK(card::Render(f, 300, 200, load) != nullptr);
    f.bgMode = 1;
    f.photos.clear();
    bmp = card::Render(f, 300, 200, load);
    CHECK(bmp != nullptr);

    // Speichern in Druckgroesse (JPG und PNG), Aufloesung ist gesetzt
    card::Spec g;
    g.photos = {red, blue, red, blue};
    g.layout = 5;
    g.text = L"Alles Gute";
    const std::wstring jpg = Tmp(L"out.jpg"), png = Tmp(L"out.png");
    DeleteFileW(jpg.c_str());
    DeleteFileW(png.c_str());
    std::wstring err;
    CHECK(card::Save(g, jpg, 300, &err));
    CHECK(card::Save(g, png, 150, &err));
    {
        Gdiplus::Bitmap j(jpg.c_str()), p(png.c_str());
        CHECK(j.GetWidth() == 1772 && j.GetHeight() == 1181);
        CHECK(p.GetWidth() == 886 && p.GetHeight() == 591);
        CHECK(j.GetHorizontalResolution() > 299 && j.GetHorizontalResolution() < 301);
    }
    CHECK(!card::Save(g, L"C:\\gibt\\es\\nicht\\x.jpg", 300, &err) && !err.empty());

    for (const std::wstring& p : {red, blue, jpg, png}) DeleteFileW(p.c_str());
    if (g_fail) { std::printf("%d Pruefung(en) fehlgeschlagen\n", g_fail); return 1; }
    std::printf("Karten: alle Pruefungen bestanden\n");
    return 0;
}
