#pragma once
// Duncan's Boost - Live-Werte fuer die Anzeige im Fenster: CPU-, RAM- und GPU-Auslastung sowie GPU-Temperatur.
// CPU: GetSystemTimes (Differenz zweier Messungen). RAM: GlobalMemoryStatusEx. GPU-Auslastung: Windows-Leistungsindikator
// "GPU Engine\Utilization Percentage" (alle Hersteller, 3D-Engines). GPU-Temperatur: nur ueber NVML (nvml.dll aus System32) bei NVIDIA;
// bei anderen Herstellern gibt Windows ohne Zusatztreiber keinen verlaesslichen Wert her, dann bleibt es leer (-1).
// Eine CPU-Temperatur gibt es bewusst nicht (Windows liefert sie nicht verlaesslich, falsche Zahlen waeren schlimmer als keine).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace boostmode {

struct Live {
    int cpu = -1;                 // 0..100, -1 = noch kein Wert (erste Messung)
    int ram = -1;                 // 0..100
    unsigned long long ramUsedMb = 0, ramTotalMb = 0;
    int gpu = -1;                 // 0..100, -1 = nicht verfuegbar
    int gpuTemp = -1;             // Grad Celsius, -1 = nicht verfuegbar
    std::wstring gpuName;         // z. B. "NVIDIA GeForce RTX ..." (nur mit NVML)
};

// --- reine Rechenhilfen (getrennt testbar) ---
// CPU-Auslastung in Prozent aus zwei Messungen von GetSystemTimes (Idle, Kernel inkl. Idle, User); -1 bei Nulldifferenz
int CpuPercent(unsigned long long idle0, unsigned long long kernel0, unsigned long long user0,
               unsigned long long idle1, unsigned long long kernel1, unsigned long long user1);
// GPU-Auslastung: Summe der 3D-Engine-Instanzen, auf 0..100 begrenzt; instances = Namen der Instanzen, values = Prozent je Instanz
int GpuPercent(const std::vector<std::wstring>& instances, const std::vector<double>& values);
// Bewertung fuer die Farbe: 0 = normal, 1 = hoch (orange), 2 = sehr hoch (rot)
int LoadLevel(int percent);
int TempLevel(int celsius);

class LiveStats {
public:
    LiveStats();
    ~LiveStats();
    LiveStats(const LiveStats&) = delete;
    LiveStats& operator=(const LiveStats&) = delete;
    Live Sample();                // alle ~1 s aufrufen; die erste Messung hat noch keine CPU-/GPU-Auslastung
private:
    struct Impl;
    Impl* m;
};

}  // namespace boostmode
