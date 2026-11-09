#include "app/hash_panel.h"

#include "core/crypto.h"
#include "core/text.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <fstream>
#include <memory>

namespace obex {

namespace fs = std::filesystem;

namespace {

enum ControlId { kTextChip = 100, kFileChip, kText, kChooseFile, kExpected, kCheckList, kResultFirst = 120, kCopyFirst = 130 };

constexpr UINT WM_APP_HASHED = WM_APP + 40;
constexpr std::array<const wchar_t*, 4> kNames{L"MD5", L"SHA-1", L"SHA-256", L"SHA-512"};
constexpr std::array<HashAlgorithm, 4> kAlgorithms{HashAlgorithm::Md5, HashAlgorithm::Sha1, HashAlgorithm::Sha256, HashAlgorithm::Sha512};

using Digests = std::array<std::wstring, 4>;

// All four hashes of a file in one pass; empty if it can't be read.
std::optional<Digests> HashFileAll(const fs::path& path, const std::atomic<bool>& stop) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    Hasher hashers[] = {Hasher(kAlgorithms[0]), Hasher(kAlgorithms[1]), Hasher(kAlgorithms[2]), Hasher(kAlgorithms[3])};
    std::vector<uint8_t> buffer(1 << 20);
    while (file && !stop) {
        file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const auto read = static_cast<size_t>(file.gcount());
        if (read == 0) break;
        for (auto& hasher : hashers) hasher.Update({buffer.data(), read});
    }
    if (file.bad() || stop) return std::nullopt;
    Digests digests;
    for (int i = 0; i < 4; ++i) digests[i] = ToHex(hashers[i].Finish());
    return digests;
}

std::wstring Lower(std::wstring text) {
    for (wchar_t& c : text) c = static_cast<wchar_t>(towlower(c));
    return text;
}

bool IsHex(std::wstring_view text) {
    return !text.empty() && text.find_first_not_of(L"0123456789abcdefABCDEF") == std::wstring_view::npos;
}

std::wstring FileSize(uintmax_t bytes) {
    const wchar_t* units[] = {L"bytes", L"KB", L"MB", L"GB", L"TB"};
    double size = static_cast<double>(bytes);
    int unit = 0;
    while (size >= 1024 && unit < 4) {
        size /= 1024;
        ++unit;
    }
    wchar_t text[32];
    swprintf_s(text, unit == 0 ? L"%.0f %s" : L"%.1f %s", size, units[unit]);
    return text;
}

}  // namespace

HashPanel::~HashPanel() { Stop(); }

void HashPanel::Stop() {
    stop_ = true;
    if (worker_.joinable()) worker_.join();
    stop_ = false;
}

