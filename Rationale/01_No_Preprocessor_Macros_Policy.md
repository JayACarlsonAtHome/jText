//File:    /home/jay/git/jText/Rationale/01_No_Preprocessor_Macros_Policy.md
//Date:    2026-06-05
//Purpose: Rationale Document for jText: 01 No Preprocessor Macros Policy
//
# Rationale 01: No Preprocessor Macros in Source Code (except CMake-controlled)

**Date**: 2026-06-02  
**Author**: Grok (following explicit user directive)  
**Project**: jText

## Context
The user has established a strict policy across all their C++ projects: the *only* acceptable use of preprocessor macros is inside CMakeLists.txt files for build configuration (e.g., `TS_STORE_ENABLE_JTEXT_PERSIST`).

No `#ifdef`, `#if`, `#define` (except for include guards via `#pragma once`), etc., are allowed in `.hpp` or `.cpp` files.

## Decision
Adopt and enforce a "zero macros in C++ source" rule for jText.

### Why this policy?
- Improves readability and maintainability.
- Forces use of modern C++23 features (`if constexpr`, concepts, `std::source_location`, `std::expected`, etc.).
- Reduces accidental platform/compiler leakage.
- Makes the code easier to reason about and refactor.
- Aligns with the broader goal of bringing all projects to clean, modern C++23.

## Specific Case Handled: Pretty Function Names in Test Framework
The only macros found in jText were in `tests/framework/type_name.hpp` for selecting `__PRETTY_FUNCTION__` (GCC/Clang) vs `__FUNCSIG__` (MSVC).

### Options Considered

| Option | Pros | Cons | Verdict |
|--------|------|------|---------|
| Keep existing `#if` / `#define` block | Works on MSVC today, minimal change | Violates user's explicit "no macros in source" rule | Rejected |
| Always use `__PRETTY_FUNCTION__` | Zero macros, simple | Breaks on MSVC | Rejected (we want broad compiler support where easy) |
| **Use `std::source_location::current().function_name()` (C++20+)** | **Clean C++23, no macros, portable, future-proof** | Requires C++20+, slightly different output format on some compilers | **Selected** |

## Implementation
- Removed all `#if / #define / #elif / #endif` from `type_name.hpp`.
- Switched to `std::source_location` (available since the project already targets C++23).
- Updated the test framework's pretty-printing to use the modern facility.
- Added this Rationale document.

## Follow-up Actions
- Update CMakeLists.txt to explicitly require C++23 (already close).
- Continue scanning jText for any other legacy patterns (old-style casts, manual resource management, etc.).
- Ensure all new code follows the same zero-macro rule.
- When future compiler-specific needs arise, prefer `if constexpr` + `std::is_same_v` + `std::source_location` over preprocessor.

This decision sets the standard for the entire jText codebase and serves as a template for the same cleanup on AI_DataPacking, galactic_standard_time, jac_safe_math, etc.