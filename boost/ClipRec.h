#pragma once
// Duncan's Boost - Video-Clips: nimmt den Hauptbildschirm laufend in kurzen Abschnitten auf (je ein paar Sekunden, als MP4 im Temp-Ordner) und
// setzt auf Tastendruck die letzten Sekunden zu einer MP4-Datei zusammen ("Rueckblick-Aufnahme", wie ShadowPlay/Outplayed). Es wird nur
// aufgenommen, solange die Funktion an ist; die Abschnitte liegen nur lokal und alte werden laufend geloescht.
// Technik: Desktop Duplication (DXGI) fuer das Bild, Media Foundation (H.264, wenn moeglich in der Grafikkarte) fuers Kodieren.
// Stufe 1: nur Bild, kein Ton, nur der Hauptbildschirm. Spiele im exklusiven Vollbild koennen schwarz oder leer bleiben (dann Fenster-Vollbild nutzen).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

namespace cliprec {

struct Options {
    int seconds = 30;            // so viel Rueckblick soll im Clip stecken (mindestens; der Clip ist bis zu einem Abschnitt laenger)
    int fps = 30;
    int segmentSeconds = 10;     // Laenge eines Abschnitts
    int bitrateKbps = 0;         // 0 = automatisch nach Bildgroesse
    std::wstring outDir;         // Ordner fuer fertige Clips (leer = Videos\Boost-Clips)
    std::wstring tempDir;        // Ordner fuer die laufenden Abschnitte (leer = Temp\DuncansBoostClips)
};

constexpr int kMinSeconds = 10, kMaxSeconds = 120;

// Anzahl Abschnitte, die fuer den gewuenschten Rueckblick aufgehoben werden (immer genug fuer "seconds", plus der laufende)
int SegmentsToKeep(int seconds, int segmentSeconds);
// Bitrate (kbit/s) nach Bildgroesse und Bildrate, begrenzt auf 3000 ... 25000
int AutoBitrateKbps(int width, int height, int fps);
// Dateiname "Clip_2026-11-14_20-15-03.mp4" fuer die angegebene Zeit
std::wstring ClipFileName(const SYSTEMTIME& t);
std::wstring DefaultClipDir();          // Videos\Boost-Clips (wird nicht angelegt)

class Recorder {
public:
    Recorder();
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    bool Start(const Options& o, std::wstring& err);   // startet den Aufnahme-Thread; false mit Meldung, wenn Bildschirm/Kodierer nicht verfuegbar
    void Stop();                                        // beendet die Aufnahme und loescht die Abschnitte
    bool Running() const;
    // Setzt die letzten Sekunden zu einer MP4 zusammen und gibt den Dateipfad zurueck. Blockiert kurz (ein bis wenige Sekunden); aus einem
    // Arbeitsthread aufrufen. false mit Meldung, wenn noch nichts aufgenommen ist.
    bool Save(std::wstring& outFile, std::wstring& err);
    std::wstring Problem() const;                       // laufendes Problem der Aufnahme (z. B. keine Bilder), sonst leer
    unsigned FramesWritten() const;

private:
    struct Impl;
    Impl* p;
};

}  // namespace cliprec
