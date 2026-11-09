#pragma once

#include <string>
#include <string_view>

namespace obex {

struct GeneratorOptions {
    int length = 20;
    bool upper = true;
    bool lower = true;
    bool digits = true;
    bool symbols = true;
    bool excludeAmbiguous = false;  // l 1 I O 0

    static constexpr int kMinLength = 4;
    static constexpr int kMaxLength = 128;
};

// Uses at least one character of every chosen kind. Empty if none is chosen.
std::wstring GeneratePassword(const GeneratorOptions& options);
// Bits of entropy of a password made with these options.
double GeneratedEntropy(const GeneratorOptions& options);

enum class Strength { VeryWeak, Weak, Fair, Strong, VeryStrong };

// A rough estimate for a typed password: character pool and length, with
// repeats and simple sequences ("aaaa", "1234") counting for less.
double EstimateEntropy(std::wstring_view password);
Strength StrengthOf(double bits);
const wchar_t* StrengthName(Strength strength);
// Time to guess it at ten billion tries per second, e.g. "3 hours".
std::wstring CrackTime(double bits);

}  // namespace obex
