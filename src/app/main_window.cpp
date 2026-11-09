#include "app/main_window.h"

#include "app/resource.h"
#include "core/clipboard.h"
#include "core/csv.h"
#include "core/paths.h"
#include "core/text.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <ctime>
#include <fstream>
#include <set>
#include <tuple>

namespace obex {

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kClassName[] = L"ObexPasswordVault";
constexpr wchar_t kQuickClassName[] = L"ObexQuickPassword";
constexpr int kQuickWidth = 400;
constexpr UINT WM_APP_TASK_DONE = WM_APP + 30;
constexpr UINT_PTR kClipboardTimer = 1;
constexpr UINT_PTR kAutoLockTimer = 2;
constexpr UINT_PTR kHelloFocusTimer = 3;
constexpr int kHeaderHeight = 64;
constexpr int kClipboardSeconds = 30;

enum HeaderId { kTabVault = 100, kTabGenerator, kTabHash, kLockButton, kSettingsButton };

std::wstring DateStamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    wchar_t stamp[16];
    wcsftime(stamp, 16, L"%Y-%m-%d", &local);
    return stamp;
}

std::optional<std::wstring> SaveDialog(HWND owner, const wchar_t* name, const wchar_t* filter, const wchar_t* extension) {
    wchar_t file[MAX_PATH];
    wcsncpy_s(file, name, _TRUNCATE);
    const std::wstring folder = DocumentsDirectory().wstring();
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrInitialDir = folder.empty() ? nullptr : folder.c_str();
    dialog.lpstrDefExt = extension;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) return std::nullopt;
    return std::wstring(file);
}

}  // namespace

// Tabs on the left, Lock and Settings on the right.
class HeaderPanel : public Panel {
public:
    std::function<void(int id)> onClick;

    void SetState(int selectedTab, bool unlocked) {
        for (int id : {kTabVault, kTabGenerator, kTabHash}) SetChip(id, id == selectedTab);
        unlocked_ = unlocked;
        Layout();
    }

private:
    void CreateControls() override {
        AddChip(kTabVault, L"Vault");
        AddChip(kTabGenerator, L"Generator");
        AddChip(kTabHash, L"Hash");
        AddButton(kLockButton, ButtonStyle::Subtle, icons::kLock, L"Lock");
        AddButton(kSettingsButton, ButtonStyle::Subtle, icons::kSettings, L"");
    }
    void Layout() override {
        int x = S(20);
        for (const auto& [id, label] : {std::pair{kTabVault, L"Vault"}, std::pair{kTabGenerator, L"Generator"}, std::pair{kTabHash, L"Hash"}}) {
            const int width = S(32) + TextWidth(window_, style_->text, label);
            Place(id, x, S(18), width, S(32));
            x += width + S(8);
        }
        const int right = Width() - S(20);
        if (unlocked_) {
            Place(kSettingsButton, right - S(36), S(16), S(36), S(36));
            Place(kLockButton, right - S(36) - S(8) - S(90), S(16), S(90), S(36));
        } else {
            Show(kSettingsButton, false);
            Show(kLockButton, false);
        }
    }
    void OnPaint(HDC dc) override {
        RECT line{0, Height() - 1, Width(), Height()};
        HBRUSH brush = CreateSolidBrush(style_->theme.divider);
        FillRect(dc, &line, brush);
        DeleteObject(brush);
    }
    void OnCommand(int id, int) override {
        if (onClick) onClick(id);
    }

    bool unlocked_ = false;
};

MainWindow::MainWindow() : header_(std::make_unique<HeaderPanel>()) {}

MainWindow::~MainWindow() = default;

