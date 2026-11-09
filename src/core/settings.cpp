#include "core/settings.h"

#include <algorithm>

namespace obex {

namespace {

bool ReadDword(HKEY key, const wchar_t* name, DWORD& value) {
    DWORD size = sizeof(value);
    return RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS;
}

bool WriteDword(HKEY key, const wchar_t* name, DWORD value) {
    return RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
}

}  // namespace

const wchar_t* SettingsKey() {
    static const std::wstring key = [] {
        wchar_t custom[256];
        return GetEnvironmentVariableW(L"OBEX_SETTINGS_KEY", custom, 256) ? std::wstring(custom) : std::wstring(kSettingsKey);
    }();
    return key.c_str();
}

Settings Settings::Load(const wchar_t* keyPath) {
    Settings settings;
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_READ, &key) != ERROR_SUCCESS) return settings;

    DWORD value = 0;
    if (ReadDword(key, L"OpenHotkey", value) && Unpack(value).IsValid()) settings.openHotkey = Unpack(value);
    if (ReadDword(key, L"AutoLockMinutes", value) && value <= 24 * 60) settings.autoLockMinutes = static_cast<int>(value);
    if (ReadDword(key, L"LockWithWindows", value)) settings.lockWithWindows = value != 0;
    if (ReadDword(key, L"Hello", value)) settings.hello = value != 0;
    if (ReadDword(key, L"MasterEveryDays", value) && value >= 1 && value <= 90) settings.masterEveryDays = static_cast<int>(value);
    GeneratorOptions& g = settings.generator;
    if (ReadDword(key, L"GeneratorLength", value)) {
        g.length = std::clamp(static_cast<int>(value), GeneratorOptions::kMinLength, GeneratorOptions::kMaxLength);
    }
    if (ReadDword(key, L"GeneratorSets", value) && (value & 15)) {
        g.upper = value & 1;
        g.lower = value & 2;
        g.digits = value & 4;
        g.symbols = value & 8;
        g.excludeAmbiguous = value & 16;
    }
    if (ReadDword(key, L"GeneratorCount", value) && value >= 1 && value <= 50) settings.generatorCount = static_cast<int>(value);

    RegCloseKey(key);
    return settings;
}

bool Settings::Save(const wchar_t* keyPath) const {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const GeneratorOptions& g = generator;
    const DWORD sets = (g.upper ? 1 : 0) | (g.lower ? 2 : 0) | (g.digits ? 4 : 0) | (g.symbols ? 8 : 0) | (g.excludeAmbiguous ? 16 : 0);
    const bool saved = WriteDword(key, L"OpenHotkey", Pack(openHotkey)) &&
                       WriteDword(key, L"AutoLockMinutes", static_cast<DWORD>(autoLockMinutes)) &&
                       WriteDword(key, L"LockWithWindows", lockWithWindows ? 1 : 0) && WriteDword(key, L"Hello", hello ? 1 : 0) &&
                       WriteDword(key, L"MasterEveryDays", static_cast<DWORD>(masterEveryDays)) &&
                       WriteDword(key, L"GeneratorLength", static_cast<DWORD>(g.length)) && WriteDword(key, L"GeneratorSets", sets) &&
                       WriteDword(key, L"GeneratorCount", static_cast<DWORD>(generatorCount));
    RegCloseKey(key);
    return saved;
}

std::wstring AutostartCommand(const std::wstring& exePath) { return L"\"" + exePath + L"\" --background"; }

bool IsAutostartEnabled(const wchar_t* valueName) {
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, valueName, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    // Task Manager's "Disable" keeps the Run value and flags it here instead:
    // an odd first byte means disabled.
    BYTE state[12]{};
    DWORD size = sizeof(state);
    if (RegGetValueW(HKEY_CURRENT_USER, kStartupApprovedKey, valueName, RRF_RT_REG_BINARY, nullptr, state, &size) ==
            ERROR_SUCCESS &&
        size > 0) {
        return (state[0] & 1) == 0;
    }
    return true;
}

bool SetAutostartEnabled(bool enabled, const std::wstring& exePath, const wchar_t* valueName) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    LSTATUS status;
    if (enabled) {
        // Setting one named value can never create a duplicate entry.
        const std::wstring command = AutostartCommand(exePath);
        status = RegSetValueExW(key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, valueName);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    // Drop any Task Manager override so the Run value alone decides.
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kStartupApprovedKey, valueName);
    return status == ERROR_SUCCESS;
}

}  // namespace obex
