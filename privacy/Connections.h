#pragma once
// Duncan's Privacy Shield - Verbindungsuebersicht: welche Programme gerade offene TCP-Verbindungen haben (nur Anzeige, nichts wird veraendert
// oder blockiert). Quelle: Windows-Verbindungstabelle (GetExtendedTcpTable). Keine Oberflaeche.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace conn {

struct Entry {
    unsigned pid = 0;
    std::wstring process;      // Dateiname des Programms, z. B. "chrome.exe"
    std::wstring remote;       // Adresse ohne Port
    unsigned port = 0;         // Port der Gegenstelle
    bool lan = false;          // Adresse im eigenen Netzwerk (privat/Link-lokal) statt im Internet
};

struct Group {
    std::wstring process;
    unsigned pid = 0;
    int total = 0;             // alle Verbindungen des Programms
    int internet = 0;          // davon ins Internet (nicht Heimnetz)
    std::vector<std::wstring> samples;   // bis zu kMaxSamples Gegenstellen "Adresse:Port", Internet zuerst
};
constexpr size_t kMaxSamples = 3;

// Adresse in Text; v4: vier Bytes in Netzwerk-Reihenfolge (wie in der Windows-Tabelle), v6: 16 Bytes
std::wstring FormatV4(const unsigned char a[4]);
std::wstring FormatV6(const unsigned char a[16]);
bool IsLoopbackV4(const unsigned char a[4]);
bool IsLanV4(const unsigned char a[4]);          // 10/8, 172.16/12, 192.168/16, 169.254/16
bool IsLoopbackV6(const unsigned char a[16]);
bool IsLanV6(const unsigned char a[16]);         // fc00::/7 und fe80::/10

// Offene (aufgebaute) Verbindungen aller Programme, ohne Verbindungen auf diesem PC selbst (127.x, ::1). Leer bei Fehler.
std::vector<Entry> Snapshot();
// Nach Programm zusammenfassen: meiste Internet-Verbindungen zuerst, dann nach Name.
std::vector<Group> GroupByProcess(const std::vector<Entry>& entries);

}  // namespace conn
