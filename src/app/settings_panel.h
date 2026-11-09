#pragma once

#include "app/panel.h"
#include "core/settings.h"

#include <functional>

namespace obex {

// Security and general settings on the left, vault actions on the right.
// Changes apply right away.
class SettingsPanel : public Panel {
public:
    enum class Action { ChangeMaster, NewRecoveryCode, Import, Export, Backup, OpenBackups };

    // Saves and applies the settings; returns an error to show, or nothing.
    std::function<std::wstring(const Settings& settings)> onChanged;
    std::function<std::wstring(bool enabled)> onAutostart;
    std::function<void(bool enabled)> onHello;
    std::function<void(Action action)> onAction;
    // Global shortcuts pause while a new one is being recorded.
    std::function<void(bool recording)> onRecording;

    void Load(const Settings& settings, bool autostart);
    void SetStatus(std::wstring status, bool error = false);
    void SetHello(bool enabled);

private:
    void CreateControls() override;
    void Layout() override;
    void OnCommand(int id, int code) override;
    bool OnDrawItem(const DRAWITEMSTRUCT& item) override;
    void ShowChoices(int id);
    void Apply();
    void SetRecording(bool recording);
    friend LRESULT CALLBACK ShortcutProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

    Settings settings_;
    bool autostart_ = false;
    bool hello_ = false;
    bool recording_ = false;
    std::wstring status_;
    bool statusError_ = false;
};

}  // namespace obex
