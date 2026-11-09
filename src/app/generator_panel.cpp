#include "app/generator_panel.h"

#include "core/crypto.h"

#include <commctrl.h>

#include <algorithm>

namespace obex {

namespace {

enum ControlId { kSlider = 100, kLength, kUpper, kLower, kDigits, kSymbols, kAmbiguous, kCount, kList, kGenerate, kCopy };

constexpr int kOptionsWidth = 330;
constexpr int kRowHeight = 40;

int ReadNumber(HWND edit, int fallback) {
    wchar_t text[16]{};
    GetWindowTextW(edit, text, 16);
    return text[0] ? _wtoi(text) : fallback;
}

}  // namespace

void GeneratorPanel::CreateControls() {
    AddSlider(kSlider, GeneratorOptions::kMinLength, GeneratorOptions::kMaxLength);
    AddEdit(kLength, ES_NUMBER | ES_CENTER);
    SendMessageW(Item(kLength), EM_SETLIMITTEXT, 3, 0);
    static bool placeholder = false;
    for (int id : {kUpper, kLower, kDigits, kSymbols, kAmbiguous}) AddToggle(id, &placeholder);
    AddEdit(kCount, ES_NUMBER | ES_CENTER);
    SendMessageW(Item(kCount), EM_SETLIMITTEXT, 2, 0);
    AddList(kList, kRowHeight);
    AddButton(kGenerate, ButtonStyle::Primary, icons::kRefresh, L"Generate");
    AddButton(kCopy, ButtonStyle::Standard, icons::kCopy, L"Copy");
}

void GeneratorPanel::SetOptions(GeneratorOptions* options, int* count) {
    options_ = options;
    count_ = count;
    // Toggles point straight at the options.
    BindToggle(kUpper, &options->upper);
    BindToggle(kLower, &options->lower);
    BindToggle(kDigits, &options->digits);
    BindToggle(kSymbols, &options->symbols);
    BindToggle(kAmbiguous, &options->excludeAmbiguous);
    updating_ = true;
    SendMessageW(Item(kSlider), TBM_SETPOS, TRUE, options->length);
    SetText(kLength, std::to_wstring(options->length));
    SetText(kCount, std::to_wstring(*count));
    updating_ = false;
    Refresh();
    Generate();
}

void GeneratorPanel::Generate() {
    if (!options_) return;
    for (auto& password : passwords_) Wipe(password);
    passwords_.clear();
    for (int i = 0; i < *count_; ++i) passwords_.push_back(GeneratePassword(*options_));
    HWND list = Item(kList);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < passwords_.size(); ++i) SendMessageW(list, LB_ADDSTRING, 0, static_cast<LPARAM>(i));
    SendMessageW(list, LB_SETCURSEL, 0, 0);
    EnableWindow(Item(kCopy), !passwords_.empty() && !passwords_[0].empty());
    Repaint();
}

void GeneratorPanel::SyncLength(bool fromSlider) {
    if (updating_ || !options_) return;
    updating_ = true;
    if (fromSlider) {
        options_->length = static_cast<int>(SendMessageW(Item(kSlider), TBM_GETPOS, 0, 0));
        SetText(kLength, std::to_wstring(options_->length));
    } else {
        const int length = ReadNumber(Item(kLength), options_->length);
        if (length >= GeneratorOptions::kMinLength && length <= GeneratorOptions::kMaxLength) {
            options_->length = length;
            SendMessageW(Item(kSlider), TBM_SETPOS, TRUE, length);
        }
    }
    updating_ = false;
    if (onOptionsChanged) onOptionsChanged();
    Generate();
}

void GeneratorPanel::CopySelected() {
    const LRESULT index = SendMessageW(Item(kList), LB_GETCURSEL, 0, 0);
    if (index < 0 || index >= static_cast<LRESULT>(passwords_.size()) || passwords_[index].empty()) return;
    if (onCopy) onCopy(passwords_[index]);
    FlashCopied(kCopy);
}

