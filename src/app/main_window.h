#pragma once

#include "app/generator_panel.h"
#include "app/hash_panel.h"
#include "app/panel.h"
#include "app/password_panel.h"
#include "app/quick_panel.h"
#include "app/recovery_panel.h"
#include "app/settings_panel.h"
#include "app/vault_panel.h"
#include "core/hello.h"
#include "core/settings.h"
#include "core/vault.h"

#include <windows.h>

#include <functional>
#include <memory>
#include <optional>
#include <thread>

namespace obex {

class HeaderPanel;

// The Obex window: Vault, Generator and Hash tabs and Settings. It owns the
// open vault and decides when it locks.
class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    // Registers the global shortcut again; returns an error to show, or nothing.
    std::function<std::wstring()> onHotkeyChanged;
    std::function<void(bool paused)> onPauseHotkey;
    std::function<void()> onFirstHide;

    bool Create(HINSTANCE instance, Settings* settings);
    void Destroy();
    // Brings the window up; with `search`, ready to type in the vault search.
    void Show(bool search);
    // The small quick password window of the global shortcut.
    void ShowQuick();
    void Lock();
    bool IsUnlocked() const { return vault_.IsOpen(); }
    bool PreTranslate(MSG& message);
    HWND window() const { return window_; }

private:
    enum class Screen { Password, Recovery, Vault, Generator, Hash, Settings };
    enum class Pending { None, Create, Recover, NewCode };
    enum class Verify { None, Export, NewCode, EnableHello };

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK QuickProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void HideQuick();
    void SaveQuickEntry(Entry entry);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void ConnectPanels();
    void Layout();
    void RefreshStyle();
    void ShowScreen(Screen screen);
    void ShowVaultTab();
    void UpdateHeader();
    int Scale(int value) const { return MulDiv(value, GetDpiForWindow(window_), 96); }

    void Submit(PasswordPanel::Mode mode, const std::wstring& code, const std::wstring& password);
    void AfterUnlock();
    void MasterEntered();
    void RecoveryDone();
    void StartHelloUnlock();
    void EnrollHello();
    bool HelloUsable() const;
    void SetHelloSetting(bool enabled);
    void RunAction(SettingsPanel::Action action);
    void VerifyMaster(Verify purpose, const wchar_t* heading, const wchar_t* message);
    void Import();
    void Export();
    void Backup();
    bool SaveVault();
    void Copy(const std::wstring& text, bool secret);
    void ClearClipboard();
    void SaveSettings();
    void RunTask(std::function<std::function<void()>()> work);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    Settings* settings_ = nullptr;
    Style style_;
    Screen screen_ = Screen::Password;
    Pending pending_ = Pending::None;
    Verify verify_ = Verify::None;
    std::wstring pendingCode_;
    bool shownOnce_ = false;
    bool hiddenOnce_ = false;

    Vault vault_;
    std::optional<HelloState> helloState_;
    bool helloSupported_ = false;
    bool helloChecked_ = false;
    bool helloPrompted_ = false;

    std::unique_ptr<HeaderPanel> header_;
    PasswordPanel password_;
    RecoveryPanel recovery_;
    VaultPanel vaultPanel_;
    GeneratorPanel generator_;
    HashPanel hash_;
    SettingsPanel settingsPanel_;
    HWND quickWindow_ = nullptr;
    QuickPanel quick_;
    std::optional<Entry> pendingEntry_;

    DWORD clipboardSequence_ = 0;
    ULONGLONG lastActivity_ = 0;
    std::thread task_;
    std::function<void()> taskDone_;
};

}  // namespace obex
