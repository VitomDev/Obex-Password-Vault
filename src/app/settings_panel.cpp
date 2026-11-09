#include "app/settings_panel.h"

#include "version.h"

#include <commctrl.h>

namespace obex {

namespace {

enum ControlId {
    kAutoLock = 100,
    kLockWithWindows,
    kHello,
    kMasterEvery,
    kShortcut,
    kAutostart,
    kChangeMaster,
    kNewRecovery,
    kImport,
    kExport,
    kBackup,
    kOpenBackups,
};

constexpr int kAutoLockChoices[] = {1, 5, 15, 30, 60, 0};
constexpr int kMasterChoices[] = {1, 3, 7, 14, 30};

std::wstring AutoLockName(int minutes) {
    if (minutes == 0) return L"Never";
    if (minutes == 60) return L"1 hour";
    return std::to_wstring(minutes) + (minutes == 1 ? L" minute" : L" minutes");
}

std::wstring DaysName(int days) { return days == 1 ? L"Every day" : L"Every " + std::to_wstring(days) + L" days"; }

}  // namespace

LRESULT CALLBACK ShortcutProc(HWND field, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
    auto& panel = *reinterpret_cast<SettingsPanel*>(data);
    if (panel.recording_) {
        switch (message) {
            case WM_GETDLGCODE:
                return DLGC_WANTALLKEYS;
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                const UINT key = static_cast<UINT>(wParam);
                if (key == VK_ESCAPE) {
                    panel.SetRecording(false);
                } else if (key == VK_CONTROL || key == VK_MENU || key == VK_SHIFT || key == VK_LWIN || key == VK_RWIN) {
                    return 0;  // Wait for the actual key.
                } else {
                    Hotkey hotkey{0, key};
                    if (GetKeyState(VK_CONTROL) < 0) hotkey.modifiers |= MOD_CONTROL;
                    if (GetKeyState(VK_MENU) < 0) hotkey.modifiers |= MOD_ALT;
                    if (GetKeyState(VK_SHIFT) < 0) hotkey.modifiers |= MOD_SHIFT;
                    if (!hotkey.IsValid()) {
                        panel.SetStatus(L"Use Ctrl, Alt or Shift together with a key.", true);
                        return 0;
                    }
                    panel.SetRecording(false);
                    const Hotkey previous = panel.settings_.openHotkey;
                    panel.settings_.openHotkey = hotkey;
                    panel.Apply();
                    if (panel.statusError_) panel.settings_.openHotkey = previous;
                }
                InvalidateRect(field, nullptr, FALSE);
                return 0;
            }
            case WM_SYSCHAR:
            case WM_CHAR:
                return 0;
            case WM_KILLFOCUS:
                panel.SetRecording(false);
                InvalidateRect(field, nullptr, FALSE);
                break;
        }
    }
    return DefSubclassProc(field, message, wParam, lParam);
}

void SettingsPanel::CreateControls() {
    AddButton(kAutoLock, ButtonStyle::Standard, nullptr, L"");
    AddToggle(kLockWithWindows, &settings_.lockWithWindows);
    AddToggle(kHello, &hello_);
    AddButton(kMasterEvery, ButtonStyle::Standard, nullptr, L"");
    SetWindowSubclass(AddButton(kShortcut, ButtonStyle::Standard, nullptr, L""), ShortcutProc, 0, reinterpret_cast<DWORD_PTR>(this));
    AddToggle(kAutostart, &autostart_);
    AddButton(kChangeMaster, ButtonStyle::Standard, icons::kKey, L"Change master password");
    AddButton(kNewRecovery, ButtonStyle::Standard, icons::kRefresh, L"New recovery code");
    AddButton(kImport, ButtonStyle::Standard, icons::kImport, L"Import from Chrome or Edge…");
    AddButton(kExport, ButtonStyle::Standard, icons::kExport, L"Export to CSV…");
    AddButton(kBackup, ButtonStyle::Standard, icons::kSave, L"Save a copy of the vault…");
    AddButton(kOpenBackups, ButtonStyle::Standard, icons::kFolder, L"Open the backups folder");
}

void SettingsPanel::Load(const Settings& settings, bool autostart) {
    settings_ = settings;
    autostart_ = autostart;
    hello_ = settings.hello;
    status_.clear();
    Layout();
    Repaint();
    for (int id = kAutoLock; id <= kOpenBackups; ++id) InvalidateRect(Item(id), nullptr, FALSE);
}

