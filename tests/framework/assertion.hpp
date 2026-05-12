// File: tests/framework/assertion.hpp
//
// Assertion functions used inside test bodies. These record their results
// into a per-thread test context that the runner reads when summarizing
// each test's outcome.
//
// Session 1 establishes the basic vocabulary:
//   test_eq(actual, expected)        — equality
//   test_ne(actual, expected)        — inequality
//   test_true(condition)             — boolean truthy
//   test_false(condition)            — boolean falsy
//
// Each assertion captures the call site via std::source_location, so no
// macros are needed. The framework reports each check with file:line and
// the formatted expected/actual values.
//
// Session 1 has a minimal "context" — just a thread-local pair of counters
// (passed and failed). Session 2 introduces the full runner that owns
// the context and produces structured reports.

#pragma once

#include "concepts.hpp"
#include "format_value.hpp"
#include "type_name.hpp"

#include <cstddef>
#include <print>
#include <source_location>
#include <string>
#include <string_view>

namespace jtext::test {

// ──────────────────────────────────────────────────────────────
//  Per-thread test context (Session 1 minimal version).
//
//  This is a small thread-local struct that holds counters and the
//  identity of the currently-running test. Assertion functions write
//  to it; the runner reads from it.
//
//  Session 2 replaces this with a richer Context that captures
//  individual assertion records for report-file generation.
// ──────────────────────────────────────────────────────────────

struct context {
    std::size_t  passed       = 0;
    std::size_t  failed       = 0;
    std::string  current_test = "<no test running>";
    bool         verbose      = true;   // print each assertion to stdout
};

inline auto current_context() -> context&
{
    thread_local context ctx;
    return ctx;
}

// ──────────────────────────────────────────────────────────────
//  Internal: report a single check result.
//
//  Session 1 just prints. Session 2 will also append a structured
//  record to the context's assertion log for the report file.
// ──────────────────────────────────────────────────────────────

namespace detail {

inline auto record_result(
    bool passed,
    std::string_view kind,
    std::string_view type_str,
    std::string_view expected_str,
    std::string_view actual_str,
    const std::source_location& loc) -> void
{
    auto& ctx = current_context();
    if (passed) {
        ctx.passed += 1;
        if (ctx.verbose) {
            std::println("  [PASS] {}:{} ({}) {}",
                         loc.file_name(),
                         loc.line(),
                         type_str,
                         kind);
        }
    } else {
        ctx.failed += 1;
        std::println("  [FAIL] {}:{} ({}) {}",
                     loc.file_name(),
                     loc.line(),
                     type_str,
                     kind);
        std::println("         expected: {}", expected_str);
        std::println("         actual:   {}", actual_str);
    }
}

}  // namespace detail

// ──────────────────────────────────────────────────────────────
//  test_eq(actual, expected)
//  Asserts that actual == expected. Records pass/fail.
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
        ok,
        "test_eq",
        type_name<A>(),
        format_value(expected),
        format_value(actual),
        loc);
    return ok;
}

// ──────────────────────────────────────────────────────────────
//  test_ne(actual, expected)
//  Asserts that actual != expected.
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
        ok,
        "test_ne",
        type_name<A>(),
        std::string{"!= "} + format_value(expected),
        format_value(actual),
        loc);
    return ok;
}

// ──────────────────────────────────────────────────────────────
//  test_true(condition)
//  Asserts that condition is true.
// ──────────────────────────────────────────────────────────────

inline auto test_true(
    bool condition,
    std::source_location loc = std::source_location::current()) -> bool
{
    detail::record_result(
        condition,
        "test_true",
        "bool",
        "true",
        condition ? "true" : "false",
        loc);
    return condition;
}

// ──────────────────────────────────────────────────────────────
//  test_false(condition)
//  Asserts that condition is false.
// ──────────────────────────────────────────────────────────────

inline auto test_false(
    bool condition,
    std::source_location loc = std::source_location::current()) -> bool
{
    detail::record_result(
        !condition,
        "test_false",
        "bool",
        "false",
        condition ? "true" : "false",
        loc);
    return !condition;
}

// ──────────────────────────────────────────────────────────────
//  Begin / end a test by name. Session 1 just updates the context's
//  current_test field and prints a header. Session 2's runner takes
//  over this responsibility and adds per-test result aggregation.
// ──────────────────────────────────────────────────────────────

inline auto begin_test(std::string_view name) -> void
{
    auto& ctx = current_context();
    ctx.current_test = name;
    std::println("");
    std::println("──────────────────────────────────────────");
    std::println(" TEST: {}", name);
    std::println("──────────────────────────────────────────");
}

inline auto end_test() -> void
{
    // Session 1 no-op. Session 2 will close out the test record here.
}

// ──────────────────────────────────────────────────────────────
//  Print final summary. Session 2 replaces this with a full reporter.
// ──────────────────────────────────────────────────────────────

inline auto print_summary() -> int
{
    auto& ctx = current_context();
    const auto total = ctx.passed + ctx.failed;
    std::println("");
    std::println("══════════════════════════════════════════");
    std::println(" SUMMARY");
    std::println("══════════════════════════════════════════");
    std::println("  Total:   {}", total);
    std::println("  Passed:  {}  {}", ctx.passed, ctx.failed == 0 ? "✓" : "");
    std::println("  Failed:  {}  {}", ctx.failed, ctx.failed != 0 ? "✗" : "");
    std::println("══════════════════════════════════════════");
    return ctx.failed == 0 ? 0 : 1;
}

}  // namespace jtext::test
