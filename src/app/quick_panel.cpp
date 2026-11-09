#include "app/quick_panel.h"

#include "core/hello.h"
#include "core/text.h"

namespace obex {

namespace {

enum ControlId { kPassword = 100, kSlider, kLength, kName, kUrl, kUser, kCopy, kAnother, kSave, kOpenObex };

constexpr int kPad = 18;
constexpr int kField = 34;

}  // namespace

void QuickPanel::CreateControls() {
    AddEdit(kPassword, ES_AUTOHSCROLL, TextKind::Mono);
    SendMessageW(Item(kPassword), EM_SETLIMITTEXT, 256, 0);
    AddSlider(kSlider, GeneratorOptions::kMinLength, GeneratorOptions::kMaxLength);
    AddEdit(kLength, ES_NUMBER | ES_CENTER);
    SendMessageW(Item(kLength), EM_SETLIMITTEXT, 3, 0);
    const std::pair<int, const wchar_t*> fields[] = {{kName, L"Name, e.g. GitHub"}, {kUrl, L"Website"}, {kUser, L"Username or email"}};
    for (const auto& [id, cue] : fields) {
        AddEdit(id);
        SendMessageW(Item(id), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cue));
    }
    AddButton(kCopy, ButtonStyle::Primary, icons::kCopy, L"Copy");
    AddButton(kAnother, ButtonStyle::Standard, icons::kRefresh, L"Another");
    AddButton(kSave, ButtonStyle::Standard, icons::kSave, L"Save to vault");
    AddButton(kOpenObex, ButtonStyle::Subtle, icons::kOpen, L"Open Obex");
}

void QuickPanel::Start(bool unlocked) {
    unlocked_ = unlocked;
    status_.clear();
    for (int id : {kName, kUrl, kUser}) SetText(id, L"");
    updating_ = true;
    SendMessageW(Item(kSlider), TBM_SETPOS, TRUE, options_->length);
    SetText(kLength, std::to_wstring(options_->length));
    updating_ = false;
    SetButton(kSave, unlocked ? icons::kSave : icons::kLock, unlocked ? L"Save to vault" : L"Unlock and save");
    Generate();
    Layout();
    Repaint();
}

void QuickPanel::Clear() {
    for (int id : {kPassword, kName, kUrl, kUser}) WipeText(id);
}

void QuickPanel::SetStatus(std::wstring status, bool error) {
    status_ = std::move(status);
    statusError_ = error;
    Layout();
    Repaint();
}

void QuickPanel::Saved(const std::wstring& name) {
    for (int id : {kName, kUrl, kUser}) SetText(id, L"");
    SetStatus(L"✓  Saved “" + name + L"” and copied the password.", false);
}

void QuickPanel::Generate() {
    std::wstring password = GeneratePassword(*options_);
    SetText(kPassword, password);
    Wipe(password);
    Repaint();
}

void QuickPanel::SyncLength(bool fromSlider) {
    if (updating_) return;
    updating_ = true;
    if (fromSlider) {
        options_->length = static_cast<int>(SendMessageW(Item(kSlider), TBM_GETPOS, 0, 0));
        SetText(kLength, std::to_wstring(options_->length));
    } else {
        wchar_t text[8]{};
        GetWindowTextW(Item(kLength), text, 8);
        const int length = _wtoi(text);
        if (length < GeneratorOptions::kMinLength || length > GeneratorOptions::kMaxLength) {
            updating_ = false;
            return;
        }
        options_->length = length;
        SendMessageW(Item(kSlider), TBM_SETPOS, TRUE, length);
    }
    updating_ = false;
    if (onOptionsChanged) onOptionsChanged();
    Generate();
}

void QuickPanel::Save() {
    Entry entry;
    entry.name = Trim(GetText(kName));
    entry.url = Trim(GetText(kUrl));
    entry.username = Trim(GetText(kUser));
    entry.password = GetText(kPassword);
    if (entry.name.empty()) entry.name = entry.url;
    if (entry.name.empty()) {
        SetStatus(L"Give it a name or a website to save it.", true);
        SetFocus(Item(kName));
        return;
    }
    if (entry.password.empty()) return;
    if (onSave) onSave(std::move(entry));
}

