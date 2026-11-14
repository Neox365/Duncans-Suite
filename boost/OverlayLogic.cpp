#include "Overlay.h"

#include <algorithm>

namespace boostmode {

const wchar_t* OverlayCornerName(int corner)
{
    static const wchar_t* const n[kOverlayCorners] = { L"oben links", L"oben rechts", L"unten links", L"unten rechts" };
    return n[ClampOverlayCorner(corner)];
}

int ClampOverlayCorner(int c) { return c < 0 || c >= kOverlayCorners ? 1 : c; }
int NextOverlayCorner(int c) { return (ClampOverlayCorner(c) + 1) % kOverlayCorners; }

bool OverlayHudVisible(const std::vector<std::wstring>& gameExes, const std::wstring& fg, const OverlayState& s)
{
    if (!s.toast.empty()) return true;
    if (gameExes.empty()) return s.boostActive || s.recording;
    if (s.boostActive) return true;
    return !fg.empty() && std::find(gameExes.begin(), gameExes.end(), fg) != gameExes.end();
}

std::wstring StatsLine(const Live& l)
{
    std::wstring t;
    auto add = [&](const std::wstring& x) { t += (t.empty() ? L"" : L"   ") + x; };
    if (l.cpu >= 0) add(L"CPU " + std::to_wstring(l.cpu) + L" %");
    if (l.gpu >= 0) add(L"GPU " + std::to_wstring(l.gpu) + L" %");
    if (l.gpuTemp >= 0) add(std::to_wstring(l.gpuTemp) + L" °C");
    return t;
}

}  // namespace boostmode
