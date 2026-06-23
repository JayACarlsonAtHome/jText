// Negative / hardening tests for the jText line parser and structure parser.
//
// The line parser already has dedicated error kinds for the malformed cases
// below; these tests lock that behaviour in (regression guard) and cover the
// newly added rule that a multiline sentinel must be printable ASCII.
//
// Links jac313::jText, which exposes the in-tree src/ headers.

#include "parser/line.hpp"
#include "parser/section.hpp"

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

    if (failures == 0) {
        std::cout << "parser/validator negative tests: all passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << failures << " parser/validator negative test(s) failed\n";
    return EXIT_FAILURE;
}
