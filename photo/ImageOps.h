#pragma once
// Bild-Metadaten lesen, EXIF/GPS entfernen, drehen. Alle Funktionen sind
// thread-sicher, setzen aber voraus, dass der aufrufende Thread COM
// initialisiert hat (CoInitializeEx).
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace imgops {

struct InfoRow {
    std::wstring label, value;
    bool sensitive = false;  // Datenschutz-relevant (z. B. GPS) -> rot dargestellt
};

struct ImageInfo {
    UINT width = 0, height = 0;
    ULONGLONG fileSize = 0;
    FILETIME modified{}, created{};
    int orientation = 1;  // EXIF-Orientation 1..8
    bool hasExif = false;
    bool hasGps = false;
    std::vector<InfoRow> rows;  // aufbereitete EXIF-Zeilen
};

bool IsSupportedImage(const std::wstring& path);

// Liefert die Quelle auf genau tw x th verkleinert. JPEG wird dabei schon beim Dekodieren auf 1/2, 1/4 oder 1/8
// gebracht (DCT-Skalierung), das ist bei grossen Fotos um ein Vielfaches schneller als erst voll zu dekodieren.
// Gemeinsam genutzt von Thumbnails, Duplikat-Hash und Bearbeiten-Vorschau. nullptr bei Fehlern.
Microsoft::WRL::ComPtr<IWICBitmapSource> ScaledSource(IWICImagingFactory* f, IWICBitmapFrameDecode* frame, UINT tw,
                                                      UINT th);

// Schnelle Metadaten fuer Sortieren/Filtern: Aufnahmezeit (UTC als FILETIME-Wert, 0 = unbekannt) und GPS-Block.
struct QuickMeta {
    ULONGLONG taken = 0;
    bool hasGps = false;
};
bool ReadQuickMeta(const std::wstring& path, QuickMeta& out);

// EXIF-Zeitstempel "YYYY:MM:DD HH:MM:SS" (Ortszeit) -> UTC als FILETIME-Wert; false bei ungueltigem Text.
bool ParseExifDate(const std::wstring& s, ULONGLONG& utc);

// Doppelte Bilder: Inhaltshash der ganzen Datei (zwei unabhaengige 64-Bit-Laeufe) fuer "byte-identisch" ...
bool HashFile(const std::wstring& path, uint64_t& h1, uint64_t& h2);

// ... und eine Signatur fuer "sieht gleich aus": 64-Bit-Differenzhash (dHash, Helligkeitsverlauf einer 9x8-Verkleinerung)
// plus die 9x8 Farbwerte selbst. Der dHash allein erkennt nur die Struktur und wuerde z. B. gleich aufgebaute Bilder in
// anderen Farben verwechseln; die Farben schliessen das aus.
struct ImageSig {
    uint64_t dhash = 0;
    BYTE bgr[9 * 8 * 3] = {};
};
bool ComputeSignature(const std::wstring& path, ImageSig& out);
int HammingDistance(uint64_t a, uint64_t b);
// Aehnlich = dHash-Abstand <= maxHamming UND mittlere Farbabweichung (je Kanal, 0..255) <= maxColorDiff.
bool SignaturesSimilar(const ImageSig& a, const ImageSig& b, int maxHamming, int maxColorDiff);

bool ReadImageInfo(const std::wstring& path, ImageInfo& out);

// EXIF-Orientation (1..8) eines Frames; 1 wenn nicht vorhanden.
int ReadOrientation(IWICBitmapFrameDecode* frame);
WICBitmapTransformOptions OrientationToTransform(int orientation);

// Entfernt alle Metadaten (Kamera, Aufnahmezeit, GPS, XMP, IPTC, Thumbnails).
// dst darf gleich src sein (atomares Ersetzen ueber Temp-Datei).
//  - JPEG ohne Rotations-Tag: verlustfrei, Segmente werden auf Byte-Ebene entfernt.
//  - JPEG mit Rotations-Tag / PNG / TIFF: Re-Encode ueber WIC (JPEG Qualitaet 95),
//    die Drehung wird dabei in die Pixel eingebrannt.
//  - WebP: verlustfrei, EXIF-/XMP-Chunks werden entfernt. BMP: hat keine Metadaten, wird nur kopiert.
//  - GIF/HEIC: false + Fehlertext (nicht unterstuetzt).
bool StripMetadata(const std::wstring& src, const std::wstring& dst, std::wstring* err = nullptr);

// Formate, die StripMetadata bzw. die Re-Encode-Funktionen (Drehen, Zuschneiden, Verkleinern) verarbeiten.
bool CanStripMetadata(const std::wstring& path);
bool CanReencode(const std::wstring& path);

// Schneidet einen Ausschnitt aus (Bruchteile 0..1 des aufrecht stehenden Bildes) und schreibt ihn nach dst
// (dst darf gleich src sein). Re-Encode (JPEG Q95), Metadaten bleiben erhalten, Orientation wird eingebrannt.
bool CropImage(const std::wstring& src, const std::wstring& dst, double x0, double y0, double x1, double y1,
               std::wstring* err = nullptr);

// Verkleinert so, dass die laengste Kante <= maxEdge ist (nie vergroessern), JPEG Q85, ohne Metadaten
// (zum Teilen gedacht). Drehung wird eingebrannt.
bool ResizeImage(const std::wstring& src, const std::wstring& dst, UINT maxEdge, std::wstring* err = nullptr);

// ---- Bildkorrektur (Helligkeit, Kontrast, Saettigung, Horizont) ----
struct EditParams {
    int brightness = 0, contrast = 0, saturation = 0;  // -100..+100
    double angle = 0;                                    // Horizont ausrichten, Grad (-45..+45), positiv = im Uhrzeigersinn
    bool IsIdentity() const { return !brightness && !contrast && !saturation && angle > -0.005 && angle < 0.005; }
};

// Faktor, mit dem das Bild nach dem Drehen um angleDeg wieder randlos (gleiches Seitenverhaeltnis) zugeschnitten wird.
double StraightenScale(double w, double h, double angleDeg);

// Farbkorrektur an BGRA-Pixeln (nicht vormultipliziert), Alpha bleibt unveraendert.
void ApplyColor(BYTE* bgra, size_t pixelCount, const EditParams& p);

// Dreht px (w x h, BGRA) um p.angle, schneidet randlos zu und wendet die Farbkorrektur an; w/h werden angepasst.
// Gleiche Funktion fuer Live-Vorschau-Daten und Speichern, damit beides gleich aussieht.
bool ApplyEdits(std::vector<BYTE>& px, UINT& w, UINT& h, const EditParams& p);

// Laedt das Bild aufrecht (EXIF-Drehung angewendet) als BGRA, verkleinert auf maxEdge (nie vergroessert).
bool LoadPixels(const std::wstring& path, UINT maxEdge, std::vector<BYTE>& bgra, UINT& w, UINT& h);

// Wendet p auf das Vollbild an und schreibt es nach dst (dst darf gleich src sein). Re-Encode (JPEG Q95),
// Metadaten bleiben erhalten.
bool EditImage(const std::wstring& src, const std::wstring& dst, const EditParams& p, std::wstring* err = nullptr);

// Dreht um 90 Grad (verlustbehaftet bei JPEG, Metadaten bleiben erhalten).
bool RotateImage(const std::wstring& path, bool clockwise, std::wstring* err = nullptr);

}  // namespace imgops
