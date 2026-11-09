#pragma once
// Duncan's Boost - Fadenkreuz: ein kleines, durchklickbares Fenster in der Bildschirmmitte ueber dem Spiel.
// Es ist ein normales Windows-Fenster (kein Eingriff in das Spiel, kein Hook, kein Speicherzugriff). Es zeigt sich nur ueber den
// angehakten Spielen (ist keines angehakt, immer) und ueber Boost selbst als Vorschau. Im echten Vollbild (Exclusive) ist es nicht
// zu sehen, nur im Fenster- oder randlosen Modus. Manche Spiele und Turniere verbieten Overlays.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace boostmode {

struct CrosshairSpec {
    bool on = false;
    int style = 0;      // Index in CrosshairStyleName
    int color = 0;      // Index in CrosshairColorName
    int size = 1;       // Index in CrosshairSizeName
};

int CrosshairStyleCount();
const wchar_t* CrosshairStyleName(int i);        // Kreuz mit Luecke, Kreuz, Punkt, Kreuz mit Punkt, Kreis
int CrosshairColorCount();
const wchar_t* CrosshairColorName(int i);
COLORREF CrosshairColorValue(int i);
int CrosshairSizeCount();
const wchar_t* CrosshairSizeName(int i);         // Klein, Mittel, Gross
int CrosshairSizePx(int i);                      // Halbe Laenge der Arme bei 96 dpi
CrosshairSpec ClampCrosshair(CrosshairSpec s);   // Indizes in den gueltigen Bereich

// Soll das Fadenkreuz jetzt zu sehen sein? gameExes = angehakte Spiele, foregroundExe = Programm im Vordergrund (klein),
// selfExe = EXE von Boost (klein; Vorschau beim Einstellen).
bool CrosshairVisible(const CrosshairSpec& s, const std::vector<std::wstring>& gameExes, const std::wstring& foregroundExe, const std::wstring& selfExe);

// Das Fenster. Update zeichnet bei Aenderung neu und setzt es auf den Monitor des Vordergrundfensters.
class CrosshairOverlay {
public:
    ~CrosshairOverlay();
    // monitor: Monitor, auf dem das Fadenkreuz in der Mitte erscheint; dpi: Skalierung
    void Update(bool visible, const CrosshairSpec& spec, HMONITOR monitor, UINT dpi);
    void Destroy();
private:
    bool Ensure();
    void Render(const CrosshairSpec& spec, UINT dpi);
    HWND hwnd_ = nullptr;
    int side_ = 0;
    CrosshairSpec drawn_{};
    UINT drawnDpi_ = 0;
    bool shown_ = false;
    int cx_ = -1, cy_ = -1;
};

}  // namespace boostmode
