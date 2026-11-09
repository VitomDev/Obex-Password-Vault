#pragma once

#include "core/generator.h"
#include "core/hotkey.h"

#include <windows.h>

#include <string>

namespace obex {

// Registry locations. The installer and the tests rely on these names.
inline constexpr wchar_t kSettingsKey[] = L"Software\\Obex Password Vault";
inline constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t kRunValue[] = L"Obex Password Vault";
inline constexpr wchar_t kStartupApprovedKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

// The settings key, or the one in OBEX_SETTINGS_KEY when testing.
const wchar_t* SettingsKey();

struct Settings {
    Hotkey openHotkey{MOD_CONTROL | MOD_ALT, 'O'};
    int autoLockMinutes = 15;  // 0: never
    bool lockWithWindows = true;
    bool hello = true;
    int masterEveryDays = 7;
    GeneratorOptions generator;
    int generatorCount = 5;

    // `keyPath` is relative to HKEY_CURRENT_USER; tests use a scratch key.
    static Settings Load(const wchar_t* keyPath = SettingsKey());
    bool Save(const wchar_t* keyPath = SettingsKey()) const;
};

// "Start with Windows" is the presence of Obex's value in the per-user Run
// key, so Settings always shows the real state.
bool IsAutostartEnabled(const wchar_t* valueName = kRunValue);
bool SetAutostartEnabled(bool enabled, const std::wstring& exePath, const wchar_t* valueName = kRunValue);

std::wstring AutostartCommand(const std::wstring& exePath);

}  // namespace obex
