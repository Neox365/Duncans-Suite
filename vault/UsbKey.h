#pragma once
// USB-Vault-Key fuer Duncan's Vault: ein Wechseldatentraeger traegt eine versteckte Schluesseldatei (.duncansvault.key im Hauptordner
// des Laufwerks) mit 32 Byte Zufall. Ohne diese Datei laesst sich ein USB-Tresor nicht oeffnen (siehe Vault.h, Dateiversion 3).
// Reine Logik ohne Oberflaeche (testbar): alle Funktionen arbeiten auf einem Laufwerkswurzel-Pfad wie L"E:\\".
#include <cstring>
#include <string>
#include <vector>

#include "Crypto.h"

namespace vault {

inline const wchar_t* KeyFileName() { return L".duncansvault.key"; }

struct StickKey {
    Bytes id;          // 16 Byte; nicht geheim, steht auch im Kopf der Tresor-Datei und ordnet Stick und Tresor einander zu
    SecBytes secret;   // 32 Byte Zufall: das eigentliche Schluesselmaterial
    bool Valid() const { return id.size() == 16 && secret.size() == 32; }
    void Close() { id.clear(); Wipe(secret); }
};

inline bool Sha256(const unsigned char* data, size_t n, unsigned char out[32])
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return false;
    const NTSTATUS st = BCryptHash(alg, nullptr, 0, const_cast<PUCHAR>(data), static_cast<ULONG>(n), out, 32);
    BCryptCloseAlgorithmProvider(alg, 0);
    return st == 0;
}

// Schluesseldatei: "DVKY" | 1 | Id (16) | Geheimnis (32) | SHA-256 der vorherigen 53 Byte (Pruefsumme gegen Beschaedigung)
constexpr size_t kKeyFileLen = 4 + 1 + 16 + 32 + 32;

inline bool NewStickKey(StickKey& k)
{
    k.Close();
    k.id.assign(16, 0);
    k.secret.assign(32, 0);
    if (!RandomBytes(k.id.data(), 16) || !RandomBytes(k.secret.data(), 32)) { k.Close(); return false; }
    return true;
}

inline SecBytes EncodeKeyFile(const StickKey& k)
{
    SecBytes d;
    const unsigned char head[5] = {'D', 'V', 'K', 'Y', 1};
    d.insert(d.end(), head, head + 5);
    d.insert(d.end(), k.id.begin(), k.id.end());
    d.insert(d.end(), k.secret.begin(), k.secret.end());
    unsigned char h[32];
    if (!Sha256(d.data(), d.size(), h)) return SecBytes();
    d.insert(d.end(), h, h + 32);
    return d;
}

inline bool DecodeKeyFile(const SecBytes& d, StickKey& k)
{
    k.Close();
    if (d.size() != kKeyFileLen || d[0] != 'D' || d[1] != 'V' || d[2] != 'K' || d[3] != 'Y' || d[4] != 1) return false;
    unsigned char h[32];
    if (!Sha256(d.data(), kKeyFileLen - 32, h) || memcmp(h, d.data() + kKeyFileLen - 32, 32) != 0) return false;
    k.id.assign(d.begin() + 5, d.begin() + 21);
    k.secret.assign(d.begin() + 21, d.begin() + 53);
    return true;
}

inline std::wstring KeyFilePath(const std::wstring& root)
{
    std::wstring r = root;
    if (!r.empty() && r.back() != L'\\' && r.back() != L'/') r += L'\\';
    return r + KeyFileName();
}

// Legt die Schluesseldatei an (versteckt + System). overwrite=false: eine vorhandene Datei bleibt unberuehrt (false).
inline bool WriteKeyFile(const std::wstring& root, const StickKey& k, bool overwrite)
{
    const std::wstring path = KeyFilePath(root);
    SecBytes d = EncodeKeyFile(k);
    if (d.empty()) return false;
    const DWORD old = GetFileAttributesW(path.c_str());
    if (old != INVALID_FILE_ATTRIBUTES) {
        if (!overwrite) return false;
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);   // versteckte Systemdateien lassen sich sonst nicht ueberschreiben
    }
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    bool ok = WriteFile(f, d.data(), static_cast<DWORD>(d.size()), &w, nullptr) && w == d.size() && FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok) { SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(path.c_str()); return false; }
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    return true;
}

