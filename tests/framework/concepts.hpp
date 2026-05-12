// File: tests/framework/concepts.hpp
//
// Type concepts used by the test framework to dispatch value formatting
// at compile time. These let `format_value<T>(v)` pick the right rendering
// strategy without runtime branching.
//
// The framework uses these concepts in a fall-through cascade:
//   1. If T has a to_string() member, use it
//   2. Else if T is streamable via operator<<, stream it
//   3. Else if T is a string-like type, render as a quoted string
//   4. Else if T is a container, render its elements
//   5. Else fall back to a generic "<type at address>" rendering
//
// This file declares the concepts; format_value.hpp implements the dispatch.

#pragma once

#include <concepts>
#include <iterator>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

namespace jtext::test {

// ──────────────────────────────────────────────────────────────
//  has_to_string
//  T provides a t.to_string() method returning something convertible
//  to std::string.
// ──────────────────────────────────────────────────────────────

template <typename T>
concept has_to_string = requires(const T& t) {
    { t.to_string() } -> std::convertible_to<std::string>;
};

// ──────────────────────────────────────────────────────────────
//  streamable
//  T can be written to a std::ostream with operator<<.
//  Covers all built-in arithmetic types, strings, and anything
//  with a user-defined operator<<.
// ──────────────────────────────────────────────────────────────

template <typename T>
concept streamable = requires(std::ostream& os, const T& t) {
    { os << t } -> std::same_as<std::ostream&>;
};

// ──────────────────────────────────────────────────────────────
//  string_like
//  T is one of the canonical string types. Distinguished from
//  generic streamable so we can render them with surrounding quotes
//  in test output (which makes "" vs " " visible).
// ──────────────────────────────────────────────────────────────

template <typename T>
concept string_like =
    std::same_as<std::remove_cvref_t<T>, std::string> ||
    std::same_as<std::remove_cvref_t<T>, std::string_view> ||
    std::same_as<std::remove_cvref_t<T>, const char*> ||
    std::same_as<std::remove_cvref_t<T>, char*>;

// ──────────────────────────────────────────────────────────────
//  iterable_container
//  T has begin() and end() yielding iterators, and a value_type.
//  Strings are excluded because they're handled separately for
//  prettier output.
// ──────────────────────────────────────────────────────────────

template <typename T>
concept iterable_container = requires(const T& t) {
    typename T::value_type;
    { std::begin(t) } -> std::input_iterator;
    { std::end(t) }   -> std::input_iterator;
} && !string_like<T>;

// ──────────────────────────────────────────────────────────────
//  boolean_type
//  Distinguish bool from generic integers so we can print
//  "true"/"false" instead of "1"/"0" in test output.
// ──────────────────────────────────────────────────────────────

template <typename T>
concept boolean_type = std::same_as<std::remove_cvref_t<T>, bool>;

// ──────────────────────────────────────────────────────────────
//  character_type
//  Single-character types deserve special handling so a char value
//  of 'x' renders as 'x' (quoted) rather than 120 (its int value).
// ──────────────────────────────────────────────────────────────

template <typename T>
concept character_type =
    std::same_as<std::remove_cvref_t<T>, char> ||
    std::same_as<std::remove_cvref_t<T>, unsigned char> ||
    std::same_as<std::remove_cvref_t<T>, signed char>;

// ──────────────────────────────────────────────────────────────
//  equality_comparable
//  T can be compared with == and !=. The test framework's primary
//  assertion (test_eq) requires this.
// ──────────────────────────────────────────────────────────────

template <typename T, typename U = T>
concept equality_comparable = requires(const T& a, const U& b) {
    { a == b } -> std::convertible_to<bool>;
    { a != b } -> std::convertible_to<bool>;
};

}  // namespace jtext::test
