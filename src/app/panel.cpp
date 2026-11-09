#include "app/panel.h"

#include "core/crypto.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>

namespace obex {

namespace {

constexpr wchar_t kClassName[] = L"ObexPanel";
constexpr UINT_PTR kCopiedTimer = 9001;
constexpr int kRadius = 4;

bool FontExists(const wchar_t* face) {
    LOGFONTW query{};
    query.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(query.lfFaceName, face);
    bool found = false;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(dc, &query, [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) {
        *reinterpret_cast<bool*>(found) = true;
        return 0;
    }, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

HFONT CreateMonoFont(UINT dpi, int points, int weight) {
    static const bool cascadia = FontExists(L"Cascadia Mono");
    LOGFONTW font{};
    font.lfHeight = -MulDiv(points, static_cast<int>(dpi), 72);
    font.lfWeight = weight;
    font.lfCharSet = DEFAULT_CHARSET;
    font.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(font.lfFaceName, cascadia ? L"Cascadia Mono" : L"Consolas");
    return CreateFontIndirectW(&font);
}

int FontHeight(HFONT font) {
    HDC dc = GetDC(nullptr);
    HGDIOBJ previous = SelectObject(dc, font);
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    SelectObject(dc, previous);
    ReleaseDC(nullptr, dc);
    return metrics.tmHeight;
}

}  // namespace

COLORREF StrengthColor(Strength strength) {
    switch (strength) {
        case Strength::VeryWeak: return RGB(232, 72, 85);
        case Strength::Weak: return RGB(240, 140, 50);
        case Strength::Fair: return RGB(230, 190, 40);
        case Strength::Strong: return RGB(80, 180, 90);
        case Strength::VeryStrong: return RGB(16, 160, 90);
    }
    return RGB(128, 128, 128);
}

void Style::Refresh(HWND window) {
    Release();
    theme = Theme::Current();
    dpi = GetDpiForWindow(window);
    background = CreateSolidBrush(theme.background);
    field = CreateSolidBrush(theme.pressed);
    title = CreateUiFont(dpi, 16, FW_SEMIBOLD);
    heading = CreateUiFont(dpi, 10, FW_SEMIBOLD);
    text = CreateUiFont(dpi, 10);
    small = CreateUiFont(dpi, 9);
    icon = CreateIconFont(dpi, 10);
    bigIcon = CreateIconFont(dpi, 36);
    mono = CreateMonoFont(dpi, 10, FW_NORMAL);
    bigMono = CreateMonoFont(dpi, 15, FW_SEMIBOLD);
}

void Style::Release() {
    for (HGDIOBJ object : {static_cast<HGDIOBJ>(background), static_cast<HGDIOBJ>(field), static_cast<HGDIOBJ>(title),
                           static_cast<HGDIOBJ>(heading), static_cast<HGDIOBJ>(text), static_cast<HGDIOBJ>(small),
                           static_cast<HGDIOBJ>(icon), static_cast<HGDIOBJ>(bigIcon), static_cast<HGDIOBJ>(mono),
                           static_cast<HGDIOBJ>(bigMono)}) {
        if (object) DeleteObject(object);
    }
    background = field = nullptr;
    title = heading = text = small = icon = bigIcon = mono = bigMono = nullptr;
}

bool Panel::Create(HINSTANCE instance, HWND parent, Style* style) {
    instance_ = instance;
    style_ = style;
    static const ATOM registered = [instance] {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        return RegisterClassExW(&wc);
    }();
    if (!registered) return false;
    window_ = CreateWindowExW(WS_EX_CONTROLPARENT, kClassName, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, parent, nullptr,
                              instance, this);
    if (!window_) return false;
    CreateControls();
    Refresh();
    return true;
}

void Panel::SetBounds(const RECT& bounds) {
    SetWindowPos(window_, nullptr, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    Layout();
    Repaint();
}

void Panel::SetVisible(bool visible) {
    if (visible) {
        Layout();
        Repaint();
    }
    ShowWindow(window_, visible ? SW_SHOW : SW_HIDE);
}

void Panel::Refresh() {
    for (HWND child = GetWindow(window_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        const int id = GetDlgCtrlID(child);
        const auto font = editFonts_.find(id);
        HFONT chosen = font != editFonts_.end() ? FontOf(font->second) : style_->text;
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(chosen), FALSE);
        wchar_t type[16]{};
        GetClassNameW(child, type, 16);
        if (_wcsicmp(type, L"EDIT") == 0) SendMessageW(child, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
        SetWindowTheme(child, style_->theme.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    }
    Layout();
    Repaint();
}

HWND Panel::AddEdit(int id, DWORD style, TextKind font) {
    HWND edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, 0, 0, 0, 0, window_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    SendMessageW(edit, EM_SETLIMITTEXT, (style & ES_MULTILINE) ? 20000 : 1000, 0);
    editFonts_[id] = font;
    return edit;
}

HWND Panel::AddButton(int id, ButtonStyle style, const wchar_t* icon, std::wstring label) {
    HWND button = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, window_,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    EnableHover(button);
    // The accessible name for screen readers.
    SetWindowTextW(button, label.c_str());
    buttons_[id] = {style, icon, std::move(label)};
    return button;
}

HWND Panel::AddToggle(int id, bool* value) {
    HWND button = AddButton(id, ButtonStyle::Subtle, nullptr, L"");
    buttons_.erase(id);
    toggles_[id] = value;
    return button;
}

HWND Panel::AddChip(int id, std::wstring label) {
    HWND button = AddButton(id, ButtonStyle::Subtle, nullptr, label);
    buttons_.erase(id);
    chips_[id] = {std::move(label), false};
    return button;
}

HWND Panel::AddList(int id, int itemHeight) {
    HWND list = CreateWindowExW(0, L"LISTBOX", L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_OWNERDRAWFIXED | LBS_NOTIFY |
                                    LBS_NOINTEGRALHEIGHT,
                                0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    SendMessageW(list, LB_SETITEMHEIGHT, 0, S(itemHeight));
    return list;
}

HWND Panel::AddSlider(int id, int minimum, int maximum) {
    HWND slider = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS | TBS_BOTH, 0,
                                  0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    SendMessageW(slider, TBM_SETRANGE, FALSE, MAKELPARAM(minimum, maximum));
    SendMessageW(slider, TBM_SETPAGESIZE, 0, 8);
    return slider;
}

LRESULT Panel::DrawSlider(const NMCUSTOMDRAW& draw) {
    if (draw.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (draw.dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
    const Theme& theme = style_->theme;
    HWND slider = draw.hdr.hwndFrom;
    if (draw.dwItemSpec == TBCD_CHANNEL) {
        RECT client;
        GetClientRect(slider, &client);
        FillRect(draw.hdc, &client, style_->background);
        RECT thumb{};
        SendMessageW(slider, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
        const int middle = (client.top + client.bottom) / 2;
        RECT track{draw.rc.left, middle - S(2), draw.rc.right, middle + S(2)};
        FillRounded(draw.hdc, track, theme.divider, S(2));
        track.right = (thumb.left + thumb.right) / 2;
        FillRounded(draw.hdc, track, theme.accent, S(2));
    } else if (draw.dwItemSpec == TBCD_THUMB) {
        const int size = S(18);
        const int cx = (draw.rc.left + draw.rc.right) / 2;
        const int cy = (draw.rc.top + draw.rc.bottom) / 2;
        RECT knob{cx - size / 2, cy - size / 2, cx + size / 2, cy + size / 2};
        FillRounded(draw.hdc, knob, theme.background, size / 2);
        OutlineRounded(draw.hdc, knob, theme.divider, size / 2);
        InflateRect(&knob, -S(5), -S(5));
        FillRounded(draw.hdc, knob, theme.accent, (knob.right - knob.left) / 2);
    }
    return CDRF_SKIPDEFAULT;
}

void Panel::PaintStrength(HDC dc, const RECT& rect, double bits) {
    const Theme& theme = style_->theme;
    const Strength strength = StrengthOf(bits);
    SelectObject(dc, style_->heading);
    SetTextColor(dc, theme.text);
    RECT title{rect.left, rect.top, rect.right, rect.top + S(22)};
    const std::wstring heading = bits > 0 ? std::wstring(StrengthName(strength)) : L"Choose at least one kind of character";
    DrawTextW(dc, heading.c_str(), -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (bits <= 0) return;
    SelectObject(dc, style_->small);
    SetTextColor(dc, theme.secondaryText);
    const std::wstring detail = CrackTime(bits) + L" to crack · " + std::to_wstring(static_cast<int>(bits)) + L" bits";
    DrawTextW(dc, detail.c_str(), -1, &title, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    RECT bar{rect.left, rect.top + S(30), rect.right, rect.top + S(36)};
    FillRounded(dc, bar, theme.divider, S(3));
    bar.right = bar.left + (bar.right - bar.left) * (static_cast<int>(strength) + 1) / 5;
    FillRounded(dc, bar, StrengthColor(strength), S(3));
}

void Panel::SetButton(int id, const wchar_t* icon, std::wstring label) {
    auto& info = buttons_[id];
    info.icon = icon;
    SetWindowTextW(Item(id), label.c_str());
    info.label = std::move(label);
    InvalidateRect(Item(id), nullptr, FALSE);
}

void Panel::SetChip(int id, bool selected) {
    chips_[id].second = selected;
    InvalidateRect(Item(id), nullptr, FALSE);
}

void Panel::FlashCopied(int id) {
    if (std::find(copied_.begin(), copied_.end(), id) == copied_.end()) copied_.push_back(id);
    InvalidateRect(Item(id), nullptr, FALSE);
    SetTimer(window_, kCopiedTimer, 1500, nullptr);
}

void Panel::Place(int id, int x, int y, int width, int height) {
    SetWindowPos(Item(id), nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    Show(id, true);
}

void Panel::Place(int id, const RECT& rect) { Place(id, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top); }

void Panel::PlaceField(int id, const RECT& frame, int rightInset) {
    HWND edit = Item(id);
    const bool multiline = (GetWindowLongW(edit, GWL_STYLE) & ES_MULTILINE) != 0;
    const bool readOnly = (GetWindowLongW(edit, GWL_STYLE) & ES_READONLY) != 0;
    const int inset = readOnly ? 0 : S(10);
    if (readOnly) {
        frames_.erase(id);
    } else {
        frames_[id] = frame;
    }
    if (multiline) {
        Place(id, frame.left + inset, frame.top + S(7), frame.right - frame.left - inset - S(4) - rightInset,
              frame.bottom - frame.top - S(12));
    } else {
        const auto font = reinterpret_cast<HFONT>(SendMessageW(edit, WM_GETFONT, 0, 0));
        const int height = FontHeight(font ? font : style_->text) + S(2);
        Place(id, frame.left + inset, frame.top + (frame.bottom - frame.top - height) / 2,
              frame.right - frame.left - 2 * inset - rightInset, height);
    }
}

void Panel::HideField(int id) {
    frames_.erase(id);
    Show(id, false);
}

void Panel::Show(int id, bool visible) {
    HWND control = Item(id);
    if (!visible && GetFocus() == control) SetFocus(window_);
    ShowWindow(control, visible ? SW_SHOWNA : SW_HIDE);
}

void Panel::ClearTexts() { texts_.clear(); }

void Panel::AddText(const RECT& rect, std::wstring text, TextKind kind, UINT format) {
    texts_.push_back({rect, std::move(text), kind, format});
}

int Panel::TextHeight(std::wstring_view text, TextKind kind, int width) const {
    HDC dc = GetDC(window_);
    HGDIOBJ previous = SelectObject(dc, FontOf(kind));
    RECT rect{0, 0, width, 0};
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &rect, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    SelectObject(dc, previous);
    ReleaseDC(window_, dc);
    return rect.bottom;
}

void Panel::Repaint() { InvalidateRect(window_, nullptr, FALSE); }

std::wstring Panel::GetText(int id) const {
    HWND control = Item(id);
    std::wstring text(static_cast<size_t>(GetWindowTextLengthW(control)) + 1, L'\0');
    text.resize(static_cast<size_t>(GetWindowTextW(control, text.data(), static_cast<int>(text.size()))));
    return text;
}

void Panel::SetText(int id, std::wstring_view text) {
    const std::wstring value(text);
    SetWindowTextW(Item(id), value.c_str());
}

void Panel::WipeText(int id) {
    // Overwrite the edit's own buffer before emptying it.
    HWND edit = Item(id);
    const int length = GetWindowTextLengthW(edit);
    if (length > 0) SetWindowTextW(edit, std::wstring(static_cast<size_t>(length), L' ').c_str());
    SetWindowTextW(edit, L"");
}

int Panel::Width() const {
    RECT client;
    GetClientRect(window_, &client);
    return client.right;
}

int Panel::Height() const {
    RECT client;
    GetClientRect(window_, &client);
    return client.bottom;
}

HFONT Panel::FontOf(TextKind kind) const {
    switch (kind) {
        case TextKind::Title: return style_->title;
        case TextKind::Heading: return style_->heading;
        case TextKind::Hint: return style_->small;
        case TextKind::Mono: return style_->mono;
        case TextKind::BigMono: return style_->bigMono;
        case TextKind::Icon: return style_->icon;
        case TextKind::BigIcon: return style_->bigIcon;
        default: return style_->text;
    }
}

void Panel::Paint() {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window_, &paint);
    RECT client;
    GetClientRect(window_, &client);
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, std::max<LONG>(1, client.right), std::max<LONG>(1, client.bottom));
    HGDIOBJ previousBitmap = SelectObject(buffer, bitmap);
    FillRect(buffer, &client, style_->background);
    SetBkMode(buffer, TRANSPARENT);
    const Theme& theme = style_->theme;

    HWND focus = GetFocus();
    for (const auto& [id, frame] : frames_) {
        HWND edit = Item(id);
        if (!IsWindowVisible(edit)) continue;
        const bool focused = focus == edit;
        FillRounded(buffer, frame, theme.pressed, S(kRadius));
        OutlineRounded(buffer, frame, focused ? theme.accent : theme.divider, S(kRadius), focused ? 2 : 1);
    }
    HGDIOBJ previousFont = SelectObject(buffer, style_->text);
    for (auto& item : texts_) {
        COLORREF color = theme.text;
        switch (item.kind) {
            case TextKind::Hint: color = theme.secondaryText; break;
            case TextKind::Error: color = kErrorColor; break;
            case TextKind::Success: color = kSuccessColor; break;
            case TextKind::BigIcon: color = theme.accent; break;
            default: break;
        }
        SelectObject(buffer, FontOf(item.kind));
        SetTextColor(buffer, color);
        DrawTextW(buffer, item.text.c_str(), -1, &item.rect, item.format | DT_NOPREFIX | (item.format & DT_SINGLELINE ? DT_END_ELLIPSIS : 0));
    }
    OnPaint(buffer);

    BitBlt(dc, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
    SelectObject(buffer, previousFont);
    SelectObject(buffer, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(buffer);
    EndPaint(window_, &paint);
}

bool Panel::DrawShared(const DRAWITEMSTRUCT& item) {
    const int id = static_cast<int>(item.CtlID);
    const Theme& theme = style_->theme;
    HDC dc = item.hDC;
    const RECT& rect = item.rcItem;
    const bool hot = IsHot(item.hwndItem);

    if (const auto button = buttons_.find(id); button != buttons_.end()) {
        const bool copied = std::find(copied_.begin(), copied_.end(), id) != copied_.end();
        const ButtonInfo& info = button->second;
        const wchar_t* icon = copied ? icons::kCheck : info.icon;
        const std::wstring label = copied && !info.label.empty() && info.icon ? L"Copied" : info.label;
        DrawButton(item, theme, {style_->text, style_->icon}, info.style, icon, label.c_str(), S(kRadius),
                   copied ? kSuccessColor : CLR_INVALID);
        return true;
    }
    if (const auto toggle = toggles_.find(id); toggle != toggles_.end()) {
        const bool on = *toggle->second;
        FillRect(dc, &rect, style_->background);
        const int height = rect.bottom - rect.top;
        const bool enabled = IsWindowEnabled(item.hwndItem);
        if (on) {
            FillRounded(dc, rect, enabled ? theme.accent : theme.divider, height / 2);
        } else {
            if (hot) FillRounded(dc, rect, theme.hover, height / 2);
            OutlineRounded(dc, rect, theme.secondaryText, height / 2);
        }
        const int knob = S(12);
        const int knobX = on ? rect.right - S(4) - knob : rect.left + S(4);
        RECT knobRect{knobX, rect.top + (height - knob) / 2, knobX + knob, rect.top + (height + knob) / 2};
        FillRounded(dc, knobRect, on ? theme.accentText : theme.secondaryText, knob / 2);
        if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
            RECT focus = rect;
            InflateRect(&focus, 2, 2);
            DrawFocusRect(dc, &focus);
        }
        return true;
    }
    if (const auto chip = chips_.find(id); chip != chips_.end()) {
        const bool selected = chip->second.second;
        FillRect(dc, &rect, style_->background);
        const int pill = (rect.bottom - rect.top) / 2;
        if (selected) {
            FillRounded(dc, rect, theme.accent, pill);
        } else {
            if (hot) FillRounded(dc, rect, theme.hover, pill);
            OutlineRounded(dc, rect, theme.divider, pill);
        }
        SetBkMode(dc, TRANSPARENT);
        HGDIOBJ previous = SelectObject(dc, style_->text);
        SetTextColor(dc, selected ? theme.accentText : theme.text);
        RECT text = rect;
        DrawTextW(dc, chip->second.first.c_str(), -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, previous);
        if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
            RECT focus = rect;
            InflateRect(&focus, -3, -3);
            DrawFocusRect(dc, &focus);
        }
        return true;
    }
    return false;
}

LRESULT Panel::OnMessage(UINT, WPARAM, LPARAM, bool& handled) {
    handled = false;
    return 0;
}

LRESULT CALLBACK Panel::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* self = static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<Panel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return self ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Panel::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    bool handled = false;
    const LRESULT result = OnMessage(message, wParam, lParam, handled);
    if (handled) return result;

    switch (message) {
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);
            if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {
                if (const auto frame = frames_.find(id); frame != frames_.end()) {
                    RECT dirty = frame->second;
                    InflateRect(&dirty, 2, 2);
                    InvalidateRect(window_, &dirty, FALSE);
                }
            }
            if (const auto toggle = toggles_.find(id); toggle != toggles_.end() && code == BN_CLICKED) {
                *toggle->second = !*toggle->second;
                InvalidateRect(Item(id), nullptr, FALSE);
            }
            OnCommand(id, code);
            return 0;
        }
        case WM_DRAWITEM: {
            const auto& item = *reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (!OnDrawItem(item)) DrawShared(item);
            return TRUE;
        }
        case WM_PAINT:
            Paint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORLISTBOX: {
            HWND control = reinterpret_cast<HWND>(lParam);
            const bool field = message == WM_CTLCOLOREDIT && frames_.count(GetDlgCtrlID(control));
            SetBkColor(reinterpret_cast<HDC>(wParam), field ? style_->theme.pressed : style_->theme.background);
            SetTextColor(reinterpret_cast<HDC>(wParam), style_->theme.text);
            return reinterpret_cast<LRESULT>(field ? style_->field : style_->background);
        }
        case WM_TIMER:
            if (wParam == kCopiedTimer) {
                KillTimer(window_, kCopiedTimer);
                const auto copied = std::move(copied_);
                copied_.clear();
                for (int id : copied) InvalidateRect(Item(id), nullptr, FALSE);
                return 0;
            }
            break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

bool Confirm(HWND owner, const wchar_t* title, const wchar_t* text, const wchar_t* yes, bool warning) {
    const TASKDIALOG_BUTTON buttons[] = {{IDYES, yes}, {IDCANCEL, L"Cancel"}};
    TASKDIALOGCONFIG config{sizeof(config)};
    config.hwndParent = owner;
    config.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW | TDF_ALLOW_DIALOG_CANCELLATION;
    config.pszWindowTitle = L"Obex Password Vault";
    config.pszMainIcon = warning ? TD_WARNING_ICON : nullptr;
    config.pszMainInstruction = title;
    config.pszContent = text;
    config.cButtons = 2;
    config.pButtons = buttons;
    config.nDefaultButton = warning ? IDCANCEL : IDYES;
    int pressed = 0;
    return SUCCEEDED(TaskDialogIndirect(&config, &pressed, nullptr, nullptr)) && pressed == IDYES;
}

}  // namespace obex
