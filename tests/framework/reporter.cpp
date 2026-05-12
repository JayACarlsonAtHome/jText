// File: tests/framework/reporter.cpp

#include "reporter.hpp"

#include <chrono>
#include <cstddef>
#include <fstream>
#include <format>
#include <print>
#include <string>

namespace jtext::test {

namespace {

// Render the current local time as YYYY-MM-DD HH:MM:SS.
auto timestamp_now() -> std::string
{
    const auto now = std::chrono::system_clock::now();
    return std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::floor<std::chrono::seconds>(now));
}

// Sum totals across all tests.
struct totals {
    std::size_t test_count        = 0;
    std::size_t test_passed       = 0;
    std::size_t test_failed       = 0;
    std::size_t assertion_passed  = 0;
    std::size_t assertion_failed  = 0;
};

auto compute_totals(const std::vector<test_result>& results) -> totals
{
    totals t;
    t.test_count = results.size();
    for (const auto& r : results) {
        t.assertion_passed += r.passed;
        t.assertion_failed += r.failed;
        if (r.failed == 0) {
            t.test_passed += 1;
        } else {
            t.test_failed += 1;
        }
    }
    return t;
}

}  // namespace

// ──────────────────────────────────────────────────────────────
//  write_report
// ──────────────────────────────────────────────────────────────

auto write_report(
    std::string_view path,
    const std::vector<test_result>& results) -> bool
{
    std::ofstream out{std::string{path}};
    if (!out) return false;

    const auto t = compute_totals(results);

    // ── Summary header ──────────────────────────────────────
    out << "================================================================\n";
    out << " jText Test Report\n";
    out << "================================================================\n";
    out << "Date:      " << timestamp_now() << "\n";
    out << "Tests:     " << t.test_count
        << "   (passed: " << t.test_passed
        << ", failed: " << t.test_failed << ")\n";
    out << "Asserts:   " << (t.assertion_passed + t.assertion_failed)
        << "   (passed: " << t.assertion_passed
        << ", failed: " << t.assertion_failed << ")\n";

    if (t.test_failed > 0) {
        out << "\nFailed tests:\n";
        for (const auto& r : results) {
            if (r.failed > 0) {
                const auto total = r.passed + r.failed;
                out << "  - " << r.name
                    << " (" << r.failed << " of " << total << " "
                    << (total == 1 ? "assertion" : "assertions")
                    << " failed)\n";
            }
        }
    }

    // ── Per-test detail ─────────────────────────────────────
    out << "\n================================================================\n";
    out << " Test Details\n";
    out << "================================================================\n";

    for (const auto& r : results) {
        out << "\n";
        out << "----------------------------------------------------------------\n";
        out << " " << (r.failed == 0 ? "[PASS]" : "[FAIL]")
            << "  " << r.name
            << "   (" << r.passed << " passed, " << r.failed << " failed)\n";
        out << "----------------------------------------------------------------\n";

        for (const auto& a : r.assertions) {
            out << "  " << (a.passed ? "[PASS]" : "[FAIL]")
                << " " << a.file << ":" << a.line
                << " (" << a.type_name << ") " << a.kind << "\n";
            if (!a.passed) {
                out << "         expected: " << a.expected_str << "\n";
                out << "         actual:   " << a.actual_str   << "\n";
            }
        }
    }

    out << "\n================================================================\n";
    out << " End of Report\n";
    out << "================================================================\n";

    return out.good();
}

// ──────────────────────────────────────────────────────────────
//  print_terminal_summary
// ──────────────────────────────────────────────────────────────

auto print_terminal_summary(
    const std::vector<test_result>& results,
    const runner_config&            cfg) -> void
{
    if (cfg.quiet) return;

    const auto t = compute_totals(results);

    std::println("");
    std::println("══════════════════════════════════════════");
    std::println(" SUMMARY");
    std::println("══════════════════════════════════════════");
    std::println("  Tests:   {}  (passed: {}, failed: {})",
                 t.test_count, t.test_passed, t.test_failed);
    std::println("  Asserts: {}  (passed: {}, failed: {})",
                 t.assertion_passed + t.assertion_failed,
                 t.assertion_passed, t.assertion_failed);

    if (t.test_failed > 0) {
        std::println("");
        std::println("  Failed tests:");
        for (const auto& r : results) {
            if (r.failed > 0) {
                const auto total = r.passed + r.failed;
                std::println("    ✗ {}  ({} of {} {} failed)",
                             r.name, r.failed, total,
                             total == 1 ? "assertion" : "assertions");
            }
        }
    } else {
        std::println("");
        std::println("  ✓ All tests passed.");
    }

    if (cfg.write_report) {
        std::println("");
        std::println("  Report: {}", cfg.report_path);
    }
    std::println("══════════════════════════════════════════");
}

}  // namespace jtext::test