bool MainWindow::Create(HINSTANCE instance, Settings* settings) {
    instance_ = instance;
    settings_ = settings;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_OBEX));
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return false;
    window_ = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_APPWINDOW, kClassName, L"Obex Password Vault",
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, nullptr, nullptr,
                              instance, this);
    if (!window_) return false;
    style_.Refresh(window_);
    for (Panel* panel : std::initializer_list<Panel*>{header_.get(), &password_, &recovery_, &vaultPanel_, &generator_, &hash_, &settingsPanel_}) {
        if (!panel->Create(instance, window_, &style_)) return false;
    }
    WNDCLASSEXW quickClass{sizeof(quickClass)};
    quickClass.lpfnWndProc = QuickProc;
    quickClass.hInstance = instance;
    quickClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    quickClass.lpszClassName = kQuickClassName;
    RegisterClassExW(&quickClass);
    quickWindow_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_CONTROLPARENT, kQuickClassName, L"Obex quick password",
                                   WS_POPUP | WS_CLIPCHILDREN, 0, 0, 100, 100, nullptr, nullptr, instance, this);
    if (!quickWindow_ || !quick_.Create(instance, quickWindow_, &style_)) return false;
    quick_.SetOptions(&settings_->generator);
    quick_.SetVisible(true);
    ConnectPanels();
    header_->SetVisible(true);
    generator_.SetOptions(&settings_->generator, &settings_->generatorCount);
    ApplyWindowStyle(window_, style_.theme);

    helloState_ = LoadHelloState(HelloPath());
    password_.SetMode(fs::exists(VaultPath()) ? PasswordPanel::Mode::Unlock : PasswordPanel::Mode::Create);
    ShowScreen(Screen::Password);
    SetTimer(window_, kAutoLockTimer, 10000, nullptr);

    // Finding out whether Hello is set up takes a moment; do it in the background.
    RunTask([this]() -> std::function<void()> {
        const bool supported = HelloAvailable();
        return [this, supported] {
            helloSupported_ = supported;
            helloChecked_ = true;
            password_.SetHello(HelloUsable());
            if (IsWindowVisible(window_) && !vault_.IsOpen() && HelloUsable() && !helloPrompted_) StartHelloUnlock();
        };
    });
    return true;
}

void MainWindow::Destroy() {
    if (task_.joinable()) task_.join();
    hash_.Stop();
    ClearClipboard();
    vaultPanel_.Reset();
    vault_.Close();
    Wipe(pendingCode_);
    recovery_.Clear();
    password_.Clear();
    quick_.Clear();
    if (pendingEntry_) pendingEntry_->Wipe();
    pendingEntry_.reset();
    if (quickWindow_) DestroyWindow(quickWindow_);
    quickWindow_ = nullptr;
    if (window_) DestroyWindow(window_);
    window_ = nullptr;
    style_.Release();
}

void MainWindow::ConnectPanels() {
    header_->onClick = [this](int id) {
        switch (id) {
            case kTabVault: ShowVaultTab(); break;
            case kTabGenerator:
                generator_.SetOptions(&settings_->generator, &settings_->generatorCount);
                ShowScreen(Screen::Generator);
                break;
            case kTabHash: ShowScreen(Screen::Hash); break;
            case kLockButton: Lock(); break;
            case kSettingsButton:
                settingsPanel_.Load(*settings_, IsAutostartEnabled());
                ShowScreen(Screen::Settings);
                break;
        }
    };
    password_.onSubmit = [this](PasswordPanel::Mode mode, const std::wstring& code, const std::wstring& password) {
        Submit(mode, code, password);
    };
    password_.onHello = [this] { StartHelloUnlock(); };
    password_.onCancel = [this] {
        verify_ = Verify::None;
        ShowScreen(Screen::Settings);
    };
    recovery_.onDone = [this] { RecoveryDone(); };

    vaultPanel_.onChanged = [this] { return SaveVault(); };
    vaultPanel_.onCopy = [this](const std::wstring& text, bool secret) { Copy(text, secret); };
    vaultPanel_.onGenerate = [this] { return GeneratePassword(settings_->generator); };
    vaultPanel_.onImport = [this] { Import(); };

    generator_.onCopy = [this](const std::wstring& text) { Copy(text, true); };
    generator_.onOptionsChanged = [this] { SaveSettings(); };
    hash_.onCopy = [this](const std::wstring& text) {
        SetClipboardText(window_, text, false);
    };

    settingsPanel_.onChanged = [this](const Settings& changed) -> std::wstring {
        const Settings previous = *settings_;
        settings_->openHotkey = changed.openHotkey;
        settings_->autoLockMinutes = changed.autoLockMinutes;
        settings_->lockWithWindows = changed.lockWithWindows;
        settings_->masterEveryDays = changed.masterEveryDays;
        if (onHotkeyChanged) {
            std::wstring error = onHotkeyChanged();
            if (!error.empty()) {
                settings_->openHotkey = previous.openHotkey;
                if (onHotkeyChanged) onHotkeyChanged();
                return error;
            }
        }
        SaveSettings();
        return L"";
    };
    settingsPanel_.onAutostart = [](bool enabled) -> std::wstring {
        return SetAutostartEnabled(enabled, ExecutablePath().wstring()) ? L"" : L"Could not change Start with Windows.";
    };
    settingsPanel_.onHello = [this](bool enabled) {
        if (!enabled) {
            SetHelloSetting(false);
            settingsPanel_.SetStatus(L"Windows Hello is off. Obex asks for your master password every time.");
            return;
        }
        if (!helloSupported_) {
            settingsPanel_.SetHello(false);
            settingsPanel_.SetStatus(L"Windows Hello isn't set up. Add a PIN in Windows Settings > Accounts > Sign-in options.", true);
            return;
        }
        VerifyMaster(Verify::EnableHello, L"Turn on Windows Hello", L"Confirm your master password to unlock Obex with Windows Hello from now on.");
    };
    quick_.onCopy = [this](const std::wstring& text) { Copy(text, true); };
    quick_.onSave = [this](Entry entry) { SaveQuickEntry(std::move(entry)); };
    quick_.onOpenObex = [this] {
        HideQuick();
        Show(true);
    };
    quick_.onClose = [this] { HideQuick(); };
    quick_.onOptionsChanged = [this] { SaveSettings(); };

    settingsPanel_.onAction = [this](SettingsPanel::Action action) { RunAction(action); };
    settingsPanel_.onRecording = [this](bool recording) {
        if (onPauseHotkey) onPauseHotkey(recording);
    };
}

