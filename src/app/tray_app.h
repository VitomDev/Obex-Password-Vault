#pragma once

#include "app/main_window.h"
#include "core/settings.h"

#include <windows.h>
#include <shellapi.h>

namespace obex {

inline constexpr wchar_t kTrayWindowClass[] = L"ObexPasswordVaultTray";
inline constexpr UINT WM_APP_SHOW = WM_APP + 12;

// The background part: tray icon, the global shortcut and locking when
// Windows locks or sleeps.
class TrayApp {
public:
    bool Initialize(HINSTANCE instance, bool startedAtSignIn);
    bool PreTranslate(MSG& message) { return main_.PreTranslate(message); }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void AddTrayIcon();
    void ShowTrayMenu(POINT point);
    void Notify(const std::wstring& message);
    std::wstring RegisterHotkey();
    void Exit();

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;
    NOTIFYICONDATAW icon_{};
    Settings settings_;
    MainWindow main_;
    bool exiting_ = false;
};

}  // namespace obex
