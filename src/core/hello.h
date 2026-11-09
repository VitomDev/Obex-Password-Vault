#pragma once

#include "core/crypto.h"

#include <filesystem>
#include <optional>

namespace obex {

// Windows Hello (PIN, fingerprint or face) through a TPM-backed key of
// KeyCredentialManager. Hello signs a fixed random challenge; that signature
// is always the same, so its hash can wrap the vault key without the master
// password ever being stored. These calls block: run them off the UI thread.
bool HelloAvailable();
// `create` makes a new Hello key first (one prompt), replacing an old one.
std::optional<Bytes> HelloSign(std::span<const uint8_t> challenge, bool create);
void HelloRemove();

// What unlocking with Hello needs, kept in a file encrypted for the current
// Windows user (DPAPI). Hello only works in the same Windows sign-in session
// as the last master password, and for a limited number of days.
struct HelloState {
    std::array<uint8_t, 32> challenge{};
    std::array<uint8_t, kNonceSize> nonce{};
    std::array<uint8_t, 48> sealed{};  // The vault key, wrapped
    int64_t masterTime = 0;            // Unix seconds of the last master password
    uint64_t logonId = 0;

    bool IsUsable(int days) const;
};

std::optional<HelloState> LoadHelloState(const std::filesystem::path& path);
bool SaveHelloState(const std::filesystem::path& path, const HelloState& state);

// Wraps and unwraps the vault key with the key derived from a Hello signature.
HelloState MakeHelloState(std::span<const uint8_t> challenge, std::span<const uint8_t> signature, const Key& vaultKey);
std::optional<Key> UnwrapHelloKey(const HelloState& state, std::span<const uint8_t> signature);

// Changes with every Windows sign-in (including restarts and fast startup).
uint64_t CurrentLogonId();
int64_t UnixNow();

}  // namespace obex
