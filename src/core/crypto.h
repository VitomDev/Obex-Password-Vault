#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace obex {

using Bytes = std::vector<uint8_t>;
using Key = std::array<uint8_t, 32>;

inline constexpr size_t kNonceSize = 12;
inline constexpr size_t kTagSize = 16;

// Everything here uses Windows CNG (bcrypt.dll); no other crypto code.
void RandomBytes(std::span<uint8_t> out);
Bytes RandomBytes(size_t count);
// Uniform in [0, bound).
uint32_t RandomBelow(uint32_t bound);

// PBKDF2-HMAC-SHA256 of the UTF-8 password.
Key DeriveKey(std::wstring_view password, std::span<const uint8_t> salt, uint32_t iterations);

// AES-256-GCM. Output and input are ciphertext followed by the 16-byte tag.
Bytes Encrypt(const Key& key, std::span<const uint8_t> nonce, std::span<const uint8_t> aad, std::span<const uint8_t> plain);
std::optional<Bytes> Decrypt(const Key& key, std::span<const uint8_t> nonce, std::span<const uint8_t> aad,
                             std::span<const uint8_t> sealed);

enum class HashAlgorithm { Md5, Sha1, Sha256, Sha512 };

// Incremental hash, for files that do not fit in memory.
class Hasher {
public:
    explicit Hasher(HashAlgorithm algorithm);
    ~Hasher();
    Hasher(const Hasher&) = delete;
    Hasher& operator=(const Hasher&) = delete;

    void Update(std::span<const uint8_t> data);
    Bytes Finish();

private:
    void* algorithm_ = nullptr;
    void* hash_ = nullptr;
    size_t size_ = 0;
};

Bytes Hash(HashAlgorithm algorithm, std::span<const uint8_t> data);
std::wstring ToHex(std::span<const uint8_t> data);

// Overwrites memory the compiler cannot optimize away.
void Wipe(void* data, size_t size);
template <typename T>
void Wipe(T& container) {
    if (!container.empty()) Wipe(container.data(), container.size() * sizeof(container[0]));
}

}  // namespace obex
