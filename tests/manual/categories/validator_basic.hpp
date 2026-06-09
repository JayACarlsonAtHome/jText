// File: tests/manual/categories/validator_basic.hpp
//
// Tests for the validator (src/validator/validator.cpp). Covers:
//   - Field declaration interpretation (name/type/length/constraint)
//   - Field schema validation (positions contiguous, types/constraints
//     recognized)
//   - Record assembly (blank-line separators, strict increase rule)
//   - Per-field type checking (Number, Date, String length)
//   - Header interpretation (recognized fields, unknown warnings)
//   - End-to-end: the SPEC worked example validates cleanly

#pragma once

#include "framework/assertion.hpp"
#include "framework/runner.hpp"
#include "parser/section.hpp"
#include "validator/validator.hpp"

#include <string>
#include <string_view>
#include <optional>

namespace {

// Helper: parse a jText file from inline text and validate it.
// Returns the validate_result; tests assert on its contents.
inline auto validate_text(std::string_view text) -> jtext::validate_result
{
    auto pf = jtext::parse_file_structure(text);
    if (!pf) {
        // Return an empty validated file with an error to mark this
        // case (tests should rely on parse_file_structure succeeding;
        // this just keeps the API consistent on parse failure).
        jtext::validate_result r;
        r.report.add(jtext::validation_issue{
            jtext::issue_severity::error,
            jtext::issue_kind::field_invalid_declaration,
            pf.error().line_no,
            "parse",
            pf.error().to_string(),
        });
        return r;
    }
    return jtext::validate(*pf);
}

// Helper: count issues of a given kind in a report.
inline auto count_issues_of(
    const jtext::validation_report& r,
    jtext::issue_kind k) -> std::size_t
{
    std::size_t n = 0;
    for (const auto& i : r.issues) {
        if (i.kind == k) ++n;
    }
    return n;
}

}  // anonymous namespace

