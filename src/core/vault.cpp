#include "core/vault.h"

#include "core/text.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <fstream>

namespace obex {

namespace fs = std::filesystem;

namespace {

constexpr char kMagic[4] = {'O', 'B', 'E', 'X'};
constexpr uint16_t kVersion = 1;
constexpr wchar_t kAlphabet[] = L"23456789ABCDEFGHJKMNPQRSTUVWXYZ";

struct Writer {
    Bytes bytes;
    void Raw(const void* data, size_t size) {
        const auto* p = static_cast<const uint8_t*>(data);
        bytes.insert(bytes.end(), p, p + size);
    }
    void U16(uint16_t v) { Raw(&v, sizeof(v)); }
    void U32(uint32_t v) { Raw(&v, sizeof(v)); }
    void I64(int64_t v) { Raw(&v, sizeof(v)); }
    void Text(std::wstring_view text) {
        std::string utf8 = ToUtf8(text);
        U32(static_cast<uint32_t>(utf8.size()));
        Raw(utf8.data(), utf8.size());
        Wipe(utf8);
    }
};

struct Reader {
    std::span<const uint8_t> data;
    size_t at = 0;
    bool ok = true;
    bool Raw(void* out, size_t size) {
        if (!ok || data.size() - at < size) return ok = false;
        std::memcpy(out, data.data() + at, size);
        at += size;
        return true;
    }
    uint16_t U16() { uint16_t v = 0; Raw(&v, sizeof(v)); return v; }
    uint32_t U32() { uint32_t v = 0; Raw(&v, sizeof(v)); return v; }
    int64_t I64() { int64_t v = 0; Raw(&v, sizeof(v)); return v; }
    std::wstring Text() {
        const uint32_t size = U32();
        if (!ok || data.size() - at < size) {
            ok = false;
            return {};
        }
        std::wstring text = FromUtf8({reinterpret_cast<const char*>(data.data() + at), size});
        at += size;
        return text;
    }
};

std::wstring DateStamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    wchar_t stamp[16];
    wcsftime(stamp, 16, L"%Y-%m-%d", &local);
    return stamp;
}

void KeepBackup(const fs::path& file, const fs::path& dir, int keep) {
    std::error_code error;
    if (dir.empty() || keep <= 0 || !fs::exists(file, error)) return;
    fs::create_directories(dir, error);
    const fs::path today = dir / (L"vault-" + DateStamp() + L".obex");
    if (!fs::exists(today, error)) fs::copy_file(file, today, error);

    std::vector<fs::path> backups;
    for (const auto& item : fs::directory_iterator(dir, error)) {
        const std::wstring name = item.path().filename().wstring();
        if (name.starts_with(L"vault-") && item.path().extension() == L".obex") backups.push_back(item.path());
    }
    // Dated names sort oldest first.
    std::sort(backups.begin(), backups.end());
    for (size_t i = 0; i + keep < backups.size(); ++i) fs::remove(backups[i], error);
}

}  // namespace

void Entry::Wipe() {
    for (std::wstring* field : {&name, &url, &username, &password, &notes}) {
        obex::Wipe(*field);
        field->clear();
    }
}

std::wstring NewRecoveryCode() {
    const uint32_t size = static_cast<uint32_t>(wcslen(kAlphabet));
    std::wstring code;
    for (int group = 0; group < kRecoveryGroups; ++group) {
        if (group) code += L'-';
        for (int i = 0; i < 4; ++i) code += kAlphabet[RandomBelow(size)];
    }
    return code;
}

