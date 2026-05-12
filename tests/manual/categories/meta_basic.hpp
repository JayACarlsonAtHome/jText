// File: tests/manual/categories/meta_basic.hpp
//
// Meta self-tests for the test framework itself.
//
// These tests verify that the framework's machinery (concepts dispatch,
// value formatting, assertion recording) works correctly against simple
// known-good inputs. If these pass, the framework is sound and we can
// trust it for testing the actual jText code in later sessions.
//
// The tests deliberately include comparisons of various types — ints,
// strings, bools, characters, containers — so the type-name and
// value-formatting paths all get exercised.

#pragma once

#include "framework/assertion.hpp"
#include "framework/format_value.hpp"
#include "framework/type_name.hpp"

#include <print>
#include <string>
#include <string_view>
#include <vector>

inline auto run_meta_basic_tests() -> void
{
    using namespace jtext::test;

    // ──────────────────────────────────────────────────────────
    //  Integer comparisons
    // ──────────────────────────────────────────────────────────

    begin_test("integer equality (passing)");
    test_eq(1 + 1, 2);
    test_eq(7 * 6, 42);
    test_eq(100 - 1, 99);
    end_test();

    begin_test("integer inequality (passing)");
    test_ne(5, 6);
    test_ne(0, 1);
    end_test();

    // ──────────────────────────────────────────────────────────
    //  Boolean comparisons
    // ──────────────────────────────────────────────────────────

    begin_test("boolean assertions");
    test_true(true);
    test_false(false);
    test_true(1 + 1 == 2);
    test_false(1 + 1 == 3);
    end_test();

    // ──────────────────────────────────────────────────────────
    //  String comparisons
    //
    //  String literals decay to const char* and would compare
    //  by pointer rather than content, so use std::string
    //  explicitly to exercise value comparison.
    // ──────────────────────────────────────────────────────────

    begin_test("string equality");
    const std::string greeting{"hello"};
    const std::string same{"hello"};
    const std::string different{"goodbye"};
    test_eq(greeting, same);
    test_ne(greeting, different);
    end_test();

    // ──────────────────────────────────────────────────────────
    //  std::string_view comparisons
    // ──────────────────────────────────────────────────────────

    begin_test("string_view equality");
    constexpr std::string_view sv1{"jText"};
    constexpr std::string_view sv2{"jText"};
    constexpr std::string_view sv3{"other"};
    test_eq(sv1, sv2);
    test_ne(sv1, sv3);
    end_test();

    // ──────────────────────────────────────────────────────────
    //  Character comparisons
    // ──────────────────────────────────────────────────────────

    begin_test("character equality");
    test_eq('a', 'a');
    test_ne('a', 'b');
    test_eq('\n', '\n');
    end_test();

    // ──────────────────────────────────────────────────────────
    //  Container comparisons
    //
    //  Tests both value equality and the format_value rendering
    //  of containers (which only shows up if something fails;
    //  here we just verify it compiles and runs).
    // ──────────────────────────────────────────────────────────

    begin_test("vector equality");
    const std::vector<int> v1{1, 2, 3};
    const std::vector<int> v2{1, 2, 3};
    const std::vector<int> v3{1, 2, 4};
    test_eq(v1, v2);
    test_ne(v1, v3);
    end_test();

    // ──────────────────────────────────────────────────────────
    //  Direct format_value verification
    //
    //  Confirm that format_value produces the expected strings for
    //  each category. These are themselves test_eq assertions on
    //  the framework's own output, which is a nice circularity check.
    // ──────────────────────────────────────────────────────────

    begin_test("format_value: booleans");
    test_eq(format_value(true),  std::string{"true"});
    test_eq(format_value(false), std::string{"false"});
    end_test();

    begin_test("format_value: characters");
    test_eq(format_value('x'),  std::string{"'x'"});
    test_eq(format_value(' '),  std::string{"' '"});
    test_eq(format_value('\n'), std::string{"'\\x0a'"});
    end_test();

    begin_test("format_value: strings");
    test_eq(format_value(std::string{"hello"}),  std::string{R"("hello")"});
    test_eq(format_value(std::string{""}),       std::string{R"("")"});
    test_eq(format_value(std::string_view{"x"}), std::string{R"("x")"});
    end_test();

    begin_test("format_value: integers");
    test_eq(format_value(42),     std::string{"42"});
    test_eq(format_value(-1),     std::string{"-1"});
    test_eq(format_value(0),      std::string{"0"});
    end_test();

    begin_test("format_value: containers");
    const std::vector<int> empty_vec;
    const std::vector<int> three_elems{1, 2, 3};
    test_eq(format_value(empty_vec),   std::string{"[]"});
    test_eq(format_value(three_elems), std::string{"[1, 2, 3]"});
    end_test();

    // ──────────────────────────────────────────────────────────
    //  type_name verification
    //
    //  These check that type_name produces something reasonable.
    //  We don't pin the exact format because it varies by compiler,
    //  but we can verify it contains the type's identifying tokens.
    // ──────────────────────────────────────────────────────────

    begin_test("type_name: basic types");
    const auto int_name    = std::string{type_name<int>()};
    const auto bool_name   = std::string{type_name<bool>()};
    const auto string_name = std::string{type_name<std::string>()};

    test_true(int_name.find("int")     != std::string::npos);
    test_true(bool_name.find("bool")   != std::string::npos);
    test_true(string_name.find("string") != std::string::npos ||
              string_name.find("basic_string") != std::string::npos);

    // For debugging: print what the compiler actually produced.
    std::println("");
    std::println("  type_name<int>           = {}", int_name);
    std::println("  type_name<bool>          = {}", bool_name);
    std::println("  type_name<std::string>   = {}", string_name);
    std::println("  type_name<std::vector<int>> = {}",
                 std::string{type_name<std::vector<int>>()});
    end_test();

    // ──────────────────────────────────────────────────────────
    //  Failure path
    //
    //  Verify that a deliberately-failing assertion is correctly
    //  recorded as a failure. The TWO assertions below MUST fail
    //  for the framework to be working correctly. The summary at
    //  the end should report exactly 2 failures.
    // ──────────────────────────────────────────────────────────

    begin_test("INTENTIONAL FAILURES (expected: 2 failures below)");
    test_eq(1, 2);                                       // expected failure
    test_eq(std::string{"x"}, std::string{"y"});         // expected failure
    end_test();
}
