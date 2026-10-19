#pragma once
// Gemeinsame Farbpalette aller Duncan's-Programme (Suite, Driver Updater, PDF Reader, Privacy Shield).
// Eine Aenderung hier wirkt in allen Programmen. Programmspezifische Farben bleiben in der jeweiligen Datei.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace theme {

inline constexpr COLORREF kBackground  = RGB(18, 19, 22);
inline constexpr COLORREF kCardRaised  = RGB(35, 38, 47);
inline constexpr COLORREF kCardHover   = RGB(43, 47, 59);
inline constexpr COLORREF kBorder      = RGB(45, 49, 61);
inline constexpr COLORREF kBorderHover = RGB(96, 104, 126);
inline constexpr COLORREF kText        = RGB(235, 237, 242);
inline constexpr COLORREF kTextDim     = RGB(150, 155, 168);
inline constexpr COLORREF kAccent      = RGB(64, 156, 255);
inline constexpr COLORREF kGreen       = RGB(52, 199, 120);
inline constexpr COLORREF kRed         = RGB(240, 84, 84);
inline constexpr COLORREF kOrange      = RGB(255, 150, 50);

}  // namespace theme
