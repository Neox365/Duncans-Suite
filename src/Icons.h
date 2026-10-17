#pragma once
// Icons.h - Antialiased gezeichnete Vektor-Symbole und Glow-Balken (GDI+), ohne Bilddateien.
//
// Linker-Bibliothek: gdiplus.lib

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "DriverInfo.h"

// GDI+ starten/beenden (einmal pro Prozess).
bool IconsInit();
void IconsShutdown();

// Zeichnet eine abgerundete Kachel (getoent in "color") mit dem Kategorie-Symbol darin.
void DrawCategoryTile(HDC dc, const RECT& tile, CategoryIcon icon, COLORREF color, COLORREF cardColor);

// Lupe, einbeschrieben in "box".
void DrawSearchIcon(HDC dc, const RECT& box, COLORREF color);

// Fortschrittsbalken mit Verlauf, Glow und wanderndem Glanzlicht.
//   fraction      0..1 (ignoriert bei indeterminate)
//   indeterminate wandernder Abschnitt, wenn die Dauer unbekannt ist
//   phase         beliebiger, steigender Zaehler fuer die Animation
void DrawGlowBar(HDC dc, const RECT& track, double fraction, bool indeterminate, int phase,
                 COLORREF from, COLORREF to, COLORREF trackColor, bool animate);

// Weicher Schlagschatten unter einer abgerundeten Karte (Karte danach darueberzeichnen).
void DrawShadow(HDC dc, const RECT& card, int radius, int scalePx);

// Akkordeon-Pfeil: zugeklappt = nach rechts, aufgeklappt = nach unten.
void DrawChevron(HDC dc, const RECT& box, bool open, COLORREF color);

// Halbtransparent gefuelltes Rechteck (z. B. Abdunkelung hinter Dialogen).
void FillAlphaRect(HDC dc, const RECT& rc, COLORREF color, unsigned char alpha);

// Antialiased abgerundetes Panel mit Rand.
void DrawPanelAA(HDC dc, const RECT& rc, int radius, COLORREF fill, COLORREF border);

// Banner-Hintergrund: Verlauf, Rand, feines Punktraster und Schatten.
void DrawBannerPanel(HDC dc, const RECT& rc, int radius, int scalePx);

// Banner-/Splash-Szene: Eine Leiterbahn mit Knoten; die Pruefsonde (Chip) faehrt darueber und
// "prueft" die Knoten (gruen). probe = 0..1 Position, phase = steigender Zaehler fuer Pulse/Glow,
// busy = Scan aktiv (Pulsring + Strahl intensiver).
void DrawScanScene(HDC dc, const RECT& area, float probe, int phase, int scalePx, bool busy);
