#include "app/vault_panel.h"

#include "core/hello.h"
#include "core/text.h"

#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>

namespace obex {

namespace {

enum ControlId {
    kSearch = 100,
    kList,
    kNew,
    kAddFirst,
    kImportFirst,
    kName,
    kUrl,
    kUser,
    kPassword,
    kNotes,
    kOpenUrl,
    kCopyUrl,
    kCopyUser,
    kCopyPassword,
    kReveal,
    kGenerate,
    kEdit,
    kDelete,
    kSave,
    kCancel,
};

constexpr int kListWidth = 290;
constexpr int kRowHeight = 52;
constexpr int kIconButton = 34;

bool Before(const Entry& a, const Entry& b) {
    return CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE | SORT_DIGITSASNUMBERS, a.name.c_str(),
                           static_cast<int>(a.name.size()), b.name.c_str(), static_cast<int>(b.name.size()), nullptr, nullptr,
                           0) == CSTR_LESS_THAN;
}

// A muted color per name, for the initial in the list.
COLORREF AvatarColor(const std::wstring& name) {
    static constexpr COLORREF kColors[] = {RGB(79, 109, 191), RGB(46, 139, 120), RGB(176, 96, 64), RGB(132, 86, 170),
                                           RGB(190, 140, 40), RGB(60, 130, 170), RGB(170, 70, 100), RGB(90, 140, 70)};
    unsigned hash = 0;
    for (wchar_t c : name) hash = hash * 31 + static_cast<unsigned>(towlower(c));
    return kColors[hash % std::size(kColors)];
}

LRESULT CALLBACK SearchProc(HWND edit, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (message == WM_KEYDOWN && wParam == VK_DOWN) {
        HWND list = GetDlgItem(GetParent(edit), kList);
        if (SendMessageW(list, LB_GETCOUNT, 0, 0) > 0) {
            if (SendMessageW(list, LB_GETCURSEL, 0, 0) < 0) {
                SendMessageW(list, LB_SETCURSEL, 0, 0);
                SendMessageW(GetParent(edit), WM_COMMAND, MAKEWPARAM(kList, LBN_SELCHANGE), reinterpret_cast<LPARAM>(list));
            }
            SetFocus(list);
        }
        return 0;
    }
    return DefSubclassProc(edit, message, wParam, lParam);
}

// Ctrl + C copies the password and Ctrl + B the username, as in KeePass.
LRESULT CALLBACK ListProc(HWND list, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (message == WM_KEYDOWN && GetKeyState(VK_CONTROL) < 0 && (wParam == 'C' || wParam == 'B')) {
        SendMessageW(GetParent(list), WM_COMMAND, MAKEWPARAM(wParam == 'C' ? kCopyPassword : kCopyUser, BN_CLICKED), 0);
        return 0;
    }
    return DefSubclassProc(list, message, wParam, lParam);
}

}  // namespace

