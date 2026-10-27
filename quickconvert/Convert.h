#pragma once
// Konvertierungs-Engine von Duncan's QuickConvert: Bilder ueber WIC, Audio/Video ueber Windows Media Foundation.
// Alles laeuft lokal; die Funktionen sind thread-sicher (jeder Aufruf legt eigene COM-Objekte an) und blockieren,
// gehoeren also in einen Worker-Thread. Der aufrufende Thread muss COM (CoInitializeEx) initialisiert haben und
// MFStartup einmal im Prozess aufgerufen haben (conv::MediaStartup).
#include <atomic>
#include <functional>
#include <string>

namespace conv {

enum class Kind { Unknown, Image, Media };
enum class ImageFmt { Png, Jpeg, Webp, Bmp, Heic };
enum class AudioFmt { Mp3, M4a, Wav, Mp4 };   // Mp4 = Video (H.264 + AAC); Audio-Dateien werden dabei zu M4A (EffectiveFmt)

Kind KindOfFile(const std::wstring& path);                  // nach Endung
const wchar_t* ImageExt(ImageFmt f);                        // "png", "jpg", ...
const wchar_t* AudioExt(AudioFmt f);                        // "mp3", "m4a", "wav"

// Qualitaets-Slider (0..100 %) -> Bitrate in kbit/s (MP3: 128..320, AAC: 96..192, WAV verlustfrei: 0)
int BitrateKbps(AudioFmt f, int quality);
// Videobitrate in kbit/s aus Aufloesung, Bildrate und Slider (0,04..0,20 Bit je Pixel und Bild, 300..40000 kbit/s)
int VideoBitrateKbps(int width, int height, double fps, int quality);
// Reine Audio-Quelle (mp3, wav, m4a, flac ...)? Dann liefert Mp4 als Ziel M4A statt eines Videos.
bool IsAudioOnlyFile(const std::wstring& path);
AudioFmt EffectiveFmt(const std::wstring& src, AudioFmt requested);

bool MediaStartup();                                        // MFStartup, einmal pro Prozess
void MediaShutdown();

// Bild konvertieren. quality gilt fuer JPEG, WebP und HEIC. Liefert false mit err (lesbarer Text), wenn etwas fehlt,
// z. B. der WebP-/HEVC-Codec von Windows nicht installiert ist. dst darf nicht src sein.
bool ConvertImage(const std::wstring& src, const std::wstring& dst, ImageFmt fmt, int quality, std::wstring* err);

// Audio konvertieren oder aus einem Video extrahieren. progress(0..100) wird aus dem Worker-Thread gerufen.
// cancel wird laufend geprueft; bei Abbruch liefert die Funktion false mit err == L"Abgebrochen" und loescht die Zieldatei.
bool ConvertMedia(const std::wstring& src, const std::wstring& dst, AudioFmt fmt, int quality, const std::atomic<bool>& cancel,
                  const std::function<void(int)>& progress, std::wstring* err);

}  // namespace conv