inline bool ReadKeyFile(const std::wstring& root, StickKey& k)
{
    k.Close();
    HANDLE f = CreateFileW(KeyFilePath(root).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    SecBytes d(kKeyFileLen + 1, 0);       // ein Byte mehr lesen: zu lange Dateien werden abgelehnt
    DWORD got = 0;
    const bool read = ReadFile(f, d.data(), static_cast<DWORD>(d.size()), &got, nullptr) != 0;
    CloseHandle(f);
    if (!read) return false;
    d.resize(got);
    return DecodeKeyFile(d, k);
}

inline bool DeleteKeyFile(const std::wstring& root)
{
    const std::wstring path = KeyFilePath(root);
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    return DeleteFileW(path.c_str()) != 0;
}

struct Stick {
    std::wstring root;            // z. B. L"E:\\"
    std::wstring label;           // Datentraegername (kann leer sein)
    unsigned long long bytes = 0;
    bool hasKey = false;          // enthaelt eine Schluesseldatei (die Datei wird hier nur gefunden, nicht gelesen)
};

// Alle angeschlossenen Wechseldatentraeger (GetDriveType == DRIVE_REMOVABLE) mit Medium. A: und B: (Diskette) werden uebersprungen.
inline std::vector<Stick> ListSticks()
{
    std::vector<Stick> out;
#ifdef VAULT_TESTING   // nur Test-Builds: ein Ordner aus DUNCANSVAULT_TEST_STICK gilt als eingesteckter Stick, solange er existiert
    {
        wchar_t fake[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"DUNCANSVAULT_TEST_STICK", fake, MAX_PATH) > 0 && GetFileAttributesW(fake) != INVALID_FILE_ATTRIBUTES) {
            Stick s;
            s.root = std::wstring(fake) + L"\\";
            s.label = L"TESTSTICK";
            s.bytes = 8ull << 30;
            s.hasKey = GetFileAttributesW(KeyFilePath(s.root).c_str()) != INVALID_FILE_ATTRIBUTES;
            out.push_back(std::move(s));
        }
    }
#endif
    const UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS);      // keine "Laufwerk nicht bereit"-Dialoge bei leeren Kartenlesern
    const DWORD mask = GetLogicalDrives();
    for (int i = 2; i < 26; ++i) {
        if (!(mask & (1u << i))) continue;
        Stick s;
        s.root = std::wstring(1, static_cast<wchar_t>(L'A' + i)) + L":\\";
        if (GetDriveTypeW(s.root.c_str()) != DRIVE_REMOVABLE) continue;
        wchar_t label[MAX_PATH + 1]{};
        if (!GetVolumeInformationW(s.root.c_str(), label, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0)) continue;   // kein Medium
        s.label = label;
        ULARGE_INTEGER total{};
        if (GetDiskFreeSpaceExW(s.root.c_str(), nullptr, &total, nullptr)) s.bytes = total.QuadPart;
        s.hasKey = GetFileAttributesW(KeyFilePath(s.root).c_str()) != INVALID_FILE_ATTRIBUTES;
        out.push_back(std::move(s));
    }
    SetErrorMode(oldMode);
    return out;
}

// Sucht unter den angeschlossenen Sticks den mit der Schluesseldatei zur Id wantId. Bei Erfolg liegen Schluessel und Laufwerk vor.
inline bool FindStick(const Bytes& wantId, StickKey& k, std::wstring* root)
{
    for (const Stick& s : ListSticks()) {
        if (!s.hasKey) continue;
        StickKey cand;
        if (ReadKeyFile(s.root, cand) && cand.id == wantId) {
            k = cand;
            if (root) *root = s.root;
            cand.Close();
            return true;
        }
        cand.Close();
    }
    return false;
}

// ---- Wiederherstellungscode: 20 Zufallsbyte als 32 Zeichen Base32 in acht Vierergruppen (160 Bit) ----
inline const char* Base32Alphabet() { return "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"; }

inline std::wstring NewRecoveryCode()
{
    unsigned char b[20]{};
    RandomBytes(b, sizeof b);
    std::wstring s;
    unsigned buf = 0;
    int bits = 0, count = 0;
    for (unsigned char c : b) {
        buf = (buf << 8) | c;
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            if (count && count % 4 == 0) s += L'-';
            s += static_cast<wchar_t>(Base32Alphabet()[(buf >> bits) & 31]);
            ++count;
        }
    }
    SecureZeroMemory(b, sizeof b);
    return s;
}

// Grossbuchstaben, nur A-Z und 2-7; Bindestriche, Leerzeichen und alles andere fallen weg
inline std::wstring NormalizeCode(const std::wstring& in)
{
    std::wstring s;
    for (wchar_t c : in) {
        if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - L'a' + L'A');
        if ((c >= L'A' && c <= L'Z') || (c >= L'2' && c <= L'7')) s += c;
    }
    return s;
}

}  // namespace vault
