#pragma once
// Karten gestalten: Fotos in einem Layout anordnen, Hintergrund und Text setzen und als Bild in Druckgroesse ausgeben.
// Reine Zeichenlogik auf GDI+ ohne Fenster (testbar). Der Aufrufer muss GDI+ gestartet haben (card::Session).
#include <windows.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Gdiplus { class Bitmap; }

namespace card {

// GDI+ fuer die Lebensdauer des Objekts starten.
class Session {
public:
    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    bool Ok() const { return m_ok; }
private:
    ULONG_PTR m_token = 0;
    bool m_ok = false;
};

struct Paper {
    const wchar_t* name;
    double wMm, hMm;   // Hochformat
};
int PaperCount();
const Paper& GetPaper(int i);

struct Layout {
    const wchar_t* name;
    int cells;
};
int LayoutCount();
const Layout& GetLayout(int i);

struct RectN { float x, y, w, h; };   // normiert auf 0..1
// Zellen des Layouts im Bereich mit dem Seitenverhaeltnis aspect (Breite/Hoehe)
std::vector<RectN> Cells(int layout, float aspect);

struct Spec {
    std::vector<std::wstring> photos;   // Zelle i zeigt Foto i % Anzahl; ohne Fotos bleiben die Zellen leer
    int layout = 0;
    int paper = 0;
    bool landscape = true;
    int marginPct = 4;    // Rand in % der kuerzeren Seite (0..15)
    int gapPct = 2;       // Abstand zwischen den Fotos in % (0..10)
    int radiusPct = 0;    // runde Ecken, % der kuerzeren Zellkante (0..50)
    int borderPct = 0;    // weisser Fotorahmen in % der kuerzeren Seite (0..10)
    int bgMode = 0;       // 0 Farbe, 1 Verlauf, 2 unscharfes erstes Foto
    COLORREF bg1 = RGB(255, 255, 255), bg2 = RGB(205, 220, 240);
    bool textBand = true;   // Schreibfeld unten: Fotos bleiben darueber
    std::wstring text;
    std::wstring font = L"Georgia";
    int textSizePct = 8;    // Schrifthoehe in % der Kartenhoehe (2..25)
    COLORREF textColor = RGB(40, 40, 50);
    bool bold = false, italic = false, shadow = false;
    float textX = 0.5f, textY = -1.f;   // Mittelpunkt, normiert; textY < 0: automatisch (im Schreibfeld bzw. unten)
};

// Fertige Vorlagen fuer Anlaesse: setzen Hintergrund, Rahmen, Ecken, Text und Schrift. Fotos, Papier, Layout und Hoch-/Querformat
// bleiben unveraendert. Index 0 ist "Eigene Einstellungen" und aendert nichts.
int PresetCount();
const wchar_t* PresetName(int i);
void ApplyPreset(Spec& s, int i);

// Pixelgroesse der Karte bei dpi
void CanvasSize(const Spec& s, UINT dpi, UINT& w, UINT& h);

// Foto laden (EXIF-Drehung angewendet), auf hoechstens maxEdge Pixel Kantenlaenge verkleinert; nullptr bei Fehlern.
std::shared_ptr<Gdiplus::Bitmap> LoadPhoto(const std::wstring& path, UINT maxEdge);
using PhotoLoader = std::function<std::shared_ptr<Gdiplus::Bitmap>(const std::wstring&)>;

// Karte in w x h Pixeln zeichnen
std::unique_ptr<Gdiplus::Bitmap> Render(const Spec& s, UINT w, UINT h, const PhotoLoader& load);

// Karte in Druckaufloesung als .jpg oder .png speichern
bool Save(const Spec& s, const std::wstring& path, UINT dpi, std::wstring* err = nullptr);

}  // namespace card
