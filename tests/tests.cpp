// Obex's automated tests: a minimal runner without dependencies.
//
//   ObexTests.exe

#include "core/crypto.h"
#include "core/csv.h"
#include "core/generator.h"
#include "core/hello.h"
#include "core/settings.h"
#include "core/text.h"
#include "core/vault.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace obex;
namespace fs = std::filesystem;

namespace {

struct TestCase {
    const char* name;
    std::function<void()> run;
};

std::vector<TestCase>& Tests() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> run) { Tests().push_back({name, std::move(run)}); }
};

struct Failure {
    std::string message;
};

#define TEST(name)                                  \
    static void name();                             \
    static Registrar name##_registrar(#name, name); \
    static void name()

#define CHECK(condition)                                                                              \
    do {                                                                                              \
        if (!(condition)) throw Failure{std::string(#condition) + " (line " + std::to_string(__LINE__) + ")"}; \
    } while (false)

Bytes Ascii(const char* text) { return Bytes(text, text + strlen(text)); }

fs::path ScratchDir() {
    const fs::path dir = fs::temp_directory_path() / L"ObexTests";
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir);
    return dir;
}

}  // namespace

TEST(KnownHashes) {
    CHECK(ToHex(Hash(HashAlgorithm::Md5, Ascii("abc"))) == L"900150983cd24fb0d6963f7d28e17f72");
    CHECK(ToHex(Hash(HashAlgorithm::Sha1, Ascii("abc"))) == L"a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK(ToHex(Hash(HashAlgorithm::Sha256, Ascii("abc"))) ==
          L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(ToHex(Hash(HashAlgorithm::Sha512, Ascii(""))).starts_with(L"cf83e1357eefb8bdf1542850d66d8007"));
}

TEST(Pbkdf2KnownVector) {
    // RFC 7914, section 11.
    const Bytes salt = Ascii("salt");
    const Key key = DeriveKey(L"passwd", salt, 1);
    CHECK(ToHex(key) == L"55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc");
}

TEST(AesGcmRoundTripAndTamper) {
    Key key{};
    RandomBytes(key);
    const Bytes nonce = RandomBytes(kNonceSize);
    const Bytes aad = Ascii("header");
    const Bytes plain = Ascii("hunter2 and more");
    Bytes sealed = Encrypt(key, nonce, aad, plain);
    CHECK(sealed.size() == plain.size() + kTagSize);
    auto opened = Decrypt(key, nonce, aad, sealed);
    CHECK(opened && *opened == plain);
    sealed[0] ^= 1;
    CHECK(!Decrypt(key, nonce, aad, sealed));
    sealed[0] ^= 1;
    CHECK(!Decrypt(key, nonce, Ascii("other"), sealed));
}

TEST(RecoveryCodeFormat) {
    const std::wstring code = NewRecoveryCode();
    CHECK(code.size() == kRecoveryGroups * 5 - 1);
    CHECK(code[4] == L'-');
    CHECK(code.find_first_of(L"01ILO") == std::wstring::npos);
    CHECK(NormalizeRecoveryCode(L" ab2c-DEF3 \r\n") == L"AB2CDEF3");
    CHECK(NewRecoveryCode() != code);
}

TEST(VaultCreateOpenRecover) {
    const fs::path dir = ScratchDir();
    const fs::path path = dir / L"vault.obex";
    const std::wstring code = NewRecoveryCode();
    {
        Vault vault;
        vault.Create(path, L"correct horse battery", code);
        vault.entries.push_back({L"GitHub", L"github.com", L"vitom", L"p@ss w0rd ñ 日本", L"line 1\nline 2", 1, 2});
        CHECK(vault.Save(dir / L"Backups"));
    }
    Vault vault;
    CHECK(vault.Open(path, L"wrong password") == Vault::Result::WrongSecret);
    CHECK(vault.Open(path, L"correct horse battery") == Vault::Result::Ok);
    CHECK(vault.entries.size() == 1);
    CHECK(vault.entries[0].password == L"p@ss w0rd ñ 日本");
    CHECK(vault.entries[0].notes == L"line 1\nline 2");
    const Key key = vault.key();
    vault.Close();
    CHECK(vault.entries.empty());

    // Lower case and without dashes works too.
    std::wstring typed = code;
    for (wchar_t& c : typed) c = static_cast<wchar_t>(towlower(c));
    CHECK(vault.OpenWithRecoveryCode(path, typed) == Vault::Result::Ok);
    CHECK(vault.OpenWithRecoveryCode(path, NewRecoveryCode()) == Vault::Result::WrongSecret);
    CHECK(vault.OpenWithKey(path, key) == Vault::Result::Ok);
    Key wrongKey = key;
    wrongKey[0] ^= 1;
    CHECK(vault.OpenWithKey(path, wrongKey) == Vault::Result::WrongSecret);
    CHECK(vault.Open(dir / L"missing.obex", L"x") == Vault::Result::Missing);
}

TEST(VaultChangeSecretsKeepsKey) {
    const fs::path dir = ScratchDir();
    const fs::path path = dir / L"vault.obex";
    Vault vault;
    vault.Create(path, L"first password", L"AAAA-BBBB-CCCC-DDDD-EEEE-FFFF");
    CHECK(vault.Save());
    const Key key = vault.key();
    CHECK(vault.CheckMaster(L"first password"));
    CHECK(!vault.CheckMaster(L"second password"));
    vault.SetMaster(L"second password");
    vault.SetRecoveryCode(L"GGGG-HHHH-JJJJ-KKKK-MMMM-NNNN");
    CHECK(vault.Save());
    vault.Close();
    CHECK(vault.Open(path, L"first password") == Vault::Result::WrongSecret);
    CHECK(vault.Open(path, L"second password") == Vault::Result::Ok);
    CHECK(vault.key() == key);
    CHECK(vault.OpenWithRecoveryCode(path, L"AAAA-BBBB-CCCC-DDDD-EEEE-FFFF") == Vault::Result::WrongSecret);
    CHECK(vault.OpenWithRecoveryCode(path, L"gggghhhhjjjjkkkkmmmmnnnn") == Vault::Result::Ok);
}

TEST(VaultDetectsTampering) {
    const fs::path dir = ScratchDir();
    const fs::path path = dir / L"vault.obex";
    {
        Vault vault;
        vault.Create(path, L"tamper test 123", NewRecoveryCode());
        vault.entries.push_back({L"Site", L"", L"user", L"secret", L"", 0, 0});
        CHECK(vault.Save());
    }
    std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
    file.seekp(-5, std::ios::end);
    file.put('\x42');
    file.close();
    Vault vault;
    CHECK(vault.Open(path, L"tamper test 123") == Vault::Result::Damaged);
    std::ofstream(path, std::ios::binary | std::ios::trunc) << "not a vault";
    CHECK(vault.Open(path, L"tamper test 123") == Vault::Result::Damaged);
}

TEST(BackupsKeepOnePerDay) {
    const fs::path dir = ScratchDir();
    const fs::path path = dir / L"vault.obex";
    const fs::path backups = dir / L"Backups";
    fs::create_directories(backups);
    for (int day = 1; day <= 6; ++day) std::ofstream(backups / (L"vault-2020-01-0" + std::to_wstring(day) + L".obex")) << "old";
    Vault vault;
    vault.Create(path, L"backup test 123", NewRecoveryCode());
    CHECK(vault.Save(backups, 5));  // No previous file: nothing to back up yet.
    CHECK(vault.Save(backups, 5));
    CHECK(vault.Save(backups, 5));
    int count = 0;
    bool oldestGone = true;
    for (const auto& item : fs::directory_iterator(backups)) {
        ++count;
        if (item.path().filename() == L"vault-2020-01-01.obex") oldestGone = false;
    }
    CHECK(count == 5);
    CHECK(oldestGone);
}

TEST(EntriesSerialization) {
    std::vector<Entry> entries{{L"a", L"b", L"c", L"d", L"e", 10, 20}, {L"", L"", L"", L"", L"", 0, 0}};
    const Bytes data = SerializeEntries(entries);
    std::vector<Entry> parsed;
    CHECK(ParseEntries(data, parsed));
    CHECK(parsed.size() == 2 && parsed[0].notes == L"e" && parsed[0].modified == 20);
    CHECK(!ParseEntries(std::span(data).first(data.size() - 1), parsed));
}

TEST(CsvParsing) {
    const auto rows = ParseCsv(L"﻿a,\"b,c\",\"d \"\"q\"\"\"\r\n\"multi\nline\",,x\n\n");
    CHECK(rows.size() == 2);
    CHECK(rows[0].size() == 3 && rows[0][1] == L"b,c" && rows[0][2] == L"d \"q\"");
    CHECK(rows[1][0] == L"multi\nline" && rows[1][1].empty() && rows[1][2] == L"x");
}

TEST(ChromeImportExport) {
    std::vector<Entry> entries;
    CHECK(ImportBrowserCsv(L"name,url,username,password,note\n"
                           L"github.com,https://github.com/login,vitom,\"pa,ss\",\"my note\"\n"
                           L",https://example.com/,,only-password,\n"
                           L"empty,https://x.com,,,\n",
                           entries));
    CHECK(entries.size() == 2);
    CHECK(entries[0].name == L"github.com" && entries[0].password == L"pa,ss" && entries[0].notes == L"my note");
    CHECK(entries[1].name == L"https://example.com/");
    // Edge's older export puts the columns in another order.
    std::vector<Entry> edge;
    CHECK(ImportBrowserCsv(L"url,username,password,name\nhttps://a.com,u,p,A\n", edge));
    CHECK(edge.size() == 1 && edge[0].name == L"A" && edge[0].password == L"p");
    std::vector<Entry> none;
    CHECK(!ImportBrowserCsv(L"just,some,columns\n1,2,3\n", none));

    std::vector<Entry> again;
    CHECK(ImportBrowserCsv(ExportBrowserCsv(entries), again));
    CHECK(again.size() == 2 && again[0].password == L"pa,ss" && again[0].url == L"https://github.com/login");
}

TEST(GeneratorRules) {
    GeneratorOptions options;
    options.length = 4;
    for (int i = 0; i < 200; ++i) {
        const std::wstring password = GeneratePassword(options);
        CHECK(password.size() == 4);
        CHECK(password.find_first_of(L"ABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::wstring::npos);
        CHECK(password.find_first_of(L"abcdefghijklmnopqrstuvwxyz") != std::wstring::npos);
        CHECK(password.find_first_of(L"0123456789") != std::wstring::npos);
    }
    options = {};
    options.length = 128;
    options.excludeAmbiguous = true;
    for (int i = 0; i < 50; ++i) CHECK(GeneratePassword(options).find_first_of(L"l1IO0") == std::wstring::npos);
    options.length = 500;
    CHECK(GeneratePassword(options).size() == 128);
    options = {};
    options.upper = options.lower = options.digits = options.symbols = false;
    CHECK(GeneratePassword(options).empty());
    CHECK(GeneratedEntropy(options) == 0);
}

TEST(StrengthEstimates) {
    CHECK(StrengthOf(EstimateEntropy(L"aaaaaaaaaa")) == Strength::VeryWeak);
    CHECK(StrengthOf(EstimateEntropy(L"1234567890")) == Strength::VeryWeak);
    CHECK(StrengthOf(EstimateEntropy(L"Tr0ub4dor&3xQ!zP9mV")) >= Strength::Strong);
    GeneratorOptions options;
    CHECK(StrengthOf(GeneratedEntropy(options)) == Strength::VeryStrong);
    CHECK(CrackTime(0) == L"Instantly");
    CHECK(CrackTime(200) == L"Billions of years or more");
}

TEST(HelloWrapRoundTrip) {
    Key vaultKey{};
    RandomBytes(vaultKey);
    const Bytes challenge = RandomBytes(32);
    const Bytes signature = RandomBytes(256);
    const HelloState state = MakeHelloState(challenge, signature, vaultKey);
    CHECK(state.IsUsable(7));
    auto unwrapped = UnwrapHelloKey(state, signature);
    CHECK(unwrapped && *unwrapped == vaultKey);
    Bytes other = signature;
    other[10] ^= 1;
    CHECK(!UnwrapHelloKey(state, other));

    HelloState old = state;
    old.masterTime -= 8 * 86400;
    CHECK(!old.IsUsable(7));
    CHECK(old.IsUsable(30));
    HelloState otherSession = state;
    otherSession.logonId ^= 1;
    CHECK(!otherSession.IsUsable(7));

    const fs::path path = ScratchDir() / L"hello.dat";
    CHECK(SaveHelloState(path, state));
    const auto loaded = LoadHelloState(path);
    CHECK(loaded && loaded->sealed == state.sealed && loaded->masterTime == state.masterTime);
}

TEST(SettingsRoundTrip) {
    const wchar_t* key = L"Software\\Obex Password Vault Tests";
    RegDeleteTreeW(HKEY_CURRENT_USER, key);
    Settings defaults = Settings::Load(key);
    CHECK(defaults.autoLockMinutes == 15 && defaults.masterEveryDays == 7 && defaults.hello);
    CHECK(defaults.openHotkey.key == 'O' && defaults.openHotkey.modifiers == (MOD_CONTROL | MOD_ALT));
    Settings changed;
    changed.autoLockMinutes = 0;
    changed.lockWithWindows = false;
    changed.hello = false;
    changed.masterEveryDays = 30;
    changed.generator.length = 64;
    changed.generator.symbols = false;
    changed.generator.excludeAmbiguous = true;
    changed.generatorCount = 12;
    CHECK(changed.Save(key));
    const Settings loaded = Settings::Load(key);
    CHECK(loaded.autoLockMinutes == 0 && !loaded.lockWithWindows && !loaded.hello && loaded.masterEveryDays == 30);
    CHECK(loaded.generator.length == 64 && !loaded.generator.symbols && loaded.generator.excludeAmbiguous && loaded.generatorCount == 12);
    RegDeleteTreeW(HKEY_CURRENT_USER, key);
}

int wmain() {
    int failed = 0;
    for (const auto& test : Tests()) {
        try {
            test.run();
            std::cout << "PASS " << test.name << "\n";
        } catch (const Failure& failure) {
            ++failed;
            std::cout << "FAIL " << test.name << ": " << failure.message << "\n";
        } catch (const std::exception& error) {
            ++failed;
            std::cout << "FAIL " << test.name << ": exception " << error.what() << "\n";
        }
    }
    std::error_code error;
    fs::remove_all(fs::temp_directory_path() / L"ObexTests", error);
    std::cout << Tests().size() - failed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
