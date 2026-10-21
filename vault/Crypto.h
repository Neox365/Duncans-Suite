#pragma once
// Kryptografie fuer Duncan's Vault ueber die Windows-eigene CNG-Bibliothek (bcrypt.dll), ohne Fremdcode:
//   - Schluessel aus dem Master-Passwort: PBKDF2-HMAC-SHA256 (BCryptDeriveKeyPBKDF2), zufaelliges Salz, viele Iterationen
//   - Verschluesselung: AES-256 im Galois/Counter-Modus (AES-GCM, authentifiziert: Manipulationen und falsche Passwoerter
//     fallen sofort auf), pro Speichern ein frischer 96-Bit-Nonce
//   - Zufall: BCryptGenRandom (Systemzufallsgenerator)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

namespace vault {

using Bytes = std::vector<unsigned char>;

inline void Wipe(void* p, size_t n)
{
    if (p && n) SecureZeroMemory(p, n);
}
inline void Wipe(Bytes& b)
{
    Wipe(b.data(), b.size());
    b.clear();
}
inline void Wipe(std::string& s)
{
    if (!s.empty()) Wipe(&s[0], s.size());
    s.clear();
}
inline void Wipe(std::wstring& s)
{
    if (!s.empty()) Wipe(&s[0], s.size() * sizeof(wchar_t));
    s.clear();
}

inline bool RandomBytes(unsigned char* p, size_t n)
{
    return BCryptGenRandom(nullptr, p, static_cast<ULONG>(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

inline std::string ToUtf8(const std::wstring& s)
{
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n, nullptr, nullptr);
    return out;
}

inline std::wstring FromUtf8(const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n);
    return out;
}

// 32-Byte-Schluessel aus Passwort und Salz
inline bool DeriveKey(const std::wstring& password, const Bytes& salt, unsigned iterations, Bytes& key)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0) return false;
    std::string pw = ToUtf8(password);
    key.assign(32, 0);
    const NTSTATUS st = BCryptDeriveKeyPBKDF2(alg, reinterpret_cast<PUCHAR>(&pw[0]), static_cast<ULONG>(pw.size()), const_cast<PUCHAR>(salt.data()),
                                              static_cast<ULONG>(salt.size()), iterations, key.data(), 32, 0);
    Wipe(pw);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (st != 0) { Wipe(key); return false; }
    return true;
}

// AES-256-GCM. Verschluesseln: out = Chiffretext, tag (16 Byte) wird geschrieben. Entschluesseln: tag wird geprueft;
// false bei falschem Schluessel oder veraenderten Daten.
inline bool GcmCrypt(bool encrypt, const Bytes& key, const Bytes& nonce, const Bytes& aad, const unsigned char* in, size_t inLen, Bytes& out,
                     unsigned char tag[16])
{
    if (key.size() != 32 || nonce.size() != 12 || inLen == 0) return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE hk = nullptr;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) == 0 &&
        BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)), sizeof(BCRYPT_CHAIN_MODE_GCM), 0) == 0 &&
        BCryptGenerateSymmetricKey(alg, &hk, nullptr, 0, const_cast<PUCHAR>(key.data()), 32, 0) == 0) {
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
        BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce = const_cast<PUCHAR>(nonce.data());
        info.cbNonce = 12;
        info.pbAuthData = aad.empty() ? nullptr : const_cast<PUCHAR>(aad.data());
        info.cbAuthData = static_cast<ULONG>(aad.size());
        info.pbTag = tag;
        info.cbTag = 16;
        out.assign(inLen, 0);
        ULONG got = 0;
        const NTSTATUS st = encrypt ? BCryptEncrypt(hk, const_cast<PUCHAR>(in), static_cast<ULONG>(inLen), &info, nullptr, 0, out.data(), static_cast<ULONG>(inLen), &got, 0)
                                    : BCryptDecrypt(hk, const_cast<PUCHAR>(in), static_cast<ULONG>(inLen), &info, nullptr, 0, out.data(), static_cast<ULONG>(inLen), &got, 0);
        ok = st == 0 && got == inLen;
        if (!ok) Wipe(out);
    }
    if (hk) BCryptDestroyKey(hk);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

// Schluessel an das Windows-Benutzerkonto binden (DPAPI): nur dieser Benutzer auf diesem PC kann ihn wieder entschluesseln.
// Dient dem Tresor "ohne Master-Passwort".
inline bool DpapiProtect(const Bytes& in, Bytes& out)
{
    static const BYTE kEntropy[] = "DuncansVault-DPAPI-1";
    DATA_BLOB src{static_cast<DWORD>(in.size()), const_cast<BYTE*>(in.data())}, ent{static_cast<DWORD>(sizeof(kEntropy) - 1), const_cast<BYTE*>(kEntropy)}, dst{};
    if (!CryptProtectData(&src, L"Duncan's Vault", &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &dst)) return false;
    out.assign(dst.pbData, dst.pbData + dst.cbData);
    SecureZeroMemory(dst.pbData, dst.cbData);
    LocalFree(dst.pbData);
    return true;
}

inline bool DpapiUnprotect(const Bytes& in, Bytes& out)
{
    static const BYTE kEntropy[] = "DuncansVault-DPAPI-1";
    DATA_BLOB src{static_cast<DWORD>(in.size()), const_cast<BYTE*>(in.data())}, ent{static_cast<DWORD>(sizeof(kEntropy) - 1), const_cast<BYTE*>(kEntropy)}, dst{};
    if (!CryptUnprotectData(&src, nullptr, &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &dst)) return false;
    out.assign(dst.pbData, dst.pbData + dst.cbData);
    SecureZeroMemory(dst.pbData, dst.cbData);
    LocalFree(dst.pbData);
    return true;
}

}  // namespace vault
