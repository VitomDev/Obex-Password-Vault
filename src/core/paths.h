#pragma once

#include <filesystem>

namespace obex {

std::filesystem::path ExecutablePath();

// %LOCALAPPDATA%\Obex Password Vault: the vault, its daily backups and the
// Windows Hello data. Settings live in the registry.
std::filesystem::path DataDirectory();
std::filesystem::path VaultPath();
std::filesystem::path BackupDirectory();
std::filesystem::path HelloPath();

std::filesystem::path DocumentsDirectory();

}  // namespace obex
