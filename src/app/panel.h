#pragma once

#include "app/theme.h"
#include "core/generator.h"

#include <windows.h>
#include <commctrl.h>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace obex {

// Colors, brushes and fonts shared by every panel of the main window.
struct Style {
    Theme theme{};
    UINT dpi = 96;
    HBRUSH background = nullptr;
    HBRUSH field = nullptr;
    HFONT title = nullptr;
    HFONT heading = nullptr;
    HFONT text = nullptr;
    HFONT small = nullptr;
    HFONT icon = nullptr;
    HFONT bigIcon = nullptr;
    HFONT mono = nullptr;
    HFONT bigMono = nullptr;

    void Refresh(HWND window);
    void Release();
    int Scale(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
};

inline constexpr COLORREF kErrorColor = RGB(232, 72, 85);
inline constexpr COLORREF kSuccessColor = RGB(16, 160, 90);
COLORREF StrengthColor(Strength strength);

enum class TextKind { Title, Heading, Body, Hint, Error, Success, Mono, BigMono, Icon, BigIcon };

// A child window holding one screen of the main window (the vault, the
// generator...). It owns the painting of the shared pieces: text, frames
// around borderless edits, owner-drawn buttons, switches and chips.
class Panel {
public:
    virtual ~Panel() = default;
    bool Create(HINSTANCE instance, HWND parent, Style* style);
    HWND window() const { return window_; }
    void SetBounds(const RECT& bounds);
    void SetVisible(bool visible);
    bool IsVisible() const { return IsWindowVisible(window_) != FALSE; }
    // After a theme or DPI change.
    void Refresh();
    // Enter and Escape, forwarded by the main window.
    virtual void OnDefault() {}
    virtual void OnEscape() {}

protected:
    virtual void CreateControls() = 0;
    virtual void Layout() = 0;
    virtual void OnCommand(int /*id*/, int /*code*/) {}
    virtual bool OnDrawItem(const DRAWITEMSTRUCT& /*item*/) { return false; }
    virtual void OnPaint(HDC /*dc*/) {}
    virtual LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM lParam, bool& handled);

    HWND AddEdit(int id, DWORD style = ES_AUTOHSCROLL, TextKind font = TextKind::Body);
    HWND AddButton(int id, ButtonStyle style, const wchar_t* icon, std::wstring label);
    HWND AddToggle(int id, bool* value);
    void BindToggle(int id, bool* value) { toggles_[id] = value; }
    HWND AddChip(int id, std::wstring label);
    HWND AddList(int id, int itemHeight);
    HWND AddSlider(int id, int minimum, int maximum);
    // NM_CUSTOMDRAW of a slider: a thin track and an accent knob.
    LRESULT DrawSlider(const NMCUSTOMDRAW& draw);
    // Strength name, crack time and a colored bar for `bits` of entropy.
    void PaintStrength(HDC dc, const RECT& rect, double bits);
    void SetButton(int id, const wchar_t* icon, std::wstring label);
    void SetChip(int id, bool selected);
    // Shows the copied check on a button for a moment.
    void FlashCopied(int id);

    void Place(int id, int x, int y, int width, int height);
    void Place(int id, const RECT& rect);
    // An edit inside a painted frame; `rightInset` leaves room for icon buttons.
    void PlaceField(int id, const RECT& frame, int rightInset = 0);
    void HideField(int id);
    void Show(int id, bool visible);
    void ClearTexts();
    void AddText(const RECT& rect, std::wstring text, TextKind kind, UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    // Height that `text` needs when wrapped to `width`.
    int TextHeight(std::wstring_view text, TextKind kind, int width) const;
    void Repaint();

    HWND Item(int id) const { return GetDlgItem(window_, id); }
    std::wstring GetText(int id) const;
    void SetText(int id, std::wstring_view text);
    void WipeText(int id);
    int S(int value) const { return style_->Scale(value); }
    int Width() const;
    int Height() const;
    HFONT FontOf(TextKind kind) const;

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    Style* style_ = nullptr;

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void Paint();
    bool DrawShared(const DRAWITEMSTRUCT& item);

    struct ButtonInfo {
        ButtonStyle style;
        const wchar_t* icon;
        std::wstring label;
    };
    struct TextItem {
        RECT rect;
        std::wstring text;
        TextKind kind;
        UINT format;
    };
    std::map<int, ButtonInfo> buttons_;
    std::map<int, bool*> toggles_;
    std::map<int, std::pair<std::wstring, bool>> chips_;
    std::map<int, RECT> frames_;
    std::map<int, TextKind> editFonts_;
    std::vector<int> copied_;
    std::vector<TextItem> texts_;
};

// A themed yes/no question (TaskDialog). Returns true for the first button.
bool Confirm(HWND owner, const wchar_t* title, const wchar_t* text, const wchar_t* yes, bool warning = false);

}  // namespace obex
