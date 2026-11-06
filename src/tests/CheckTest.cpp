// Logik-Tests fuer die geplante Hintergrund-Pruefung (Aufgabenbeschreibung) und die Hersteller-Links des Driver Updaters
#include "../CheckTask.h"
#include "../VendorLinks.h"

#include <cstdio>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #c); } } while (0)

static bool Has(const std::wstring& s, const std::wstring& sub) { return s.find(sub) != std::wstring::npos; }

int main()
{
    // Aufgabenbeschreibung
    const std::wstring xml = drvcheck::TaskXml(L"C:\\ProgramData\\DuncansSuite\\watch\\DuncansDriverUpdater.exe", L"PC\\Du & Ich <1>");
    CHECK(xml.compare(0, 5, L"<?xml") == 0);
    CHECK(Has(xml, L"<Command>C:\\ProgramData\\DuncansSuite\\watch\\DuncansDriverUpdater.exe</Command>") && Has(xml, L"<Arguments>--check</Arguments>"));
    CHECK(Has(xml, L"<RunLevel>HighestAvailable</RunLevel>") && Has(xml, L"<LogonType>InteractiveToken</LogonType>"));
    CHECK(Has(xml, L"<CalendarTrigger>") && Has(xml, L"<ScheduleByWeek>") && Has(xml, L"<Monday/>") && Has(xml, L"<WeeksInterval>1</WeeksInterval>"));
    CHECK(Has(xml, L"<StartWhenAvailable>true</StartWhenAvailable>") && Has(xml, L"<RunOnlyIfNetworkAvailable>true</RunOnlyIfNetworkAvailable>"));
    CHECK(Has(xml, L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>") && Has(xml, L"<ExecutionTimeLimit>PT30M</ExecutionTimeLimit>"));
    CHECK(Has(xml, L"PC\\Du &amp; Ich &lt;1&gt;") && !Has(xml, L"Du & Ich"));
    CHECK(Has(xml, L"<StartBoundary>2026-01-05T12:00:00</StartBoundary>"));                 // ein Montag
    const std::wstring copy = drvcheck::CopyPath();
    CHECK(copy.empty() || Has(copy, L"\\watch\\DuncansDriverUpdater.exe"));
    CHECK(std::wstring(drvcheck::TaskName()).find(L"Treiber") != std::wstring::npos);

    // Hersteller-Links
    CHECK(VendorPageForVen(L"VEN_8086") == L"https://www.intel.com/content/www/us/en/download-center/home.html");
    CHECK(VendorPageForVen(L"ven_10ec") == L"https://www.realtek.com/Download");
    CHECK(VendorPageForVen(L"VEN_1022") == VendorPageForVen(L"VEN_1002") && !VendorPageForVen(L"VEN_1022").empty());
    CHECK(VendorPageForVen(L"VEN_FFFF").empty() && VendorPageForVen(L"").empty());
    DriverInfo wifi;
    wifi.deviceName = L"Intel(R) Wi-Fi 6 AX201 160MHz";
    wifi.manufacturer = L"Intel Corporation";
    wifi.hardwareId = L"PCI\\VEN_8086&DEV_06F0&SUBSYS_00748086&REV_00";
    CHECK(VendorSupportUrl(wifi) == VendorPageForVen(L"VEN_8086"));
    DriverInfo usb;
    usb.deviceName = L"USB-Audiogerät Größe";
    usb.manufacturer = L"Acme & Co";
    usb.hardwareId = L"USB\\VID_1234&PID_5678\\ABC";
    const std::wstring url = VendorSupportUrl(usb);
    CHECK(url == L"https://duckduckgo.com/?q=Acme+%26+Co+USB-Audioger%C3%A4t+Gr%C3%B6%C3%9Fe+Treiber");
    DriverInfo viaList;                                                                         // Hardware-ID nur in der Liste
    viaList.deviceName = L"Netzwerk";
    viaList.hardwareIds = { L"PCI\\VEN_10EC&DEV_8168" };
    CHECK(VendorSupportUrl(viaList) == L"https://www.realtek.com/Download");
    DriverInfo none;                                                                            // ohne Angaben: nur der Name
    none.deviceName = L"X";
    CHECK(VendorSupportUrl(none) == L"https://duckduckgo.com/?q=X+Treiber");
    CHECK(UrlEncode(L"a b/c?d=e") == L"a+b%2Fc%3Fd%3De" && UrlEncode(L"").empty() && UrlEncode(L"Ab-_.~09") == L"Ab-_.~09");
    CHECK(UrlEncode(L"\xD83D\xDE00") == L"%F0%9F%98%80");                                       // Emoji (Surrogatpaar)

    std::printf("%d Pruefungen, %d Fehler\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
