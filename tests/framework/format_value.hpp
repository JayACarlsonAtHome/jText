// File: tests/framework/format_value.hpp
//
// Format a value of any type as a human-readable string, using compile-time
// concept dispatch to pick the right strategy per type.
//
// Strategy (first match wins):
//   1. nullptr_t      → "nullptr"
//   2. bool           → "true" / "false"
//   3. character_type → 'x' (quoted single char)
//   4. string_like    → "text" (double-quoted)
//   5. has_to_string  → call t.to_string()
//   6. streamable     → operator<< to a stringstream
//   7. iterable       → "[elem1, elem2, elem3]"
//   8. fallback       → "<TypeName at 0xADDR>"
//
// This is the value-rendering used by test_eq and friends when a comparison
// fails and the framework reports "expected: X, actual: Y".

#pragma once

#include "concepts.hpp"
#include "type_name.hpp"

#include <cstddef>
#include <format>
#include <sstream>
#include <string>
#include <string_view>

namespace jtext::test {

// Forward declaration so containers can recurse on element formatting.
template <typename T>
auto format_value(const T& v) -> std::string;

namespace detail {

// ──────────────────────────────────────────────────────────────
//  Render a container as "[a, b, c]" by recursively formatting
//  each element with format_value<>.
// ──────────────────────────────────────────────────────────────

template <iterable_container T>
auto render_container(const T& c) -> std::string
{
    std::string out = "[";
    bool first = true;
    for (const auto& elem : c) {
        if (!first) out += ", ";
        out += format_value(elem);
        first = false;
    }
    out += "]";
    return out;
}

// ──────────────────────────────────────────────────────────────
//  Render a single character as 'x' (with single quotes).
//  Non-printable characters are rendered as their integer value.
// ──────────────────────────────────────────────────────────────

template <character_type T>
auto render_character(T c) -> std::string
{
    const auto ch = static_cast<unsigned char>(c);
    if (ch >= 0x20 && ch < 0x7F) {
        return std::string{'\''} + static_cast<char>(ch) + '\'';
    }
    return std::format("'\\x{:02x}'", static_cast<unsigned>(ch));
}

// ──────────────────────────────────────────────────────────────
//  Render a string with surrounding double quotes so empty strings
//  and whitespace-only strings are visible in test output.
// ──────────────────────────────────────────────────────────────

inline auto render_string(std::string_view sv) -> std::string
{
    std::string out;
    out.reserve(sv.size() + 2);
    out.push_back('"');
    out.append(sv);
    out.push_back('"');
    return out;
}

// ──────────────────────────────────────────────────────────────
//  Fallback rendering for types we can't format any other way.
//  Shows the type name and the object's address, which is at least
//  enough information to tell two distinct objects apart.
// ──────────────────────────────────────────────────────────────

template <typename T>
auto render_fallback(const T& v) -> std::string
{
    return std::format("<{} at {:p}>",
                       type_name<T>(),
                       static_cast<const void*>(&v));
}

}  // namespace detail

// ──────────────────────────────────────────────────────────────
//  format_value<T>(v)
//  Main entry point. Dispatches to the right rendering strategy
//  based on T's properties.
// ──────────────────────────────────────────────────────────────

template <typename T>
auto format_value(const T& v) -> std::string
{
    using Bare = std::remove_cvref_t<T>;

    if constexpr (std::same_as<Bare, std::nullptr_t>) {
        return "nullptr";
    }
    else if constexpr (boolean_type<Bare>) {
        return v ? "true" : "false";
    }
    else if constexpr (character_type<Bare>) {
        return detail::render_character(v);
    }
    else if constexpr (string_like<Bare>) {
        if constexpr (std::same_as<Bare, const char*> ||
                      std::same_as<Bare, char*>) {
            return detail::render_string(v == nullptr ? "<null>" : v);
        } else {
            return detail::render_string(std::string_view{v});
        }
    }
    else if constexpr (has_to_string<Bare>) {
        return v.to_string();
    }
    else if constexpr (streamable<Bare>) {
        std::ostringstream oss;
        oss << v;
        return oss.str();
    }
    else if constexpr (iterable_container<Bare>) {
        return detail::render_container(v);
    }
    else {
        return detail::render_fallback(v);
    }
}

}  // namespace jtext::test
