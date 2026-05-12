// File: tests/framework/runner.cpp

#include "runner.hpp"
#include "reporter.hpp"

#include <cstring>
#include <print>
#include <string>
#include <string_view>

namespace jtext::test {

namespace {

// Strip the directory portion of argv[0] for the usage message.
auto basename(std::string_view path) -> std::string_view
{
    const auto slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

}  // namespace

// ──────────────────────────────────────────────────────────────
//  parse_args
// ──────────────────────────────────────────────────────────────

auto parse_args(int argc, char* argv[]) -> runner_config
{
    runner_config cfg;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        }
        else if (arg == "-v" || arg == "--verbose") {
            cfg.verbose = true;
        }
        else if (arg == "-q" || arg == "--quiet") {
            cfg.quiet = true;
        }
        else if (arg == "--no-report") {
            cfg.write_report = false;
        }
        else if (arg.starts_with("--report=")) {
            cfg.report_path = std::string{arg.substr(std::string_view{"--report="}.size())};
        }
        else if (arg == "--report") {
            if (i + 1 < argc) {
                cfg.report_path = argv[++i];
            } else {
                std::println(stderr, "error: --report requires an argument");
                std::exit(2);
            }
        }
        else {
            std::println(stderr, "error: unknown argument: {}", arg);
            print_usage(argv[0]);
            std::exit(2);
        }
    }

    return cfg;
}

// ──────────────────────────────────────────────────────────────
//  print_usage
// ──────────────────────────────────────────────────────────────

auto print_usage(std::string_view prog_name) -> void
{
    const auto name = basename(prog_name);
    std::println("Usage: {} [options]", name);
    std::println("");
    std::println("Runs all registered tests and reports results.");
    std::println("");
    std::println("Options:");
    std::println("  -h, --help          show this message and exit");
    std::println("  -v, --verbose       print each PASS line to stdout");
    std::println("                      (default: only FAIL lines are printed)");
    std::println("  -q, --quiet         suppress all stdout output");
    std::println("                      (exit code still reflects pass/fail)");
    std::println("      --no-report     do not write the report file");
    std::println("      --report=PATH   write report to PATH (default: test_report.log)");
}

// ──────────────────────────────────────────────────────────────
//  suite::run(argc, argv)
// ──────────────────────────────────────────────────────────────

auto suite::run(int argc, char* argv[]) -> int
{
    const auto cfg = parse_args(argc, argv);
    return run(cfg);
}

// ──────────────────────────────────────────────────────────────
//  suite::run(cfg)
//
//  Iterates the registered tests in registration order. For each:
//    1. Creates a fresh sink and points the assertion machinery at it
//    2. Prints a one-line header (unless --quiet)
//    3. Invokes the test function
//    4. Captures the sink's results into a test_result
//
//  After all tests run, generates terminal summary and report file.
// ──────────────────────────────────────────────────────────────

auto suite::run(const runner_config& cfg) -> int
{
    std::vector<test_result> results;
    results.reserve(tests_.size());

    if (!cfg.quiet) {
        std::println("══════════════════════════════════════════");
        std::println(" jText test runner");
        std::println("══════════════════════════════════════════");
        std::println("  Tests registered: {}", tests_.size());
        std::println("  Verbose:          {}", cfg.verbose ? "yes" : "no");
        std::println("");
    }

    for (const auto& [name, fn] : tests_) {
        sink s;
        s.verbose = cfg.verbose;
        s.quiet   = cfg.quiet;
        set_active_sink(s);

        if (!cfg.quiet) {
            std::println("──────────────────────────────────────────");
            std::println(" TEST: {}", name);
            std::println("──────────────────────────────────────────");
        }

        fn();

        clear_active_sink();

        // Per-test footer line (so failures stand out even without --verbose)
        if (!cfg.quiet) {
            if (s.failed == 0) {
                std::println("    [PASS] all {} {} passed",
                             s.passed,
                             s.passed == 1 ? "assertion" : "assertions");
            } else {
                std::println("    [FAIL] {} of {} {} failed",
                             s.failed, s.passed + s.failed,
                             (s.passed + s.failed) == 1 ? "assertion" : "assertions");
            }
        }

        results.push_back(test_result{
            .name       = name,
            .passed     = s.passed,
            .failed     = s.failed,
            .assertions = std::move(s.log),
        });
    }

    // Final summary
    print_terminal_summary(results, cfg);

    // Report file
    if (cfg.write_report) {
        if (!write_report(cfg.report_path, results)) {
            if (!cfg.quiet) {
                std::println(stderr,
                             "warning: could not write report file: {}",
                             cfg.report_path);
            }
        }
    }

    // Exit code: 0 if every test passed, 1 otherwise
    for (const auto& r : results) {
        if (r.failed > 0) return 1;
    }
    return 0;
}

}  // namespace jtext::test

