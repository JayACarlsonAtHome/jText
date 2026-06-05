// File: tests/framework/type_name.hpp
//
// Compile-time type name extraction that produces clean, human-readable
// type names instead of compiler-mangled forms.
//
// typeid(T).name() returns ugly strings like "i" for int or
// "St6vectorIiSaIiEE" for std::vector<int> (on Itanium ABI / GCC / Clang).
//
// This file extracts the type name from __PRETTY_FUNCTION__, which has
// a predictable shape on GCC and Clang:
//
//   GCC:   "std::string_view jtext::test::type_name() [with T = int; ...]"
//   Clang: "std::string_view jtext::test::type_name() [T = int]"
//
// We find the "T = ..." substring, walk to the end of the type name,
// and return that slice as a string_view.
//
// On MSVC, __FUNCSIG__ has a different shape; supported but with a
// slightly different parser.

#pragma once

#include <string_view>
#include <source_location>

namespace jtext::test {

namespace detail {

// Extract the substring between `prefix` and the first occurrence
// of any character in `terminators`.
constexpr std::string_view extract_between(
    std::string_view src,
    std::string_view prefix,
    std::string_view terminators)
{
    const auto start = src.find(prefix);
    if (start == std::string_view::npos) return {};
    const auto value_start = start + prefix.size();
    const auto end = src.find_first_of(terminators, value_start);
    if (end == std::string_view::npos) {
        return src.substr(value_start);
    }
    return src.substr(value_start, end - value_start);
}

}  // namespace detail

// ──────────────────────────────────────────────────────────────
//  type_name<T>()
//  Returns a clean, human-readable name for type T using C++20
//  std::source_location (no preprocessor macros).
// ──────────────────────────────────────────────────────────────

template <typename T>
constexpr std::string_view type_name()
{
    // Use source_location to get the function signature in a portable way.
    // This works on GCC, Clang, and MSVC without any #ifdefs.
    constexpr std::source_location loc = std::source_location::current();
    constexpr std::string_view sig = loc.function_name();

    // Different compilers embed the template argument in slightly different ways.
    // We try the most common patterns.
    if (auto name = detail::extract_between(sig, "T = ", ";]"); !name.empty())
        return name;

    if (auto name = detail::extract_between(sig, "type_name<", ">("); !name.empty())
        return name;

    // Fallback: return the whole signature (better than nothing)
    return sig;
}

}  // namespace jtext::test