void GeneratorPanel::Layout() {
    ClearTexts();
    const int pad = S(24);
    const int width = S(kOptionsWidth);
    int y = S(16);
    AddText({pad, y, pad + width, y + S(32)}, L"Password generator", TextKind::Title);
    y += S(32) + S(16);

    AddText({pad, y, pad + width, y + S(22)}, L"Length", TextKind::Body);
    y += S(24);
    Place(kSlider, pad - S(6), y, width - S(70), S(32));
    PlaceField(kLength, {pad + width - S(60), y, pad + width, y + S(32)});
    y += S(32) + S(14);

    const std::pair<int, const wchar_t*> rows[] = {{kUpper, L"Uppercase (A-Z)"},
                                                   {kLower, L"Lowercase (a-z)"},
                                                   {kDigits, L"Numbers (0-9)"},
                                                   {kSymbols, L"Symbols (!@#$%…)"},
                                                   {kAmbiguous, L"Avoid look-alikes (l 1 I O 0)"}};
    for (const auto& [id, label] : rows) {
        AddText({pad, y, pad + width - S(56), y + S(36)}, label, TextKind::Body);
        Place(id, pad + width - S(44), y + S(8), S(44), S(20));
        y += S(36);
    }
    y += S(10);
    AddText({pad, y, pad + width - S(70), y + S(32)}, L"How many", TextKind::Body);
    PlaceField(kCount, {pad + width - S(60), y, pad + width, y + S(32)});

    // Results column.
    const int left = pad + width + S(36);
    const int right = Width() - pad;
    y = S(16) + S(32) + S(16);
    meter_ = {left, y, right, y + S(46)};
    y += S(46) + S(10);
    const int buttonsTop = Height() - S(56);
    Place(kList, left - S(4), y, right - left + S(8), buttonsTop - y - S(12));
    Place(kGenerate, right - S(130), buttonsTop, S(130), S(36));
    Place(kCopy, right - S(130) - S(8) - S(110), buttonsTop, S(110), S(36));
    AddText({left, buttonsTop, right - S(260), buttonsTop + S(36)}, L"Click a password to copy it", TextKind::Hint);
}

void GeneratorPanel::OnPaint(HDC dc) {
    if (options_) PaintStrength(dc, meter_, GeneratedEntropy(*options_));
}

bool GeneratorPanel::OnDrawItem(const DRAWITEMSTRUCT& item) {
    if (item.CtlID != kList) return false;
    HDC dc = item.hDC;
    const RECT& rect = item.rcItem;
    const Theme& theme = style_->theme;
    FillRect(dc, &rect, style_->background);
    if (item.itemID >= passwords_.size()) return true;
    RECT card = rect;
    InflateRect(&card, -S(4), -S(2));
    const bool selected = item.itemState & ODS_SELECTED;
    FillRounded(dc, card, selected ? theme.hover : theme.pressed, S(4));
    if (selected) OutlineRounded(dc, card, theme.divider, S(4));
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ previous = SelectObject(dc, style_->mono);
    SetTextColor(dc, theme.text);
    RECT text{card.left + S(12), card.top, card.right - S(36), card.bottom};
    DrawTextW(dc, passwords_[item.itemID].c_str(), -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, style_->icon);
    SetTextColor(dc, theme.secondaryText);
    RECT icon{card.right - S(34), card.top, card.right - S(8), card.bottom};
    DrawTextW(dc, icons::kCopy, -1, &icon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, previous);
    return true;
}

LRESULT GeneratorPanel::OnMessage(UINT message, WPARAM wParam, LPARAM lParam, bool& handled) {
    handled = false;
    switch (message) {
        case WM_MEASUREITEM:
            reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)->itemHeight = S(kRowHeight);
            handled = true;
            return TRUE;
        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(lParam) == Item(kSlider)) SyncLength(true);
            handled = true;
            return 0;
        case WM_NOTIFY: {
            auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(lParam);
            if (draw->hdr.idFrom != kSlider || draw->hdr.code != NM_CUSTOMDRAW) break;
            handled = true;
            return DrawSlider(*draw);
        }
    }
    (void)wParam;
    return 0;
}

void GeneratorPanel::OnCommand(int id, int code) {
    switch (id) {
        case kLength:
            if (code == EN_CHANGE) SyncLength(false);
            if (code == EN_KILLFOCUS) SetText(kLength, std::to_wstring(options_->length));
            break;
        case kCount:
            if (code == EN_CHANGE && !updating_) {
                const int count = ReadNumber(Item(kCount), *count_);
                if (count >= 1 && count <= 50) {
                    *count_ = count;
                    if (onOptionsChanged) onOptionsChanged();
                    Generate();
                }
            }
            if (code == EN_KILLFOCUS) SetText(kCount, std::to_wstring(*count_));
            break;
        case kUpper:
        case kLower:
        case kDigits:
        case kSymbols:
        case kAmbiguous:
            if (onOptionsChanged) onOptionsChanged();
            Generate();
            break;
        case kList:
            if (code == LBN_SELCHANGE || code == LBN_DBLCLK) CopySelected();
            break;
        case kGenerate: Generate(); break;
        case kCopy: CopySelected(); break;
    }
}

}  // namespace obex
