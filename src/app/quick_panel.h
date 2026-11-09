#pragma once

#include "app/panel.h"
#include "core/generator.h"
#include "core/vault.h"

#include <functional>

namespace obex {

// The small window of the global shortcut: a new password with its
// strength and length, and optionally a name, website and username to keep
// it in the vault.
class QuickPanel : public Panel {
public:
    std::function<void(const std::wstring& password)> onCopy;
    // Copies and stores the entry; the main window unlocks first if needed.
    std::function<void(Entry entry)> onSave;
    std::function<void()> onOpenObex;
    std::function<void()> onClose;
    std::function<void()> onOptionsChanged;

    void SetOptions(GeneratorOptions* options) { options_ = options; }
    // A fresh password and empty fields, each time the window opens.
    void Start(bool unlocked);
    void Clear();
    void SetStatus(std::wstring status, bool error);
    // Empties the entry fields after a save, so Enter doesn't save it twice.
    void Saved(const std::wstring& name);
    void FocusFirst() { SetFocus(Item(103)); }
    int PreferredHeight() const;

    void OnDefault() override;
    void OnEscape() override {
        if (onClose) onClose();
    }

private:
    void CreateControls() override;
    void Layout() override;
    void OnCommand(int id, int code) override;
    void OnPaint(HDC dc) override;
    LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM lParam, bool& handled) override;
    void Generate();
    void Save();
    void SyncLength(bool fromSlider);

    GeneratorOptions* options_ = nullptr;
    bool unlocked_ = false;
    bool updating_ = false;
    std::wstring status_;
    bool statusError_ = false;
    RECT meter_{};
};

}  // namespace obex
