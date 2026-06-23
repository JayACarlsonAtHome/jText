// Negative / hardening tests for the jText line parser and structure parser.
//
// The line parser already has dedicated error kinds for the malformed cases
// below; these tests lock that behaviour in (regression guard) and cover the
// newly added rule that a multiline sentinel must be printable ASCII.
//
// Links jac313::jText, which exposes the in-tree src/ headers.

#include "parser/line.hpp"
#include "parser/section.hpp"
#include "validator/validator.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

// Assert parse_line(in) fails with exactly error kind k.
void expect_err(std::string_view in, jtext::parse_error_kind k, const char* what) {
    auto r = jtext::parse_line(in);
    if (r.has_value()) {
        std::cerr << "FAIL[" << what << "]: expected error, parsed OK: \"" << in << "\"\n";
        ++failures;
        return;
    }
    if (r.error().kind != k) {
        std::cerr << "FAIL[" << what << "]: expected " << jtext::to_string(k)
                  << " but got " << jtext::to_string(r.error().kind)
                  << " for \"" << in << "\"\n";
        ++failures;
    }
}

// Assert parse_line(in) succeeds.
void expect_ok(std::string_view in, const char* what) {
    auto r = jtext::parse_line(in);
    if (!r.has_value()) {
        std::cerr << "FAIL[" << what << "]: expected OK, got "
                  << jtext::to_string(r.error().kind) << " for \"" << in << "\"\n";
        ++failures;
    }
}

// Build a minimal valid jText file carrying `date` in a Date field, parse +
// validate it, and report whether validation produced any error. Everything
// else in the document is valid, so an error means the date was rejected.
bool date_validation_errors(const std::string& date) {
    const std::string doc =
        "=== jText File ===\n"
        " 1. #?# date_test\n"
        " 2. #?# 2026-06-23\n"
        " 3. #?# strict date validation\n"
        " 4. #?# 0.6\n"
        "\n"
        "=== Section: D ===\n"
        "\n"
        "=== Fields ===\n"
        " 1. #/# id/Number/Not Null\n"
        " 2. #/# d/Date/Nullable\n"
        "\n"
        "=== Data ===\n"
        " 1. #?# 1\n"
        " 2. #?# " + date + "\n"
        "\n"
        "=== End Data ===\n"
        "=== End Section ===\n"
        "=== End File ===\n";
    auto pf = jtext::parse_file_structure(doc);
    if (!pf.has_value()) return true;  // structural rejection counts as an error
    return jtext::validate(*pf).report.has_errors();
}

void expect_date_ok(const std::string& date) {
    if (date_validation_errors(date)) {
        std::cerr << "FAIL[date-ok]: valid date rejected: \"" << date << "\"\n";
        ++failures;
    }
}
void expect_date_rejected(const std::string& date) {
    if (!date_validation_errors(date)) {
        std::cerr << "FAIL[date-reject]: invalid date accepted: \"" << date << "\"\n";
        ++failures;
    }
}

// Build a 2-field section whose template body is `body`, then parse + validate
// and report whether validation produced any error. Used to exercise the
// template {N} placeholder range check.
bool template_validation_errors(const std::string& body) {
    const std::string doc =
        "=== jText File ===\n"
        " 1. #?# tmpl_test\n"
        "\n"
        "=== Section: S ===\n"
        "\n"
        "=== Template: T ===\n"
        " 1. <<< !!!T!!!\n"
        + body + "\n"
        "!!!T!!!\n"
        "\n"
        "=== Fields ===\n"
        " 1. #/# a/String/Nullable\n"
        " 2. #/# b/String/Nullable\n"
        "\n"
        "=== Data ===\n"
        " 1. #?# x\n"
        " 2. #?# y\n"
        "\n"
        "=== End Data ===\n"
        "=== End Section ===\n"
        "=== End File ===\n";
    auto pf = jtext::parse_file_structure(doc);
    if (!pf.has_value()) return true;
    return jtext::validate(*pf).report.has_errors();
}

}  // namespace

int main() {
    using K = jtext::parse_error_kind;

    // --- line numbers ---
    expect_err("0. #?# x",   K::invalid_line_number, "line-number-zero");
    expect_err("100. #?# x", K::invalid_line_number, "line-number-over-99");
    expect_err("x. #?# data", K::invalid_line_number, "line-number-nonnumeric");
    expect_err("1 #?# x",    K::missing_period,      "missing-period");

    // --- formatter rules ---
    expect_err("1. #?$ x", K::formatter_bookends_mismatch,        "bookend-mismatch");
    expect_err("1. ### x", K::formatter_separator_equals_bookend, "sep-equals-bookend");

    // --- hierarchy ---
    expect_err("1. #/# A//B", K::hierarchy_empty_segment, "hierarchy-empty-middle");

    // --- multiline sentinel ---
    expect_err("1. <<< ab", K::multiline_sentinel_invalid, "sentinel-too-short");
    {
        // sentinel with an embedded control byte -> rejected by the new ASCII rule
        std::string ctl = "1. <<< a";
        ctl += '\x01';
        ctl += 'c';
        expect_err(ctl, K::multiline_sentinel_invalid, "sentinel-control-char");
    }

    // --- positives (must still parse) ---
    expect_ok("1. #?# hello world", "ok-simple-data");
    expect_ok("1. #/# A/B/C",       "ok-hierarchy");
    expect_ok("1. <<< !!!END!!!",   "ok-sentinel");

    // --- strict Date validation: exactly YYYY-MM-DD, real calendar date ---
    expect_date_ok("2026-06-23");                 // ordinary valid date
    expect_date_ok("2024-02-29");                 // valid leap day
    expect_date_rejected("2026-02-29");           // Feb 29 in a non-leap year
    expect_date_rejected("2026-13-45");           // month/day out of range
    expect_date_rejected("2026-06-23T10:00:00");  // datetime no longer accepted
    expect_date_rejected("2026/06/23");           // wrong separator
    expect_date_rejected("06-23-2026");           // wrong order / not ISO
    expect_date_rejected("26-06-23");             // 2-digit year

    // --- template {N} placeholder range (section declares 2 fields) ---
    if (template_validation_errors("INSERT VALUES ({1}, {2})")) {
        std::cerr << "FAIL[tmpl-ok]: in-range placeholders rejected\n"; ++failures;
    }
    if (!template_validation_errors("INSERT VALUES ({1}, {3})")) {
        std::cerr << "FAIL[tmpl-range]: {3} accepted with only 2 fields\n"; ++failures;
    }
    if (!template_validation_errors("VALUES ({0})")) {
        std::cerr << "FAIL[tmpl-zero]: {0} accepted\n"; ++failures;
    }

    if (failures == 0) {
        std::cout << "parser/validator negative tests: all passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << failures << " parser/validator negative test(s) failed\n";
    return EXIT_FAILURE;
}
