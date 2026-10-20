#pragma once
// Farben des Photo Commanders (0xRRGGBB fuer Direct2D). Basis ist die gemeinsame Suite-Palette in common/Theme.h;
// nur Flaechen, die dort fehlen (Seitenspalten, Leisten), sind hier abgeleitet.
#include "../common/Theme.h"

namespace Theme {
// COLORREF (0x00BBGGRR) -> 0xRRGGBB
constexpr unsigned Rgb(COLORREF c) { return (GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c); }

constexpr unsigned Bg        = Rgb(theme::kBackground);
constexpr unsigned Panel     = Rgb(RGB(22, 24, 29));   // wie kPanel im PDF Reader
constexpr unsigned Topbar    = Rgb(RGB(22, 24, 29));
constexpr unsigned Card      = Rgb(theme::kCardRaised);
constexpr unsigned CardHover = Rgb(theme::kCardHover);
constexpr unsigned Border    = Rgb(theme::kBorder);
constexpr unsigned Text      = Rgb(theme::kText);
constexpr unsigned TextDim   = Rgb(theme::kTextDim);
constexpr unsigned Accent    = Rgb(theme::kAccent);
constexpr unsigned Danger    = Rgb(theme::kRed);
constexpr unsigned Warn      = Rgb(theme::kOrange);
}  // namespace Theme
