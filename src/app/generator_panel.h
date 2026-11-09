#pragma once

#include "app/panel.h"
#include "core/generator.h"

#include <functional>
#include <vector>

namespace obex {

// Length, kinds of characters and how many; the strength of the result and
// the list of new passwords, each one a click away from the clipboard.
class GeneratorPanel : public Panel {
public:
    std::function<void(const std::wstring& text)> onCopy;
    std::function<void()> onOptionsChanged;

    void SetOptions(GeneratorOptions* options, int* count);
    void Generate();
    void OnDefault() override { Generate(); }

private:
    void CreateControls() override;
    void Layout() override;
    void OnCommand(int id, int code) override;
    bool OnDrawItem(const DRAWITEMSTRUCT& item) override;
    void OnPaint(HDC dc) override;
    LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM lParam, bool& handled) override;
    void SyncLength(bool fromSlider);
    void CopySelected();

    GeneratorOptions* options_ = nullptr;
    int* count_ = nullptr;
    std::vector<std::wstring> passwords_;
    RECT meter_{};
    bool updating_ = false;
};

}  // namespace obex
