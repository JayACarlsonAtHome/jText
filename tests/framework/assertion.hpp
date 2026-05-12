// File: tests/framework/assertion.hpp
//
// Assertion functions used inside test bodies.
//
// Session 2 changes from Session 1:
//   - Each assertion produces an assertion_log record (captured for
//     the report file)
//   - The runner sets a per-test "current sink" that assertions write to
//   - stdout output respects the runner's verbose/quiet flags
//   - The Session 1 begin_test/end_test/print_summary functions are
//     gone — the runner takes over their responsibilities
//
// Public API (unchanged from Session 1):
//   test_eq(actual, expected)
//   test_ne(actual, expected)
//   test_true(condition)
//   test_false(condition)

#pragma once

#include "concepts.hpp"
#include "format_value.hpp"
#include "type_name.hpp"

#include <cstddef>
#include <print>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace jtext::test {

// ──────────────────────────────────────────────────────────────
//  assertion_log
//  One record per assertion. The reporter consumes these to produce
//  the detailed section of the report file.
// ──────────────────────────────────────────────────────────────

struct assertion_log {
    bool         passed       = false;
    std::string  kind;          // "test_eq", "test_ne", etc.
    std::string  type_name;     // type of the values being compared
    std::string  expected_str;  // formatted expected value
    std::string  actual_str;    // formatted actual value
    std::string  file;          // source file
    unsigned     line  = 0;     // source line
};

// ──────────────────────────────────────────────────────────────
//  sink — where assertion results go.
//  The runner sets the current sink before each test runs, then
//  reads back the accumulated assertions when the test completes.
//
//  Outside a runner (e.g., manual experimentation), a thread-local
//  fallback sink catches assertions; useful for ad-hoc debugging.
// ──────────────────────────────────────────────────────────────

struct sink {
    std::vector<assertion_log>  log;
    std::size_t                 passed  = 0;
    std::size_t                 failed  = 0;
    bool                        verbose = false;  // print PASS lines too
    bool                        quiet   = false;  // no stdout at all
};

namespace detail {

// Per-thread pointer to the active sink. The runner sets this before
// invoking each test; assertion functions read it.
inline auto active_sink_ptr() -> sink*&
{
    thread_local sink* p = nullptr;
    return p;
}

// Thread-local fallback sink, used when no runner is active.
inline auto fallback_sink() -> sink&
{
    thread_local sink s;
    return s;
}

inline auto current_sink() -> sink&
{
    if (auto* p = active_sink_ptr()) {
        return *p;
    }
    return fallback_sink();
}

// Record one assertion: append to the sink's log, update counters,
// print to stdout per verbose/quiet flags.
inline auto record_result(
    bool passed,
    std::string_view kind,
    std::string_view type_str,
    std::string expected_str,
    std::string actual_str,
    const std::source_location& loc) -> void
{
    auto& s = current_sink();

    s.log.push_back(assertion_log{
        .passed       = passed,
        .kind         = std::string{kind},
        .type_name    = std::string{type_str},
        .expected_str = expected_str,
        .actual_str   = actual_str,
        .file         = loc.file_name(),
        .line         = loc.line(),
    });

    if (passed) {
        s.passed += 1;
        if (s.verbose && !s.quiet) {
            std::println("    [PASS] {}:{} ({}) {}",
                         loc.file_name(),
                         loc.line(),
                         type_str,
                         kind);
        }
    } else {
        s.failed += 1;
        if (!s.quiet) {
            std::println("    [FAIL] {}:{} ({}) {}",
                         loc.file_name(),
                         loc.line(),
                         type_str,
                         kind);
            std::println("           expected: {}", expected_str);
            std::println("           actual:   {}", actual_str);
        }
    }
}

}  // namespace detail

// ──────────────────────────────────────────────────────────────
//  set_active_sink / clear_active_sink
//  Used by the runner. Test code should not call these directly.
// ──────────────────────────────────────────────────────────────

inline auto set_active_sink(sink& s) -> void
{
    detail::active_sink_ptr() = &s;
}

inline auto clear_active_sink() -> void
{
    detail::active_sink_ptr() = nullptr;
}

// ──────────────────────────────────────────────────────────────
//  test_eq(actual, expected)
// ──────────────────────────────────────────────────────────────

template <typename A, typename E>
    requires equality_comparable<A, E>
auto test_eq(
    const A& actual,
    const E& expected,
    std::source_location loc = std::source_location::current()) -> bool
{
    const bool ok = (actual == expected);
    detail::record_result(
        ok, "test_eq", type_name<A>(),
        format_value(expected), format_value(actual), loc);
    return ok;
}

// ──────────────────────────────────────────────────────────────
//  test_ne(actual, expected)
// ──────────────────────────────────────────────────────────────

template <typename A, typename E>
    requires equality_comparable<A, E>
auto test_ne(
    const A& actual,
    const E& expected,
    std::source_location loc = std::source_location::current()) -> bool
{
    const bool ok = (actual != expected);
    detail::record_result(
        ok, "test_ne", type_name<A>(),
        std::string{"!= "} + format_value(expected),
        format_value(actual), loc);
    return ok;
}

// ──────────────────────────────────────────────────────────────
//  test_true(condition)
// ──────────────────────────────────────────────────────────────

inline auto test_true(
    bool condition,
    std::source_location loc = std::source_location::current()) -> bool
{
    detail::record_result(
        condition, "test_true", "bool",
        std::string{"true"},
        std::string{condition ? "true" : "false"}, loc);
    return condition;
}

// ──────────────────────────────────────────────────────────────
//  test_false(condition)
// ──────────────────────────────────────────────────────────────

inline auto test_false(
    bool condition,
    std::source_location loc = std::source_location::current()) -> bool
{
    detail::record_result(
        !condition, "test_false", "bool",
        std::string{"false"},
        std::string{condition ? "true" : "false"}, loc);
    return !condition;
}

}  // namespace jtext::test
