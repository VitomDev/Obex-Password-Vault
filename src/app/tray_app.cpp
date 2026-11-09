#include "app/tray_app.h"

#include "app/resource.h"
#include "app/theme.h"
#include "core/paths.h"

#include <commctrl.h>
#include <windowsx.h>
#include <wtsapi32.h>

namespace obex {

namespace {

constexpr UINT WM_APP_TRAY = WM_APP + 10;
constexpr int kHotkeyId = 1;

enum MenuId { kMenuQuick = 1, kMenuOpen, kMenuLock, kMenuExit };

}  // namespace

bool TrayApp::Initialize(HINSTANCE instance, bool startedAtSignIn) {
    instance_ = instance;
    settings_ = Settings::Load();

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kTrayWindowClass;
    if (!RegisterClassExW(&wc)) return false;
    // A hidden top-level window: it gets TaskbarCreated, power and session
    // notifications, and the installer finds it by class.
    window_ = CreateWindowExW(0, kTrayWindowClass, L"Obex Password Vault", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (!window_) return false;

    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    AddTrayIcon();
    EnableDarkMenus();
    WTSRegisterSessionNotification(window_, NOTIFY_FOR_THIS_SESSION);

    main_.onHotkeyChanged = [this] { return RegisterHotkey(); };
    main_.onPauseHotkey = [this](bool paused) {
        if (paused) {
            UnregisterHotKey(window_, kHotkeyId);
        } else {
            RegisterHotkey();
        }
    };
    main_.onFirstHide = [this] {
        Notify(L"Obex is still running in the notification area. Press " + ToString(settings_.openHotkey) + L" for a quick password.");
    };
    if (!main_.Create(instance, &settings_)) return false;

    const std::wstring conflict = RegisterHotkey();
    if (!startedAtSignIn || !std::filesystem::exists(VaultPath())) {
        main_.Show(true);
    }
    if (!conflict.empty()) Notify(conflict + L" Choose another one in Settings.");
    return true;
}

std::wstring TrayApp::RegisterHotkey() {
    UnregisterHotKey(window_, kHotkeyId);
    if (!RegisterHotKey(window_, kHotkeyId, settings_.openHotkey.modifiers | MOD_NOREPEAT, settings_.openHotkey.key)) {
        return ToString(settings_.openHotkey) + L" is already used by another app.";
    }
    return L"";
}

void TrayApp::AddTrayIcon() {
    icon_ = {sizeof(icon_)};
    icon_.hWnd = window_;
    icon_.uID = 1;
    icon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    icon_.uCallbackMessage = WM_APP_TRAY;
    LoadIconMetric(instance_, MAKEINTRESOURCEW(IDI_OBEX), LIM_SMALL, &icon_.hIcon);
    wcscpy_s(icon_.szTip, L"Obex Password Vault");
    Shell_NotifyIconW(NIM_ADD, &icon_);
    icon_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &icon_);
}

void TrayApp::ShowTrayMenu(POINT point) {
    HMENU menu = CreatePopupMenu();
    const std::wstring quick = L"Quick password\t" + ToString(settings_.openHotkey);
    AppendMenuW(menu, MF_STRING, kMenuOpen, L"Open Obex");
    AppendMenuW(menu, MF_STRING, kMenuQuick, quick.c_str());
    AppendMenuW(menu, MF_STRING | (main_.IsUnlocked() ? 0 : MF_GRAYED), kMenuLock, L"Lock now");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit Obex");
    SetMenuDefaultItem(menu, kMenuOpen, FALSE);

    // Required so the menu closes when clicking elsewhere.
    SetForegroundWindow(window_);
    const UINT command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, point.x, point.y, window_, nullptr);
    DestroyMenu(menu);
    switch (command) {
        case kMenuOpen: main_.Show(true); break;
        case kMenuQuick: main_.ShowQuick(); break;
        case kMenuLock: main_.Lock(); break;
        case kMenuExit: Exit(); break;
    }
}

void TrayApp::Notify(const std::wstring& message) {
    NOTIFYICONDATAW data = icon_;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    wcscpy_s(data.szInfoTitle, L"Obex Password Vault");
    wcsncpy_s(data.szInfo, message.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void TrayApp::Exit() {
    if (exiting_) return;
    exiting_ = true;
    main_.Destroy();
    UnregisterHotKey(window_, kHotkeyId);
    WTSUnRegisterSessionNotification(window_);
    Shell_NotifyIconW(NIM_DELETE, &icon_);
    if (icon_.hIcon) DestroyIcon(icon_.hIcon);
    DestroyWindow(window_);
}

LRESULT CALLBACK TrayApp::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* app = static_cast<TrayApp*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        app->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    auto* app = reinterpret_cast<TrayApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return app ? app->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT TrayApp::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == taskbarCreatedMessage_ && taskbarCreatedMessage_ != 0) {
        AddTrayIcon();  // Explorer restarted.
        return 0;
    }
    switch (message) {
        case WM_HOTKEY:
            main_.ShowQuick();
            return 0;
        case WM_APP_SHOW:
            main_.Show(true);
            return 0;
        case WM_APP_TRAY:
            switch (LOWORD(lParam)) {
                case NIN_SELECT:
                case NIN_KEYSELECT: main_.Show(true); break;
                case WM_CONTEXTMENU: ShowTrayMenu({GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)}); break;
            }
            return 0;
        case WM_WTSSESSION_CHANGE:
            if (wParam == WTS_SESSION_LOCK && settings_.lockWithWindows) main_.Lock();
            return 0;
        case WM_POWERBROADCAST:
            if (wParam == PBT_APMSUSPEND && settings_.lockWithWindows) main_.Lock();
            return TRUE;
        case WM_CLOSE:  // Sent by the installer before upgrading or uninstalling.
        case WM_ENDSESSION:
            if (message == WM_CLOSE || wParam) Exit();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

}  // namespace obex
