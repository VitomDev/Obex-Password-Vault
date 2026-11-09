#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

namespace obex {

// Puts Unicode text on the clipboard. A `secret` is kept out of the Windows
// clipboard history (Win + V) and cloud clipboard. Returns the clipboard
// sequence number afterwards, or 0 on failure.
DWORD SetClipboardText(HWND owner, std::wstring_view text, bool secret);

// Empties the clipboard only if nothing was copied since `sequence`.
void ClearClipboardIfUnchanged(HWND owner, DWORD sequence);

std::optional<std::wstring> GetClipboardText(HWND owner);

}  // namespace obex
