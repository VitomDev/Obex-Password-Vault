#include "core/generator.h"

#include "core/crypto.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <vector>

namespace obex {

namespace {

constexpr wchar_t kUpper[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr wchar_t kLower[] = L"abcdefghijklmnopqrstuvwxyz";
constexpr wchar_t kDigits[] = L"0123456789";
constexpr wchar_t kSymbols[] = L"!@#$%^&*()-_=+[]{};:,.<>/?~";
constexpr wchar_t kAmbiguous[] = L"l1IO0";

std::vector<std::wstring> Sets(const GeneratorOptions& options) {
    std::vector<std::wstring> sets;
    for (const auto& [on, chars] : {std::pair{options.upper, kUpper}, std::pair{options.lower, kLower},
                                    std::pair{options.digits, kDigits}, std::pair{options.symbols, kSymbols}}) {
        if (!on) continue;
        std::wstring set;
        for (const wchar_t* c = chars; *c; ++c) {
            if (!options.excludeAmbiguous || !wcschr(kAmbiguous, *c)) set += *c;
        }
        sets.push_back(set);
    }
    return sets;
}

}  // namespace

std::wstring GeneratePassword(const GeneratorOptions& options) {
    const auto sets = Sets(options);
    if (sets.empty()) return {};
    std::wstring all;
    for (const auto& set : sets) all += set;
    const int length = std::clamp(options.length, GeneratorOptions::kMinLength, GeneratorOptions::kMaxLength);

    std::wstring password;
    for (int i = 0; i < length; ++i) password += all[RandomBelow(static_cast<uint32_t>(all.size()))];
    // One of each kind at random places, then nothing else changes.
    std::vector<int> free(length);
    for (int i = 0; i < length; ++i) free[i] = i;
    for (const auto& set : sets) {
        const uint32_t pick = RandomBelow(static_cast<uint32_t>(free.size()));
        password[free[pick]] = set[RandomBelow(static_cast<uint32_t>(set.size()))];
        free.erase(free.begin() + pick);
    }
    return password;
}

double GeneratedEntropy(const GeneratorOptions& options) {
    size_t pool = 0;
    for (const auto& set : Sets(options)) pool += set.size();
    return pool ? std::clamp(options.length, GeneratorOptions::kMinLength, GeneratorOptions::kMaxLength) * std::log2(double(pool)) : 0;
}

double EstimateEntropy(std::wstring_view password) {
    bool upper = false, lower = false, digit = false, symbol = false, other = false;
    for (wchar_t c : password) {
        if (c >= L'A' && c <= L'Z') upper = true;
        else if (c >= L'a' && c <= L'z') lower = true;
        else if (c >= L'0' && c <= L'9') digit = true;
        else if (c < 128) symbol = true;
        else other = true;
    }
    const int pool = (upper ? 26 : 0) + (lower ? 26 : 0) + (digit ? 10 : 0) + (symbol ? 33 : 0) + (other ? 100 : 0);
    if (pool == 0) return 0;
    double length = 0;
    for (size_t i = 0; i < password.size(); ++i) {
        const bool predictable = i > 0 && (std::abs(int(password[i]) - int(password[i - 1])) <= 1);
        length += predictable ? 0.25 : 1;
    }
    return length * std::log2(double(pool));
}

Strength StrengthOf(double bits) {
    if (bits < 28) return Strength::VeryWeak;
    if (bits < 40) return Strength::Weak;
    if (bits < 60) return Strength::Fair;
    if (bits < 80) return Strength::Strong;
    return Strength::VeryStrong;
}

const wchar_t* StrengthName(Strength strength) {
    switch (strength) {
        case Strength::VeryWeak: return L"Very weak";
        case Strength::Weak: return L"Weak";
        case Strength::Fair: return L"Fair";
        case Strength::Strong: return L"Strong";
        case Strength::VeryStrong: return L"Very strong";
    }
    return L"";
}

std::wstring CrackTime(double bits) {
    // On average half the possibilities are tried.
    const double seconds = std::pow(2.0, bits - 1) / 1e10;
    const auto amount = [](double value, const wchar_t* unit) {
        const long long n = std::max(1LL, std::llround(value));
        return std::to_wstring(n) + L" " + unit + (n == 1 ? L"" : L"s");
    };
    if (seconds < 1) return L"Instantly";
    if (seconds < 60) return amount(seconds, L"second");
    if (seconds < 3600) return amount(seconds / 60, L"minute");
    if (seconds < 86400) return amount(seconds / 3600, L"hour");
    if (seconds < 86400 * 365.0) return amount(seconds / 86400, L"day");
    const double years = seconds / (86400 * 365.0);
    if (years < 1000) return amount(years, L"year");
    if (years < 1e6) return L"Thousands of years";
    if (years < 1e9) return L"Millions of years";
    return L"Billions of years or more";
}

}  // namespace obex
