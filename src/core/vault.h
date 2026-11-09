#pragma once

#include "core/crypto.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace obex {

struct Entry {
    std::wstring name;
    std::wstring url;
    std::wstring username;
    std::wstring password;
    std::wstring notes;
    int64_t created = 0;   // Unix seconds
    int64_t modified = 0;

    void Wipe();
};

// A random 24-character code in groups of four: K7QM-2XRP-9WFD-...
std::wstring NewRecoveryCode();
// Uppercase, without spaces or dashes. The alphabet has no 0, 1, I, L or O.
std::wstring NormalizeRecoveryCode(std::wstring_view code);
inline constexpr int kRecoveryGroups = 6;

// The encrypted vault file. A random vault key encrypts the entries and is
// stored twice, wrapped by a key derived from the master password and by one
// derived from the recovery code.
//
// File layout (little endian):
//   "OBEX" u16 version u16 0
//   master wrap:   u32 iterations, salt[16], nonce[12], key+tag[48]
//   recovery wrap: same
//   data: nonce[12], u32 size, entries+tag[size]
// Everything before the data nonce is authenticated with the data.
class Vault {
public:
    enum class Result { Ok, WrongSecret, Damaged, Missing };

    Vault() = default;
    ~Vault() { Close(); }
    Vault(const Vault&) = delete;
    Vault& operator=(const Vault&) = delete;

    // A new, empty vault in memory; Save() writes it.
    void Create(const std::filesystem::path& path, std::wstring_view master, std::wstring_view recoveryCode);
    Result Open(const std::filesystem::path& path, std::wstring_view master);
    Result OpenWithRecoveryCode(const std::filesystem::path& path, std::wstring_view code);
    Result OpenWithKey(const std::filesystem::path& path, const Key& key);
    void Close();
    bool IsOpen() const { return open_; }

    bool CheckMaster(std::wstring_view master) const;
    void SetMaster(std::wstring_view master);
    void SetRecoveryCode(std::wstring_view code);

    // Writes atomically; keeps one backup per day of the previous file, up to
    // `backups` days, in `backupDir`.
    bool Save(const std::filesystem::path& backupDir = {}, int backups = 5);

    const Key& key() const { return key_; }
    std::vector<Entry> entries;

    static constexpr uint32_t kIterations = 600000;

private:
    struct Wrap {
        uint32_t iterations = kIterations;
        std::array<uint8_t, 16> salt{};
        std::array<uint8_t, kNonceSize> nonce{};
        std::array<uint8_t, 48> sealed{};
    };
    Wrap MakeWrap(std::wstring_view secret) const;
    Result Load(const std::filesystem::path& path, std::wstring_view secret, bool recovery, const Key* key);
    Bytes Header() const;

    std::filesystem::path path_;
    bool open_ = false;
    Key key_{};
    Wrap master_;
    Wrap recovery_;
};

// Serialization of the entries, exposed for the tests.
Bytes SerializeEntries(const std::vector<Entry>& entries);
bool ParseEntries(std::span<const uint8_t> data, std::vector<Entry>& entries);

}  // namespace obex
