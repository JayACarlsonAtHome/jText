// File: tests/manual/main.cpp
//
// Session 2 entry point.
//
// Creates a suite, registers all test categories with it, then hands off
// to suite::run(argc, argv) which:
//   - parses command-line arguments (--verbose, --quiet, --report=...)
//   - executes each test in registration order
//   - prints per-test output to stdout per verbosity settings
//   - writes a detailed test_report.log file
//   - returns 0 if all passed, 1 if any failed
//
// Adding a new test category means:
//   1. #include the category header below
//   2. Call its register_*_tests() function with the suite

#include "framework/runner.hpp"

#include "categories/meta_basic.hpp"

int main(int argc, char* argv[])
{
    jtext::test::suite s;

    // Register all test categories.
    register_meta_basic_tests(s);

    // Session 3+: parser, validator, emitter test categories registered here.

    return s.run(argc, argv);
}
