// File: src/parser/utf8.hpp
//
// UTF-8 for formatter characters (SPEC 2.2, 3.1). A formatter character is one
// Unicode code point whose General Category is Punctuation or Symbol; it can be
// one to four bytes in the file.

#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include "unicode_ps_table.hpp"

namespace jtext {

struct decoded_code_point {
    char32_t    cp  = 0;
    std::size_t len = 0;   // bytes it took
};

// The code point at the start of `s`. Strict: a truncated sequence, an overlong
// form, a surrogate or anything past U+10FFFF is not UTF-8 and gives nullopt.
constexpr auto decode_utf8(std::string_view s) -> std::optional<decoded_code_point>
{
    if (s.empty()) return std::nullopt;
    const auto b0 = static_cast<unsigned char>(s[0]);
    if (b0 < 0x80) return decoded_code_point{b0, 1};

    std::size_t len = 0;
    char32_t cp = 0;
    char32_t smallest = 0;
    if ((b0 & 0xE0) == 0xC0)      { len = 2; cp = b0 & 0x1F; smallest = 0x80; }
    else if ((b0 & 0xF0) == 0xE0) { len = 3; cp = b0 & 0x0F; smallest = 0x800; }
    else if ((b0 & 0xF8) == 0xF0) { len = 4; cp = b0 & 0x07; smallest = 0x10000; }
    else return std::nullopt;

    if (s.size() < len) return std::nullopt;
    for (std::size_t i = 1; i < len; ++i) {
        const auto b = static_cast<unsigned char>(s[i]);
        if ((b & 0xC0) != 0x80) return std::nullopt;
        cp = (cp << 6) | (b & 0x3F);
    }
    if (cp < smallest || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return std::nullopt;
    return decoded_code_point{cp, len};
}

// The UTF-8 bytes of `cp`, which must be a valid code point.
inline auto encode_utf8(char32_t cp) -> std::string
{
    std::string out;
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return out;
}

// True when `cp`'s General Category is Punctuation or Symbol (SPEC 3.1). Every
// printable non-alphanumeric ASCII character is one of these.
constexpr auto is_punctuation_or_symbol(char32_t cp) -> bool
{
    const auto it = std::ranges::upper_bound(kPunctuationOrSymbol, cp, {}, &code_point_range::first);
    return it != kPunctuationOrSymbol.begin() && cp <= std::prev(it)->last;
}

}  // namespace jtext
