#pragma once

#include "app/panel.h"

#include <functional>

namespace obex {

// Shows a new recovery code once. The user prints or saves the emergency
// sheet and types one group of the code back before going on.
class RecoveryPanel : public Panel {
public:
    std::function<void()> onDone;

    void ShowCode(const std::wstring& code);
    void Clear();
    void FocusFirst() { SetFocus(Item(100)); }
    void OnDefault() override { Finish(); }

private:
    void CreateControls() override;
    void Layout() override;
    void OnCommand(int id, int code) override;
    void OnPaint(HDC dc) override;
    void Finish();
    void Print();
    void SaveSheet();

    std::wstring code_;
    int group_ = 0;  // Zero-based group the user has to type
    std::wstring error_;
    RECT card_{};
    RECT warning_{};
};

// The text of the emergency sheet, also used for printing.
std::wstring EmergencySheet(const std::wstring& code);

}  // namespace obex
