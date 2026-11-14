#pragma once
// Duncan's Boost - Overlay im Spiel: ein eigenes Fenster (immer oben, nimmt nie den Fokus, nichts wird in das Spiel eingeklinkt).
// Zwei Zustaende: (1) kleine Statusleiste in einer Ecke, durchklickbar: Zock-Modus, Aufnahme-Punkt, Auslastung, kurze Meldungen;
// (2) Panel (auf Tastendruck): Knoepfe fuer Clip speichern, Aufnahme an/aus, Zock-Modus beenden, dazu Live-Werte.
// Wie das Fadenkreuz erscheint es im Fenster- oder randlosen Modus, nicht im echten Vollbild.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "LiveStats.h"

namespace boostmode {

constexpr int kOverlayCorners = 4;      // 0 oben links, 1 oben rechts, 2 unten links, 3 unten rechts
const wchar_t* OverlayCornerName(int corner);
int ClampOverlayCorner(int c);
int NextOverlayCorner(int c);

struct OverlayState {
    bool boostActive = false;       // Zock-Modus laeuft
    bool recording = false;         // Rueckblick-Aufnahme laeuft
    bool clipEnabled = false;       // Video-Clips sind eingeschaltet (auch wenn die Aufnahme gerade pausiert)
    int clipSeconds = 30;
    bool showStats = true;
    int corner = 1;
    Live live;
    std::wstring toast;             // kurze Meldung (leer = keine)
    bool toastGood = true;          // gruen oder rot
};

// Soll die Statusleiste jetzt zu sehen sein? Mit angehakten Spielen: wenn eines vorne ist (oder der Zock-Modus laeuft); ohne angehakte
// Spiele: nur wenn etwas anzuzeigen ist (Zock-Modus, Aufnahme oder Meldung). Eine Meldung wird immer gezeigt.
bool OverlayHudVisible(const std::vector<std::wstring>& gameExes, const std::wstring& foregroundExe, const OverlayState& s);

// Text der Leiste "CPU 23 %  GPU 31 %  54 °C" (fehlende Werte werden weggelassen; leer, wenn gar nichts da ist)
std::wstring StatsLine(const Live& l);

enum OverlayAction { OA_NONE = 0, OA_CLOSE = 1, OA_END_BOOST = 2, OA_SAVE_CLIP = 3, OA_TOGGLE_REC = 4 };

class GameOverlay {
public:
    ~GameOverlay();
    // Aktion eines Knopfs im Panel; wird im Fensterthread aufgerufen (der Aufrufer sollte nichts Langes darin tun)
    void SetActionHandler(std::function<void(int)> h) { onAction_ = std::move(h); }
    // Leiste/Panel aktualisieren. visible = Leiste zeigen (das Panel ist unabhaengig davon sichtbar, solange es offen ist).
    void Update(bool visible, const OverlayState& st, HMONITOR monitor, UINT dpi);
    void SetPanelOpen(bool open);
    bool PanelOpen() const { return panel_; }
    void Destroy();

private:
    bool Ensure();
    void Render();
    static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l);
    LRESULT OnMessage(HWND h, UINT m, WPARAM w, LPARAM l);
    void Place(HMONITOR monitor, int w, int h);

    HWND hwnd_ = nullptr;
    bool shown_ = false;
    bool panel_ = false;
    OverlayState st_;
    UINT dpi_ = 96;
    HMONITOR mon_ = nullptr;
    bool visible_ = false;
    int hover_ = 0;                          // OverlayAction unter der Maus
    bool tracking_ = false;
    RECT btn_[5]{};                          // Knopf-Rechtecke im Panel (Index = OverlayAction), in Fenster-Koordinaten
    std::wstring lastKey_;                   // zuletzt gezeichneter Zustand: nur bei Aenderung neu zeichnen
    std::function<void(int)> onAction_;
};

}  // namespace boostmode
