// Test des Diagnose-Texts (common/Diagnostics.h).  Baut mit MinGW:  g++ -std=c++17 -I.. DiagnosticsTest.cpp -o DiagnosticsTest.exe
#include "../Diagnostics.h"

#include <cstdio>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)
static bool Has(const std::wstring& h, const wchar_t* n) { return h.find(n) != std::wstring::npos; }

int main()
{
    const std::wstring t = diag::Text(L"Oggys Docs-Files", L"0.15.0", 144);
    CHECK(Has(t, L"Programm: Oggys Docs-Files") && Has(t, L"Version: 0.15.0") && Has(t, L"Skalierung: 150 %") && Has(t, L"Windows: "));
    CHECK(Has(t, L"keine persönlichen Daten") && t.find(L'\\') == std::wstring::npos && t.find(L':', 0) != std::wstring::npos);   // keine Pfade
    CHECK(Has(diag::Text(L"X", L"1", 0), L"Skalierung: 100 %"));                                                                    // 0 = unbekannt: 100 %
    const std::wstring win = diag::WindowsVersion();
    CHECK(win.empty() || win.find(L"Build") != std::wstring::npos);
    CHECK(Has(t, L"64 Bit") || Has(t, L"32 Bit"));
    CHECK(t.find(L"\r\n") != std::wstring::npos);                                                                                    // Zeilenende fuer Notepad und Mail
    std::wprintf(L"%ls", t.c_str());
    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
