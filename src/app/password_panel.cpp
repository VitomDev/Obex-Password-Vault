#include "app/password_panel.h"

#include "core/generator.h"

namespace obex {

namespace {

enum ControlId { kCode = 100, kPassword, kConfirm, kReveal, kSubmit, kHello, kForgot, kCancel };

constexpr int kColumn = 380;
constexpr int kFieldHeight = 36;

}  // namespace

void PasswordPanel::CreateControls() {
    AddEdit(kCode, ES_AUTOHSCROLL | ES_UPPERCASE, TextKind::Mono);
    AddEdit(kPassword, ES_AUTOHSCROLL | ES_PASSWORD);
    AddEdit(kConfirm, ES_AUTOHSCROLL | ES_PASSWORD);
    AddButton(kReveal, ButtonStyle::Subtle, icons::kView, L"");
    AddButton(kSubmit, ButtonStyle::Primary, nullptr, L"Unlock");
    AddButton(kHello, ButtonStyle::Standard, icons::kFingerprint, L"Use Windows Hello");
    AddButton(kForgot, ButtonStyle::Subtle, nullptr, L"Forgot your master password?");
    AddButton(kCancel, ButtonStyle::Standard, nullptr, L"Cancel");
    SendMessageW(Item(kCode), EM_SETLIMITTEXT, 40, 0);
}

void PasswordPanel::SetMode(Mode mode, std::wstring heading, std::wstring message) {
    mode_ = mode;
    heading_ = std::move(heading);
    message_ = std::move(message);
    error_.clear();
    busy_ = false;
    Clear();
    static constexpr const wchar_t* kSubmitLabels[] = {L"Create vault", L"Unlock", L"Recover vault", L"Change password", L"Continue"};
    SetButton(kSubmit, nullptr, kSubmitLabels[static_cast<int>(mode)]);
    SetButton(kCancel, nullptr, mode == Mode::Recover ? L"Back" : L"Cancel");
    Layout();
    Repaint();
}

void PasswordPanel::SetHello(bool available) {
    hello_ = available;
    Layout();
    Repaint();
}

void PasswordPanel::SetError(std::wstring error) {
    error_ = std::move(error);
    Layout();
    Repaint();
}

void PasswordPanel::SetBusy(bool busy) {
    busy_ = busy;
    for (int id : {kSubmit, kHello, kCancel, kForgot}) EnableWindow(Item(id), !busy);
    SetCursor(LoadCursorW(nullptr, busy ? IDC_WAIT : IDC_ARROW));
}

void PasswordPanel::Clear() {
    for (int id : {kCode, kPassword, kConfirm}) WipeText(id);
    reveal_ = false;
    for (int id : {kPassword, kConfirm}) SendMessageW(Item(id), EM_SETPASSWORDCHAR, L'●', 0);
    SetButton(kReveal, icons::kView, L"");
}

void PasswordPanel::FocusFirst() {
    SetFocus(Item(mode_ == Mode::Recover ? kCode : mode_ == Mode::Change ? kCode : kPassword));
}

void PasswordPanel::OnEscape() {
    if (mode_ == Mode::Recover) {
        SetMode(Mode::Unlock);
        FocusFirst();
    } else if (mode_ != Mode::Unlock && mode_ != Mode::Create && onCancel) {
        onCancel();
    }
}

void PasswordPanel::Layout() {
    ClearTexts();
    const int width = std::min(S(kColumn), Width() - S(48));
    const int left = (Width() - width) / 2;
    const int field = S(kFieldHeight);
    const bool hasCode = mode_ == Mode::Recover || mode_ == Mode::Change;
    const bool unlock = mode_ == Mode::Unlock;

    std::wstring title, intro;
    switch (mode_) {
        case Mode::Create:
            title = L"Create your vault";
            intro = L"Choose a master password. It opens everything in Obex, so make it long and don't use it anywhere else. "
                    L"Next you'll get a recovery code, in case you ever forget it.";
            break;
        case Mode::Unlock:
            title = L"Obex is locked";
            intro = hello_ ? L"Unlock with Windows Hello or your master password." : L"Enter your master password to unlock your vault.";
            break;
        case Mode::Recover:
            title = L"Use your recovery code";
            intro = L"Type the recovery code from your emergency sheet and choose a new master password. "
                    L"You'll get a new recovery code afterwards; the old one stops working.";
            break;
        case Mode::Change:
            title = L"Change master password";
            intro = L"Your recovery code and Windows Hello keep working.";
            break;
        case Mode::Verify:
            title = heading_;
            intro = message_;
            break;
    }

    // Measure first to center the whole block vertically.
    int contentHeight = (unlock ? S(64) : 0) + S(36) + S(8) + TextHeight(intro, TextKind::Body, width) + S(20);
    if (hasCode) contentHeight += S(22) + field + S(12);
    contentHeight += S(22) + field + S(12);
    if (NewPassword()) contentHeight += S(22) + field + S(12) + S(28);
    contentHeight += S(20) + field;
    if (unlock) contentHeight += (hello_ ? field + S(10) : 0) + S(8) + field;
    int y = std::max(S(24), (Height() - contentHeight) / 2);

    if (unlock) {
        AddText({left, y, left + width, y + S(56)}, icons::kLock, TextKind::BigIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        y += S(64);
    }
    AddText({left, y, left + width, y + S(36)}, title, TextKind::Title, unlock ? DT_CENTER | DT_VCENTER | DT_SINGLELINE : DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    y += S(36) + S(8);
    const int introHeight = TextHeight(intro, TextKind::Body, width);
    AddText({left, y, left + width, y + introHeight}, intro, TextKind::Hint, (unlock ? DT_CENTER : DT_LEFT) | DT_WORDBREAK);
    y += introHeight + S(20);

    if (hasCode) {
        AddText({left, y, left + width, y + S(22)}, mode_ == Mode::Recover ? L"Recovery code" : L"Current master password", TextKind::Body);
        y += S(22);
        // The current password reuses the code field, hidden like a password.
        HWND code = Item(kCode);
        SetWindowLongW(code, GWL_STYLE, (GetWindowLongW(code, GWL_STYLE) & ~ES_UPPERCASE) | (mode_ == Mode::Recover ? ES_UPPERCASE : 0));
        SendMessageW(code, EM_SETPASSWORDCHAR, mode_ == Mode::Change && !reveal_ ? L'●' : 0, 0);
        PlaceField(kCode, {left, y, left + width, y + field});
        y += field + S(12);
    } else {
        HideField(kCode);
    }

    AddText({left, y, left + width, y + S(22)}, NewPassword() && mode_ != Mode::Create ? L"New master password" : L"Master password", TextKind::Body);
    y += S(22);
    PlaceField(kPassword, {left, y, left + width, y + field}, S(36));
    Place(kReveal, left + width - S(36), y + S(2), S(34), field - S(4));
    y += field + S(12);

    if (NewPassword()) {
        AddText({left, y, left + width, y + S(22)}, L"Confirm it", TextKind::Body);
        y += S(22);
        PlaceField(kConfirm, {left, y, left + width, y + field});
        y += field + S(12);
        meter_ = {left, y, left + width, y + S(28)};
        y += S(28);
    } else {
        HideField(kConfirm);
        meter_ = {};
    }

    const RECT errorRect{left, y, left + width, y + S(20)};
    if (!error_.empty()) AddText(errorRect, error_, TextKind::Error, (unlock ? DT_CENTER : DT_LEFT) | DT_VCENTER | DT_SINGLELINE);
    y += S(20);

    const bool cancel = mode_ == Mode::Recover || mode_ == Mode::Change || mode_ == Mode::Verify;
    if (cancel) {
        const int half = (width - S(8)) / 2;
        Place(kCancel, left, y, half, field);
        Place(kSubmit, left + width - half, y, half, field);
    } else {
        Show(kCancel, false);
        Place(kSubmit, left, y, width, field);
    }
    y += field;
    if (unlock) {
        y += S(10);
        if (hello_) {
            Place(kHello, left, y, width, field);
            y += field + S(8);
        } else {
            Show(kHello, false);
        }
        Place(kForgot, left, y, width, field);
    } else {
        Show(kHello, false);
        Show(kForgot, false);
    }
}

void PasswordPanel::OnPaint(HDC dc) {
    if (meter_.right <= meter_.left) return;
    const std::wstring password = GetText(kPassword);
    const Theme& theme = style_->theme;
    const int barHeight = S(4);
    RECT bar{meter_.left, meter_.top + S(4), meter_.right, meter_.top + S(4) + barHeight};
    FillRounded(dc, bar, theme.divider, barHeight / 2);
    if (password.empty()) {
        SelectObject(dc, style_->small);
        SetTextColor(dc, theme.secondaryText);
        RECT text{meter_.left, bar.bottom + S(2), meter_.right, meter_.bottom};
        const std::wstring hint = L"At least " + std::to_wstring(kMinLength) + L" characters. A short sentence works well.";
        DrawTextW(dc, hint.c_str(), -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }
    const double bits = EstimateEntropy(password);
    const Strength strength = StrengthOf(bits);
    RECT filled = bar;
    filled.right = bar.left + (bar.right - bar.left) * (static_cast<int>(strength) + 1) / 5;
    FillRounded(dc, filled, StrengthColor(strength), barHeight / 2);
    SelectObject(dc, style_->small);
    SetTextColor(dc, theme.secondaryText);
    RECT text{meter_.left, bar.bottom + S(2), meter_.right, meter_.bottom};
    std::wstring label = std::wstring(StrengthName(strength)) + L" · " + CrackTime(bits) + L" to crack";
    if (password.size() < static_cast<size_t>(kMinLength)) label = L"Too short · at least " + std::to_wstring(kMinLength) + L" characters";
    DrawTextW(dc, label.c_str(), -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void PasswordPanel::Submit() {
    if (busy_) return;
    const std::wstring code = GetText(kCode);
    const std::wstring password = GetText(kPassword);
    if (mode_ == Mode::Recover && code.empty()) {
        SetError(L"Type your recovery code.");
        SetFocus(Item(kCode));
        return;
    }
    if (mode_ == Mode::Change && code.empty()) {
        SetError(L"Type your current master password.");
        SetFocus(Item(kCode));
        return;
    }
    if (password.empty()) {
        SetError(L"Type your master password.");
        SetFocus(Item(kPassword));
        return;
    }
    if (NewPassword()) {
        if (password.size() < static_cast<size_t>(kMinLength)) {
            SetError(L"Use at least " + std::to_wstring(kMinLength) + L" characters.");
            SetFocus(Item(kPassword));
            return;
        }
        if (GetText(kConfirm) != password) {
            SetError(L"The two passwords don't match.");
            SetFocus(Item(kConfirm));
            return;
        }
    }
    SetError(L"");
    if (onSubmit) onSubmit(mode_, code, password);
}

void PasswordPanel::OnCommand(int id, int code) {
    switch (id) {
        case kPassword:
            if (code == EN_CHANGE && meter_.right > meter_.left) {
                RECT dirty = meter_;
                InvalidateRect(window_, &dirty, FALSE);
            }
            break;
        case kReveal:
            reveal_ = !reveal_;
            for (int field : {kPassword, kConfirm}) {
                SendMessageW(Item(field), EM_SETPASSWORDCHAR, reveal_ ? 0 : L'●', 0);
                InvalidateRect(Item(field), nullptr, TRUE);
            }
            if (mode_ == Mode::Change) {
                SendMessageW(Item(kCode), EM_SETPASSWORDCHAR, reveal_ ? 0 : L'●', 0);
                InvalidateRect(Item(kCode), nullptr, TRUE);
            }
            SetButton(kReveal, reveal_ ? icons::kHide : icons::kView, L"");
            break;
        case kSubmit: Submit(); break;
        case kHello:
            if (onHello) onHello();
            break;
        case kForgot: SetMode(Mode::Recover); FocusFirst(); break;
        case kCancel:
            if (mode_ == Mode::Recover) {
                SetMode(Mode::Unlock);
                FocusFirst();
            } else if (onCancel) {
                onCancel();
            }
            break;
    }
}

}  // namespace obex
