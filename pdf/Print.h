#pragma once
// Duncan's PDF - Drucken. Jede Seite wird in hoechstens 300 dpi gerendert (Markierungen, Stempel und Notizen sind enthalten, der Nachtmodus nicht)
// und eingepasst auf den bedruckbaren Bereich gedruckt (nur verkleinert, nie vergroessert, zentriert).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

#include "PdfDocument.h"

namespace pdfprint {

// Druckt die Seiten (0-basiert, in dieser Reihenfolge) auf das Geraet dc. outFile: in eine Datei statt aufs Papier (z. B. fuer
// "Microsoft Print to PDF" ohne Rueckfrage); leer = normal. true = alle Seiten gesendet.
bool PrintPages(HDC dc, const PdfDocument& doc, const std::vector<int>& pages, const wchar_t* jobName, const wchar_t* outFile, std::wstring& err);

// Standard-Druckdialog (Drucker, Seitenbereich, Exemplare), dann PrintPages. cancelled = true, wenn der Dialog abgebrochen wurde.
bool PrintWithDialog(HWND owner, const PdfDocument& doc, const std::wstring& jobName, bool& cancelled, std::wstring& err);

// Seitenbereich aus dem Dialog (1-basiert von bis, 0 = alle) in Indizes
std::vector<int> PageRange(int pageCount, int from, int to);

}  // namespace pdfprint
