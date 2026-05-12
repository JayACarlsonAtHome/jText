# jText Test Framework

A small, header-only C++23 test framework written for the jText project.
It lives inside the jText repository for now but has no dependencies on
jText code — it's designed to be extracted into a standalone project
later if reuse demands it.

## Design Goals

- **Type-aware**: comparisons report the type being checked, formatted
  per-type via compile-time concept dispatch
- **No macros**: assertions are plain function calls; call sites are
  captured via `std::source_location`
- **No registration magic**: tests are explicit function calls in a
  `main()` coordinator. What you see is what runs.
- **Plain text output**: results go to stdout (Session 1) and a report
  file (Session 2); no JSON, no structured formats
- **Small**: target ~600 lines total when the framework is complete

## Status

### Session 1 (current)

- `concepts.hpp` — type concepts for dispatch
- `type_name.hpp` — compile-time type name extraction
- `format_value.hpp` — render any value as a human-readable string
- `assertion.hpp` — `test_eq`, `test_ne`, `test_true`, `test_false`

The framework is header-only at this stage. Output goes to stdout.

### Session 2 (planned)

- `runner.hpp` / `runner.cpp` — test registration and orchestration
- `reporter.hpp` / `reporter.cpp` — generates a structured report file
- `meta_test.cpp` becomes a richer self-test suite

### Session 3+ (planned)

- Conformance test loader for the file-driven test pattern
  (input `.jText` file + `.expected.txt` companion)

## Usage Example

```cpp
#include "framework/assertion.hpp"

using namespace jtext::test;

int main()
{
    begin_test("integer equality");
    test_eq(1 + 1, 2);
    test_eq(7 * 6, 42);
    end_test();

    begin_test("string handling");
    std::string greeting = "hello";
    test_eq(greeting, std::string{"hello"});
    test_ne(greeting, std::string{"goodbye"});
    end_test();

    return print_summary();
}
```

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

## Naming Conventions

- Namespace: `jtext::test`
- Functions and variables: `snake_case`
- Types and concepts: `snake_case` (matches std library naming)
- Public header files: `.hpp` extension
- No macros (this is a deliberate constraint)

## Not Goals

- Not a replacement for Catch2, doctest, or GoogleTest when those fit
- Not a parallel test runner (tests run sequentially in declaration order)
- Not a benchmark framework (use Google Benchmark or equivalent for that)
- Not extensible via dynamic plugins (extensibility is via concept
  specializations and overloads of `format_value`)