void QuickPanel::OnDefault() {
    // Enter copies; with a name, website or username typed it also saves.
    if (!Trim(GetText(kName) + GetText(kUrl) + GetText(kUser)).empty()) {
        Save();
        return;
    }
    const std::wstring password = GetText(kPassword);
    if (password.empty() || !onCopy) return;
    onCopy(password);
    if (onClose) onClose();
}

int QuickPanel::PreferredHeight() const {
    return S(kPad) + S(30) + S(10) + S(kField) + S(10) + S(40) + S(4) + S(32) + S(16) + S(22) + 3 * (S(kField) + S(8)) + S(22) + S(8) +
           S(kField) + S(kPad);
}

void QuickPanel::Layout() {
    ClearTexts();
    const int left = S(kPad);
    const int right = Width() - S(kPad);
    int y = S(kPad);

    AddText({left, y, right - S(130), y + S(30)}, L"Quick password", TextKind::Heading);
    Place(kOpenObex, right - S(124), y - S(2), S(124), S(32));
    y += S(30) + S(10);

    PlaceField(kPassword, {left, y, right, y + S(kField)});
    y += S(kField) + S(10);
    meter_ = {left, y, right, y + S(40)};
    y += S(40) + S(4);

    AddText({left, y, left + S(60), y + S(32)}, L"Length", TextKind::Body);
    Place(kSlider, left + S(56), y, right - left - S(56) - S(60), S(32));
    PlaceField(kLength, {right - S(52), y, right, y + S(32)});
    y += S(32) + S(16);

    AddText({left, y, right, y + S(22)}, L"Keep it in your vault (optional)", TextKind::Hint);
    y += S(22);
    for (int id : {kName, kUrl, kUser}) {
        PlaceField(id, {left, y, right, y + S(kField)});
        y += S(kField) + S(8);
    }
    if (!status_.empty()) AddText({left, y, right, y + S(22)}, status_, statusError_ ? TextKind::Error : TextKind::Success);
    y += S(22) + S(8);

    const int gap = S(8);
    const int saveWidth = S(140);
    const int half = (right - left - saveWidth - 2 * gap) / 2;
    Place(kCopy, left, y, half, S(kField));
    Place(kAnother, left + half + gap, y, half, S(kField));
    Place(kSave, right - saveWidth, y, saveWidth, S(kField));
}

void QuickPanel::OnPaint(HDC dc) {
    const std::wstring password = GetText(kPassword);
    PaintStrength(dc, meter_, password.empty() ? 0 : EstimateEntropy(password));
}

LRESULT QuickPanel::OnMessage(UINT message, WPARAM, LPARAM lParam, bool& handled) {
    handled = false;
    if (message == WM_HSCROLL && reinterpret_cast<HWND>(lParam) == Item(kSlider)) {
        handled = true;
        SyncLength(true);
    } else if (message == WM_NOTIFY) {
        auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(lParam);
        if (draw->hdr.idFrom == kSlider && draw->hdr.code == NM_CUSTOMDRAW) {
            handled = true;
            return DrawSlider(*draw);
        }
    }
    return 0;
}

void QuickPanel::OnCommand(int id, int code) {
    switch (id) {
        case kPassword:
            if (code == EN_CHANGE) InvalidateRect(window_, &meter_, FALSE);
            break;
        case kLength:
            if (code == EN_CHANGE) SyncLength(false);
            if (code == EN_KILLFOCUS) SetText(kLength, std::to_wstring(options_->length));
            break;
        case kCopy: {
            const std::wstring password = GetText(kPassword);
            if (!password.empty() && onCopy) {
                onCopy(password);
                FlashCopied(kCopy);
            }
            break;
        }
        case kAnother: Generate(); break;
        case kSave: Save(); break;
        case kOpenObex:
            if (onOpenObex) onOpenObex();
            break;
    }
}

}  // namespace obex
