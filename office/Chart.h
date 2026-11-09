#pragma once
// Diagramm-Daten fuer Duncan's Office (Sheets): liest einen markierten Bereich und macht daraus Beschriftungen und Datenreihen.
// Reine Logik ohne Win32, damit sie getrennt testbar bleibt (office/tests/OfficeTest.cpp).
//
// Regeln: Enthaelt die erste Zeile des Bereichs keine Zahl, sind ihre Zellen die Namen der Reihen. Enthaelt die erste Spalte
// (ohne Kopfzeile) keine Zahl, sind ihre Zellen die Beschriftungen der Werte. Alle uebrigen Spalten sind Datenreihen.
// Zellen ohne Zahl zaehlen als 0. Hoechstens 8 Reihen und 60 Werte je Reihe.
#include <algorithm>
#include <string>
#include <vector>

#include "Sheet.h"

namespace chart {

constexpr int kMaxSeries = 8, kMaxPoints = 60;

struct Data {
    bool ok = false;
    std::wstring error;                      // Grund, wenn ok == false
    std::vector<std::wstring> labels;        // eine je Wert
    std::vector<std::wstring> names;         // eine je Reihe (leer, wenn es keine Kopfzeile gibt)
    std::vector<std::vector<double>> series; // series[reihe][wert]
};

inline bool HasNumber(const sheet::Sheet& s, int r1, int c1, int r2, int c2)
{
    for (int r = r1; r <= r2; ++r)
        for (int c = c1; c <= c2; ++c)
            if (s.Get(r, c).kind == sheet::Value::Number) return true;
    return false;
}

inline Data Extract(const sheet::Sheet& s, int r1, int c1, int r2, int c2)
{
    Data d;
    if (r1 == r2 && c1 == c2) { d.error = L"Markiere zuerst einen Bereich mit Zahlen (mit der Maus ziehen)."; return d; }
    if (!HasNumber(s, r1, c1, r2, c2)) { d.error = L"Im markierten Bereich stehen keine Zahlen."; return d; }

    const bool header = r2 > r1 && !HasNumber(s, r1, c1, r1, c2);
    const int dr1 = header ? r1 + 1 : r1;
    const bool labelCol = c2 > c1 && !HasNumber(s, dr1, c1, r2, c1);
    const int sc1 = labelCol ? c1 + 1 : c1;
    if (dr1 > r2 || sc1 > c2) { d.error = L"Der Bereich enthält keine Datenreihe."; return d; }

    const int rows = std::min(r2 - dr1 + 1, kMaxPoints), cols = std::min(c2 - sc1 + 1, kMaxSeries);
    for (int i = 0; i < rows; ++i) {
        const int r = dr1 + i;
        d.labels.push_back(labelCol ? s.Display(r, c1) : std::to_wstring(i + 1));
    }
    for (int j = 0; j < cols; ++j) {
        const int c = sc1 + j;
        d.names.push_back(header ? s.Display(r1, c) : std::wstring());
        std::vector<double> v;
        for (int i = 0; i < rows; ++i) {
            const sheet::Value& x = s.Get(dr1 + i, c);
            v.push_back(x.kind == sheet::Value::Number ? x.num : 0.0);
        }
        d.series.push_back(std::move(v));
    }
    d.ok = true;
    return d;
}

// "Schoene" obere Achsengrenze (1, 2, 2.5, 5 mal Zehnerpotenz) und Schrittweite fuer etwa fuenf Linien
inline void NiceAxis(double lo, double hi, double& axMin, double& axMax, double& step)
{
    if (lo > 0) lo = 0;
    if (hi < 0) hi = 0;
    if (hi - lo < 1e-12) hi = lo + 1;
    const double raw = (hi - lo) / 5.0;
    double mag = 1;
    while (raw / mag >= 10) mag *= 10;
    while (raw / mag < 1) mag /= 10;
    const double f = raw / mag;
    step = (f <= 1 ? 1 : f <= 2 ? 2 : f <= 5 ? 5 : 10) * mag;
    axMin = std::floor(lo / step) * step;
    axMax = std::ceil(hi / step) * step;
}

}  // namespace chart
