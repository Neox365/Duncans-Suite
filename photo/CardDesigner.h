#pragma once
// Fenster "Karte gestalten" (Photo Commander): Fotos, Layout, Hintergrund und Text einstellen, Vorschau, Speichern als Bild.
#include <windows.h>

#include <string>
#include <vector>

namespace card {

// Zeigt das Fenster modal zum Elternfenster und kehrt nach dem Schliessen zurueck. photos: vorgewaehlte Fotos.
void ShowDesigner(HWND owner, const std::vector<std::wstring>& photos);

}  // namespace card