// --- Screens -------------------------------------------------------------------

void MainWindow::Layout() {
    RECT client;
    GetClientRect(window_, &client);
    const int header = Scale(kHeaderHeight);
    header_->SetBounds({0, 0, client.right, header});
    const RECT content{0, header, client.right, client.bottom};
    for (Panel* panel : std::initializer_list<Panel*>{&password_, &recovery_, &vaultPanel_, &generator_, &hash_, &settingsPanel_}) {
        if (panel->IsVisible()) panel->SetBounds(content);
        else SetWindowPos(panel->window(), nullptr, content.left, content.top, content.right - content.left,
                          content.bottom - content.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void MainWindow::ShowScreen(Screen screen) {
    screen_ = screen;
    Panel* shown = nullptr;
    switch (screen) {
        case Screen::Password: shown = &password_; break;
        case Screen::Recovery: shown = &recovery_; break;
        case Screen::Vault: shown = &vaultPanel_; break;
        case Screen::Generator: shown = &generator_; break;
        case Screen::Hash: shown = &hash_; break;
        case Screen::Settings: shown = &settingsPanel_; break;
    }
    for (Panel* panel : std::initializer_list<Panel*>{&password_, &recovery_, &vaultPanel_, &generator_, &hash_, &settingsPanel_}) {
        if (panel != shown) panel->SetVisible(false);
    }
    Layout();
    shown->SetVisible(true);
    UpdateHeader();
    switch (screen) {
        case Screen::Password: password_.FocusFirst(); break;
        case Screen::Recovery: recovery_.FocusFirst(); break;
        case Screen::Vault: vaultPanel_.FocusSearch(); break;
        default: SetFocus(shown->window()); break;
    }
}

void MainWindow::ShowVaultTab() {
    if (!vault_.IsOpen()) {
        const auto mode = password_.mode();
        if (mode == PasswordPanel::Mode::Change || mode == PasswordPanel::Mode::Verify) {
            password_.SetMode(PasswordPanel::Mode::Unlock);
        }
        password_.SetHello(HelloUsable());
        ShowScreen(Screen::Password);
    } else if (pending_ != Pending::None) {
        ShowScreen(Screen::Recovery);
    } else {
        ShowScreen(Screen::Vault);
    }
}

void MainWindow::UpdateHeader() {
    int tab = 0;
    switch (screen_) {
        case Screen::Generator: tab = kTabGenerator; break;
        case Screen::Hash: tab = kTabHash; break;
        case Screen::Settings: break;
        case Screen::Password: {
            const auto mode = password_.mode();
            if (mode != PasswordPanel::Mode::Change && mode != PasswordPanel::Mode::Verify) tab = kTabVault;
            break;
        }
        default: tab = kTabVault; break;
    }
    header_->SetState(tab, vault_.IsOpen() && pending_ == Pending::None);
}

void MainWindow::RefreshStyle() {
    style_.Refresh(window_);
    ApplyWindowStyle(window_, style_.theme);
    for (Panel* panel : std::initializer_list<Panel*>{header_.get(), &password_, &recovery_, &vaultPanel_, &generator_, &hash_, &settingsPanel_, &quick_}) {
        panel->Refresh();
    }
    ApplyWindowStyle(quickWindow_, style_.theme);
    Layout();
    InvalidateRect(window_, nullptr, TRUE);
}

void MainWindow::Show(bool search) {
    if (!shownOnce_) {
        shownOnce_ = true;
        // Open on the monitor with the mouse, centered.
        POINT cursor;
        GetCursorPos(&cursor);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
        SetWindowPos(window_, nullptr, monitor.rcWork.left, monitor.rcWork.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        RefreshStyle();
        const RECT& work = monitor.rcWork;
        const int width = std::min<int>(Scale(960), work.right - work.left);
        const int height = std::min<int>(Scale(680), work.bottom - work.top);
        SetWindowPos(window_, nullptr, work.left + (work.right - work.left - width) / 2, work.top + (work.bottom - work.top - height) / 2,
                     width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    lastActivity_ = GetTickCount64();
    ShowWindow(window_, IsIconic(window_) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(window_);
    SendMessageW(window_, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
    if (!vault_.IsOpen()) {
        if (search) ShowVaultTab();
        if (screen_ == Screen::Password && password_.mode() == PasswordPanel::Mode::Unlock) {
            password_.SetHello(HelloUsable());
            if (HelloUsable()) StartHelloUnlock();
        }
    } else if (search) {
        if (pending_ == Pending::None && !vaultPanel_.IsEditing()) {
            ShowScreen(Screen::Vault);
        }
    }
}

// --- Unlocking -----------------------------------------------------------------

void MainWindow::Submit(PasswordPanel::Mode mode, const std::wstring& code, const std::wstring& password) {
    using Mode = PasswordPanel::Mode;
    HCURSOR previous = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    switch (mode) {
        case Mode::Create: {
            std::wstring newCode = NewRecoveryCode();
            vault_.Create(VaultPath(), password, newCode);
            pending_ = Pending::Create;
            recovery_.ShowCode(newCode);
            Wipe(newCode);
            password_.Clear();
            ShowScreen(Screen::Recovery);
            break;
        }
        case Mode::Unlock: {
            const Vault::Result result = vault_.Open(VaultPath(), password);
            if (result == Vault::Result::Ok) {
                password_.Clear();
                MasterEntered();
                AfterUnlock();
            } else if (result == Vault::Result::WrongSecret) {
                password_.SetError(L"That's not the right master password.");
            } else if (result == Vault::Result::Missing) {
                password_.SetError(L"The vault file is missing. Restore a copy from the Backups folder.");
            } else {
                password_.SetError(L"The vault file is damaged. Restore a copy from the Backups folder.");
            }
            break;
        }
        case Mode::Recover: {
            const Vault::Result result = vault_.OpenWithRecoveryCode(VaultPath(), code);
            if (result == Vault::Result::Ok) {
                vault_.SetMaster(password);
                std::wstring newCode = NewRecoveryCode();
                vault_.SetRecoveryCode(newCode);
                pending_ = Pending::Recover;
                recovery_.ShowCode(newCode);
                Wipe(newCode);
                password_.Clear();
                ShowScreen(Screen::Recovery);
            } else if (result == Vault::Result::WrongSecret) {
                password_.SetError(L"That recovery code doesn't open this vault.");
            } else {
                password_.SetError(L"The vault file is missing or damaged.");
            }
            break;
        }
        case Mode::Change:
            if (!vault_.CheckMaster(code)) {
                password_.SetError(L"Your current master password isn't right.");
                break;
            }
            vault_.SetMaster(password);
            password_.Clear();
            ShowScreen(Screen::Settings);
            if (SaveVault()) {
                MasterEntered();
                settingsPanel_.SetStatus(L"Master password changed.");
            } else {
                settingsPanel_.SetStatus(L"Couldn't save the vault file.", true);
            }
            break;
        case Mode::Verify: {
            if (!vault_.CheckMaster(password)) {
                password_.SetError(L"That's not the right master password.");
                break;
            }
            password_.Clear();
            MasterEntered();
            const Verify purpose = verify_;
            verify_ = Verify::None;
            if (purpose == Verify::NewCode) {
                pendingCode_ = NewRecoveryCode();
                pending_ = Pending::NewCode;
                recovery_.ShowCode(pendingCode_);
                ShowScreen(Screen::Recovery);
            } else {
                ShowScreen(Screen::Settings);
                if (purpose == Verify::Export) Export();
                if (purpose == Verify::EnableHello) EnrollHello();
            }
            break;
        }
    }
    SetCursor(previous);
}

void MainWindow::MasterEntered() {
    // Hello keeps working for the configured days from now.
    if (helloState_) {
        helloState_->masterTime = UnixNow();
        helloState_->logonId = CurrentLogonId();
        SaveHelloState(HelloPath(), *helloState_);
    } else if (settings_->hello && helloSupported_ && pending_ == Pending::None) {
        EnrollHello();
    }
}

void MainWindow::AfterUnlock() {
    lastActivity_ = GetTickCount64();
    std::wstring added;
    if (pendingEntry_) {
        added = pendingEntry_->name;
        vault_.entries.push_back(std::move(*pendingEntry_));
        pendingEntry_.reset();
        SaveVault();
    }
    vaultPanel_.SetEntries(&vault_.entries);
    password_.SetMode(PasswordPanel::Mode::Unlock);
    ShowScreen(Screen::Vault);
    if (!added.empty()) vaultPanel_.SelectNamed(added);
}

void MainWindow::ShowQuick() {
    RECT frame{0, 0, Scale(kQuickWidth), quick_.PreferredHeight()};
    // Bottom right of the screen with the mouse, above the taskbar.
    POINT cursor;
    GetCursorPos(&cursor);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
    const RECT& work = monitor.rcWork;
    const int margin = Scale(12);
    const int width = frame.right;
    const int height = frame.bottom;
    SetWindowPos(quickWindow_, HWND_TOPMOST, work.right - width - margin, work.bottom - height - margin, width, height, SWP_NOACTIVATE);
    quick_.SetBounds({0, 0, width, height});
    quick_.Start(vault_.IsOpen() && pending_ == Pending::None);
    ShowWindow(quickWindow_, SW_SHOW);
    SetForegroundWindow(quickWindow_);
    SendMessageW(quickWindow_, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
    quick_.FocusFirst();
}

void MainWindow::HideQuick() {
    if (!IsWindowVisible(quickWindow_)) return;
    ShowWindow(quickWindow_, SW_HIDE);
    quick_.Clear();
}

void MainWindow::SaveQuickEntry(Entry entry) {
    Copy(entry.password, true);
    entry.created = entry.modified = UnixNow();
    if (vault_.IsOpen() && pending_ == Pending::None) {
        const std::wstring name = entry.name;
        vault_.entries.push_back(std::move(entry));
        if (!SaveVault()) {
            quick_.SetStatus(L"Couldn't save the vault file.", true);
            return;
        }
        if (!vaultPanel_.IsEditing()) {
            vaultPanel_.Reload();
            vaultPanel_.SelectNamed(name);
        }
        lastActivity_ = GetTickCount64();
        quick_.Saved(name);
        return;
    }
    // Locked: keep it until the vault opens, then add it.
    if (pendingEntry_) pendingEntry_->Wipe();
    pendingEntry_ = std::move(entry);
    HideQuick();
    Show(true);
    password_.SetError(L"Unlock to save “" + pendingEntry_->name + L"”. Its password is already copied.");
}

void MainWindow::RecoveryDone() {
    const Pending pending = pending_;
    pending_ = Pending::None;
    if (pending == Pending::NewCode) vault_.SetRecoveryCode(pendingCode_);
    Wipe(pendingCode_);
    pendingCode_.clear();
    recovery_.Clear();
    const bool saved = SaveVault();
    if (pending == Pending::NewCode) {
        ShowScreen(Screen::Settings);
        settingsPanel_.SetStatus(saved ? L"New recovery code saved. The old one no longer works." : L"Couldn't save the vault file.", !saved);
        return;
    }
    // A new or recovered vault: the master password was just typed.
    if (helloState_) {
        helloState_->masterTime = UnixNow();
        helloState_->logonId = CurrentLogonId();
        SaveHelloState(HelloPath(), *helloState_);
    } else if (settings_->hello && helloSupported_) {
        EnrollHello();
    }
    AfterUnlock();
}

bool MainWindow::HelloUsable() const {
    return settings_->hello && helloSupported_ && helloState_ && helloState_->IsUsable(settings_->masterEveryDays);
}

void MainWindow::StartHelloUnlock() {
    if (task_.joinable() || vault_.IsOpen() || !helloState_) return;
    helloPrompted_ = true;
    password_.SetBusy(true);
    password_.SetError(L"");
    const auto challenge = helloState_->challenge;
    SetTimer(window_, kHelloFocusTimer, 150, nullptr);
    RunTask([this, challenge]() -> std::function<void()> {
        auto signature = HelloSign(challenge, false);
        return [this, signature] {
            KillTimer(window_, kHelloFocusTimer);
            password_.SetBusy(false);
            if (!signature || !helloState_) {
                password_.SetError(L"Windows Hello was canceled. Use your master password.");
                password_.FocusFirst();
                return;
            }
            auto key = UnwrapHelloKey(*helloState_, *signature);
            if (key && vault_.OpenWithKey(VaultPath(), *key) == Vault::Result::Ok) {
                Wipe(key->data(), key->size());
                AfterUnlock();
                return;
            }
            if (key) Wipe(key->data(), key->size());
            // The Hello key changed or the vault was replaced: start over.
            helloState_.reset();
            DeleteFileW(HelloPath().c_str());
            password_.SetHello(false);
            password_.SetError(L"Windows Hello couldn't open the vault. Use your master password.");
        };
    });
}

void MainWindow::EnrollHello() {
    if (task_.joinable() || !vault_.IsOpen()) return;
    std::array<uint8_t, 32> challenge{};
    RandomBytes(challenge);
    SetTimer(window_, kHelloFocusTimer, 150, nullptr);
    RunTask([this, challenge]() -> std::function<void()> {
        auto signature = HelloSign(challenge, true);
        return [this, challenge, signature] {
            KillTimer(window_, kHelloFocusTimer);
            if (signature && vault_.IsOpen()) {
                helloState_ = MakeHelloState(challenge, *signature, vault_.key());
                if (SaveHelloState(HelloPath(), *helloState_)) {
                    SetHelloSetting(true);
                    if (screen_ == Screen::Settings) settingsPanel_.SetStatus(L"Windows Hello is on.");
                    return;
                }
            }
            helloState_.reset();
            SetHelloSetting(false);
            if (screen_ == Screen::Settings) {
                settingsPanel_.SetStatus(L"Windows Hello wasn't set up. You can turn it on again here.", true);
            }
        };
    });
}

void MainWindow::SetHelloSetting(bool enabled) {
    settings_->hello = enabled;
    SaveSettings();
    settingsPanel_.SetHello(enabled);
    if (!enabled) {
        helloState_.reset();
        DeleteFileW(HelloPath().c_str());
        if (!task_.joinable()) RunTask([]() -> std::function<void()> {
            HelloRemove();
            return [] {};
        });
    }
}

void MainWindow::Lock() {
    if (!vault_.IsOpen()) return;
    // A vault being created or recovered isn't saved yet; it can't lock.
    if (pending_ == Pending::Create || pending_ == Pending::Recover) return;
    if (pending_ == Pending::NewCode) {
        pending_ = Pending::None;
        Wipe(pendingCode_);
        pendingCode_.clear();
        recovery_.Clear();
    }
    verify_ = Verify::None;
    ClearClipboard();
    vaultPanel_.Reset();
    vault_.Close();
    password_.SetMode(PasswordPanel::Mode::Unlock);
    password_.SetHello(HelloUsable());
    helloPrompted_ = false;
    if (screen_ != Screen::Generator && screen_ != Screen::Hash) {
        ShowScreen(Screen::Password);
    } else {
        UpdateHeader();
    }
}

// --- Vault actions -------------------------------------------------------------

bool MainWindow::SaveVault() { return vault_.Save(BackupDirectory(), 5); }

void MainWindow::VerifyMaster(Verify purpose, const wchar_t* heading, const wchar_t* message) {
    verify_ = purpose;
    password_.SetMode(PasswordPanel::Mode::Verify, heading, message);
    ShowScreen(Screen::Password);
}

void MainWindow::RunAction(SettingsPanel::Action action) {
    using Action = SettingsPanel::Action;
    switch (action) {
        case Action::ChangeMaster:
            password_.SetMode(PasswordPanel::Mode::Change);
            ShowScreen(Screen::Password);
            break;
        case Action::NewRecoveryCode:
            VerifyMaster(Verify::NewCode, L"New recovery code",
                         L"Confirm your master password to make a new recovery code. The old code stops working once you've saved the new one.");
            break;
        case Action::Import: Import(); break;
        case Action::Export:
            VerifyMaster(Verify::Export, L"Export to CSV",
                         L"Confirm your master password. The CSV file holds all your passwords without encryption.");
            break;
        case Action::Backup: Backup(); break;
        case Action::OpenBackups: {
            std::error_code error;
            fs::create_directories(BackupDirectory(), error);
            ShellExecuteW(nullptr, L"open", BackupDirectory().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
    }
}

void MainWindow::Import() {
    if (!vault_.IsOpen()) return;
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = L"Passwords exported from a browser (*.csv)\0*.csv\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return;

    std::ifstream in(file, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::wstring text = FromUtf8(bytes);
    Wipe(bytes);
    std::vector<Entry> imported;
    const bool ok = ImportBrowserCsv(text, imported);
    Wipe(text);
    const auto report = [this](const std::wstring& message, bool error) {
        if (screen_ == Screen::Settings) {
            settingsPanel_.SetStatus(message, error);
        } else {
            MessageBoxW(window_, message.c_str(), L"Obex Password Vault", MB_OK | (error ? MB_ICONWARNING : MB_ICONINFORMATION));
        }
    };
    if (!ok) {
        report(L"That file doesn't look like a password export from Chrome or Edge.", true);
        return;
    }

    std::set<std::tuple<std::wstring, std::wstring, std::wstring, std::wstring>> existing;
    for (const Entry& e : vault_.entries) existing.insert({e.name, e.url, e.username, e.password});
    int added = 0, skipped = 0;
    const int64_t now = UnixNow();
    for (Entry& e : imported) {
        if (!existing.insert({e.name, e.url, e.username, e.password}).second) {
            ++skipped;
            e.Wipe();
            continue;
        }
        e.created = e.modified = now;
        vault_.entries.push_back(std::move(e));
        ++added;
    }
    existing.clear();
    if (added > 0 && !SaveVault()) {
        report(L"Couldn't save the vault file.", true);
        return;
    }
    vaultPanel_.Reload();
    std::wstring message = L"Imported " + std::to_wstring(added) + (added == 1 ? L" password" : L" passwords");
    if (skipped) message += L" (" + std::to_wstring(skipped) + L" already in your vault)";
    message += L".";
    report(message, false);
    if (Confirm(window_, L"Delete the CSV file?",
                L"It still has your passwords in plain text, readable by anyone who opens it. Obex has them safe now.",
                L"Delete file", true)) {
        DeleteFileW(file);
    }
}

void MainWindow::Export() {
    if (!Confirm(window_, L"Export all passwords without encryption?",
                 L"Anyone who gets the CSV file can read every password in it. Delete it as soon as you're done with it.",
                 L"Export", true)) {
        return;
    }
    const auto file = SaveDialog(window_, L"Obex passwords.csv", L"CSV file (*.csv)\0*.csv\0", L"csv");
    if (!file) return;
    std::wstring csv = ExportBrowserCsv(vault_.entries);
    std::string utf8 = ToUtf8(csv);
    Wipe(csv);
    std::ofstream out(*file, std::ios::binary | std::ios::trunc);
    out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    Wipe(utf8);
    if (out) {
        settingsPanel_.SetStatus(L"Exported " + std::to_wstring(vault_.entries.size()) + L" passwords. Delete the file when you're done.");
    } else {
        settingsPanel_.SetStatus(L"Couldn't write the file there. Try another folder.", true);
    }
}

void MainWindow::Backup() {
    const std::wstring name = L"Obex vault " + DateStamp() + L".obex";
    const auto file = SaveDialog(window_, name.c_str(), L"Obex vault (*.obex)\0*.obex\0", L"obex");
    if (!file) return;
    if (CopyFileW(VaultPath().c_str(), file->c_str(), FALSE)) {
        settingsPanel_.SetStatus(L"Copy saved. It's encrypted: it opens with your master password or recovery code.");
    } else {
        settingsPanel_.SetStatus(L"Couldn't save the copy there. Try another folder.", true);
    }
}

// --- Clipboard and settings ------------------------------------------------------

void MainWindow::Copy(const std::wstring& text, bool secret) {
    clipboardSequence_ = SetClipboardText(window_, text, secret);
    if (clipboardSequence_) SetTimer(window_, kClipboardTimer, kClipboardSeconds * 1000, nullptr);
}

void MainWindow::ClearClipboard() {
    if (clipboardSequence_) ClearClipboardIfUnchanged(window_, clipboardSequence_);
    clipboardSequence_ = 0;
    if (window_) KillTimer(window_, kClipboardTimer);
}

void MainWindow::SaveSettings() { settings_->Save(); }

void MainWindow::RunTask(std::function<std::function<void()>()> work) {
    if (task_.joinable()) return;
    task_ = std::thread([this, work = std::move(work)] {
        auto done = std::make_unique<std::function<void()>>(work());
        PostMessageW(window_, WM_APP_TASK_DONE, 0, reinterpret_cast<LPARAM>(done.release()));
    });
}

bool MainWindow::PreTranslate(MSG& message) {
    if (quickWindow_ && IsWindowVisible(quickWindow_) && (message.hwnd == quickWindow_ || IsChild(quickWindow_, message.hwnd))) {
        lastActivity_ = GetTickCount64();
        return IsDialogMessageW(quickWindow_, &message) != FALSE;
    }
    if (!window_ || !IsWindowVisible(window_) || (message.hwnd != window_ && !IsChild(window_, message.hwnd))) return false;
    if ((message.message >= WM_KEYFIRST && message.message <= WM_KEYLAST) ||
        (message.message >= WM_MOUSEFIRST && message.message <= WM_MOUSELAST)) {
        lastActivity_ = GetTickCount64();
    }
    if (message.message == WM_KEYDOWN && GetKeyState(VK_CONTROL) < 0) {
        if (message.wParam == 'F' && vault_.IsOpen() && pending_ == Pending::None) {
            ShowScreen(Screen::Vault);
            return true;
        }
        if (message.wParam == 'L' && vault_.IsOpen()) {
            Lock();
            return true;
        }
    }
    return IsDialogMessageW(window_, &message) != FALSE;
}

LRESULT CALLBACK MainWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return self ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK MainWindow::QuickProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    }
    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self) {
        switch (message) {
            case WM_COMMAND:
                if (LOWORD(wParam) == IDOK) self->quick_.OnDefault();
                if (LOWORD(wParam) == IDCANCEL) self->quick_.OnEscape();
                return 0;
            case WM_ACTIVATE:
                // Like a flyout: clicking elsewhere closes it.
                if (LOWORD(wParam) == WA_INACTIVE) self->HideQuick();
                return 0;
            case WM_CLOSE:
                self->HideQuick();
                return 0;
        }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND: {
            Panel* current = nullptr;
            switch (screen_) {
                case Screen::Password: current = &password_; break;
                case Screen::Recovery: current = &recovery_; break;
                case Screen::Vault: current = &vaultPanel_; break;
                case Screen::Generator: current = &generator_; break;
                case Screen::Hash: current = &hash_; break;
                case Screen::Settings: current = &settingsPanel_; break;
            }
            if (LOWORD(wParam) == IDOK) current->OnDefault();
            if (LOWORD(wParam) == IDCANCEL) current->OnEscape();
            return 0;
        }
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) Layout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize = {Scale(860), Scale(620)};
            return 0;
        }
        case WM_ERASEBKGND: {
            RECT client;
            GetClientRect(window_, &client);
            FillRect(reinterpret_cast<HDC>(wParam), &client, style_.background);
            return 1;
        }
        case WM_TIMER:
            if (wParam == kClipboardTimer) {
                ClearClipboard();
            } else if (wParam == kAutoLockTimer) {
                if (vault_.IsOpen() && settings_->autoLockMinutes > 0 &&
                    GetTickCount64() - lastActivity_ > static_cast<ULONGLONG>(settings_->autoLockMinutes) * 60000) {
                    Lock();
                }
            } else if (wParam == kHelloFocusTimer) {
                // The Windows Security prompt can open behind Obex; bring it forward once.
                if (HWND prompt = FindWindowW(L"Credential Dialog Xaml Host", nullptr)) {
                    SetForegroundWindow(prompt);
                    KillTimer(window_, kHelloFocusTimer);
                }
            }
            return 0;
        case WM_APP_TASK_DONE: {
            if (task_.joinable()) task_.join();
            std::unique_ptr<std::function<void()>> done(reinterpret_cast<std::function<void()>*>(lParam));
            if (*done && window_) (*done)();
            return 0;
        }
        case WM_SETTINGCHANGE:
            if (lParam && wcscmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0) RefreshStyle();
            return 0;
        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(window_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            RefreshStyle();
            return 0;
        }
        case WM_CLOSE:
            // Closing hides the window; Obex stays in the notification area.
            ShowWindow(window_, SW_HIDE);
            if (!hiddenOnce_) {
                hiddenOnce_ = true;
                if (onFirstHide) onFirstHide();
            }
            return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

}  // namespace obex
