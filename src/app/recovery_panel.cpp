#include "app/recovery_panel.h"

#include "core/crypto.h"
#include "core/paths.h"
#include "core/text.h"
#include "core/vault.h"

#include <commdlg.h>

#include <ctime>
#include <fstream>

namespace obex {

namespace {

enum ControlId { kConfirm = 100, kPrint, kSave, kContinue };

constexpr int kColumn = 520;

std::wstring Today() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    wchar_t date[32];
    wcsftime(date, 32, L"%Y-%m-%d", &local);
    return date;
}

const wchar_t kSheetSteps[] =
    L"If you forget your master password:\n"
    L"  1. Open Obex and click \"Forgot your master password?\".\n"
    L"  2. Type this code and choose a new master password.\n"
    L"  3. Obex gives you a new recovery code; this one stops working.\n\n"
    L"Keep this sheet somewhere safe and private. Anyone who has this code can open your vault.\n"
    L"If you lose both your master password and this code, nobody can recover your passwords.";

}  // namespace

std::wstring EmergencySheet(const std::wstring& code) {
    return L"OBEX PASSWORD VAULT - EMERGENCY SHEET\n\nRecovery code:\n\n    " + code + L"\n\nCreated on " + Today() + L"\n\n" + kSheetSteps + L"\n";
}

void RecoveryPanel::CreateControls() {
    AddEdit(kConfirm, ES_AUTOHSCROLL | ES_UPPERCASE, TextKind::Mono);
    SendMessageW(Item(kConfirm), EM_SETLIMITTEXT, 4, 0);
    AddButton(kPrint, ButtonStyle::Standard, icons::kPrint, L"Print…");
    AddButton(kSave, ButtonStyle::Standard, icons::kSave, L"Save as file…");
    AddButton(kContinue, ButtonStyle::Primary, nullptr, L"I've saved it, continue");
}

void RecoveryPanel::ShowCode(const std::wstring& code) {
    Wipe(code_);
    code_ = code;
    group_ = static_cast<int>(1 + RandomBelow(kRecoveryGroups - 1));
    error_.clear();
    WipeText(kConfirm);
    Layout();
    Repaint();
}

void RecoveryPanel::Clear() {
    Wipe(code_);
    code_.clear();
    WipeText(kConfirm);
}

