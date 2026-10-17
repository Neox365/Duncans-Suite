#pragma once
// DriverInfo.h - Datenmodell fuer ein erkanntes Hardware-Geraet samt Treiberdaten.

#include <string>
#include <vector>

enum class DriverStatus
{
    Pending,          // Noch nicht gescannt
    Ok,               // Treiber installiert, keine Fehler gemeldet
    Problem,          // Geraet meldet einen Problemcode (Geraete-Manager)
    UpdateAvailable   // Reserviert fuer spaeteren Online-Abgleich
};

// Symbol-Typ fuer die Kartenansicht (wird aus der Geraeteklasse abgeleitet).
enum class CategoryIcon
{
    Other, Gpu, Audio, Network, Usb, Storage, Cpu, System, Input, Monitor,
    Bluetooth, Camera, Printer, Battery, Port
};

struct DriverInfo
{
    std::wstring deviceName;      // Anzeigename (Friendly Name oder Beschreibung)
    CategoryIcon icon = CategoryIcon::Other;
    std::wstring category;        // z. B. "Grafikkarte", "Netzwerk", "Audio"
    std::wstring manufacturer;    // Hersteller laut INF
    std::wstring driverVersion;   // z. B. "31.0.15.5222"
    std::wstring driverDate;      // Format TT.MM.JJJJ
    std::wstring provider;        // Treiber-Anbieter
    std::wstring hardwareId;      // Erste Hardware-ID (PnP-ID), z. B. PCI\VEN_10DE&DEV_2684...
    std::vector<std::wstring> hardwareIds; // Alle Hardware- + Compatible-IDs (GROSSBUCHSTABEN) fuer den Update-Abgleich
    int driverDateKey = 0;        // JJJJMMTT, 0 = unbekannt
    std::wstring updateTitle;     // Titel des verfuegbaren Updates (Windows Update)
    std::wstring updateId;        // WUA-Update-ID zum spaeteren Installieren
    std::wstring updateUrl;       // Download-Seite des Herstellers (nur bei Hersteller-Abgleich)
    std::wstring infName;         // veroeffentlichter INF-Name, z. B. "oem12.inf" (fuer Treiber-Sicherung)
    std::wstring updateDate;      // Datum des verfuegbaren Treibers (TT.MM.JJJJ)
    unsigned long problemCode = 0; // CM_PROB_* (0 = kein Problem)
    DriverStatus status = DriverStatus::Pending;
};

// Deutscher Text fuer den Status-Chip.
const wchar_t* DriverStatusText(DriverStatus status);
