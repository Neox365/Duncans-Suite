#pragma once
// Einmalcodes (TOTP, RFC 6238) fuer Duncan's Vault: aus dem 2FA-Schluessel einer Webseite wird alle 30 Sekunden ein 6-stelliger Code.
// Reine Logik ohne Oberflaeche (testbar); HMAC ueber die Windows-Kryptografie (CNG), alles bleibt lokal.
// Eingabe ist entweder der Schluessel selbst (Base32, wie ihn Webseiten unter dem QR-Code anzeigen) oder eine otpauth://totp/... -Adresse.
#include <ctime>
#include <string>
#include <vector>

#include "Crypto.h"

namespace vault {

struct TotpParams {
    Bytes key;
    int digits = 6;
    int period = 30;
    int algo = 1;   // 1 = SHA-1 (Standard), 256 = SHA-256, 512 = SHA-512
    std::wstring issuer, account;
};

inline void Wipe(TotpParams& p)
{
    Wipe(p.key);
    p.issuer.clear();
    p.account.clear();
}

// Base32 (RFC 4648): Gross-/Kleinschreibung egal, Leerzeichen, Bindestriche und "=" werden ignoriert
inline bool Base32Decode(const std::wstring& s, Bytes& out)
{
    out.clear();
    unsigned acc = 0;
    int bits = 0;
    for (wchar_t c : s) {
        if (c == L' ' || c == L'-' || c == L'=' || c == L'\t') continue;
        int v;
        if (c >= L'A' && c <= L'Z') v = c - L'A';
        else if (c >= L'a' && c <= L'z') v = c - L'a';
        else if (c >= L'2' && c <= L'7') v = 26 + (c - L'2');
        else { out.clear(); return false; }
        acc = (acc << 5) | static_cast<unsigned>(v);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<unsigned char>((acc >> bits) & 0xFF));
        }
    }
    return !out.empty();
}

namespace totp_detail {

inline std::wstring PercentDecode(const std::wstring& s)
{
    std::string bytes;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'%' && i + 2 < s.size() + 0 && iswxdigit(s[i + 1]) && iswxdigit(s[i + 2])) {
            bytes.push_back(static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else if (s[i] == L'+') {
            bytes.push_back(' ');
        } else if (s[i] < 128) {
            bytes.push_back(static_cast<char>(s[i]));
        } else {
            bytes += ToUtf8(std::wstring(1, s[i]));
        }
    }
    return FromUtf8(bytes);
}

inline std::wstring LowerW(std::wstring s)
{
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

}  // namespace totp_detail

// Schluessel oder otpauth-Adresse einlesen; false, wenn weder das eine noch das andere brauchbar ist
inline bool ParseTotp(const std::wstring& input, TotpParams& p)
{
    using namespace totp_detail;
    p = TotpParams{};
    size_t a = 0, b = input.size();
    while (a < b && iswspace(input[a])) ++a;
    while (b > a && iswspace(input[b - 1])) --b;
    const std::wstring in = input.substr(a, b - a);
    if (in.empty()) return false;
    std::wstring secret;
    if (LowerW(in.substr(0, 10)) == L"otpauth://") {
        const size_t slash = in.find(L'/', 10);
        if (LowerW(in.substr(10, slash == std::wstring::npos ? std::wstring::npos : slash - 10)) != L"totp") return false;   // hotp (Zaehler) gibt es nicht
        const size_t q = in.find(L'?');
        if (slash != std::wstring::npos) {
            const std::wstring label = PercentDecode(in.substr(slash + 1, q == std::wstring::npos ? std::wstring::npos : q - slash - 1));
            const size_t colon = label.find(L':');
            if (colon != std::wstring::npos) { p.issuer = label.substr(0, colon); p.account = label.substr(colon + 1); }
            else p.account = label;
        }
        if (q == std::wstring::npos) return false;
        size_t pos = q + 1;
        while (pos <= in.size()) {
            size_t amp = in.find(L'&', pos);
            if (amp == std::wstring::npos) amp = in.size();
            const std::wstring pair = in.substr(pos, amp - pos);
            const size_t eq = pair.find(L'=');
            if (eq != std::wstring::npos) {
                const std::wstring k = LowerW(pair.substr(0, eq)), v = PercentDecode(pair.substr(eq + 1));
                if (k == L"secret") secret = v;
                else if (k == L"issuer") p.issuer = v;
                else if (k == L"digits") { const int d = _wtoi(v.c_str()); if (d >= 6 && d <= 8) p.digits = d; }
                else if (k == L"period") { const int d = _wtoi(v.c_str()); if (d >= 1 && d <= 300) p.period = d; }
                else if (k == L"algorithm") {
                    const std::wstring al = LowerW(v);
                    if (al == L"sha1") p.algo = 1;
                    else if (al == L"sha256") p.algo = 256;
                    else if (al == L"sha512") p.algo = 512;
                    else return false;
                }
            }
            pos = amp + 1;
        }
    } else {
        secret = in;
    }
    if (!Base32Decode(secret, p.key) || p.key.size() < 10) { Wipe(p); return false; }
    return true;
}

inline bool HmacDigest(int algo, const Bytes& key, const unsigned char* msg, size_t n, Bytes& out)
{
    const wchar_t* id = algo == 512 ? BCRYPT_SHA512_ALGORITHM : algo == 256 ? BCRYPT_SHA256_ALGORITHM : BCRYPT_SHA1_ALGORITHM;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&alg, id, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0) {
        DWORD len = 0, got = 0;
        if (BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&len), sizeof(len), &got, 0) == 0 && len > 0 &&
            BCryptCreateHash(alg, &h, nullptr, 0, const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0) == 0 &&
            BCryptHashData(h, const_cast<PUCHAR>(msg), static_cast<ULONG>(n), 0) == 0) {
            out.assign(len, 0);
            ok = BCryptFinishHash(h, out.data(), len, 0) == 0;
        }
    }
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