void SettingsPanel::SetStatus(std::wstring status, bool error) {
    status_ = std::move(status);
    statusError_ = error;
    Layout();
    Repaint();
}

void SettingsPanel::SetHello(bool enabled) {
    hello_ = enabled;
    settings_.hello = enabled;
    InvalidateRect(Item(kHello), nullptr, FALSE);
    EnableWindow(Item(kMasterEvery), enabled);
}

void SettingsPanel::SetRecording(bool recording) {
    if (recording_ == recording) return;
    recording_ = recording;
    if (onRecording) onRecording(recording);
}

void SettingsPanel::Apply() {
    const std::wstring error = onChanged ? onChanged(settings_) : L"";
    SetStatus(error, !error.empty());
}

void SettingsPanel::Layout() {
    ClearTexts();
    const int pad = S(24);
    const int gap = S(40);
    const int column = (Width() - 2 * pad - gap) / 2;
    const int row = S(44);
    const int control = S(160);
    int y = S(16);
    AddText({pad, y, Width() - pad, y + S(32)}, L"Settings", TextKind::Title);
    y += S(32) + S(16);
    const int top = y;

    const auto caption = [&](int x, const wchar_t* text) {
        AddText({x, y, x + column, y + S(26)}, text, TextKind::Heading);
        y += S(28);
    };
    const auto labelRow = [&](int x, const wchar_t* label, const wchar_t* hint, int id, bool toggle) {
        const int controlWidth = toggle ? S(44) : control;
        const int height = hint ? S(52) : row;
        AddText({x, y + (hint ? S(4) : 0), x + column - controlWidth - S(12), y + (hint ? S(26) : row)}, label, TextKind::Body);
        if (hint) AddText({x, y + S(26), x + column - controlWidth - S(12), y + S(46)}, hint, TextKind::Hint);
        const int h = toggle ? S(20) : S(32);
        Place(id, x + column - controlWidth, y + (height - h) / 2, controlWidth, h);
        y += height;
    };

    caption(pad, L"Security");
    labelRow(pad, L"Lock after inactivity", L"Time without using Obex", kAutoLock, false);
    labelRow(pad, L"Lock with Windows", L"When you lock the PC or it sleeps", kLockWithWindows, true);
    labelRow(pad, L"Unlock with Windows Hello", L"PIN, fingerprint or face", kHello, true);
    labelRow(pad, L"Ask for the master password", L"And after every restart", kMasterEvery, false);
    AddText({pad, y, pad + column, y + S(22)}, L"Copied passwords are cleared from the clipboard after 30 seconds.", TextKind::Hint);
    y += S(22) + S(18);

    caption(pad, L"General");
    labelRow(pad, L"Shortcut for a quick password", nullptr, kShortcut, false);
    labelRow(pad, L"Start with Windows", nullptr, kAutostart, true);

    // Right column: the vault.
    y = top;
    const int right = pad + column + gap;
    caption(right, L"Vault");
    for (int id : {kChangeMaster, kNewRecovery, kImport, kExport, kBackup, kOpenBackups}) {
        Place(id, right, y, column, S(36));
        y += S(36) + S(10);
        if (id == kNewRecovery || id == kExport) y += S(10);
    }
    y += S(4);
    AddText({right, y, right + column, y + S(40)},
            L"Obex keeps a daily copy of your encrypted vault, the last 5 days, in the backups folder.", TextKind::Hint,
            DT_LEFT | DT_WORDBREAK);

    const int bottom = Height() - S(36);
    if (!status_.empty()) AddText({pad, bottom - S(28), Width() - pad, bottom - S(4)}, status_, statusError_ ? TextKind::Error : TextKind::Success);
    AddText({pad, bottom, Width() - pad, bottom + S(24)}, L"Obex Password Vault " OBEX_VERSION_STRING L" · VitomDev", TextKind::Hint);
    EnableWindow(Item(kMasterEvery), hello_);
}

