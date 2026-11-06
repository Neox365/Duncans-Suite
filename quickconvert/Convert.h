#pragma once
// Konvertierungs-Engine von Duncan's QuickConvert: Bilder ueber WIC, Audio/Video ueber Windows Media Foundation.
// Alles laeuft lokal; die Funktionen sind thread-sicher (jeder Aufruf legt eigene COM-Objekte an) und blockieren,
// gehoeren also in einen Worker-Thread. Der aufrufende Thread muss COM (CoInitializeEx) initialisiert haben und
// MFStartup einmal im Prozess aufgerufen haben (conv::MediaStartup).
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace conv {

enum class Kind { Unknown, Image, Media };
enum class ImageFmt { Png, Jpeg, Webp, Bmp, Heic, Gif, Pdf };   // Pdf: nur ueber ConvertImagesToPdf (mehrere Bilder in ein PDF)
enum class AudioFmt { Mp3, M4a, Wav, Mp4, Mkv, Flac };   // Mp4/Mkv = Video (H.264 + AAC); Audio-Dateien werden dabei zu M4A (EffectiveFmt); Flac = verlustfrei

Kind KindOfFile(const std::wstring& path);                  // nach Endung
const wchar_t* ImageExt(ImageFmt f);                        // "png", "jpg", ...
const wchar_t* AudioExt(AudioFmt f);                        // "mp3", "m4a", "wav", "mp4", "mkv", "flac"

// Qualitaets-Slider (0..100 %) -> Bitrate in kbit/s (MP3: 128..320, AAC: 96..192, WAV verlustfrei: 0)
int BitrateKbps(AudioFmt f, int quality);
// Videobitrate in kbit/s aus Aufloesung, Bildrate und Slider (0,04..0,20 Bit je Pixel und Bild, 300..40000 kbit/s)
int VideoBitrateKbps(int width, int height, double fps, int quality);
// Reine Audio-Quelle (mp3, wav, m4a, flac ...)? Dann liefert Mp4 als Ziel M4A statt eines Videos.
bool IsAudioOnlyFile(const std::wstring& path);
AudioFmt EffectiveFmt(const std::wstring& src, AudioFmt requested);

bool MediaStartup();                                        // MFStartup, einmal pro Prozess
void MediaShutdown();

// ---- Zieldateigroesse ("fuer Discord/WhatsApp") ----
// Audio: hoechste vom Windows-Encoder unterstuetzte Bitrate (kbit/s), die in maxBytes passt; 0 = passt auch mit der kleinsten Stufe nicht,
// -1 = dieses Format laesst sich nicht begrenzen (WAV, FLAC sind verlustfrei; Video geht ueber PlanVideoForSize)
int EncoderKbpsForSize(AudioFmt f, long long maxBytes, double seconds);

struct VideoPlan {
    bool feasible = false;     // false: auch mit der kleinsten sinnvollen Bitrate zu gross
    int width = 0, height = 0; // Ausgabegroesse (gerade Seitenlaengen); kleiner als die Quelle, wenn die Bitrate sonst zu niedrig fuer das Bild waere
    int videoKbps = 0, audioKbps = 0;
};
// Plant Bitrate und Aufloesung, damit ein Video von `seconds` Sekunden in maxBytes passt. qualityKbps/qualityAudioKbps sind die Werte, die der
// Qualitaets-Regler ohne Grenze ergeben haette (die Grenze macht nie groesser als der Regler). hasAudio: Ton einrechnen.
VideoPlan PlanVideoForSize(int width, int height, double fps, double seconds, long long maxBytes, int qualityKbps, int qualityAudioKbps, bool hasAudio);

// Reihenfolge der laengsten Bildkanten, die beim Suchen nach der Zielgroesse probiert werden: zuerst maxEdge (0 = Original), dann kleinere Stufen
std::vector<int> ImageEdgeSteps(int maxEdge);

// Bild konvertieren. quality gilt fuer JPEG, WebP und HEIC. Liefert false mit err (lesbarer Text), wenn etwas fehlt,
// z. B. der WebP-/HEVC-Codec von Windows nicht installiert ist. dst darf nicht src sein.
// maxEdge > 0: Bild wird auf diese laengste Kante verkleinert (nie vergroessert, Seitenverhaeltnis bleibt), 0 = Originalgroesse.
// maxBytes > 0: Zieldateigroesse; bei JPEG/WebP/HEIC wird die Qualitaet (nie ueber `quality`) und danach die Bildgroesse so weit gesenkt, bis die Datei passt;
// bei PNG/BMP/GIF nur die Bildgroesse. Schafft es das nicht, ist das Ergebnis false mit err.
bool ConvertImage(const std::wstring& src, const std::wstring& dst, ImageFmt fmt, int quality, std::wstring* err, int maxEdge = 0, long long maxBytes = 0);

// Mehrere Bilder in EIN PDF (eine A4-Seite je Bild, Hoch- oder Querformat nach dem Bild, Bild eingepasst und zentriert, nie vergroessert).
// Jedes Bild wird als JPEG (quality, maxEdge wie bei ConvertImage) eingebettet. cancel wird zwischen den Bildern geprueft (err == L"Abgebrochen"),
// progress(0..100) zaehlt die Bilder. Bei einem Fehler steht der Dateiname des Bildes im Fehlertext; dst wird dann nicht angelegt.
bool ConvertImagesToPdf(const std::vector<std::wstring>& srcs, const std::wstring& dst, int quality, int maxEdge, const std::atomic<bool>& cancel,
                        const std::function<void(int)>& progress, std::wstring* err);

// Audio konvertieren oder aus einem Video extrahieren. progress(0..100) wird aus dem Worker-Thread gerufen.
// cancel wird laufend geprueft; bei Abbruch liefert die Funktion false mit err == L"Abgebrochen" und loescht die Zieldatei.
// maxBytes > 0: Zieldateigroesse (MP3/M4A: Bitrate, Video: Bitrate und ggf. Aufloesung, mit Nachkodieren, falls die Datei zu gross wurde).
bool ConvertMedia(const std::wstring& src, const std::wstring& dst, AudioFmt fmt, int quality, const std::atomic<bool>& cancel,
                  const std::function<void(int)>& progress, std::wstring* err, long long maxBytes = 0);

}  // namespace conv
