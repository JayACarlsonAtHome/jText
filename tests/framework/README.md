# jText Test Framework

A small C++23 test framework written for the jText project. It lives
inside the jText repository but has no dependencies on jText code —
it's designed to be extracted into a standalone project later if reuse
demands it.

## Design Goals

- **Type-aware**: comparisons report the type being checked, formatted
  per-type via compile-time concept dispatch
- **No macros**: assertions are plain function calls; call sites are
  captured via `std::source_location`
- **Explicit registration**: tests are registered with the suite by name
  in `main()`. What you see is what runs.
- **Plain text output**: terminal output to stdout, optional report file;
  no JSON, no structured formats
- **Small**: framework code is ~700 lines total

## Status

### Session 1 (complete)

- `concepts.hpp` — type concepts for dispatch
- `type_name.hpp` — compile-time type name extraction
- `format_value.hpp` — render any value as a human-readable string
- `assertion.hpp` — `test_eq`, `test_ne`, `test_true`, `test_false`

### Session 2 (complete)

- `runner.hpp` / `runner.cpp` — suite class, test registration, argv parsing
- `reporter.hpp` / `reporter.cpp` — generates `test_report.log` with
  summary + details
- Updated `assertion.hpp` — assertions write to per-test sinks via the
  runner, captured for the report file
- Updated `meta_basic.hpp` — uses explicit registration style

### Session 3+ (planned)

- Conformance test loader for the file-driven test pattern
  (input `.jText` file + `.expected.txt` companion)

## Usage Example

```cpp
#include "framework/runner.hpp"

int main(int argc, char* argv[])
{
    jtext::test::suite s;

    s.add("integer equality", [] {
        jtext::test::test_eq(1 + 1, 2);
        jtext::test::test_eq(7 * 6, 42);
    });

    s.add("string handling", [] {
        std::string greeting{"hello"};
        jtext::test::test_eq(greeting, std::string{"hello"});
        jtext::test::test_ne(greeting, std::string{"goodbye"});
    });

    return s.run(argc, argv);
}
```

## Command-Line Flags

| Flag                | Effect                                             |
|---------------------|----------------------------------------------------|
| `-h`, `--help`      | Print usage and exit                               |
| `-v`, `--verbose`   | Print every PASS line (default: only FAIL)         |
| `-q`, `--quiet`     | Suppress all stdout (exit code still reflects pass/fail) |
| `--no-report`       | Skip writing the report file                       |
| `--report=PATH`     | Write report to PATH instead of `test_report.log`  |

## Assertion Vocabulary

| Function                          | Meaning                                    |
|-----------------------------------|--------------------------------------------|
| `test_eq(actual, expected)`       | actual == expected                         |
| `test_ne(actual, expected)`       | actual != expected                         |
| `test_true(condition)`            | condition is true                          |
| `test_false(condition)`           | condition is false                         |

Future sessions may add `test_throws`, `test_no_throw`, and `test_approx`
(for floating-point comparison with tolerance) as real test needs surface.

## Value Formatting

When an assertion fails, the framework prints both the expected and actual
values. Each value is formatted by `format_value<T>(v)`, which uses
compile-time concepts to pick the right strategy:

| T's nature              | Format                                      |
|-------------------------|---------------------------------------------|
| `bool`                  | `true` or `false`                          |
| `char` family           | `'x'` (quoted)                              |
| string types            | `"text"` (double-quoted, even if empty)    |
| has `to_string()`       | calls the method                            |
| streamable              | uses `operator<<`                           |
| container               | `[elem1, elem2, elem3]` (recursive)        |
| anything else           | `<TypeName at 0xADDR>`                      |

## Report File Format

A typical `test_report.log` has two sections:

```
================================================================
 jText Test Report
================================================================
Date:      2026-05-12 09:42:17
Tests:     14   (passed: 13, failed: 1)
Asserts:   38   (passed: 36, failed: 2)

Failed tests:
  - INTENTIONAL FAILURES   (2 of 2 assertions failed)

================================================================
 Test Details
================================================================

----------------------------------------------------------------
 [PASS]  integer equality (passing)   (3 passed, 0 failed)
----------------------------------------------------------------
  [PASS] tests/manual/categories/meta_basic.hpp:32 (int) test_eq
  [PASS] tests/manual/categories/meta_basic.hpp:33 (int) test_eq
  [PASS] tests/manual/categories/meta_basic.hpp:34 (int) test_eq

----------------------------------------------------------------
 [FAIL]  INTENTIONAL FAILURES   (0 passed, 2 failed)
----------------------------------------------------------------
  [FAIL] tests/manual/categories/meta_basic.hpp:154 (int) test_eq
         expected: 2
         actual:   1
  ...
```

The file is greppable: `grep '\[FAIL\]' test_report.log` shows every
failing assertion with file:line and expected/actual values.

## Naming Conventions

- Namespace: `jtext::test`
- Functions and variables: `snake_case`
- Types and concepts: `snake_case` (matches std library naming)
- Public header files: `.hpp` extension
- No macros (this is a deliberate constraint)

## Not Goals

- Not a replacement for Catch2, doctest, or GoogleTest when those fit
- Not a parallel test runner (tests run sequentially in registration order)
- Not a benchmark framework (use Google Benchmark or equivalent for that)
- Not extensible via dynamic plugins (extensibility is via concept
  specializations and overloads of `format_value`)