std::wstring NormalizeRecoveryCode(std::wstring_view code) {
    std::wstring result;
    for (wchar_t c : code) {
        if (c == L'-' || c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') continue;
        result += static_cast<wchar_t>(towupper(c));
    }
    return result;
}

Bytes SerializeEntries(const std::vector<Entry>& entries) {
    Writer w;
    w.U32(1);
    w.U32(static_cast<uint32_t>(entries.size()));
    for (const Entry& e : entries) {
        w.Text(e.name);
        w.Text(e.url);
        w.Text(e.username);
        w.Text(e.password);
        w.Text(e.notes);
        w.I64(e.created);
        w.I64(e.modified);
    }
    return std::move(w.bytes);
}

bool ParseEntries(std::span<const uint8_t> data, std::vector<Entry>& entries) {
    Reader r{data};
    if (r.U32() != 1) return false;
    const uint32_t count = r.U32();
    entries.clear();
    for (uint32_t i = 0; i < count && r.ok; ++i) {
        Entry e;
        e.name = r.Text();
        e.url = r.Text();
        e.username = r.Text();
        e.password = r.Text();
        e.notes = r.Text();
        e.created = r.I64();
        e.modified = r.I64();
        entries.push_back(std::move(e));
    }
    return r.ok;
}

Vault::Wrap Vault::MakeWrap(std::wstring_view secret) const {
    Wrap wrap;
    RandomBytes(wrap.salt);
    RandomBytes(wrap.nonce);
    Key derived = DeriveKey(secret, wrap.salt, wrap.iterations);
    const Bytes sealed = Encrypt(derived, wrap.nonce, {}, key_);
    Wipe(derived.data(), derived.size());
    std::copy(sealed.begin(), sealed.end(), wrap.sealed.begin());
    return wrap;
}

void Vault::Create(const fs::path& path, std::wstring_view master, std::wstring_view recoveryCode) {
    Close();
    path_ = path;
    RandomBytes(key_);
    master_ = MakeWrap(master);
    recovery_ = MakeWrap(NormalizeRecoveryCode(recoveryCode));
    open_ = true;
}

Vault::Result Vault::Open(const fs::path& path, std::wstring_view master) { return Load(path, master, false, nullptr); }

Vault::Result Vault::OpenWithRecoveryCode(const fs::path& path, std::wstring_view code) {
    return Load(path, NormalizeRecoveryCode(code), true, nullptr);
}

Vault::Result Vault::OpenWithKey(const fs::path& path, const Key& key) { return Load(path, {}, false, &key); }

Vault::Result Vault::Load(const fs::path& path, std::wstring_view secret, bool recovery, const Key* key) {
    Close();
    std::ifstream file(path, std::ios::binary);
    if (!file) return Result::Missing;
    const Bytes data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    Reader r{data};
    char magic[4]{};
    r.Raw(magic, 4);
    const uint16_t version = r.U16();
    r.U16();
    if (!r.ok || std::memcmp(magic, kMagic, 4) != 0 || version != kVersion) return Result::Damaged;
    Wrap wraps[2];
    for (Wrap& wrap : wraps) {
        wrap.iterations = r.U32();
        r.Raw(wrap.salt.data(), wrap.salt.size());
        r.Raw(wrap.nonce.data(), wrap.nonce.size());
        r.Raw(wrap.sealed.data(), wrap.sealed.size());
    }
    const size_t headerSize = r.at;
    std::array<uint8_t, kNonceSize> nonce{};
    r.Raw(nonce.data(), nonce.size());
    const uint32_t size = r.U32();
    if (!r.ok || data.size() - r.at != size || wraps[0].iterations == 0 || wraps[1].iterations == 0) return Result::Damaged;

    Key vaultKey{};
    if (key) {
        vaultKey = *key;
    } else {
        const Wrap& wrap = wraps[recovery ? 1 : 0];
        Key derived = DeriveKey(secret, wrap.salt, wrap.iterations);
        auto unwrapped = Decrypt(derived, wrap.nonce, {}, wrap.sealed);
        Wipe(derived.data(), derived.size());
        if (!unwrapped || unwrapped->size() != vaultKey.size()) return Result::WrongSecret;
        std::copy(unwrapped->begin(), unwrapped->end(), vaultKey.begin());
        Wipe(*unwrapped);
    }

    auto plain = Decrypt(vaultKey, nonce, std::span(data).first(headerSize), std::span(data).subspan(r.at));
    if (!plain) {
        Wipe(vaultKey.data(), vaultKey.size());
        // A good password unwraps the key; failing here means the file changed.
        return key ? Result::WrongSecret : Result::Damaged;
    }
    const bool parsed = ParseEntries(*plain, entries);
    Wipe(*plain);
    if (!parsed) {
        Wipe(vaultKey.data(), vaultKey.size());
        for (Entry& e : entries) e.Wipe();
        entries.clear();
        return Result::Damaged;
    }
    path_ = path;
    key_ = vaultKey;
    Wipe(vaultKey.data(), vaultKey.size());
    master_ = wraps[0];
    recovery_ = wraps[1];
    open_ = true;
    return Result::Ok;
}

void Vault::Close() {
    for (Entry& e : entries) e.Wipe();
    entries.clear();
    Wipe(key_.data(), key_.size());
    open_ = false;
}

bool Vault::CheckMaster(std::wstring_view master) const {
    if (!open_) return false;
    Key derived = DeriveKey(master, master_.salt, master_.iterations);
    auto unwrapped = Decrypt(derived, master_.nonce, {}, master_.sealed);
    Wipe(derived.data(), derived.size());
    const bool ok = unwrapped && std::equal(unwrapped->begin(), unwrapped->end(), key_.begin(), key_.end());
    if (unwrapped) Wipe(*unwrapped);
    return ok;
}

void Vault::SetMaster(std::wstring_view master) { master_ = MakeWrap(master); }

void Vault::SetRecoveryCode(std::wstring_view code) { recovery_ = MakeWrap(NormalizeRecoveryCode(code)); }

Bytes Vault::Header() const {
    Writer w;
    w.Raw(kMagic, 4);
    w.U16(kVersion);
    w.U16(0);
    for (const Wrap* wrap : {&master_, &recovery_}) {
        w.U32(wrap->iterations);
        w.Raw(wrap->salt.data(), wrap->salt.size());
        w.Raw(wrap->nonce.data(), wrap->nonce.size());
        w.Raw(wrap->sealed.data(), wrap->sealed.size());
    }
    return std::move(w.bytes);
}

bool Vault::Save(const fs::path& backupDir, int backups) {
    if (!open_) return false;
    Bytes file = Header();
    std::array<uint8_t, kNonceSize> nonce{};
    RandomBytes(nonce);
    Bytes plain = SerializeEntries(entries);
    const Bytes sealed = Encrypt(key_, nonce, file, plain);
    Wipe(plain);
    file.insert(file.end(), nonce.begin(), nonce.end());
    const uint32_t size = static_cast<uint32_t>(sealed.size());
    file.insert(file.end(), reinterpret_cast<const uint8_t*>(&size), reinterpret_cast<const uint8_t*>(&size) + 4);
    file.insert(file.end(), sealed.begin(), sealed.end());

    std::error_code error;
    fs::create_directories(path_.parent_path(), error);
    KeepBackup(path_, backupDir, backups);
    const fs::path temp = path_.wstring() + L".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
        out.flush();
        if (!out) return false;
    }
    return MoveFileExW(temp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

}  // namespace obex
