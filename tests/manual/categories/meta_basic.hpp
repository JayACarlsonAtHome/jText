// File: tests/manual/categories/meta_basic.hpp
//
// Meta self-tests for the test framework, Session 2 style.
//
// Each test is now a small lambda registered with the suite by name.
// The runner orchestrates execution; no more manual begin_test/end_test.
//
// register_meta_basic_tests() is called from main() to add all of this
// file's tests to the suite. Adding a new test means adding one more
// suite.add(name, lambda) call.

#pragma once

#include "framework/assertion.hpp"
#include "framework/format_value.hpp"
#include "framework/runner.hpp"
#include "framework/type_name.hpp"

#include <print>
#include <string>
#include <string_view>
#include <vector>

inline auto register_meta_basic_tests(jtext::test::suite& s) -> void
{
    using namespace jtext::test;

    // ──────────────────────────────────────────────────────────
    //  Integer comparisons
    // ──────────────────────────────────────────────────────────

    s.add("integer equality (passing)", [] {
        test_eq(1 + 1, 2);
        test_eq(7 * 6, 42);
        test_eq(100 - 1, 99);
    });

    s.add("integer inequality (passing)", [] {
        test_ne(5, 6);
        test_ne(0, 1);
    });

    // ──────────────────────────────────────────────────────────
    //  Boolean assertions
    // ──────────────────────────────────────────────────────────

    s.add("boolean assertions", [] {
        test_true(true);
        test_false(false);
        test_true(1 + 1 == 2);
        test_false(1 + 1 == 3);
    });

    // ──────────────────────────────────────────────────────────
    //  String comparisons
    // ──────────────────────────────────────────────────────────

    s.add("string equality", [] {
        const std::string greeting{"hello"};
        const std::string same{"hello"};
        const std::string different{"goodbye"};
        test_eq(greeting, same);
        test_ne(greeting, different);
    });

    // ──────────────────────────────────────────────────────────
    //  string_view comparisons
    // ──────────────────────────────────────────────────────────

    s.add("string_view equality", [] {
        constexpr std::string_view sv1{"jText"};
        constexpr std::string_view sv2{"jText"};
        constexpr std::string_view sv3{"other"};
        test_eq(sv1, sv2);
        test_ne(sv1, sv3);
    });

    // ──────────────────────────────────────────────────────────
    //  Character comparisons
    // ──────────────────────────────────────────────────────────

    s.add("character equality", [] {
        test_eq('a', 'a');
        test_ne('a', 'b');
        test_eq('\n', '\n');
    });

    // ──────────────────────────────────────────────────────────
    //  Container comparisons
    // ──────────────────────────────────────────────────────────

    s.add("vector equality", [] {
        const std::vector<int> v1{1, 2, 3};
        const std::vector<int> v2{1, 2, 3};
        const std::vector<int> v3{1, 2, 4};
        test_eq(v1, v2);
        test_ne(v1, v3);
    });

    // ──────────────────────────────────────────────────────────
    //  format_value verification
    // ──────────────────────────────────────────────────────────

    s.add("format_value: booleans", [] {
        test_eq(format_value(true),  std::string{"true"});
        test_eq(format_value(false), std::string{"false"});
    });

    s.add("format_value: characters", [] {
        test_eq(format_value('x'),  std::string{"'x'"});
        test_eq(format_value(' '),  std::string{"' '"});
        test_eq(format_value('\n'), std::string{"'\\x0a'"});
    });

    s.add("format_value: strings", [] {
        test_eq(format_value(std::string{"hello"}),  std::string{R"("hello")"});
        test_eq(format_value(std::string{""}),       std::string{R"("")"});
        test_eq(format_value(std::string_view{"x"}), std::string{R"("x")"});
    });

    s.add("format_value: integers", [] {
        test_eq(format_value(42),     std::string{"42"});
        test_eq(format_value(-1),     std::string{"-1"});
        test_eq(format_value(0),      std::string{"0"});
    });

    s.add("format_value: containers", [] {
        const std::vector<int> empty_vec;
        const std::vector<int> three_elems{1, 2, 3};
        test_eq(format_value(empty_vec),   std::string{"[]"});
        test_eq(format_value(three_elems), std::string{"[1, 2, 3]"});
    });

    // ──────────────────────────────────────────────────────────
    //  type_name verification
    //
    //  Verify type_name produces something containing each type's
    //  identifying tokens. Format varies by compiler but content
    //  should be recognizable.
    // ──────────────────────────────────────────────────────────

    s.add("type_name: basic types", [] {
        const auto int_name    = std::string{type_name<int>()};
        const auto bool_name   = std::string{type_name<bool>()};
        const auto string_name = std::string{type_name<std::string>()};

        test_true(int_name.find("int")   != std::string::npos);
        test_true(bool_name.find("bool") != std::string::npos);
        test_true(string_name.find("string")       != std::string::npos ||
                  string_name.find("basic_string") != std::string::npos);
    });

    // ──────────────────────────────────────────────────────────
    //  Intentional failures (proves the FAIL path works)
    // ──────────────────────────────────────────────────────────

    s.add("INTENTIONAL FAILURES (expected: 2 failures below)", [] {
        test_eq(1, 2);                                       // expected failure
        test_eq(std::string{"x"}, std::string{"y"});         // expected failure
    });
}
