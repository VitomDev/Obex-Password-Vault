#include "core/hello.h"

#include <windows.h>
#include <dpapi.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Security.Credentials.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.Streams.h>

#include <ctime>
#include <fstream>

namespace obex {

namespace {

using namespace winrt::Windows::Security::Credentials;
using winrt::Windows::Security::Cryptography::CryptographicBuffer;

constexpr wchar_t kCredentialName[] = L"Obex Password Vault";

Key KeyFromSignature(std::span<const uint8_t> signature) {
    const Bytes digest = Hash(HashAlgorithm::Sha256, signature);
    Key key{};
    std::copy(digest.begin(), digest.end(), key.begin());
    return key;
}

}  // namespace

bool HelloAvailable() {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
    }
    try {
        return KeyCredentialManager::IsSupportedAsync().get();
    } catch (...) {
        return false;
    }
}

std::optional<Bytes> HelloSign(std::span<const uint8_t> challenge, bool create) {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
    }
    try {
        const KeyCredentialRetrievalResult result =
            create ? KeyCredentialManager::RequestCreateAsync(kCredentialName, KeyCredentialCreationOption::ReplaceExisting).get()
                   : KeyCredentialManager::OpenAsync(kCredentialName).get();
        if (result.Status() != KeyCredentialStatus::Success) return std::nullopt;
        const auto buffer = CryptographicBuffer::CreateFromByteArray(winrt::array_view<const uint8_t>(challenge.data(),
                                                                     static_cast<uint32_t>(challenge.size())));
        const KeyCredentialOperationResult signedResult = result.Credential().RequestSignAsync(buffer).get();
        if (signedResult.Status() != KeyCredentialStatus::Success) return std::nullopt;
        winrt::com_array<uint8_t> signature;
        CryptographicBuffer::CopyToByteArray(signedResult.Result(), signature);
        return Bytes(signature.begin(), signature.end());
    } catch (...) {
        return std::nullopt;
    }
}

void HelloRemove() {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
    }
    try {
        KeyCredentialManager::DeleteAsync(kCredentialName).get();
    } catch (...) {
    }
}

bool HelloState::IsUsable(int days) const {
    const int64_t age = UnixNow() - masterTime;
    return logonId == CurrentLogonId() && age >= 0 && age < int64_t(days) * 86400;
}

std::optional<HelloState> LoadHelloState(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    Bytes data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    DATA_BLOB in{static_cast<DWORD>(data.size()), data.data()};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return std::nullopt;
    std::optional<HelloState> state;
    if (out.cbData == sizeof(HelloState)) {
        state.emplace();
        std::memcpy(&*state, out.pbData, sizeof(HelloState));
    }
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return state;
}

bool SaveHelloState(const std::filesystem::path& path, const HelloState& state) {
    DATA_BLOB in{sizeof(HelloState), reinterpret_cast<BYTE*>(const_cast<HelloState*>(&state))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"Obex", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return static_cast<bool>(file);
}

HelloState MakeHelloState(std::span<const uint8_t> challenge, std::span<const uint8_t> signature, const Key& vaultKey) {
    HelloState state;
    std::copy(challenge.begin(), challenge.end(), state.challenge.begin());
    RandomBytes(state.nonce);
    Key key = KeyFromSignature(signature);
    const Bytes sealed = Encrypt(key, state.nonce, {}, vaultKey);
    Wipe(key.data(), key.size());
    std::copy(sealed.begin(), sealed.end(), state.sealed.begin());
    state.masterTime = UnixNow();
    state.logonId = CurrentLogonId();
    return state;
}

std::optional<Key> UnwrapHelloKey(const HelloState& state, std::span<const uint8_t> signature) {
    Key key = KeyFromSignature(signature);
    auto plain = Decrypt(key, state.nonce, {}, state.sealed);
    Wipe(key.data(), key.size());
    if (!plain || plain->size() != sizeof(Key)) return std::nullopt;
    Key vaultKey{};
    std::copy(plain->begin(), plain->end(), vaultKey.begin());
    Wipe(*plain);
    return vaultKey;
}

uint64_t CurrentLogonId() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return 0;
    TOKEN_STATISTICS stats{};
    DWORD size = 0;
    const bool ok = GetTokenInformation(token, TokenStatistics, &stats, sizeof(stats), &size);
    CloseHandle(token);
    return ok ? (uint64_t(uint32_t(stats.AuthenticationId.HighPart)) << 32) | stats.AuthenticationId.LowPart : 0;
}

int64_t UnixNow() { return static_cast<int64_t>(std::time(nullptr)); }

}  // namespace obex