// Code zum Zeitpunkt unixTime (Sekunden); leer bei Fehler
inline std::wstring TotpCode(const TotpParams& p, long long unixTime)
{
    if (p.key.empty() || p.period <= 0) return L"";
    const unsigned long long counter = static_cast<unsigned long long>(unixTime < 0 ? 0 : unixTime) / static_cast<unsigned long long>(p.period);
    unsigned char msg[8];
    for (int i = 0; i < 8; ++i) msg[i] = static_cast<unsigned char>(counter >> (8 * (7 - i)));
    Bytes mac;
    if (!HmacDigest(p.algo, p.key, msg, 8, mac) || mac.size() < 20) return L"";
    const size_t off = mac.back() & 0x0F;
    const unsigned long bin = (static_cast<unsigned long>(mac[off] & 0x7F) << 24) | (static_cast<unsigned long>(mac[off + 1]) << 16) |
                              (static_cast<unsigned long>(mac[off + 2]) << 8) | static_cast<unsigned long>(mac[off + 3]);
    unsigned long mod = 1;
    for (int i = 0; i < p.digits; ++i) mod *= 10;
    std::wstring code = std::to_wstring(bin % mod);
    while (static_cast<int>(code.size()) < p.digits) code.insert(code.begin(), L'0');
    return code;
}

// Sekunden, bis der aktuelle Code ablaeuft (1..period)
inline int TotpSecondsLeft(const TotpParams& p, long long unixTime)
{
    const long long per = p.period > 0 ? p.period : 30;
    return static_cast<int>(per - (unixTime < 0 ? 0 : unixTime) % per);
}

// "123456" -> "123 456", "12345678" -> "1234 5678"
inline std::wstring FormatTotp(const std::wstring& code)
{
    if (code.size() < 6) return code;
    const size_t half = code.size() / 2;
    return code.substr(0, half) + L" " + code.substr(half);
}

}  // namespace vault
