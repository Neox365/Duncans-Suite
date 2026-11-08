#pragma once
// Fenster "Filter & Korrektur": Frequenzkurve mit Live-Vorschau, eigene Filter (Glocke, Shelf, Hoch-/Tiefpass, Kerbfilter) und
// Kopfhoerer-Korrektur aus Dateien (AutoEq). Bearbeitet die Filterzeilen (Settings::extra) einer Kopie und gibt sie bei "Uebernehmen" zurueck.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "EqLogic.h"

namespace eq {

// Zeigt das Fenster modal zum Elternfenster. true: cfg.extra wurde geaendert (Uebernehmen), false: Abbrechen/Schliessen.
bool ShowFilterEditor(HWND owner, Settings& cfg);

}  // namespace eq
