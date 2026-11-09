#include "core/crypto.h"

#include "core/text.h"

#include <windows.h>
#include <bcrypt.h>

#include <stdexcept>

namespace obex {

namespace {

void Check(NTSTATUS status) {
    if (!BCRYPT_SUCCESS(status)) throw std::runtime_error("CNG call failed");
}

const wchar_t* AlgorithmName(HashAlgorithm algorithm) {
    switch (algorithm) {
        case HashAlgorithm::Md5: return BCRYPT_MD5_ALGORITHM;
        case HashAlgorithm::Sha1: return BCRYPT_SHA1_ALGORITHM;
        case HashAlgorithm::Sha256: return BCRYPT_SHA256_ALGORITHM;
        case HashAlgorithm::Sha512: return BCRYPT_SHA512_ALGORITHM;
    }
    return BCRYPT_SHA256_ALGORITHM;
}

// One AES-GCM key handle per call: these run a few times per unlock or save.
BCRYPT_KEY_HANDLE OpenAesKey(BCRYPT_ALG_HANDLE& algorithm, const Key& key) {
    Check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0));
    Check(BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                            sizeof(BCRYPT_CHAIN_MODE_GCM), 0));
    BCRYPT_KEY_HANDLE handle = nullptr;
    Check(BCryptGenerateSymmetricKey(algorithm, &handle, nullptr, 0, const_cast<PUCHAR>(key.data()),
                                     static_cast<ULONG>(key.size()), 0));
    return handle;
}

}  // namespace

void RandomBytes(std::span<uint8_t> out) {
    Check(BCryptGenRandom(nullptr, out.data(), static_cast<ULONG>(out.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

Bytes RandomBytes(size_t count) {
    Bytes bytes(count);
    RandomBytes(bytes);
    return bytes;
}

uint32_t RandomBelow(uint32_t bound) {
    // Rejection sampling keeps every value equally likely.
    const uint32_t limit = UINT32_MAX - UINT32_MAX % bound;
    uint32_t value;
    do {
        RandomBytes({reinterpret_cast<uint8_t*>(&value), sizeof(value)});
    } while (value >= limit);
    return value % bound;
}

Key DeriveKey(std::wstring_view password, std::span<const uint8_t> salt, uint32_t iterations) {
    std::string utf8 = ToUtf8(password);
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    Check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG));
    Key key{};
    const NTSTATUS status = BCryptDeriveKeyPBKDF2(algorithm, reinterpret_cast<PUCHAR>(utf8.data()), static_cast<ULONG>(utf8.size()),
                                                  const_cast<PUCHAR>(salt.data()), static_cast<ULONG>(salt.size()), iterations,
                                                  key.data(), static_cast<ULONG>(key.size()), 0);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    Wipe(utf8);
    Check(status);
    return key;
}

Bytes Encrypt(const Key& key, std::span<const uint8_t> nonce, std::span<const uint8_t> aad, std::span<const uint8_t> plain) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE handle = OpenAesKey(algorithm, key);
    Bytes out(plain.size() + kTagSize);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = const_cast<PUCHAR>(nonce.data());
    info.cbNonce = static_cast<ULONG>(nonce.size());
    info.pbAuthData = const_cast<PUCHAR>(aad.data());
    info.cbAuthData = static_cast<ULONG>(aad.size());
    info.pbTag = out.data() + plain.size();
    info.cbTag = kTagSize;
    ULONG written = 0;
    const NTSTATUS status = BCryptEncrypt(handle, const_cast<PUCHAR>(plain.data()), static_cast<ULONG>(plain.size()), &info,
                                          nullptr, 0, out.data(), static_cast<ULONG>(plain.size()), &written, 0);
    BCryptDestroyKey(handle);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    Check(status);
    return out;
}

std::optional<Bytes> Decrypt(const Key& key, std::span<const uint8_t> nonce, std::span<const uint8_t> aad,
                             std::span<const uint8_t> sealed) {
    if (sealed.size() < kTagSize) return std::nullopt;
    const size_t size = sealed.size() - kTagSize;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE handle = OpenAesKey(algorithm, key);
    Bytes out(size);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = const_cast<PUCHAR>(nonce.data());
    info.cbNonce = static_cast<ULONG>(nonce.size());
    info.pbAuthData = const_cast<PUCHAR>(aad.data());
    info.cbAuthData = static_cast<ULONG>(aad.size());
    info.pbTag = const_cast<PUCHAR>(sealed.data() + size);
    info.cbTag = kTagSize;
    ULONG written = 0;
    const NTSTATUS status = BCryptDecrypt(handle, const_cast<PUCHAR>(sealed.data()), static_cast<ULONG>(size), &info, nullptr, 0,
                                          out.data(), static_cast<ULONG>(size), &written, 0);
    BCryptDestroyKey(handle);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!BCRYPT_SUCCESS(status)) {
        Wipe(out);
        return std::nullopt;
    }
    return out;
}

Hasher::Hasher(HashAlgorithm algorithm) {
    BCRYPT_ALG_HANDLE handle = nullptr;
    Check(BCryptOpenAlgorithmProvider(&handle, AlgorithmName(algorithm), nullptr, 0));
    algorithm_ = handle;
    DWORD size = 0;
    ULONG written = 0;
    Check(BCryptGetProperty(handle, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof(size), &written, 0));
    size_ = size;
    BCRYPT_HASH_HANDLE hash = nullptr;
    Check(BCryptCreateHash(handle, &hash, nullptr, 0, nullptr, 0, 0));
    hash_ = hash;
}

Hasher::~Hasher() {
    if (hash_) BCryptDestroyHash(hash_);
    if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
}

void Hasher::Update(std::span<const uint8_t> data) {
    Check(BCryptHashData(hash_, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), 0));
}

Bytes Hasher::Finish() {
    Bytes digest(size_);
    Check(BCryptFinishHash(hash_, digest.data(), static_cast<ULONG>(digest.size()), 0));
    return digest;
}

Bytes Hash(HashAlgorithm algorithm, std::span<const uint8_t> data) {
    Hasher hasher(algorithm);
    hasher.Update(data);
    return hasher.Finish();
}

std::wstring ToHex(std::span<const uint8_t> data) {
    static constexpr wchar_t kDigits[] = L"0123456789abcdef";
    std::wstring hex;
    hex.reserve(data.size() * 2);
    for (uint8_t byte : data) {
        hex += kDigits[byte >> 4];
        hex += kDigits[byte & 15];
    }
    return hex;
}

void Wipe(void* data, size_t size) { SecureZeroMemory(data, size); }

}  // namespace obex
