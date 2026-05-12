// File: tests/manual/categories/parser_basic.hpp
//
// Tests for the line-grammar parser (src/parser/line.cpp), covering
// every rule in SPEC Section 3.
//
// Test organization:
//   - validate_formatter: positive and negative formatter cases
//   - parse_line: valid forms (flat, hierarchical, edge cases)
//   - parse_line: invalid forms (every parse_error_kind exercised)
//   - parse_line: multiline-opener detection
//   - to_string(parse_error_kind): readable enum names
//
// Each test is a registered lambda; failures show file:line and the
// specific values that mismatched.

#pragma once

#include "framework/assertion.hpp"
#include "framework/runner.hpp"
#include "parser/line.hpp"

#include <string>
#include <string_view>
#include <vector>

inline auto register_parser_basic_tests(jtext::test::suite& s) -> void
{
    using namespace jtext;
    using namespace jtext::test;

    // ──────────────────────────────────────────────────────────
    //  validate_formatter — positive cases
    // ──────────────────────────────────────────────────────────

    s.add("formatter: standard #?# is valid", [] {
        auto r = validate_formatter("#?#");
        test_true(r.has_value());
        if (r) {
            test_eq(r->bookend,   '#');
            test_eq(r->separator, '?');
        }
    });

    s.add("formatter: #/# is valid (hierarchical)", [] {
        auto r = validate_formatter("#/#");
        test_true(r.has_value());
        if (r) {
            test_eq(r->bookend,   '#');
            test_eq(r->separator, '/');
        }
    });

    s.add("formatter: various ASCII punctuation pairs", [] {
        // All of these should validate cleanly.
        for (const auto* fmt : {"?-?", "$|$", "*+*", "@~@", "&%&", "^=^"}) {
            auto r = validate_formatter(fmt);
            test_true(r.has_value());
        }
    });

    // ──────────────────────────────────────────────────────────
    //  validate_formatter — negative cases
    // ──────────────────────────────────────────────────────────

    s.add("formatter: mismatched bookends are rejected", [] {
        auto r = validate_formatter("#?$");
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind,
                    parse_error_kind::formatter_bookends_mismatch);
        }
    });

    s.add("formatter: bookend equal to separator is rejected", [] {
        auto r = validate_formatter("###");
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind,
                    parse_error_kind::formatter_separator_equals_bookend);
        }
    });

    s.add("formatter: alphanumeric bookend is rejected", [] {
        for (const auto* fmt : {"a/a", "1/1", "Z?Z"}) {
            auto r = validate_formatter(fmt);
            test_false(r.has_value());
        }
    });

    s.add("formatter: alphanumeric separator is rejected", [] {
        for (const auto* fmt : {"#a#", "#1#", "#Z#"}) {
            auto r = validate_formatter(fmt);
            test_false(r.has_value());
        }
    });

    s.add("formatter: whitespace in formatter is rejected", [] {
        for (const auto* fmt : {"# #", "\t/\t", "# ?"}) {
            auto r = validate_formatter(fmt);
            test_false(r.has_value());
        }
    });

    s.add("formatter: too short is rejected", [] {
        auto r = validate_formatter("#?");
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind, parse_error_kind::formatter_too_short);
        }
    });

    // ──────────────────────────────────────────────────────────
    //  parse_line — flat data, no comment
    // ──────────────────────────────────────────────────────────

    s.add("parse_line: flat data, no comment", [] {
        auto r = parse_line("1. #?# Just plain text");
        test_true(r.has_value());
        if (r) {
            test_eq(r->kind, line_kind::data);
            test_eq(r->number, 1u);
            test_eq(r->hierarchy.size(), std::size_t{1});
            test_eq(r->hierarchy[0], std::string{"Just plain text"});
            test_eq(r->comment, std::string{""});
        }
    });

    s.add("parse_line: flat data with comment", [] {
        auto r = parse_line("1. #?# hello # a greeting");
        test_true(r.has_value());
        if (r) {
            test_eq(r->hierarchy[0], std::string{"hello"});
            test_eq(r->comment, std::string{"a greeting"});
        }
    });

    s.add("parse_line: empty data is valid", [] {
        // SPEC 2.6.1: "Empty (line, no data) — for String fields, this is
        // valid data (empty string)."
        auto r = parse_line("1. #?#");
        test_true(r.has_value());
        if (r) {
            test_eq(r->hierarchy.size(), std::size_t{1});
            test_eq(r->hierarchy[0], std::string{""});
            test_eq(r->comment, std::string{""});
        }
    });

    s.add("parse_line: empty data with comment", [] {
        auto r = parse_line("1. #?# # an empty value");
        test_true(r.has_value());
        if (r) {
            test_eq(r->hierarchy[0], std::string{""});
            test_eq(r->comment, std::string{"an empty value"});
        }
    });

    // ──────────────────────────────────────────────────────────
    //  parse_line — hierarchical data
    // ──────────────────────────────────────────────────────────

    s.add("parse_line: 3-level hierarchy", [] {
        auto r = parse_line("1. #/# Personal Tools/Drills/Battery");
        test_true(r.has_value());
        if (r) {
            test_eq(r->hierarchy.size(), std::size_t{3});
            test_eq(r->hierarchy[0], std::string{"Personal Tools"});
            test_eq(r->hierarchy[1], std::string{"Drills"});
            test_eq(r->hierarchy[2], std::string{"Battery"});
        }
    });

    s.add("parse_line: hierarchy with comment", [] {
        auto r = parse_line("1. #/# A/B/C # three levels");
        test_true(r.has_value());
        if (r) {
            test_eq(r->hierarchy.size(), std::size_t{3});
            test_eq(r->comment, std::string{"three levels"});
        }
    });

    s.add("parse_line: leading separator is cosmetic", [] {
        auto r = parse_line("1. #/# /A/B/C");
        test_true(r.has_value());
        if (r) {
            test_eq(r->hierarchy.size(), std::size_t{3});
            test_eq(r->hierarchy[0], std::string{"A"});
            test_eq(r->hierarchy[1], std::string{"B"});
            test_eq(r->hierarchy[2], std::string{"C"});
        }
    });

    s.add("parse_line: trailing separator is cosmetic", [] {
        auto r = parse_line("1. #/# A/B/C/");
        test_true(r.has_value());
        if (r) {
            test_eq(r->hierarchy.size(), std::size_t{3});
        }
    });

    s.add("parse_line: leading AND trailing separators are cosmetic", [] {
        auto r = parse_line("1. #/# /A/B/C/");
        test_true(r.has_value());
        if (r) {
            test_eq(r->hierarchy.size(), std::size_t{3});
            test_eq(r->hierarchy[0], std::string{"A"});
        }
    });

    s.add("parse_line: whitespace around segments is trimmed", [] {
        auto r = parse_line("1. #/# A / B / C");
        test_true(r.has_value());
        if (r) {
            test_eq(r->hierarchy.size(), std::size_t{3});
            test_eq(r->hierarchy[0], std::string{"A"});
            test_eq(r->hierarchy[1], std::string{"B"});
            test_eq(r->hierarchy[2], std::string{"C"});
        }
    });

    s.add("parse_line: declared-but-unused hierarchy is single-level", [] {
        // SPEC 3.5: hierarchical formatter with no separator in the
        // data → single-element hierarchy.
        auto r = parse_line("1. #/# JustOneLevel");
        test_true(r.has_value());
        if (r) {
            test_eq(r->hierarchy.size(), std::size_t{1});
            test_eq(r->hierarchy[0], std::string{"JustOneLevel"});
        }
    });

    s.add("parse_line: data containing the bookend (but no marker)", [] {
        // SPEC 3.2: a bookend char without whitespace on both sides
        // is data, not a comment marker. "a#b" has no surrounding ws.
        auto r = parse_line("1. #/# a#b");
        test_true(r.has_value());
        if (r) {
            test_eq(r->hierarchy[0], std::string{"a#b"});
            test_eq(r->comment, std::string{""});
        }
    });

    s.add("parse_line: data containing / when separator is different", [] {
        // SPEC 3.6: "Data containing a `/` that is *not* hierarchical
        // (path stored as flat value)" — author picks #-# so / is data.
        auto r = parse_line("1. #-# /etc/passwd");
        test_true(r.has_value());
        if (r) {
            test_eq(r->hierarchy.size(), std::size_t{1});
            test_eq(r->hierarchy[0], std::string{"/etc/passwd"});
        }
    });

    s.add("parse_line: fractions preserved with * as separator", [] {
        // SPEC 3.6's example.
        auto r = parse_line("1. #*# 4/3 is four thirds*3/3 is three thirds*2/3 is two thirds");
        test_true(r.has_value());
        if (r) {
            test_eq(r->hierarchy.size(), std::size_t{3});
            test_eq(r->hierarchy[0], std::string{"4/3 is four thirds"});
            test_eq(r->hierarchy[1], std::string{"3/3 is three thirds"});
            test_eq(r->hierarchy[2], std::string{"2/3 is two thirds"});
        }
    });

    // ──────────────────────────────────────────────────────────
    //  parse_line — line number padding
    // ──────────────────────────────────────────────────────────

    s.add("parse_line: 2-column padded single-digit", [] {
        auto r = parse_line(" 1. #?# value");
        test_true(r.has_value());
        if (r) test_eq(r->number, 1u);
    });

    s.add("parse_line: two-digit line number", [] {
        auto r = parse_line("12. #?# value");
        test_true(r.has_value());
        if (r) test_eq(r->number, 12u);
    });

    s.add("parse_line: max line number 99", [] {
        auto r = parse_line("99. #?# value");
        test_true(r.has_value());
        if (r) test_eq(r->number, 99u);
    });

    s.add("parse_line: extra whitespace before number is fine", [] {
        auto r = parse_line("    7. #?# value");
        test_true(r.has_value());
        if (r) test_eq(r->number, 7u);
    });

    // ──────────────────────────────────────────────────────────
    //  parse_line — error cases (structural)
    // ──────────────────────────────────────────────────────────

    s.add("parse_line: line number 0 is rejected", [] {
        auto r = parse_line("0. #?# x");
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind, parse_error_kind::invalid_line_number);
        }
    });

    s.add("parse_line: line number 100 is rejected", [] {
        auto r = parse_line("100. #?# x");
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind, parse_error_kind::invalid_line_number);
        }
    });

    s.add("parse_line: missing period is rejected", [] {
        auto r = parse_line("1 #?# x");
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind, parse_error_kind::missing_period);
        }
    });

    s.add("parse_line: missing formatter is rejected", [] {
        auto r = parse_line("1.");
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind, parse_error_kind::missing_formatter);
        }
    });

    s.add("parse_line: empty line is rejected", [] {
        auto r = parse_line("");
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind, parse_error_kind::empty_line);
        }
    });

    s.add("parse_line: whitespace-only line is rejected", [] {
        auto r = parse_line("   \t  ");
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind, parse_error_kind::empty_line);
        }
    });

    // ──────────────────────────────────────────────────────────
    //  parse_line — error cases (formatter)
    // ──────────────────────────────────────────────────────────

    s.add("parse_line: malformed formatter propagates error", [] {
        auto r = parse_line("1. #?$ x");
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind,
                    parse_error_kind::formatter_bookends_mismatch);
        }
    });

    // ──────────────────────────────────────────────────────────
    //  parse_line — error cases (hierarchy)
    //
    //  After the v0.5 cleanup, all empty-segment cases use a single
    //  error kind (hierarchy_empty_segment): there is no meaningful
    //  interpretation of an empty hierarchy level, regardless of
    //  whether it's internal ("A//B") or pervasive ("///").
    // ──────────────────────────────────────────────────────────

    s.add("parse_line: empty segment in hierarchy is rejected", [] {
        auto r = parse_line("1. #/# A//B");
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind,
                    parse_error_kind::hierarchy_empty_segment);
        }
    });

    s.add("parse_line: only-separators hierarchy is rejected", [] {
        auto r = parse_line("1. #/# ///");
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind,
                    parse_error_kind::hierarchy_empty_segment);
        }
    });

    s.add("parse_line: whitespace-only segment in hierarchy is rejected", [] {
        auto r = parse_line("1. #/# A/ /B");
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind,
                    parse_error_kind::hierarchy_empty_segment);
        }
    });

    // ──────────────────────────────────────────────────────────
    //  parse_line — multiline opener detection
    // ──────────────────────────────────────────────────────────

    s.add("parse_line: multiline opener with sentinel", [] {
        auto r = parse_line("1. <<< !!!END!!!");
        test_true(r.has_value());
        if (r) {
            test_eq(r->kind, line_kind::multiline_opener);
            test_eq(r->number, 1u);
            test_eq(r->sentinel, std::string{"!!!END!!!"});
            test_eq(r->comment, std::string{""});
        }
    });

    s.add("parse_line: multiline opener with trailing comment", [] {
        auto r = parse_line("1. <<< !!!END!!!  this is a comment");
        test_true(r.has_value());
        if (r) {
            test_eq(r->kind, line_kind::multiline_opener);
            test_eq(r->sentinel, std::string{"!!!END!!!"});
            test_eq(r->comment, std::string{"this is a comment"});
        }
    });

    s.add("parse_line: multiline opener without sentinel is rejected", [] {
        auto r = parse_line("1. <<<");
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind,
                    parse_error_kind::multiline_missing_sentinel);
        }
    });

    s.add("parse_line: multiline sentinel too short is rejected", [] {
        auto r = parse_line("1. <<< !!");
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind,
                    parse_error_kind::multiline_sentinel_invalid);
        }
    });

    // ──────────────────────────────────────────────────────────
    //  parse_line — round-trip / whitespace robustness
    // ──────────────────────────────────────────────────────────

    s.add("parse_line: trailing newline is tolerated", [] {
        auto r = parse_line("1. #?# x\n");
        test_true(r.has_value());
        if (r) test_eq(r->hierarchy[0], std::string{"x"});
    });

    s.add("parse_line: trailing CRLF is tolerated", [] {
        auto r = parse_line("1. #?# x\r\n");
        test_true(r.has_value());
        if (r) test_eq(r->hierarchy[0], std::string{"x"});
    });

    s.add("parse_line: extra whitespace between formatter and data", [] {
        auto r = parse_line("1. #?#       value");
        test_true(r.has_value());
        if (r) test_eq(r->hierarchy[0], std::string{"value"});
    });

    s.add("parse_line: trailing whitespace before EOL is trimmed", [] {
        auto r = parse_line("1. #?# value     ");
        test_true(r.has_value());
        if (r) test_eq(r->hierarchy[0], std::string{"value"});
    });

    // ──────────────────────────────────────────────────────────
    //  parse_line — worked example bits from SPEC
    // ──────────────────────────────────────────────────────────

    s.add("parse_line: SPEC example — file-header filename line", [] {
        auto r = parse_line(" 1. #?# tools_inventory.jText                       # filename");
        test_true(r.has_value());
        if (r) {
            test_eq(r->number, 1u);
            test_eq(r->hierarchy.size(), std::size_t{1});
            test_eq(r->hierarchy[0], std::string{"tools_inventory.jText"});
            test_eq(r->comment, std::string{"filename"});
        }
    });

    s.add("parse_line: SPEC example — field declaration line", [] {
        auto r = parse_line(" 2. #/# name/String/40/Not Null             # tool name");
        test_true(r.has_value());
        if (r) {
            test_eq(r->number, 2u);
            test_eq(r->hierarchy.size(), std::size_t{4});
            test_eq(r->hierarchy[0], std::string{"name"});
            test_eq(r->hierarchy[1], std::string{"String"});
            test_eq(r->hierarchy[2], std::string{"40"});
            test_eq(r->hierarchy[3], std::string{"Not Null"});
            test_eq(r->comment, std::string{"tool name"});
        }
    });

    // ──────────────────────────────────────────────────────────
    //  to_string(parse_error_kind) — readable names
    //
    //  Verifies the ADL to_string function returns the expected
    //  identifier strings. This is also what makes test failures
    //  involving parse_error_kind comparisons show readable names
    //  instead of memory addresses.
    // ──────────────────────────────────────────────────────────

    s.add("to_string(parse_error_kind): produces readable names", [] {
        test_eq(std::string{to_string(parse_error_kind::empty_line)},
                std::string{"empty_line"});
        test_eq(std::string{to_string(parse_error_kind::invalid_line_number)},
                std::string{"invalid_line_number"});
        test_eq(std::string{to_string(parse_error_kind::formatter_bookends_mismatch)},
                std::string{"formatter_bookends_mismatch"});
        test_eq(std::string{to_string(parse_error_kind::hierarchy_empty_segment)},
                std::string{"hierarchy_empty_segment"});
    });
}
