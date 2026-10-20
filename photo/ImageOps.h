#pragma once
// Bild-Metadaten lesen, EXIF/GPS entfernen, drehen. Alle Funktionen sind
// thread-sicher, setzen aber voraus, dass der aufrufende Thread COM
// initialisiert hat (CoInitializeEx).
#include <windows.h>
#include <wincodec.h>
#include <cstdint>
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
    FILETIME modified{};
    int orientation = 1;  // EXIF-Orientation 1..8
    bool hasExif = false;
    bool hasGps = false;
    std::vector<InfoRow> rows;  // aufbereitete EXIF-Zeilen
};

bool IsSupportedImage(const std::wstring& path);

// Schnelle Metadaten fuer Sortieren/Filtern: Aufnahmezeit (UTC als FILETIME-Wert, 0 = unbekannt) und GPS-Block.
struct QuickMeta {
    ULONGLONG taken = 0;
    bool hasGps = false;
};
bool ReadQuickMeta(const std::wstring& path, QuickMeta& out);

// EXIF-Zeitstempel "YYYY:MM:DD HH:MM:SS" (Ortszeit) -> UTC als FILETIME-Wert; false bei ungueltigem Text.
bool ParseExifDate(const std::wstring& s, ULONGLONG& utc);

// Doppelte Bilder: Inhaltshash der ganzen Datei (zwei unabhaengige 64-Bit-Laeufe) und 64-Bit-Differenzhash
// (dHash, 9x8 Graustufen) fuer "sieht gleich aus". HammingDistance vergleicht zwei dHashes.
bool HashFile(const std::wstring& path, uint64_t& h1, uint64_t& h2);
bool ComputeDHash(const std::wstring& path, uint64_t& hash);
int HammingDistance(uint64_t a, uint64_t b);

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

// Dreht um 90 Grad (verlustbehaftet bei JPEG, Metadaten bleiben erhalten).
bool RotateImage(const std::wstring& path, bool clockwise, std::wstring* err = nullptr);

}  // namespace imgops