void VaultPanel::CreateControls() {
    SetWindowSubclass(AddEdit(kSearch), SearchProc, 0, 0);
    SendMessageW(Item(kSearch), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search"));
    SetWindowSubclass(AddList(kList, kRowHeight), ListProc, 0, 0);
    AddButton(kNew, ButtonStyle::Standard, icons::kAdd, L"New password");
    AddButton(kAddFirst, ButtonStyle::Primary, icons::kAdd, L"Add a password");
    AddButton(kImportFirst, ButtonStyle::Standard, icons::kImport, L"Import from Chrome or Edge…");

    AddEdit(kName, ES_AUTOHSCROLL | ES_READONLY, TextKind::Title);
    AddEdit(kUrl, ES_AUTOHSCROLL | ES_READONLY);
    AddEdit(kUser, ES_AUTOHSCROLL | ES_READONLY);
    AddEdit(kPassword, ES_AUTOHSCROLL | ES_READONLY | ES_PASSWORD, TextKind::Mono);
    AddEdit(kNotes, ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_READONLY);
    SendMessageW(Item(kPassword), EM_SETPASSWORDCHAR, L'●', 0);

    AddButton(kOpenUrl, ButtonStyle::Subtle, icons::kOpen, L"");
    AddButton(kCopyUrl, ButtonStyle::Subtle, icons::kCopy, L"");
    AddButton(kCopyUser, ButtonStyle::Subtle, icons::kCopy, L"");
    AddButton(kCopyPassword, ButtonStyle::Subtle, icons::kCopy, L"");
    AddButton(kReveal, ButtonStyle::Subtle, icons::kView, L"");
    AddButton(kGenerate, ButtonStyle::Subtle, icons::kRefresh, L"");
    AddButton(kEdit, ButtonStyle::Standard, icons::kEdit, L"Edit");
    AddButton(kDelete, ButtonStyle::Subtle, icons::kDelete, L"Delete");
    AddButton(kSave, ButtonStyle::Primary, icons::kSave, L"Save");
    AddButton(kCancel, ButtonStyle::Standard, nullptr, L"Cancel");

    // Tooltips name the icon-only buttons.
    HWND tips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP, 0, 0, 0, 0, window_, nullptr,
                                instance_, nullptr);
    const std::pair<int, const wchar_t*> tipTexts[] = {
        {kOpenUrl, L"Open in browser"}, {kCopyUrl, L"Copy website"}, {kCopyUser, L"Copy username (Ctrl + B)"},
        {kCopyPassword, L"Copy password (Ctrl + C)"}, {kReveal, L"Show or hide"}, {kGenerate, L"Generate a new password"}};
    for (const auto& [id, text] : tipTexts) {
        TOOLINFOW tool{sizeof(tool)};
        tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tool.hwnd = window_;
        tool.uId = reinterpret_cast<UINT_PTR>(Item(id));
        tool.lpszText = const_cast<wchar_t*>(text);
        SendMessageW(tips, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    }
}

void VaultPanel::SetEntries(std::vector<Entry>* entries) {
    entries_ = entries;
    selected_ = -1;
    SetEditing(false);
    SetText(kSearch, L"");
    Reload();
}

void VaultPanel::Reset() {
    entries_ = nullptr;
    selected_ = -1;
    editing_ = false;
    isNew_ = false;
    for (int id : {kSearch, kName, kUrl, kUser, kPassword, kNotes}) WipeText(id);
    SendMessageW(Item(kList), LB_RESETCONTENT, 0, 0);
    visible_.clear();
}

void VaultPanel::FocusSearch() {
    if (editing_) return;
    SetFocus(Item(kSearch));
    SendMessageW(Item(kSearch), EM_SETSEL, 0, -1);
}

void VaultPanel::Reload() {
    if (entries_) std::stable_sort(entries_->begin(), entries_->end(), Before);
    Filter();
}

void VaultPanel::SelectNamed(const std::wstring& name) {
    if (!entries_ || editing_) return;
    SetText(kSearch, L"");
    for (int i = 0; i < static_cast<int>(entries_->size()); ++i) {
        if ((*entries_)[i].name == name) selected_ = i;
    }
    Filter();
}

void VaultPanel::Filter() {
    HWND list = Item(kList);
    visible_.clear();
    const std::wstring query = Trim(GetText(kSearch));
    if (entries_) {
        for (int i = 0; i < static_cast<int>(entries_->size()); ++i) {
            const Entry& e = (*entries_)[i];
            if (query.empty() || ContainsNoCase(e.name, query) || ContainsNoCase(e.url, query) || ContainsNoCase(e.username, query) ||
                ContainsNoCase(e.notes, query)) {
                visible_.push_back(i);
            }
        }
    }
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < visible_.size(); ++i) SendMessageW(list, LB_ADDSTRING, 0, static_cast<LPARAM>(i));
    const auto found = std::find(visible_.begin(), visible_.end(), selected_);
    if (found != visible_.end()) {
        SendMessageW(list, LB_SETCURSEL, found - visible_.begin(), 0);
    } else if (!editing_) {
        selected_ = -1;
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
    ShowEntry();
}

Entry* VaultPanel::Current() {
    return entries_ && selected_ >= 0 && selected_ < static_cast<int>(entries_->size()) ? &(*entries_)[selected_] : nullptr;
}

void VaultPanel::Select(int entry) {
    selected_ = entry;
    reveal_ = false;
    error_.clear();
    ShowEntry();
}

void VaultPanel::ShowEntry() {
    if (!editing_) {
        const Entry* e = Current();
        SetText(kName, e ? e->name : L"");
        SetText(kUrl, e ? e->url : L"");
        SetText(kUser, e ? e->username : L"");
        SetText(kPassword, e ? e->password : L"");
        SetText(kNotes, e ? ToWindowsLineBreaks(e->notes) : L"");
    }
    SendMessageW(Item(kPassword), EM_SETPASSWORDCHAR, reveal_ ? 0 : L'●', 0);
    SetButton(kReveal, reveal_ ? icons::kHide : icons::kView, L"");
    Layout();
    Repaint();
}

void VaultPanel::SetEditing(bool editing) {
    editing_ = editing;
    for (int id : {kName, kUrl, kUser, kPassword, kNotes}) SendMessageW(Item(id), EM_SETREADONLY, !editing, 0);
    EnableWindow(Item(kList), !editing);
    EnableWindow(Item(kSearch), !editing);
    EnableWindow(Item(kNew), !editing);
}

void VaultPanel::StartEdit(bool isNew) {
    isNew_ = isNew;
    error_.clear();
    if (isNew) {
        selected_ = -1;
        SendMessageW(Item(kList), LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
        for (int id : {kName, kUrl, kUser, kPassword, kNotes}) SetText(id, L"");
        if (onGenerate) SetText(kPassword, onGenerate());
        reveal_ = true;
    }
    SetEditing(true);
    ShowEntry();
    SetFocus(Item(kName));
    SendMessageW(Item(kName), EM_SETSEL, 0, -1);
}

void VaultPanel::SaveEdit() {
    std::wstring name = Trim(GetText(kName));
    const std::wstring url = Trim(GetText(kUrl));
    if (name.empty()) name = url;
    if (name.empty()) {
        error_ = L"Give it a name, for example the website.";
        Layout();
        Repaint();
        SetFocus(Item(kName));
        return;
    }
    Entry entry = isNew_ || !Current() ? Entry{} : *Current();
    entry.name = name;
    entry.url = url;
    entry.username = Trim(GetText(kUser));
    entry.password = GetText(kPassword);
    entry.notes = ToUnixLineBreaks(GetText(kNotes));
    entry.modified = UnixNow();
    if (isNew_ || !Current()) {
        entry.created = entry.modified;
        entries_->push_back(std::move(entry));
        selected_ = static_cast<int>(entries_->size()) - 1;
    } else {
        Current()->Wipe();
        *Current() = std::move(entry);
    }
    if (onChanged && !onChanged()) {
        error_ = L"Couldn't save the vault file. Your change is kept until Obex closes.";
    } else {
        error_.clear();
    }
    // Keep the saved entry selected after sorting.
    const std::wstring savedName = Current()->name;
    const int64_t savedTime = Current()->modified;
    SetEditing(false);
    isNew_ = false;
    reveal_ = false;
    std::stable_sort(entries_->begin(), entries_->end(), Before);
    for (int i = 0; i < static_cast<int>(entries_->size()); ++i) {
        if ((*entries_)[i].name == savedName && (*entries_)[i].modified == savedTime) selected_ = i;
    }
    SetText(kSearch, L"");
    Filter();
}

void VaultPanel::CancelEdit() {
    SetEditing(false);
    isNew_ = false;
    reveal_ = false;
    error_.clear();
    ShowEntry();
    SetFocus(Item(kList));
}

void VaultPanel::Delete() {
    Entry* e = Current();
    if (!e) return;
    const std::wstring question = L"Delete “" + e->name + L"”?";
    if (!Confirm(GetAncestor(window_, GA_ROOT), question.c_str(), L"Its password and notes are removed from your vault. This can't be undone.",
                 L"Delete")) {
        return;
    }
    e->Wipe();
    entries_->erase(entries_->begin() + selected_);
    selected_ = -1;
    if (onChanged && !onChanged()) error_ = L"Couldn't save the vault file.";
    Filter();
}

void VaultPanel::Copy(int field, int button) {
    const std::wstring text = GetText(field);
    if (text.empty() || !onCopy) return;
    onCopy(text, field == kPassword);
    FlashCopied(button);
}

void VaultPanel::OpenUrl() {
    std::wstring url = Trim(GetText(kUrl));
    if (url.empty()) return;
    if (url.find(L"://") == std::wstring::npos) url = L"https://" + url;
    // Only web links: never run a program or open a file from the vault.
    if (!StartsWithNoCase(url, L"https://") && !StartsWithNoCase(url, L"http://")) return;
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void VaultPanel::Layout() {
    ClearTexts();
    const int width = Width();
    const int height = Height();
    const int pad = S(20);

    // Left column: search, list, New.
    const int listWidth = S(kListWidth);
    PlaceField(kSearch, {pad, S(12), pad + listWidth, S(12) + S(36)}, S(28));
    AddText({pad + listWidth - S(32), S(12), pad + listWidth - S(8), S(48)}, icons::kSearch, TextKind::Icon,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    Place(kList, pad - S(4), S(60), listWidth + S(8), height - S(60) - S(64));
    Place(kNew, pad, height - S(52), listWidth, S(36));
    if (entries_ && entries_->empty()) {
        AddText({pad, S(60), pad + listWidth, S(100)}, L"No passwords yet", TextKind::Hint, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else if (visible_.empty()) {
        AddText({pad, S(60), pad + listWidth, S(100)}, L"Nothing matches your search", TextKind::Hint, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    divider_ = pad + listWidth + S(20);

    // Right column: the entry.
    const int left = divider_ + S(24);
    const int right = width - S(24);
    const int fieldWidth = right - left;
    const Entry* e = Current();
    const bool showEntry = editing_ || e;
    const int detailIds[] = {kName, kUrl, kUser, kPassword, kNotes, kOpenUrl, kCopyUrl, kCopyUser, kCopyPassword,
                             kReveal, kGenerate, kEdit, kDelete, kSave, kCancel};
    if (!showEntry) {
        for (int id : detailIds) HideField(id);
        const bool empty = entries_ && entries_->empty();
        const int top = height / 2 - S(80);
        AddText({left, top, right, top + S(56)}, icons::kKey, TextKind::BigIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        AddText({left, top + S(64), right, top + S(96)}, empty ? L"Your vault is empty" : L"Select a password", TextKind::Heading,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        AddText({left, top + S(96), right, top + S(120)},
                empty ? L"Add your first password, or bring them over from your browser." : L"Or press New password to add one.",
                TextKind::Hint, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (empty) {
            const int buttonWidth = S(260);
            const int x = left + (fieldWidth - buttonWidth) / 2;
            Place(kAddFirst, x, top + S(136), buttonWidth, S(36));
            Place(kImportFirst, x, top + S(180), buttonWidth, S(36));
        } else {
            Show(kAddFirst, false);
            Show(kImportFirst, false);
        }
        return;
    }
    Show(kAddFirst, false);
    Show(kImportFirst, false);

    const int icon = S(kIconButton);
    const int row = S(36);
    int y = S(12);
    const auto label = [&](const wchar_t* text) {
        AddText({left, y, right, y + S(20)}, text, TextKind::Hint);
        y += S(22);
    };
    const auto iconAt = [&](int id, int index) { Place(id, right - icon * (index + 1), y + (row - icon) / 2, icon, icon); };

    if (editing_) {
        label(L"Name");
        PlaceField(kName, {left, y, right, y + S(44)});
        y += S(44) + S(14);
    } else {
        PlaceField(kName, {left, y, right, y + S(44)});
        y += S(44) + S(14);
    }

    label(L"Website");
    PlaceField(kUrl, {left, y, right, y + row}, editing_ ? 0 : 2 * icon);
    if (editing_) {
        Show(kOpenUrl, false);
        Show(kCopyUrl, false);
    } else {
        iconAt(kCopyUrl, 0);
        iconAt(kOpenUrl, 1);
    }
    y += row + S(12);

    label(L"Username");
    PlaceField(kUser, {left, y, right, y + row}, editing_ ? 0 : icon);
    if (editing_) {
        Show(kCopyUser, false);
    } else {
        iconAt(kCopyUser, 0);
    }
    y += row + S(12);

    label(L"Password");
    PlaceField(kPassword, {left, y, right, y + row}, 2 * icon + (editing_ ? S(4) : 0));
    if (editing_) {
        Show(kCopyPassword, false);
        iconAt(kGenerate, 0);
        iconAt(kReveal, 1);
    } else {
        Show(kGenerate, false);
        iconAt(kCopyPassword, 0);
        iconAt(kReveal, 1);
    }
    y += row + S(12);

    label(L"Notes");
    const int buttonsTop = height - S(52);
    PlaceField(kNotes, {left, y, right, std::max(y + S(40), buttonsTop - S(28))});

    if (!error_.empty()) AddText({left, buttonsTop - S(26), right, buttonsTop - S(4)}, error_, TextKind::Error);
    if (editing_) {
        Show(kEdit, false);
        Show(kDelete, false);
        Place(kSave, right - S(110), buttonsTop, S(110), row);
        Place(kCancel, right - S(110) - S(8) - S(100), buttonsTop, S(100), row);
    } else {
        Show(kSave, false);
        Show(kCancel, false);
        Place(kEdit, right - S(100), buttonsTop, S(100), row);
        Place(kDelete, right - S(100) - S(8) - S(100), buttonsTop, S(100), row);
        if (e && e->modified) {
            wchar_t date[64] = L"";
            FILETIME file{};
            const ULONGLONG ticks = (static_cast<ULONGLONG>(e->modified) + 11644473600ULL) * 10000000ULL;
            file.dwLowDateTime = static_cast<DWORD>(ticks);
            file.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
            SYSTEMTIME utc, local;
            FileTimeToSystemTime(&file, &utc);
            SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local);
            GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, date, 64, nullptr);
            AddText({left, buttonsTop, right - S(220), buttonsTop + row}, std::wstring(L"Changed ") + date, TextKind::Hint);
        }
    }
}

void VaultPanel::OnPaint(HDC dc) {
    RECT line{divider_, S(12), divider_ + 1, Height() - S(12)};
    HBRUSH brush = CreateSolidBrush(style_->theme.divider);
    FillRect(dc, &line, brush);
    DeleteObject(brush);
}

bool VaultPanel::OnDrawItem(const DRAWITEMSTRUCT& item) {
    if (item.CtlID != kList) return false;
    HDC dc = item.hDC;
    const RECT& rect = item.rcItem;
    const Theme& theme = style_->theme;
    FillRect(dc, &rect, style_->background);
    if (item.itemID == static_cast<UINT>(-1) || !entries_ || item.itemID >= visible_.size()) return true;
    const Entry& e = (*entries_)[visible_[item.itemID]];
    const bool selected = item.itemState & ODS_SELECTED;
    RECT card = rect;
    InflateRect(&card, -S(4), -S(2));
    if (selected) {
        FillRounded(dc, card, theme.hover, S(4));
        RECT bar{card.left, card.top + S(14), card.left + S(3), card.bottom - S(14)};
        FillRounded(dc, bar, theme.accent, S(1));
    }

    const int avatar = S(32);
    RECT circle{card.left + S(12), card.top + (card.bottom - card.top - avatar) / 2, card.left + S(12) + avatar, 0};
    circle.bottom = circle.top + avatar;
    FillRounded(dc, circle, AvatarColor(e.name), S(6));
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ previous = SelectObject(dc, style_->heading);
    SetTextColor(dc, RGB(255, 255, 255));
    const std::wstring initial(1, e.name.empty() ? L'?' : static_cast<wchar_t>(towupper(e.name[0])));
    DrawTextW(dc, initial.c_str(), 1, &circle, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    const int textLeft = circle.right + S(12);
    const bool hasUser = !e.username.empty();
    RECT name{textLeft, card.top + S(hasUser ? 7 : 0), card.right - S(8), hasUser ? card.top + S(27) : card.bottom};
    SelectObject(dc, style_->text);
    SetTextColor(dc, theme.text);
    DrawTextW(dc, e.name.c_str(), -1, &name, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX | (hasUser ? 0 : DT_VCENTER));
    if (hasUser) {
        RECT user{textLeft, card.top + S(27), card.right - S(8), card.bottom - S(4)};
        SelectObject(dc, style_->small);
        SetTextColor(dc, theme.secondaryText);
        DrawTextW(dc, e.username.c_str(), -1, &user, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    SelectObject(dc, previous);
    return true;
}

LRESULT VaultPanel::OnMessage(UINT message, WPARAM, LPARAM lParam, bool& handled) {
    if (message == WM_MEASUREITEM) {
        reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)->itemHeight = S(kRowHeight);
        handled = true;
        return TRUE;
    }
    handled = false;
    return 0;
}

void VaultPanel::OnDefault() {
    HWND focus = GetFocus();
    if (editing_) {
        if (focus != Item(kNotes)) SaveEdit();
    } else if (focus == Item(kSearch) && !visible_.empty()) {
        SendMessageW(Item(kList), LB_SETCURSEL, 0, 0);
        Select(visible_[0]);
        SetFocus(Item(kList));
    } else if (focus == Item(kList) && Current()) {
        Copy(kPassword, kCopyPassword);
    }
}

void VaultPanel::OnEscape() {
    if (editing_) {
        CancelEdit();
    } else if (!GetText(kSearch).empty()) {
        SetText(kSearch, L"");
        FocusSearch();
    }
}

void VaultPanel::OnCommand(int id, int code) {
    switch (id) {
        case kSearch:
            if (code == EN_CHANGE && !editing_) Filter();
            break;
        case kList:
            if (code == LBN_SELCHANGE) {
                const LRESULT index = SendMessageW(Item(kList), LB_GETCURSEL, 0, 0);
                if (index >= 0 && index < static_cast<LRESULT>(visible_.size())) Select(visible_[index]);
            } else if (code == LBN_DBLCLK && Current()) {
                Copy(kPassword, kCopyPassword);
            }
            break;
        case kNew:
        case kAddFirst: StartEdit(true); break;
        case kImportFirst:
            if (onImport) onImport();
            break;
        case kOpenUrl: OpenUrl(); break;
        case kCopyUrl: Copy(kUrl, kCopyUrl); break;
        case kCopyUser:
            if (!editing_) Copy(kUser, kCopyUser);
            break;
        case kCopyPassword:
            if (!editing_) Copy(kPassword, kCopyPassword);
            break;
        case kReveal:
            reveal_ = !reveal_;
            ShowEntry();
            break;
        case kGenerate:
            if (onGenerate) {
                SetText(kPassword, onGenerate());
                reveal_ = true;
                ShowEntry();
            }
            break;
        case kEdit:
            if (Current()) StartEdit(false);
            break;
        case kDelete: Delete(); break;
        case kSave: SaveEdit(); break;
        case kCancel: CancelEdit(); break;
    }
}

}  // namespace obex