bool SettingsPanel::OnDrawItem(const DRAWITEMSTRUCT& item) {
    const int id = static_cast<int>(item.CtlID);
    if (id != kAutoLock && id != kMasterEvery && id != kShortcut) return false;
    const Theme& theme = style_->theme;
    HDC dc = item.hDC;
    const RECT& rect = item.rcItem;
    const bool recording = id == kShortcut && recording_;
    const bool enabled = IsWindowEnabled(item.hwndItem);
    FillRect(dc, &rect, style_->background);
    FillRounded(dc, rect, IsHot(item.hwndItem) || recording ? theme.hover : theme.pressed, S(4));
    OutlineRounded(dc, rect, recording ? theme.accent : theme.divider, S(4), recording ? 2 : 1);
    std::wstring label;
    if (id == kAutoLock) label = AutoLockName(settings_.autoLockMinutes);
    if (id == kMasterEvery) label = DaysName(settings_.masterEveryDays);
    if (id == kShortcut) label = recording ? L"Press a shortcut…" : ToString(settings_.openHotkey);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ previous = SelectObject(dc, style_->text);
    SetTextColor(dc, enabled && !recording ? theme.text : theme.secondaryText);
    RECT inner{rect.left + S(10), rect.top, rect.right - S(10), rect.bottom};
    DrawTextW(dc, label.c_str(), -1, &inner, (id == kShortcut ? DT_CENTER : DT_LEFT) | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (id != kShortcut) {
        SelectObject(dc, style_->icon);
        SetTextColor(dc, theme.secondaryText);
        DrawTextW(dc, icons::kChevronDown, -1, &inner, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(dc, previous);
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
        RECT focus = rect;
        InflateRect(&focus, -3, -3);
        DrawFocusRect(dc, &focus);
    }
    return true;
}

void SettingsPanel::ShowChoices(int id) {
    HMENU menu = CreatePopupMenu();
    if (id == kAutoLock) {
        for (int i = 0; i < static_cast<int>(std::size(kAutoLockChoices)); ++i) {
            AppendMenuW(menu, MF_STRING | (settings_.autoLockMinutes == kAutoLockChoices[i] ? MF_CHECKED : 0), i + 1,
                        AutoLockName(kAutoLockChoices[i]).c_str());
        }
    } else {
        for (int i = 0; i < static_cast<int>(std::size(kMasterChoices)); ++i) {
            AppendMenuW(menu, MF_STRING | (settings_.masterEveryDays == kMasterChoices[i] ? MF_CHECKED : 0), i + 1,
                        DaysName(kMasterChoices[i]).c_str());
        }
    }
    RECT button;
    GetWindowRect(Item(id), &button);
    const int chosen = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, button.left, button.bottom, window_, nullptr);
    DestroyMenu(menu);
    if (chosen <= 0) return;
    if (id == kAutoLock) {
        settings_.autoLockMinutes = kAutoLockChoices[chosen - 1];
    } else {
        settings_.masterEveryDays = kMasterChoices[chosen - 1];
    }
    InvalidateRect(Item(id), nullptr, FALSE);
    Apply();
}

void SettingsPanel::OnCommand(int id, int) {
    switch (id) {
        case kAutoLock:
        case kMasterEvery: ShowChoices(id); break;
        case kLockWithWindows: Apply(); break;
        case kHello:
            // The main window confirms the master password first; it calls
            // SetHello with the outcome.
            hello_ = !hello_;
            InvalidateRect(Item(kHello), nullptr, FALSE);
            if (onHello) onHello(!hello_);
            break;
        case kShortcut:
            SetRecording(!recording_);
            SetFocus(Item(kShortcut));
            InvalidateRect(Item(kShortcut), nullptr, FALSE);
            break;
        case kAutostart: {
            const std::wstring error = onAutostart ? onAutostart(autostart_) : L"";
            if (!error.empty()) {
                autostart_ = !autostart_;
                InvalidateRect(Item(kAutostart), nullptr, FALSE);
            }
            SetStatus(error, !error.empty());
            break;
        }
        case kChangeMaster: if (onAction) onAction(Action::ChangeMaster); break;
        case kNewRecovery: if (onAction) onAction(Action::NewRecoveryCode); break;
        case kImport: if (onAction) onAction(Action::Import); break;
        case kExport: if (onAction) onAction(Action::Export); break;
        case kBackup: if (onAction) onAction(Action::Backup); break;
        case kOpenBackups: if (onAction) onAction(Action::OpenBackups); break;
    }
}

}  // namespace obex
