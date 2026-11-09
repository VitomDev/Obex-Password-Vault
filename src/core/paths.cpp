#include "core/paths.h"

#include <windows.h>
#include <shlobj.h>

namespace obex {

namespace {

std::filesystem::path KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR folder = nullptr;
    std::filesystem::path path;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &folder))) path = folder;
    CoTaskMemFree(folder);
    return path;
}

}  // namespace

std::filesystem::path ExecutablePath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::filesystem::path DataDirectory() {
    // Tests point Obex at a scratch folder.
    wchar_t custom[MAX_PATH];
    if (GetEnvironmentVariableW(L"OBEX_DATA_DIR", custom, MAX_PATH)) return custom;
    const std::filesystem::path localAppData = KnownFolder(FOLDERID_LocalAppData);
    return localAppData.empty() ? localAppData : localAppData / L"Obex Password Vault";
}

std::filesystem::path VaultPath() { return DataDirectory() / L"vault.obex"; }

std::filesystem::path BackupDirectory() { return DataDirectory() / L"Backups"; }

std::filesystem::path HelloPath() { return DataDirectory() / L"hello.dat"; }

std::filesystem::path DocumentsDirectory() { return KnownFolder(FOLDERID_Documents); }

}  // namespace obex
