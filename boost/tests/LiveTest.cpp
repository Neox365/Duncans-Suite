// Tests fuer die Live-Werte von Duncan's Boost (LiveStats). Nur Windows.
// g++ -std=c++17 -DUNICODE -D_UNICODE -I.. LiveTest.cpp ../LiveStats.cpp -o LiveTest.exe -lpdh -luser32 -static
#include "../LiveStats.h"

#include <cstdio>

using namespace boostmode;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

int main()
{
    // CPU-Rechnung (Kernel-Zeit enthaelt die Leerlaufzeit)
    CHECK(CpuPercent(0, 0, 0, 50, 100, 0) == 50);                 // 100 Einheiten gesamt, davon 50 Leerlauf
    CHECK(CpuPercent(0, 0, 0, 0, 100, 100) == 100);               // nie Leerlauf
    CHECK(CpuPercent(0, 0, 0, 100, 100, 0) == 0);                 // nur Leerlauf
    CHECK(CpuPercent(10, 20, 30, 10, 20, 30) == -1);              // keine Zeit vergangen
    CHECK(CpuPercent(10, 20, 30, 5, 40, 40) == -1);               // Zaehler laeuft rueckwaerts
    CHECK(CpuPercent(0, 0, 0, 200, 100, 0) == -1);                // Leerlauf groesser als Gesamt: unplausibel
    // 400 Kernel-Zuwachs + 300 User-Zuwachs = 700 gesamt; Leerlauf 300; aktiv 400 -> 57 %
    CHECK(CpuPercent(1000, 5000, 3000, 1300, 5400, 3300) == 57);

    // GPU-Rechnung: nur 3D-Engines zaehlen, Summe wird begrenzt
    std::vector<std::wstring> n = { L"pid_1_luid_0x0_0x1_phys_0_eng_0_engtype_3D", L"pid_2_luid_0x0_0x1_phys_0_eng_0_engtype_3D",
                                    L"pid_2_luid_0x0_0x1_phys_0_eng_3_engtype_VideoDecode", L"pid_3_luid_0x0_0x1_phys_0_eng_1_engtype_Copy" };
    CHECK(GpuPercent(n, { 20.0, 15.5, 90.0, 80.0 }) == 36);       // 20 + 15,5 = 35,5 -> 36; Video/Copy zaehlen nicht
    CHECK(GpuPercent(n, { 80.0, 70.0, 0, 0 }) == 100);            // begrenzt
    CHECK(GpuPercent(n, { 0.0, 0.0, 50.0, 50.0 }) == 0);
    CHECK(GpuPercent(n, { -5.0, 10.0, 0, 0 }) == 10);             // negative Werte ignorieren
    CHECK(GpuPercent({ L"engtype_VideoDecode" }, { 10.0 }) == -1);   // keine 3D-Engine: kein Wert
    CHECK(GpuPercent({}, {}) == -1);
    CHECK(GpuPercent(n, { 1.0 }) == 1);                           // weniger Werte als Namen: kein Absturz

    // Bewertung
    CHECK(LoadLevel(0) == 0 && LoadLevel(69) == 0 && LoadLevel(70) == 1 && LoadLevel(89) == 1 && LoadLevel(90) == 2 && LoadLevel(100) == 2);
    CHECK(TempLevel(40) == 0 && TempLevel(74) == 0 && TempLevel(75) == 1 && TempLevel(84) == 1 && TempLevel(85) == 2);

    // Echte Messung (Werte muessen im erlaubten Bereich liegen; die Zahlen dienen nur der Ansicht)
    {
        LiveStats s;
        const Live first = s.Sample();
        CHECK(first.cpu == -1 || (first.cpu >= 0 && first.cpu <= 100));
        CHECK(first.ram >= 0 && first.ram <= 100 && first.ramTotalMb > 0 && first.ramUsedMb <= first.ramTotalMb);
        Sleep(1100);
        const Live l = s.Sample();
        CHECK(l.cpu >= 0 && l.cpu <= 100);
        CHECK(l.ram >= 0 && l.ram <= 100);
        CHECK(l.gpu == -1 || (l.gpu >= 0 && l.gpu <= 100));
        CHECK(l.gpuTemp == -1 || (l.gpuTemp > 0 && l.gpuTemp < 150));
        std::printf("Messung: CPU %d %%, RAM %d %% (%llu von %llu MB), GPU %d %%, GPU-Temperatur %d, Name: %ls\n", l.cpu, l.ram, l.ramUsedMb, l.ramTotalMb, l.gpu, l.gpuTemp, l.gpuName.c_str());
    }

    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
