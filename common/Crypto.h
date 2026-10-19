#pragma once
// Gemeinsame Hash- und Signaturfunktionen (Windows CNG): SHA-256 als Hex und ECDSA-P256-Signaturpruefung.
// Signaturen sind 64 Byte (r||s) ueber den SHA-256-Hash der Datei und werden mit tools\SignRelease.ps1 erzeugt.
// Benoetigt bcrypt.lib.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>

#include <string>

namespace crypto {

inline std::string ToHex(const unsigned char* bytes, size_t n)
{
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i)
    {
        s += digits[bytes[i] >> 4];
        s += digits[bytes[i] & 15];
    }
    return s;
}

// SHA-256 ueber einen Speicherblock; false bei Fehler
inline bool Sha256(const void* data, size_t size, unsigned char (&digest)[32])
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    const bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
                    BCryptHash(alg, nullptr, 0, static_cast<PUCHAR>(const_cast<void*>(data)), static_cast<ULONG>(size), digest, sizeof(digest)) == 0;
    if (alg)
        BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

// Prueft die 64-Byte-Signatur ueber einen fertigen SHA-256-Hash gegen den oeffentlichen Schluessel (ECCPUBLIC_BLOB)
inline bool VerifyDigest(const unsigned char (&digest)[32], const void* sig, size_t sigLen, const unsigned char* publicKey, size_t keyLen)
{
    if (sigLen != 64)
        return false;
    BCRYPT_ALG_HANDLE ecc = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    const bool ok = BCryptOpenAlgorithmProvider(&ecc, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) == 0 &&
                    BCryptImportKeyPair(ecc, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key, const_cast<PUCHAR>(publicKey), static_cast<ULONG>(keyLen), 0) == 0 &&
                    BCryptVerifySignature(key, nullptr, const_cast<PUCHAR>(digest), sizeof(digest),
                                          static_cast<PUCHAR>(const_cast<void*>(sig)), static_cast<ULONG>(sigLen), 0) == 0;
    if (key)
        BCryptDestroyKey(key);
    if (ecc)
        BCryptCloseAlgorithmProvider(ecc, 0);
    return ok;
}

// Signatur ueber die Daten selbst (hasht intern)
inline bool VerifyData(const std::string& data, const std::string& sig, const unsigned char* publicKey, size_t keyLen)
{
    unsigned char digest[32] = {};
    return !data.empty() && Sha256(data.data(), data.size(), digest) && VerifyDigest(digest, sig.data(), sig.size(), publicKey, keyLen);
}

}  // namespace crypto