void HashPanel::CreateControls() {
    AddChip(kTextChip, L"Text");
    AddChip(kFileChip, L"File");
    AddEdit(kText, ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN);
    SendMessageW(Item(kText), EM_SETLIMITTEXT, 1 << 20, 0);
    AddButton(kChooseFile, ButtonStyle::Standard, icons::kFolder, L"Choose a file…");
    for (int i = 0; i < 4; ++i) {
        AddEdit(kResultFirst + i, ES_AUTOHSCROLL | ES_READONLY, TextKind::Mono);
        AddButton(kCopyFirst + i, ButtonStyle::Subtle, icons::kCopy, L"");
    }
    AddEdit(kExpected, ES_AUTOHSCROLL, TextKind::Mono);
    SendMessageW(Item(kExpected), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Paste the hash you expect"));
    AddButton(kCheckList, ButtonStyle::Standard, icons::kDocument, L"Check a .sha256 file…");
    DragAcceptFiles(window_, TRUE);
    SetFileMode(false);
}

void HashPanel::SetFileMode(bool file) {
    fileMode_ = file;
    SetChip(kTextChip, !file);
    SetChip(kFileChip, file);
    for (int i = 0; i < 4; ++i) SetText(kResultFirst + i, L"");
    status_.clear();
    if (file) {
        if (!file_.empty()) HashFile(file_);
    } else {
        HashText();
    }
    Layout();
    Repaint();
}

void HashPanel::Layout() {
    ClearTexts();
    const int pad = S(24);
    const int width = Width() - 2 * pad;
    int y = S(16);
    AddText({pad, y, pad + width, y + S(32)}, L"Hash", TextKind::Title);
    y += S(32) + S(14);
    Place(kTextChip, pad, y, S(70), S(30));
    Place(kFileChip, pad + S(78), y, S(70), S(30));
    y += S(30) + S(14);

    if (fileMode_) {
        HideField(kText);
        const int buttonWidth = S(170);
        Place(kChooseFile, pad, y, buttonWidth, S(36));
        std::wstring name = file_.empty() ? L"Or drop a file here" : file_.filename().wstring();
        if (!file_.empty()) {
            std::error_code error;
            const auto size = fs::file_size(file_, error);
            if (!error) name += L"  ·  " + FileSize(size);
        }
        AddText({pad + buttonWidth + S(14), y, pad + width, y + S(36)}, name, file_.empty() ? TextKind::Hint : TextKind::Body);
        y += S(36) + S(16);
    } else {
        Show(kChooseFile, false);
        PlaceField(kText, {pad, y, pad + width, y + S(96)});
        y += S(96) + S(16);
    }

    const int labelWidth = S(80);
    const int icon = S(34);
    for (int i = 0; i < 4; ++i) {
        AddText({pad, y, pad + labelWidth, y + S(34)}, kNames[i], TextKind::Body);
        PlaceField(kResultFirst + i, {pad + labelWidth, y, pad + width - icon - S(4), y + S(34)});
        Place(kCopyFirst + i, pad + width - icon, y, icon, icon);
        y += S(38);
    }
    y += S(12);
    AddText({pad, y, pad + width, y + S(22)}, L"Compare with", TextKind::Body);
    y += S(24);
    PlaceField(kExpected, {pad, y, pad + width, y + S(36)});
    y += S(36) + S(6);
    if (!match_.empty()) AddText({pad, y, pad + width, y + S(22)}, match_, matchKind_);
    y += S(22) + S(12);

    Place(kCheckList, pad, y, S(220), S(36));
    const int statusHeight = std::max(S(36), TextHeight(status_, statusKind_, width - S(234)));
    if (!status_.empty()) {
        AddText({pad + S(234), y + (statusHeight > S(36) ? 0 : 0), pad + width, y + statusHeight}, status_, statusKind_,
                DT_LEFT | DT_WORDBREAK | (statusHeight <= S(36) ? DT_SINGLELINE | DT_VCENTER : 0));
    }
}

void HashPanel::SetStatus(std::wstring status, TextKind kind) {
    status_ = std::move(status);
    statusKind_ = kind;
    Layout();
    Repaint();
}

void HashPanel::Compare() {
    std::wstring expected = Lower(Trim(GetText(kExpected)));
    // Accept "hash  filename" pasted from a checksum file.
    if (const size_t space = expected.find_first_of(L" \t*"); space != std::wstring::npos) expected.resize(space);
    match_.clear();
    if (!expected.empty()) {
        if (!IsHex(expected)) {
            match_ = L"That isn't a hash: only 0-9 and a-f are allowed.";
            matchKind_ = TextKind::Error;
        } else {
            match_ = L"✗  Doesn't match any of the hashes above";
            matchKind_ = TextKind::Error;
            for (int i = 0; i < 4; ++i) {
                const std::wstring actual = GetText(kResultFirst + i);
                if (!actual.empty() && actual == expected) {
                    match_ = std::wstring(L"✓  Matches the ") + kNames[i] + L" hash";
                    matchKind_ = TextKind::Success;
                }
            }
            if (GetText(kResultFirst).empty()) match_.clear();
        }
    }
    Layout();
    Repaint();
}

void HashPanel::HashText() {
    // Line breaks as "\n", like text files and most online tools.
    const std::string utf8 = ToUtf8(ToUnixLineBreaks(GetText(kText)));
    const std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(utf8.data()), utf8.size());
    for (int i = 0; i < 4; ++i) SetText(kResultFirst + i, ToHex(Hash(kAlgorithms[i], bytes)));
    Compare();
}

template <typename Work>
void HashPanel::Run(Work work) {
    Stop();
    EnableWindow(Item(kChooseFile), FALSE);
    EnableWindow(Item(kCheckList), FALSE);
    worker_ = std::thread([this, work]() mutable {
        auto finish = std::make_unique<std::function<void()>>(work());
        PostMessageW(window_, WM_APP_HASHED, 0, reinterpret_cast<LPARAM>(finish.release()));
    });
}

void HashPanel::HashFile(const fs::path& path) {
    file_ = path;
    for (int i = 0; i < 4; ++i) SetText(kResultFirst + i, L"");
    match_.clear();
    SetStatus(L"Hashing…", TextKind::Hint);
    Run([this, path]() -> std::function<void()> {
        auto digests = HashFileAll(path, stop_);
        return [this, digests] {
            if (!digests) {
                SetStatus(L"Couldn't read that file.", TextKind::Error);
                return;
            }
            for (int i = 0; i < 4; ++i) SetText(kResultFirst + i, (*digests)[i]);
            SetStatus(L"", TextKind::Hint);
            Compare();
        };
    });
}

