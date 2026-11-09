#include "Crosshair.h"

#include <algorithm>

namespace boostmode {

namespace {
const wchar_t* const kStyles[] = { L"Kreuz mit Lücke", L"Kreuz", L"Punkt", L"Kreuz mit Punkt", L"Kreis" };
struct ColorDef { const wchar_t* name; COLORREF value; };
const ColorDef kColors[] = {
    { L"Grün", RGB(0, 255, 70) }, { L"Rot", RGB(255, 40, 40) }, { L"Weiß", RGB(255, 255, 255) },
    { L"Gelb", RGB(255, 235, 0) }, { L"Cyan", RGB(0, 230, 255) }, { L"Magenta", RGB(255, 60, 220) } };
struct SizeDef { const wchar_t* name; int px; };
const SizeDef kSizes[] = { { L"Klein", 7 }, { L"Mittel", 12 }, { L"Groß", 20 } };
template <class T, size_t N> constexpr int Count(const T (&)[N]) { return static_cast<int>(N); }
}  // namespace

int CrosshairStyleCount() { return Count(kStyles); }
const wchar_t* CrosshairStyleName(int i) { return kStyles[std::clamp(i, 0, Count(kStyles) - 1)]; }
int CrosshairColorCount() { return Count(kColors); }
const wchar_t* CrosshairColorName(int i) { return kColors[std::clamp(i, 0, Count(kColors) - 1)].name; }
COLORREF CrosshairColorValue(int i) { return kColors[std::clamp(i, 0, Count(kColors) - 1)].value; }
int CrosshairSizeCount() { return Count(kSizes); }
const wchar_t* CrosshairSizeName(int i) { return kSizes[std::clamp(i, 0, Count(kSizes) - 1)].name; }
int CrosshairSizePx(int i) { return kSizes[std::clamp(i, 0, Count(kSizes) - 1)].px; }

CrosshairSpec ClampCrosshair(CrosshairSpec s) {
    s.style = std::clamp(s.style, 0, Count(kStyles) - 1);
    s.color = std::clamp(s.color, 0, Count(kColors) - 1);
    s.size = std::clamp(s.size, 0, Count(kSizes) - 1);
    return s;
}

bool CrosshairVisible(const CrosshairSpec& s, const std::vector<std::wstring>& gameExes, const std::wstring& fg, const std::wstring& self) {
    if (!s.on) return false;
    if (!self.empty() && fg == self) return true;                      // Vorschau, solange Boost vorne ist
    if (gameExes.empty()) return true;                                 // kein Spiel angehakt: immer
    return !fg.empty() && std::find(gameExes.begin(), gameExes.end(), fg) != gameExes.end();
}

}  // namespace boostmode
