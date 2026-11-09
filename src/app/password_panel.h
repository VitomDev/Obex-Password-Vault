#pragma once

#include "app/panel.h"

#include <functional>

namespace obex {

// Every screen that asks for the master password: creating the vault,
// unlocking it, recovering it with the recovery code, changing the master
// password and confirming it before a sensitive action.
class PasswordPanel : public Panel {
public:
    enum class Mode { Create, Unlock, Recover, Change, Verify };

    // `code`, `password` and `confirm` as typed; the panel already checked
    // that new passwords match and are long enough.
    std::function<void(Mode mode, const std::wstring& code, const std::wstring& password)> onSubmit;
    std::function<void()> onHello;
    std::function<void()> onCancel;

    // `heading` and `message` are used by Verify.
    void SetMode(Mode mode, std::wstring heading = {}, std::wstring message = {});
    Mode mode() const { return mode_; }
    void SetHello(bool available);
    void SetError(std::wstring error);
    void SetBusy(bool busy);
    void Clear();
    void FocusFirst();

    void OnDefault() override { Submit(); }
    void OnEscape() override;

    static constexpr int kMinLength = 10;

private:
    void CreateControls() override;
    void Layout() override;
    void OnCommand(int id, int code) override;
    void OnPaint(HDC dc) override;
    void Submit();
    bool NewPassword() const { return mode_ == Mode::Create || mode_ == Mode::Recover || mode_ == Mode::Change; }

    Mode mode_ = Mode::Unlock;
    std::wstring heading_;
    std::wstring message_;
    std::wstring error_;
    bool hello_ = false;
    bool busy_ = false;
    bool reveal_ = false;
    RECT meter_{};
};

}  // namespace obex
