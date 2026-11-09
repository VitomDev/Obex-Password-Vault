#pragma once

#include "app/panel.h"
#include "core/vault.h"

#include <functional>
#include <vector>

namespace obex {

// The vault: search and list on the left, the chosen entry on the right,
// shown read-only with copy buttons or edited in place.
class VaultPanel : public Panel {
public:
    // Saves the vault after a change; false if it could not be written.
    std::function<bool()> onChanged;
    std::function<void(const std::wstring& text, bool secret)> onCopy;
    std::function<std::wstring()> onGenerate;
    std::function<void()> onImport;

    void SetEntries(std::vector<Entry>* entries);
    // Forgets everything shown, before the vault is locked.
    void Reset();
    void FocusSearch();
    void Reload();
    // Selects the entry with this name, e.g. one just added.
    void SelectNamed(const std::wstring& name);
    bool IsEditing() const { return editing_; }

    void OnDefault() override;
    void OnEscape() override;

private:
    void CreateControls() override;
    void Layout() override;
    void OnCommand(int id, int code) override;
    bool OnDrawItem(const DRAWITEMSTRUCT& item) override;
    void OnPaint(HDC dc) override;
    LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM lParam, bool& handled) override;

    void Filter();
    void Select(int entry);
    void ShowEntry();
    void StartEdit(bool isNew);
    void SaveEdit();
    void CancelEdit();
    void Delete();
    void Copy(int field, int button);
    void OpenUrl();
    void SetEditing(bool editing);
    Entry* Current();

    std::vector<Entry>* entries_ = nullptr;
    std::vector<int> visible_;
    int selected_ = -1;
    bool editing_ = false;
    bool isNew_ = false;
    bool reveal_ = false;
    std::wstring error_;
    int divider_ = 0;
};

}  // namespace obex
