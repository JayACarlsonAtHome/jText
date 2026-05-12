// File: tests/framework/runner.hpp
//
// Test registration and orchestration.
//
// Tests are registered as named function objects with the suite, then run
// in declaration order. The runner captures per-assertion results into
// the framework's context, gives each test its own pass/fail tally, and
// produces a report at the end.
//
// Usage pattern:
//
//   int main(int argc, char* argv[])
//   {
//       jtext::test::suite s;
//       s.add("integer equality", [] {
//           test_eq(1 + 1, 2);
//       });
//       s.add("string handling", [] {
//           test_eq(std::string{"hello"}, std::string{"hello"});
//       });
//       return s.run(argc, argv);
//   }

#pragma once

#include "assertion.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace jtext::test {

// ──────────────────────────────────────────────────────────────
//  Per-test result record. Populated as the test runs; consumed
//  by the reporter.
// ──────────────────────────────────────────────────────────────

struct test_result {
    std::string                 name;
    std::size_t                 passed       = 0;
    std::size_t                 failed       = 0;
    std::vector<assertion_log>  assertions;  // every assertion made in this test
};

// ──────────────────────────────────────────────────────────────
//  Configuration parsed from command-line arguments.
//  Defaults are tuned for normal local development:
//    - report file written to ./test_report.log
//    - PASS lines NOT shown on stdout (only FAIL lines)
//    - stdout output enabled
//  Flags can override each of these.
// ──────────────────────────────────────────────────────────────

struct runner_config {
    std::string  report_path = "test_report.log";
    bool         verbose     = false;   // --verbose / -v : show PASS lines too
    bool         quiet       = false;   // --quiet / -q   : no stdout at all
    bool         write_report = true;   // --no-report    : skip the file
};

// ──────────────────────────────────────────────────────────────
//  suite — the runner. Holds the registered tests and drives them.
// ──────────────────────────────────────────────────────────────

class suite {
public:
    using test_fn = std::function<void()>;

    suite()  = default;
    ~suite() = default;

    suite(const suite&)            = delete;
    suite& operator=(const suite&) = delete;
    suite(suite&&)                 = delete;
    suite& operator=(suite&&)      = delete;

    // Register a test by name. Order of registration is order of execution.
    auto add(std::string name, test_fn fn) -> void
    {
        tests_.push_back(registered_test{std::move(name), std::move(fn)});
    }

    // Parse argv into config and run all registered tests.
    // Returns 0 if all passed, 1 if any failed.
    auto run(int argc, char* argv[]) -> int;

    // Run with an explicit config (skips argv parsing). Same return value.
    auto run(const runner_config& cfg) -> int;

private:
    struct registered_test {
        std::string name;
        test_fn     fn;
    };

    std::vector<registered_test>  tests_;
};

// ──────────────────────────────────────────────────────────────
//  Parse argv into a runner_config.
//  Recognized flags:
//    -v, --verbose          show PASS lines on stdout
//    -q, --quiet            no stdout at all
//    --no-report            do not write the report file
//    --report=PATH          write report to PATH instead of default
//    -h, --help             print usage and exit
// ──────────────────────────────────────────────────────────────

auto parse_args(int argc, char* argv[]) -> runner_config;

// Print the usage block for --help.
auto print_usage(std::string_view prog_name) -> void;

}  // namespace jtext::test
