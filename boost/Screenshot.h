#pragma once
// Duncan's Boost - Screenshot: speichert den Bildschirm, auf dem gerade das vordere Fenster liegt, als PNG (neben den Clips).
// Es wird das fertige Desktop-Bild kopiert (wie bei Windows' eigenem Screenshot); das Overlay von Boost selbst ist nicht dabei.
// Im echten Vollbild bleibt das Bild eventuell schwarz, wie bei den Clips.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <string>

namespace shot {

inline std::wstring FileName(const SYSTEMTIME& t)    // "Screenshot_2026-11-16_21-30-05.png"
{
    wchar_t b[64];
    swprintf(b, 64, L"Screenshot_%04d-%02d-%02d_%02d-%02d-%02d.png", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return b;
}
// Ordner: dieselbe wie bei den Clips (Videos\Boost-Clips); leer = nicht gefunden
std::wstring Folder();
// Speichert den Bildschirm von mon in dir; file = Pfad der Datei, err = Meldung bei Fehler. GDI+ muss gestartet sein.
bool Save(HMONITOR mon, const std::wstring& dir, std::wstring& file, std::wstring& err);

}  // namespace shot