void HashPanel::ChooseFile() {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = GetAncestor(window_, GA_ROOT);
    dialog.lpstrFilter = L"All files (*.*)\0*.*\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) HashFile(file);
}

void HashPanel::ChooseChecksumList() {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = GetAncestor(window_, GA_ROOT);
    dialog.lpstrFilter = L"Checksum files (*.sha256; *.txt)\0*.sha256;*.sha256sum;*.txt\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return;
    const fs::path list = file;
    SetStatus(L"Checking…", TextKind::Hint);
    Run([this, list]() -> std::function<void()> {
        std::ifstream in(list, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::wstring text = FromUtf8(bytes);
        if (!text.empty() && text[0] == 0xFEFF) text.erase(0, 1);
        int total = 0, good = 0;
        std::wstring problems;
        size_t start = 0;
        while (start < text.size() && !stop_) {
            size_t end = text.find_first_of(L"\r\n", start);
            if (end == std::wstring::npos) end = text.size();
            std::wstring line = Trim(std::wstring_view(text).substr(start, end - start));
            start = end + 1;
            if (line.empty() || line[0] == L'#') continue;
            std::wstring hash, name;
            // GNU style "hash  name" / "hash *name", or BSD style "SHA256 (name) = hash".
            if (StartsWithNoCase(line, L"SHA256 (")) {
                const size_t close = line.rfind(L") = ");
                if (close == std::wstring::npos) continue;
                name = line.substr(8, close - 8);
                hash = Trim(line.substr(close + 4));
            } else {
                const size_t space = line.find_first_of(L" \t");
                hash = line.substr(0, space);
                if (space != std::wstring::npos) {
                    name = Trim(line.substr(space));
                    if (!name.empty() && name[0] == L'*') name.erase(0, 1);
                } else {
                    name = list.stem().wstring();  // "setup.exe.sha256" holding only the hash
                }
            }
            if (hash.size() != 64 || !IsHex(hash)) continue;
            ++total;
            const fs::path target = list.parent_path() / name;
            const auto digests = HashFileAll(target, stop_);
            if (!digests) {
                problems += L"\n" + name + L": not found";
            } else if ((*digests)[2] != Lower(hash)) {
                problems += L"\n" + name + L": doesn't match";
            } else {
                ++good;
            }
        }
        return [this, total, good, problems] {
            if (total == 0) {
                SetStatus(L"No SHA-256 hashes found in that file.", TextKind::Error);
            } else if (good == total) {
                SetStatus(L"✓  " + std::to_wstring(total) + (total == 1 ? L" file matches" : L" files match"), TextKind::Success);
            } else {
                SetStatus(L"✗  " + std::to_wstring(good) + L" of " + std::to_wstring(total) + L" files match" + problems, TextKind::Error);
            }
        };
    });
}

LRESULT HashPanel::OnMessage(UINT message, WPARAM wParam, LPARAM lParam, bool& handled) {
    handled = false;
    if (message == WM_APP_HASHED) {
        handled = true;
        if (worker_.joinable()) worker_.join();
        std::unique_ptr<std::function<void()>> finish(reinterpret_cast<std::function<void()>*>(lParam));
        EnableWindow(Item(kChooseFile), TRUE);
        EnableWindow(Item(kCheckList), TRUE);
        if (*finish) (*finish)();
        return 0;
    }
    if (message == WM_DROPFILES) {
        handled = true;
        auto drop = reinterpret_cast<HDROP>(wParam);
        wchar_t file[MAX_PATH];
        if (DragQueryFileW(drop, 0, file, MAX_PATH) && !fs::is_directory(file)) {
            fileMode_ = true;
            SetChip(kTextChip, false);
            SetChip(kFileChip, true);
            HashFile(file);
        }
        DragFinish(drop);
        return 0;
    }
    return 0;
}

void HashPanel::OnCommand(int id, int code) {
    switch (id) {
        case kTextChip: SetFileMode(false); break;
        case kFileChip: SetFileMode(true); break;
        case kText:
            if (code == EN_CHANGE) HashText();
            break;
        case kChooseFile: ChooseFile(); break;
        case kExpected:
            if (code == EN_CHANGE) Compare();
            break;
        case kCheckList: ChooseChecksumList(); break;
        default:
            if (id >= kCopyFirst && id < kCopyFirst + 4) {
                const std::wstring hash = GetText(kResultFirst + (id - kCopyFirst));
                if (!hash.empty() && onCopy) {
                    onCopy(hash);
                    FlashCopied(id);
                }
            }
            break;
    }
}

}  // namespace obex
