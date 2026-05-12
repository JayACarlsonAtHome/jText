// File: tests/framework/reporter.hpp
//
// Produces a plain-text report file from the test_results gathered by
// the runner. The report has two sections:
//
//   1. Summary at the top: total counts, list of failed tests
//   2. Details below: every assertion with file:line, expected, actual
//
// Format is hand-readable; greppable for [PASS] / [FAIL] / test names.

#pragma once

#include "runner.hpp"

#include <string_view>
#include <vector>

namespace jtext::test {

// ──────────────────────────────────────────────────────────────
//  write_report
//  Writes a report file at `path` summarizing the given results.
//  Returns true on success, false on I/O error.
// ──────────────────────────────────────────────────────────────

auto write_report(
    std::string_view             path,
    const std::vector<test_result>& results) -> bool;

// ──────────────────────────────────────────────────────────────
//  print_terminal_summary
//  Prints the high-level summary to stdout. Always shown unless
//  --quiet is set.
// ──────────────────────────────────────────────────────────────

auto print_terminal_summary(
    const std::vector<test_result>& results,
    const runner_config&            cfg) -> void;

}  // namespace jtext::test
