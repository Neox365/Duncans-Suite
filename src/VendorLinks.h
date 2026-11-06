#pragma once
// VendorLinks.h - Hersteller-Suchseite zu einem Geraet ("Beim Hersteller suchen"). Reine Logik ohne Win32.
// Der automatische Versionsvergleich gilt nur fuer Grafikkarten (vendor_releases.json); fuer alle anderen Geraete (Chipsatz, WLAN, Audio ...)
// meldet Windows Update neue Treiber. Wer beim Hersteller nachsehen will, bekommt hier die passende Download-Seite oder eine Websuche.
#include <string>

#include "DriverInfo.h"

// Hersteller-Download-Seite nach PCI-Hersteller-ID ("VEN_8086"); leer, wenn unbekannt
std::wstring VendorPageForVen(const std::wstring& venId);

// Download-Seite des Herstellers oder, wenn unbekannt, eine Websuche nach "<Hersteller> <Geraet> Treiber"
std::wstring VendorSupportUrl(const DriverInfo& d);

// Prozent-Kodierung (UTF-8) fuer URL-Abfragen
std::wstring UrlEncode(const std::wstring& text);
