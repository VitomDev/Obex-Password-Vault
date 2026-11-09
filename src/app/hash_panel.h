#pragma once

#include "app/panel.h"

#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <thread>

namespace obex {

// MD5, SHA-1, SHA-256 and SHA-512 of a text or a file, a comparison with an
// expected hash and the check of a .sha256 list. Files are hashed on a
// worker thread, all four at once in a single read.
class HashPanel : public Panel {
public:
    ~HashPanel() override;
    std::function<void(const std::wstring& text)> onCopy;

    void Stop();

private:
    void CreateControls() override;
    void Layout() override;
    void OnCommand(int id, int code) override;
    LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM lParam, bool& handled) override;

    void SetFileMode(bool file);
    void HashText();
    void ChooseFile();
    void HashFile(const std::filesystem::path& path);
    void ChooseChecksumList();
    void Compare();
    void SetStatus(std::wstring status, TextKind kind);
    template <typename Work>
    void Run(Work work);

    bool fileMode_ = false;
    std::filesystem::path file_;
    std::wstring status_;
    TextKind statusKind_ = TextKind::Hint;
    std::wstring match_;
    TextKind matchKind_ = TextKind::Hint;
    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::function<void()> done_;
};

}  // namespace obex