void RecoveryPanel::Layout() {
    ClearTexts();
    const int width = std::min(S(kColumn), Width() - S(48));
    const int left = (Width() - width) / 2;
    const std::wstring intro =
        L"Print it or save it, and keep it somewhere safe. If you ever forget your master password, this code is the only way "
        L"back into your vault. It's shown only now.";
    const std::wstring warning =
        L"Anyone with this code can open your vault. If you lose both your master password and this code, nobody can recover "
        L"your passwords, not even VitomDev.";
    const int introHeight = TextHeight(intro, TextKind::Body, width);
    const int warningHeight = TextHeight(warning, TextKind::Body, width - S(56));
    const int total = S(36) + S(8) + introHeight + S(18) + S(80) + S(14) + S(36) + S(18) + warningHeight + S(24) + S(18) + S(22) +
                      S(36) + S(8) + S(20);
    int y = std::max(S(20), (Height() - total) / 2);

    AddText({left, y, left + width, y + S(36)}, L"Your recovery code", TextKind::Title);
    y += S(36) + S(8);
    AddText({left, y, left + width, y + introHeight}, intro, TextKind::Hint, DT_LEFT | DT_WORDBREAK);
    y += introHeight + S(18);

    card_ = {left, y, left + width, y + S(80)};
    AddText(card_, code_, TextKind::BigMono, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    y += S(80) + S(14);

    const int half = (width - S(8)) / 2;
    Place(kPrint, left, y, half, S(36));
    Place(kSave, left + width - half, y, half, S(36));
    y += S(36) + S(18);

    warning_ = {left, y, left + width, y + warningHeight + S(24)};
    AddText({left + S(14), y, left + S(40), y + warningHeight + S(24)}, icons::kWarning, TextKind::Icon, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    AddText({left + S(44), y + S(12), left + width - S(12), y + S(12) + warningHeight}, warning, TextKind::Body, DT_LEFT | DT_WORDBREAK);
    y += warningHeight + S(24) + S(18);

    AddText({left, y, left + width, y + S(22)},
            L"To check you saved it, type group " + std::to_wstring(group_ + 1) + L" of " + std::to_wstring(kRecoveryGroups) +
                L" (the " + std::to_wstring(group_ + 1) + (group_ == 1 ? L"nd" : group_ == 2 ? L"rd" : L"th") + L" block of four):",
            TextKind::Body);
    y += S(22);
    PlaceField(kConfirm, {left, y, left + S(120), y + S(36)});
    Place(kContinue, left + S(132), y, width - S(132), S(36));
    y += S(36) + S(8);
    if (!error_.empty()) AddText({left, y, left + width, y + S(20)}, error_, TextKind::Error);
}

void RecoveryPanel::OnPaint(HDC dc) {
    const Theme& theme = style_->theme;
    // Drawn under the texts would hide them; the card only needs an outline.
    OutlineRounded(dc, card_, theme.accent, S(8), S(2));
    OutlineRounded(dc, warning_, RGB(230, 160, 40), S(6));
}

void RecoveryPanel::Finish() {
    const std::wstring typed = NormalizeRecoveryCode(GetText(kConfirm));
    const std::wstring expected = NormalizeRecoveryCode(code_).substr(static_cast<size_t>(group_) * 4, 4);
    if (typed != expected) {
        error_ = typed.size() < 4 ? L"Type the four characters of that group." : L"That group doesn't match. Check your sheet.";
        Layout();
        Repaint();
        SetFocus(Item(kConfirm));
        return;
    }
    if (onDone) onDone();
}

void RecoveryPanel::Print() {
    PRINTDLGW dialog{sizeof(dialog)};
    dialog.hwndOwner = GetAncestor(window_, GA_ROOT);
    dialog.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION | PD_HIDEPRINTTOFILE;
    if (!PrintDlgW(&dialog)) return;
    HDC dc = dialog.hDC;
    DOCINFOW doc{sizeof(doc)};
    doc.lpszDocName = L"Obex emergency sheet";
    if (StartDocW(dc, &doc) > 0 && StartPage(dc) > 0) {
        const int dpi = GetDeviceCaps(dc, LOGPIXELSY);
        const int margin = dpi * 3 / 4;
        const int width = GetDeviceCaps(dc, HORZRES) - 2 * margin;
        const auto font = [dpi](int points, int weight, const wchar_t* face) {
            LOGFONTW f{};
            f.lfHeight = -MulDiv(points, dpi, 72);
            f.lfWeight = weight;
            wcscpy_s(f.lfFaceName, face);
            return CreateFontIndirectW(&f);
        };
        HFONT title = font(18, FW_BOLD, L"Segoe UI");
        HFONT body = font(11, FW_NORMAL, L"Segoe UI");
        HFONT code = font(22, FW_BOLD, L"Consolas");
        int y = margin;
        const auto draw = [&](HFONT f, const std::wstring& text, UINT format) {
            SelectObject(dc, f);
            RECT rect{margin, y, margin + width, y};
            DrawTextW(dc, text.c_str(), -1, &rect, format | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
            rect.right = margin + width;
            DrawTextW(dc, text.c_str(), -1, &rect, format | DT_WORDBREAK | DT_NOPREFIX);
            y = rect.bottom + dpi / 4;
        };
        draw(title, L"Obex Password Vault — Emergency sheet", DT_LEFT);
        draw(body, L"Recovery code, created on " + Today() + L":", DT_LEFT);
        RECT box{margin, y, margin + width, y + dpi};
        Rectangle(dc, box.left, box.top, box.right, box.bottom);
        SelectObject(dc, code);
        DrawTextW(dc, code_.c_str(), -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        y = box.bottom + dpi / 3;
        draw(body, kSheetSteps, DT_LEFT);
        EndPage(dc);
        EndDoc(dc);
        for (HFONT f : {title, body, code}) DeleteObject(f);
    }
    DeleteDC(dc);
    if (dialog.hDevMode) GlobalFree(dialog.hDevMode);
    if (dialog.hDevNames) GlobalFree(dialog.hDevNames);
}

void RecoveryPanel::SaveSheet() {
    wchar_t file[MAX_PATH] = L"Obex emergency sheet.txt";
    const std::wstring folder = DocumentsDirectory().wstring();
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = GetAncestor(window_, GA_ROOT);
    dialog.lpstrFilter = L"Text file (*.txt)\0*.txt\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrInitialDir = folder.empty() ? nullptr : folder.c_str();
    dialog.lpstrDefExt = L"txt";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) return;
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    const std::string text = "\xEF\xBB\xBF" + ToUtf8(ToWindowsLineBreaks(EmergencySheet(code_)));
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    error_ = out ? L"" : L"Couldn't save the file there. Try another folder.";
    Layout();
    Repaint();
}

void RecoveryPanel::OnCommand(int id, int) {
    switch (id) {
        case kPrint: Print(); break;
        case kSave: SaveSheet(); break;
        case kContinue: Finish(); break;
    }
}

}  // namespace obex
