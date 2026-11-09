#include "core/clipboard.h"

#include <cstring>

namespace obex {

namespace {

bool OpenClipboardWithRetry(HWND owner) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (OpenClipboard(owner)) return true;
        Sleep(25);
    }
    return false;
}

HGLOBAL CopyToGlobal(const void* data, size_t size) {
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory) return nullptr;
    std::memcpy(GlobalLock(memory), data, size);
    GlobalUnlock(memory);
    return memory;
}

void SetDword(const wchar_t* format, DWORD value) {
    if (HGLOBAL memory = CopyToGlobal(&value, sizeof(value))) {
        if (!SetClipboardData(RegisterClipboardFormatW(format), memory)) GlobalFree(memory);
    }
}

}  // namespace

DWORD SetClipboardText(HWND owner, std::wstring_view text, bool secret) {
    std::wstring terminated(text);
    HGLOBAL memory = CopyToGlobal(terminated.c_str(), (terminated.size() + 1) * sizeof(wchar_t));
    SecureZeroMemory(terminated.data(), terminated.size() * sizeof(wchar_t));
    if (!memory) return 0;

    if (!OpenClipboardWithRetry(owner)) {
        GlobalFree(memory);
        return 0;
    }
    EmptyClipboard();
    const bool stored = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
    if (!stored) GlobalFree(memory);
    if (stored && secret) {
        // The formats Windows and clipboard managers check for sensitive data.
        SetDword(L"ExcludeClipboardContentFromMonitorProcessing", 0);
        SetDword(L"CanIncludeInClipboardHistory", 0);
        SetDword(L"CanUploadToCloudClipboard", 0);
    }
    CloseClipboard();
    return stored ? GetClipboardSequenceNumber() : 0;
}

void ClearClipboardIfUnchanged(HWND owner, DWORD sequence) {
    if (sequence == 0 || GetClipboardSequenceNumber() != sequence) return;
    if (!OpenClipboardWithRetry(owner)) return;
    EmptyClipboard();
    CloseClipboard();
}

std::optional<std::wstring> GetClipboardText(HWND owner) {
    if (!OpenClipboardWithRetry(owner)) return std::nullopt;
    std::optional<std::wstring> text;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* buffer = static_cast<const wchar_t*>(GlobalLock(data))) {
            text = buffer;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

}  // namespace obex