inline auto register_validator_basic_tests(jtext::test::suite& s) -> void
{
    using namespace jtext;
    using namespace jtext::test;

    // ──────────────────────────────────────────────────────────
    //  parse_field_type and to_string helpers
    // ──────────────────────────────────────────────────────────

    s.add("parse_field_type: recognizes the three types", [] {
        test_eq(parse_field_type("String").value(), field_type::string_type);
        test_eq(parse_field_type("Number").value(), field_type::number_type);
        test_eq(parse_field_type("Date").value(),   field_type::date_type);
    });

    s.add("parse_field_type: is case-insensitive", [] {
        test_eq(parse_field_type("string").value(), field_type::string_type);
        test_eq(parse_field_type("NUMBER").value(), field_type::number_type);
        test_eq(parse_field_type("dAtE").value(),   field_type::date_type);
    });

    s.add("parse_field_type: unknown type returns nullopt", [] {
        test_false(parse_field_type("Integer").has_value());
        test_false(parse_field_type("").has_value());
        test_false(parse_field_type("Foo").has_value());
    });

    s.add("to_string(field_type): readable names", [] {
        test_eq(std::string{to_string(field_type::string_type)}, std::string{"String"});
        test_eq(std::string{to_string(field_type::number_type)}, std::string{"Number"});
        test_eq(std::string{to_string(field_type::date_type)},   std::string{"Date"});
    });

    // ──────────────────────────────────────────────────────────
    //  Minimal valid file: validates cleanly
    // ──────────────────────────────────────────────────────────

    s.add("validate: minimal file with one valid section passes", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            " 2. #?# 2026-05-12\n"
            " 3. #?# test\n"
            "=== Section: Tools ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            " 2. #/# name/String/40/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 1\n"
            " 2. #?# Hammer\n"
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_false(vr.report.has_errors());
        test_eq(vr.file.sections.size(), std::size_t{1});
        if (!vr.file.sections.empty()) {
            const auto& sec = vr.file.sections[0];
            test_eq(sec.name,            std::string{"Tools"});
            test_eq(sec.fields.size(),   std::size_t{2});
            test_eq(sec.records.size(),  std::size_t{1});
            if (sec.fields.size() == 2) {
                test_eq(sec.fields[0].position, 1u);
                test_eq(sec.fields[0].name,     std::string{"id"});
                test_eq(sec.fields[0].type,     field_type::number_type);
                test_true(sec.fields[0].required);
                test_eq(sec.fields[1].position, 2u);
                test_eq(sec.fields[1].name,     std::string{"name"});
                test_eq(sec.fields[1].type,     field_type::string_type);
                test_true(sec.fields[1].max_length.has_value());
                if (sec.fields[1].max_length) {
                    test_eq(*sec.fields[1].max_length, std::size_t{40});
                }
            }
            if (sec.records.size() == 1) {
                const auto& rec = sec.records[0];
                test_eq(rec.values.size(), std::size_t{2});
                if (rec.values.size() == 2) {
                    test_true(rec.values[0].has_value());
                    test_true(rec.values[1].has_value());
                    if (rec.values[0]) test_eq(*rec.values[0], std::string{"1"});
                    if (rec.values[1]) test_eq(*rec.values[1], std::string{"Hammer"});
                }
            }
        }
    });

    // ──────────────────────────────────────────────────────────
    //  Field declaration: shape errors
    // ──────────────────────────────────────────────────────────

    s.add("field: missing type segment is rejected", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# id\n"
            "=== Data ===\n"
            " 1. #?# x\n";
        auto vr = validate_text(text);
        test_true(vr.report.has_errors());
        test_true(count_issues_of(vr.report,
                  issue_kind::field_invalid_declaration) > 0);
    });

    s.add("field: unrecognized type is rejected", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Integer/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 1\n";
        auto vr = validate_text(text);
        test_true(vr.report.has_errors());
        test_true(count_issues_of(vr.report,
                  issue_kind::field_invalid_type) > 0);
    });

    s.add("field: invalid constraint is rejected", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Required\n"
            "=== Data ===\n"
            " 1. #?# 1\n";
        auto vr = validate_text(text);
        test_true(vr.report.has_errors());
    });

    s.add("field: optional Nullable constraint accepted", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            " 2. #/# note/String/Nullable\n"
            "=== Data ===\n"
            " 1. #?# 1\n";
        auto vr = validate_text(text);
        test_false(vr.report.has_errors());
        if (!vr.file.sections.empty() && vr.file.sections[0].fields.size() == 2) {
            test_true(vr.file.sections[0].fields[0].required);
            test_false(vr.file.sections[0].fields[1].required);
        }
    });

    s.add("field: only-length form (no constraint) accepted", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# name/String/100\n"
            "=== Data ===\n"
            " 1. #?# hello\n";
        auto vr = validate_text(text);
        test_false(vr.report.has_errors());
        if (!vr.file.sections.empty() && !vr.file.sections[0].fields.empty()) {
            test_true(vr.file.sections[0].fields[0].max_length.has_value());
            if (vr.file.sections[0].fields[0].max_length) {
                test_eq(*vr.file.sections[0].fields[0].max_length, std::size_t{100});
            }
            test_false(vr.file.sections[0].fields[0].required);  // default Nullable
        }
    });

    // ──────────────────────────────────────────────────────────
    //  Field position rules
    // ──────────────────────────────────────────────────────────

    s.add("field: gap in positions is rejected", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            " 3. #/# name/String/Not Null\n"   // skipped 2
            "=== Data ===\n"
            " 1. #?# 1\n";
        auto vr = validate_text(text);
        test_true(vr.report.has_errors());
        test_true(count_issues_of(vr.report,
                  issue_kind::field_position_gap) > 0);
    });

    s.add("field: duplicate position is rejected", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            " 1. #/# name/String/Not Null\n"   // duplicate
            "=== Data ===\n"
            " 1. #?# 1\n";
        auto vr = validate_text(text);
        test_true(vr.report.has_errors());
        test_true(count_issues_of(vr.report,
                  issue_kind::field_position_duplicate) > 0);
    });

    // ──────────────────────────────────────────────────────────
    //  Record assembly (blank-line separators)
    // ──────────────────────────────────────────────────────────

    s.add("records: two records separated by blank line", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            " 2. #/# name/String/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 1\n"
            " 2. #?# Alice\n"
            "\n"
            " 1. #?# 2\n"
            " 2. #?# Bob\n"
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_false(vr.report.has_errors());
        test_eq(vr.file.sections.size(), std::size_t{1});
        if (!vr.file.sections.empty()) {
            test_eq(vr.file.sections[0].records.size(), std::size_t{2});
            if (vr.file.sections[0].records.size() == 2) {
                const auto& r1 = vr.file.sections[0].records[0];
                const auto& r2 = vr.file.sections[0].records[1];
                if (r1.values.size() == 2 && r1.values[1]) {
                    test_eq(*r1.values[1], std::string{"Alice"});
                }
                if (r2.values.size() == 2 && r2.values[1]) {
                    test_eq(*r2.values[1], std::string{"Bob"});
                }
            }
        }
    });

    s.add("records: duplicate field within record is rejected", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            " 2. #/# name/String/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 1\n"
            " 1. #?# 99\n"   // duplicate field 1
            " 2. #?# Alice\n"
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_true(vr.report.has_errors());
        test_true(count_issues_of(vr.report,
                  issue_kind::record_field_duplicate) > 0);
    });

    s.add("records: out-of-sequence field within record is rejected", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# a/String/Not Null\n"
            " 2. #/# b/String/Not Null\n"
            " 3. #/# c/String/Not Null\n"
            "=== Data ===\n"
            " 1. #?# x\n"
            " 3. #?# z\n"
            " 2. #?# y\n"   // out of sequence (3 then 2)
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_true(vr.report.has_errors());
        test_true(count_issues_of(vr.report,
                  issue_kind::record_field_out_of_sequence) > 0);
    });

    s.add("records: field number > field count is rejected", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            " 2. #/# name/String/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 1\n"
            " 5. #?# bogus\n"   // field 5 doesn't exist
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_true(vr.report.has_errors());
        test_true(count_issues_of(vr.report,
                  issue_kind::record_field_out_of_range) > 0);
    });

    s.add("records: skipping a Nullable field is allowed", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            " 2. #/# note/String/Nullable\n"
            " 3. #/# tag/String/Nullable\n"
            "=== Data ===\n"
            " 1. #?# 1\n"
            " 3. #?# important\n"   // skipped field 2 (Nullable)
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_false(vr.report.has_errors());
        if (!vr.file.sections.empty() && !vr.file.sections[0].records.empty()) {
            const auto& rec = vr.file.sections[0].records[0];
            if (rec.values.size() == 3) {
                test_true(rec.values[0].has_value());
                test_false(rec.values[1].has_value());  // missing → nullopt
                test_true(rec.values[2].has_value());
            }
        }
    });

    s.add("records: missing required field is rejected", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            " 2. #/# name/String/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 1\n"
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_true(vr.report.has_errors());
        test_true(count_issues_of(vr.report,
                  issue_kind::record_missing_required_field) > 0);
    });

    s.add("records: empty value in required field is rejected", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            " 2. #/# name/String/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 1\n"
            " 2. #?#\n"   // empty value
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_true(vr.report.has_errors());
        test_true(count_issues_of(vr.report,
                  issue_kind::record_missing_required_field) > 0);
    });

    // ──────────────────────────────────────────────────────────
    //  Value type checking
    // ──────────────────────────────────────────────────────────

    s.add("type: Number rejects non-numeric value", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            "=== Data ===\n"
            " 1. #?# abc\n"
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_true(vr.report.has_errors());
        test_true(count_issues_of(vr.report,
                  issue_kind::record_value_type_mismatch) > 0);
    });

    s.add("type: Number accepts integer, decimal, signed, scientific", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# v/Number/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 42\n"
            "\n"
            " 1. #?# -3.14\n"
            "\n"
            " 1. #?# +0.5\n"
            "\n"
            " 1. #?# 1e10\n"
            "\n"
            " 1. #?# 6.022e23\n"
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_false(vr.report.has_errors());
    });

    s.add("type: Date accepts YYYY-MM-DD", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# d/Date/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 2026-05-12\n"
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_false(vr.report.has_errors());
    });

    s.add("type: Date accepts YYYY-MM-DDTHH:MM:SS", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# d/Date/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 2026-05-12T14:30:00\n"
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_false(vr.report.has_errors());
    });

    s.add("type: Date rejects malformed values", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# d/Date/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 05/12/2026\n"
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_true(vr.report.has_errors());
        test_true(count_issues_of(vr.report,
                  issue_kind::record_value_type_mismatch) > 0);
    });

    s.add("type: String over max_length is rejected", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# code/String/3/Not Null\n"
            "=== Data ===\n"
            " 1. #?# abcdef\n"   // 6 > 3
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_true(vr.report.has_errors());
        test_true(count_issues_of(vr.report,
                  issue_kind::record_value_length_exceeded) > 0);
    });

    s.add("type: String within max_length passes", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# code/String/10/Not Null\n"
            "=== Data ===\n"
            " 1. #?# abc\n"
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_false(vr.report.has_errors());
    });

    // ──────────────────────────────────────────────────────────
    //  Header interpretation
    // ──────────────────────────────────────────────────────────

    s.add("header: recognized fields are interpreted by position", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# inv.jtext\n"
            " 2. #?# 2026-05-12\n"
            " 3. #?# Inventory data\n"
            " 4. #?# 0.6\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 1\n"
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_false(vr.report.has_errors());
        test_eq(vr.file.header.at("filename"),      std::string{"inv.jtext"});
        test_eq(vr.file.header.at("date"),          std::string{"2026-05-12"});
        test_eq(vr.file.header.at("purpose"),       std::string{"Inventory data"});
        test_eq(vr.file.header.at("jtext_version"), std::string{"0.6"});
    });

    s.add("header: unknown line number is a warning, not an error", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "20. #?# something_unknown\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 1\n"
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_false(vr.report.has_errors());
        test_true(vr.report.has_warnings());
        test_true(count_issues_of(vr.report,
                  issue_kind::header_unknown_field) > 0);
    });

    // ──────────────────────────────────────────────────────────
    //  Section-level checks
    // ──────────────────────────────────────────────────────────

    s.add("section: empty data block produces a warning", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            "=== Data ===\n"
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_false(vr.report.has_errors());
        test_true(count_issues_of(vr.report,
                  issue_kind::section_empty) > 0);
    });

    s.add("section: duplicate section names produce a warning", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: Same ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 1\n"
            "=== Section: Same ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 2\n"
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_false(vr.report.has_errors());
        test_true(count_issues_of(vr.report,
                  issue_kind::section_duplicate_name) > 0);
    });

    // ──────────────────────────────────────────────────────────
    //  Report accessors
    // ──────────────────────────────────────────────────────────

    s.add("report: error/warning counts and has_* accessors", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            "=== Data ===\n"
            " 1. #?# abc\n"     // type mismatch (error)
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_true(vr.report.has_errors());
        test_true(vr.report.error_count() > 0);
    });

    s.add("report: clean file has no errors and no warnings", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 1\n"
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_false(vr.report.has_errors());
        test_false(vr.report.has_warnings());
        test_eq(vr.report.error_count(),   std::size_t{0});
        test_eq(vr.report.warning_count(), std::size_t{0});
    });

    // ──────────────────────────────────────────────────────────
    //  to_string(issue_kind) — readable names
    // ──────────────────────────────────────────────────────────

    s.add("to_string(issue_kind): produces readable names", [] {
        test_eq(std::string{to_string(issue_kind::field_invalid_type)},
                std::string{"field_invalid_type"});
        test_eq(std::string{to_string(issue_kind::record_field_duplicate)},
                std::string{"record_field_duplicate"});
        test_eq(std::string{to_string(issue_severity::error)},
                std::string{"error"});
        test_eq(std::string{to_string(issue_severity::warning)},
                std::string{"warning"});
    });

    // ──────────────────────────────────────────────────────────
    //  End-to-end: SPEC worked example validates cleanly
    // ──────────────────────────────────────────────────────────

    s.add("validate: SPEC worked example validates cleanly", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# tools_inventory.jText\n"
            " 2. #?# 2026-05-11\n"
            " 3. #?# Personal workshop tool catalog\n"
            " 4. #?# 0.6\n"
            "\n"
            "=== Section: Hand Tools ===\n"
            "=== Template: Create Table ===\n"
            " 1. <<< !!!CT!!!\n"
            "CREATE TABLE tools (id INTEGER, name VARCHAR(40));\n"
            "!!!CT!!!\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            " 2. #/# name/String/40/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 1\n"
            " 2. #?# Stanley Hammer\n"
            "\n"
            " 1. #?# 2\n"
            " 2. #?# DeWalt Drill DCD777\n"
            "=== End Data ===\n"
            "=== End Section ===\n"
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_false(vr.report.has_errors());
        test_eq(vr.file.sections.size(),                std::size_t{1});
        if (!vr.file.sections.empty()) {
            test_eq(vr.file.sections[0].fields.size(),  std::size_t{2});
            test_eq(vr.file.sections[0].records.size(), std::size_t{2});
            test_eq(vr.file.sections[0].templates.size(), std::size_t{1});
        }
        test_eq(vr.file.header.at("filename"),
                std::string{"tools_inventory.jText"});
    });

    s.add("apply_numbered_template: basic substitution and NULL handling", [] {
        std::string body = "INSERT INTO t (a,b,c) VALUES ({1}, {2}, {3});";
        std::vector<std::optional<std::string>> vals = {"1", "foo", std::nullopt};
        std::string out = apply_numbered_template(body, vals);
        test_eq(out, "INSERT INTO t (a,b,c) VALUES (1, foo, NULL);");
    });

    s.add("apply_numbered_template: ts_store style with OR IGNORE", [] {
        std::string body = "INSERT OR IGNORE INTO persist (id, cat) VALUES ({1}, {2});";
        std::vector<std::optional<std::string>> vals = {"42", "AUTH"};
        std::string out = apply_numbered_template(body, vals);
        test_eq(out, "INSERT OR IGNORE INTO persist (id, cat) VALUES (42, AUTH);");
    });

    s.add("include markers: captured in validated_file for ts_store style logs", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# ts.jtext\n"
            " 2. #?# 2026-06-05\n"
            " 3. #?# test\n"
            "\n"
            "=== Section: test ===\n"
            "=== <#include#> Schema: common.jschma ===\n"
            "=== <#include#> Fields: common.jtFlds ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 1\n"
            "=== End Data ===\n"
            "=== End Section ===\n"
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_false(vr.report.has_errors());
        if (!vr.file.sections.empty()) {
            const auto& incs = vr.file.sections[0].includes;
            test_eq(incs.size(), std::size_t{2});
            if (incs.size() >= 2) {
                test_eq(incs[0].first, std::string{"Schema"});
                test_eq(incs[0].second, std::string{"common.jschma"});
                test_eq(incs[1].first, std::string{"Fields"});
                test_eq(incs[1].second, std::string{"common.jtFlds"});
            }
        }
    });

    s.add("full ts_store compact roundtrip with null: parse -> validate -> records + sub", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: s ===\n"
            "=== <#include#> Schema: s.jschma ===\n"
            "=== <#include#> Fields: s.jtFlds ===\n"
            "=== Template: SQL Insert ===\n"
            " 1. <<< !!!SI!!!\n"
            "INSERT INTO t (id,v) VALUES ({1},{2});\n"
            "!!!SI!!!\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            " 2. #/# v/String\n"
            "=== Data ===\n"
            " 1. #|# 1|foo\n"
            " 2. #|# 2|\x1F\n"  // null
            "=== End Data ===\n"
            "=== End Section ===\n"
            "=== End File ===\n";
        auto vr = validate_text(text);
        test_false(vr.report.has_errors());
        if (!vr.file.sections.empty()) {
            const auto& sec = vr.file.sections[0];
            test_eq(sec.records.size(), std::size_t{2});
            test_false(sec.records[1].values[1].has_value());
            // sub
            std::string tmpl = "INSERT INTO t (id,v) VALUES ({1},{2});";
            std::string out0 = apply_numbered_template(tmpl, sec.records[0].values);
            test_eq(out0, "INSERT INTO t (id,v) VALUES (1,foo);");
            std::string out1 = apply_numbered_template(tmpl, sec.records[1].values);
            test_eq(out1, "INSERT INTO t (id,v) VALUES (2,NULL);");
        }
    });
}

