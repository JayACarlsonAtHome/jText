// File: tests/manual/categories/section_basic.hpp
//
// Tests for the file/section structure parser (src/parser/section.cpp).
// Covers SPEC v0.5 Section 2: file header, section banners, fields and
// data blocks, templates with multiline bodies, end markers, and the
// structural error cases.
//
// The tests work with string-input via the std::string_view overload;
// the istream version is exercised via a small dedicated test.

#pragma once

#include "framework/assertion.hpp"
#include "framework/runner.hpp"
#include "parser/section.hpp"

#include <sstream>
#include <string>
#include <string_view>

inline auto register_section_basic_tests(jtext::test::suite& s) -> void
{
    using namespace jtext;
    using namespace jtext::test;

    // ──────────────────────────────────────────────────────────
    //  is_structural_marker — the building-block helper
    // ──────────────────────────────────────────────────────────

    s.add("marker: '=== jText File ===' recognized", [] {
        std::string interior;
        test_true(is_structural_marker("=== jText File ===", interior));
        test_eq(interior, std::string{"jText File"});
    });

    s.add("marker: leading/trailing whitespace tolerated", [] {
        std::string interior;
        test_true(is_structural_marker("   === Section: tools ===   ", interior));
        test_eq(interior, std::string{"Section: tools"});
    });

    s.add("marker: line without trailing === is not a marker", [] {
        std::string interior;
        test_false(is_structural_marker("=== Section: tools", interior));
    });

    s.add("marker: line without leading === is not a marker", [] {
        std::string interior;
        test_false(is_structural_marker("Section: tools ===", interior));
    });

    s.add("marker: ordinary data line is not a marker", [] {
        std::string interior;
        test_false(is_structural_marker("1. #?# hello", interior));
    });

    // ──────────────────────────────────────────────────────────
    //  Minimal valid file — header only
    // ──────────────────────────────────────────────────────────

    s.add("file: header-only file with End File parses", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# myfile.jtext\n"
            " 2. #?# 2026-05-12\n"
            "=== End File ===\n";
        auto r = parse_file_structure(text);
        test_true(r.has_value());
        if (r) {
            test_eq(r->header_lines.size(), std::size_t{2});
            test_eq(r->sections.size(), std::size_t{0});
            test_eq(r->header_lines[0].number, 1u);
            test_eq(r->header_lines[0].hierarchy[0], std::string{"myfile.jtext"});
        }
    });

    s.add("file: header-only file without End File parses", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# myfile.jtext\n"
            " 2. #?# 2026-05-12\n";
        auto r = parse_file_structure(text);
        test_true(r.has_value());
        if (r) {
            test_eq(r->header_lines.size(), std::size_t{2});
            test_eq(r->sections.size(), std::size_t{0});
        }
    });

    // ──────────────────────────────────────────────────────────
    //  Magic line — required
    // ──────────────────────────────────────────────────────────

    s.add("file: missing magic line is rejected", [] {
        const std::string_view text =
            " 1. #?# myfile.jtext\n"
            "=== End File ===\n";
        auto r = parse_file_structure(text);
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind, file_error_kind::missing_magic_line);
        }
    });

    s.add("file: empty input is rejected", [] {
        auto r = parse_file_structure("");
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind, file_error_kind::missing_magic_line);
        }
    });

    s.add("file: blank-only input is rejected", [] {
        auto r = parse_file_structure("\n\n   \n\t\n");
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind, file_error_kind::missing_magic_line);
        }
    });

    s.add("file: leading blank lines before magic are tolerated", [] {
        const std::string_view text =
            "\n"
            "\n"
            "=== jText File ===\n"
            " 1. #?# myfile.jtext\n";
        auto r = parse_file_structure(text);
        test_true(r.has_value());
        if (r) test_eq(r->header_lines.size(), std::size_t{1});
    });

    s.add("file: standard // header comments before magic are tolerated", [] {
        const std::string_view text =
            "//File:    test.jtFull\n"
            "//Date:    2026-06-05\n"
            "//Purpose: jText Data File\n"
            "//Related: type=PostgreSQL table=test\n"
            "//\n"
            "=== jText File ===\n"
            " 1. #?# test.jtFull\n"
            "=== End File ===\n";
        auto r = parse_file_structure(text);
        test_true(r.has_value());
        if (r) test_eq(r->header_lines.size(), std::size_t{1});
    });

    // ──────────────────────────────────────────────────────────
    //  Single section, fields + data
    // ──────────────────────────────────────────────────────────

    s.add("section: simple section with fields and data", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# tools.jtext\n"
            "\n"
            "=== Section: Tools ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            " 2. #/# name/String/40/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 1\n"
            " 2. #?# Hammer\n"
            "\n"
            " 1. #?# 2\n"
            " 2. #?# Drill\n"
            "=== End Data ===\n"
            "=== End Section ===\n"
            "=== End File ===\n";
        auto r = parse_file_structure(text);
        test_true(r.has_value());
        if (r) {
            test_eq(r->sections.size(), std::size_t{1});
            const auto& sec = r->sections[0];
            test_eq(sec.name, std::string{"Tools"});
            test_eq(sec.field_lines.size(), std::size_t{2});
            test_eq(sec.field_lines[0].hierarchy[0], std::string{"id"});
            test_eq(sec.field_lines[1].hierarchy[0], std::string{"name"});
            // 5 data_lines: 2 fields for record 1, blank sentinel
            // (number == 0), 2 fields for record 2.
            test_eq(sec.data_lines.size(), std::size_t{5});
            test_eq(sec.data_lines[0].hierarchy[0], std::string{"1"});
            test_eq(sec.data_lines[1].hierarchy[0], std::string{"Hammer"});
            test_eq(sec.data_lines[2].number,       0u);  // blank sentinel
            test_eq(sec.data_lines[3].hierarchy[0], std::string{"2"});
            test_eq(sec.data_lines[4].hierarchy[0], std::string{"Drill"});
            test_eq(sec.templates.size(), std::size_t{0});
        }
    });

    s.add("section: implicit End Data (Section banner closes data block)", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# x.jtext\n"
            "=== Section: A ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 1\n"
            "=== Section: B ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 100\n"
            "=== End File ===\n";
        auto r = parse_file_structure(text);
        test_true(r.has_value());
        if (r) {
            test_eq(r->sections.size(), std::size_t{2});
            test_eq(r->sections[0].name, std::string{"A"});
            test_eq(r->sections[1].name, std::string{"B"});
            test_eq(r->sections[0].data_lines.size(), std::size_t{1});
            test_eq(r->sections[1].data_lines.size(), std::size_t{1});
            test_eq(r->sections[1].data_lines[0].hierarchy[0], std::string{"100"});
        }
    });

    s.add("section: implicit End Section at EOF is tolerated", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# x.jtext\n"
            "=== Section: A ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 1\n";
        auto r = parse_file_structure(text);
        test_true(r.has_value());
        if (r) {
            test_eq(r->sections.size(), std::size_t{1});
            test_eq(r->sections[0].data_lines.size(), std::size_t{1});
        }
    });

    // ──────────────────────────────────────────────────────────
    //  Templates
    // ──────────────────────────────────────────────────────────

    s.add("template: simple Create Table template captured", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Template: Create Table ===\n"
            " 1. <<< !!!CT!!!\n"
            "CREATE TABLE foo (\n"
            "    id INTEGER PRIMARY KEY\n"
            ");\n"
            "!!!CT!!!\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 1\n"
            "=== End File ===\n";
        auto r = parse_file_structure(text);
        test_true(r.has_value());
        if (r) {
            const auto& sec = r->sections[0];
            test_eq(sec.templates.size(), std::size_t{1});
            const auto& tpl = sec.templates[0];
            test_eq(tpl.name,     std::string{"Create Table"});
            test_eq(tpl.sentinel, std::string{"!!!CT!!!"});
            test_true(tpl.body.find("CREATE TABLE foo") != std::string::npos);
            test_true(tpl.body.find("id INTEGER PRIMARY KEY") != std::string::npos);
            test_true(tpl.body.find("!!!CT!!!") == std::string::npos);
        }
    });

    s.add("template: two templates in one section", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Template: Create Table ===\n"
            " 1. <<< !!!CT!!!\n"
            "CREATE TABLE foo;\n"
            "!!!CT!!!\n"
            "=== Template: Insert ===\n"
            " 1. <<< !!!SI!!!\n"
            "INSERT INTO foo VALUES ({1});\n"
            "!!!SI!!!\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 1\n"
            "=== End File ===\n";
        auto r = parse_file_structure(text);
        test_true(r.has_value());
        if (r) {
            const auto& sec = r->sections[0];
            test_eq(sec.templates.size(), std::size_t{2});
            test_eq(sec.templates[0].name, std::string{"Create Table"});
            test_eq(sec.templates[1].name, std::string{"Insert"});
            test_eq(sec.templates[1].sentinel, std::string{"!!!SI!!!"});
        }
    });

    s.add("template: unterminated multiline body is rejected", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Template: Bad ===\n"
            " 1. <<< !!!END!!!\n"
            "this body never closes\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n";
        auto r = parse_file_structure(text);
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind, file_error_kind::multiline_unterminated);
        }
    });

    // ──────────────────────────────────────────────────────────
    //  Multiline data values
    // ──────────────────────────────────────────────────────────

    s.add("data: multiline data field captured as raw body", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# t.jtext\n"
            "=== Section: T ===\n"
            "=== Fields ===\n"
            " 1. #/# trace/String/Not Null\n"
            "=== Data ===\n"
            " 1. <<< !!!END!!!\n"
            "line one\n"
            "line two\n"
            "!!!END!!!\n"
            "=== End File ===\n";
        auto r = parse_file_structure(text);
        test_true(r.has_value());
        if (r) {
            const auto& sec = r->sections[0];
            test_eq(sec.data_lines.size(), std::size_t{1});
            test_eq(sec.data_lines[0].kind, line_kind::multiline_opener);
            test_eq(sec.data_multiline_bodies.size(), std::size_t{1});
            test_eq(sec.data_multiline_bodies[0].data_line_index, std::size_t{0});
            test_true(sec.data_multiline_bodies[0].body.find("line one") != std::string::npos);
            test_true(sec.data_multiline_bodies[0].body.find("line two") != std::string::npos);
        }
    });

    // ──────────────────────────────────────────────────────────
    //  Error cases
    // ──────────────────────────────────────────────────────────

    s.add("error: data line between sections is rejected", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# x.jtext\n"
            "=== Section: A ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 1\n"
            "=== End Section ===\n"
            " 5. #?# orphan\n"
            "=== End File ===\n";
        auto r = parse_file_structure(text);
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind, file_error_kind::unexpected_marker);
        }
    });

    s.add("error: duplicate Data block is rejected", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# x.jtext\n"
            "=== Section: A ===\n"
            "=== Fields ===\n"
            " 1. #/# id/Number/Not Null\n"
            "=== Data ===\n"
            " 1. #?# 1\n"
            "=== Data ===\n"
            " 1. #?# 2\n"
            "=== End File ===\n";
        auto r = parse_file_structure(text);
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind, file_error_kind::duplicate_data_block);
        }
    });

    s.add("error: malformed line inside fields propagates", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# x.jtext\n"
            "=== Section: A ===\n"
            "=== Fields ===\n"
            " 1. #?$ broken\n"
            "=== Data ===\n"
            " 1. #?# 1\n"
            "=== End File ===\n";
        auto r = parse_file_structure(text);
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind, file_error_kind::line_parse_failed);
        }
    });

    s.add("error: Data block without Fields block is rejected", [] {
        // SPEC v0.6: every section that has a Data block must declare
        // its fields first. There is no meaningful Data without a
        // Fields block — line numbers in Data refer to field
        // positions declared in Fields.
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# x.jtext\n"
            "=== Section: A ===\n"
            "=== Data ===\n"
            " 1. #?# 1\n"
            "=== End File ===\n";
        auto r = parse_file_structure(text);
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind, file_error_kind::missing_fields_block);
        }
    });

    s.add("error: Data after Template (no Fields) is rejected", [] {
        // Templates appear before Fields in the canonical order, but
        // a section that has Templates and Data without a Fields
        // block between them is still missing the Fields block.
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# x.jtext\n"
            "=== Section: A ===\n"
            "=== Template: Insert ===\n"
            " 1. <<< !!!SI!!!\n"
            "INSERT INTO foo VALUES ({1});\n"
            "!!!SI!!!\n"
            "=== Data ===\n"
            " 1. #?# 1\n"
            "=== End File ===\n";
        auto r = parse_file_structure(text);
        test_false(r.has_value());
        if (!r) {
            test_eq(r.error().kind, file_error_kind::missing_fields_block);
        }
    });

    // ──────────────────────────────────────────────────────────
    //  The SPEC worked example (Section 8) round-trip
    // ──────────────────────────────────────────────────────────

    s.add("file: SPEC worked example parses cleanly", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# tools_inventory.jText\n"
            " 2. #?# 2026-05-11\n"
            " 3. #?# Personal workshop tool catalog\n"
            "\n"
            "=== Section: Hand Tools ===\n"
            "=== Template: Create Table ===\n"
            " 1. <<< !!!CT!!!\n"
            "CREATE TABLE tools (\n"
            "    id        INTEGER PRIMARY KEY,\n"
            "    name      VARCHAR(40) NOT NULL\n"
            ");\n"
            "!!!CT!!!\n"
            "=== Template: SQL Insert ===\n"
            " 1. <<< !!!SI!!!\n"
            "INSERT INTO tools (id, name) VALUES ({1}, {2});\n"
            "!!!SI!!!\n"
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
        auto r = parse_file_structure(text);
        test_true(r.has_value());
        if (r) {
            test_eq(r->header_lines.size(), std::size_t{3});
            test_eq(r->sections.size(), std::size_t{1});
            const auto& sec = r->sections[0];
            test_eq(sec.name, std::string{"Hand Tools"});
            test_eq(sec.templates.size(), std::size_t{2});
            test_eq(sec.field_lines.size(), std::size_t{2});
            // 5 = 2 fields + blank sentinel + 2 fields
            test_eq(sec.data_lines.size(), std::size_t{5});
        }
    });

    // ──────────────────────────────────────────────────────────
    //  Stream entry point
    // ──────────────────────────────────────────────────────────

    s.add("istream: parse_file_structure(istream) works equivalently", [] {
        const std::string_view text =
            "=== jText File ===\n"
            " 1. #?# x.jtext\n"
            "=== End File ===\n";
        std::istringstream in{std::string{text}};
        auto r = parse_file_structure(in);
        test_true(r.has_value());
        if (r) {
            test_eq(r->header_lines.size(), std::size_t{1});
            test_eq(r->sections.size(), std::size_t{0});
        }
    });

    // ──────────────────────────────────────────────────────────
    //  to_string(file_error_kind) — readable names
    // ──────────────────────────────────────────────────────────

    s.add("to_string(file_error_kind): produces readable names", [] {
        test_eq(std::string{to_string(file_error_kind::missing_magic_line)},
                std::string{"missing_magic_line"});
        test_eq(std::string{to_string(file_error_kind::missing_fields_block)},
                std::string{"missing_fields_block"});
        test_eq(std::string{to_string(file_error_kind::duplicate_data_block)},
                std::string{"duplicate_data_block"});
        test_eq(std::string{to_string(file_error_kind::multiline_unterminated)},
                std::string{"multiline_unterminated"});
    });
}

