//File:    /home/jay/git/jText/Rationale/02_Cpp23_Modernization_and_No_Macros.md
//Date:    2026-06-05
//Purpose: Rationale Document for jText: 02 C++23 Modernization and No Macros
//
# Rationale 02: C++23 Modernization Pass on jText

**Date**: 2026-06-02  
**Author**: Grok

## Goal
Bring jText to a clean, modern C++23 baseline with no preprocessor macros in source code (per project-wide policy).

## Decisions Made

### 1. C++ Standard
- Explicitly set `CMAKE_CXX_STANDARD 23` with `CMAKE_CXX_EXTENSIONS OFF` (already present, reinforced).

### 2. Compiler Warnings
- Kept the existing strong warning set (good).
- No changes needed for this pass.

### 3. Macro Removal
- Only one location had macros: `tests/framework/type_name.hpp` (compiler-specific pretty function selection).
- Replaced with `std::source_location::current().function_name()` + a small extraction helper.
- See Rationale 01 for the detailed pros/cons on this specific change.

### 4. Test Framework
- The existing test framework (doctest-based) is functional.
- No new unit tests added in this pass because the project already has a reasonable test suite.
- Future work: Add property-based or more parser round-trip tests.

## Non-Goals (this pass)
- Performance optimization
- Major API changes
- Full documentation overhaul

## Outcome
jText source is now free of non-CMake preprocessor macros and uses C++23 facilities where they provide a clean replacement.

Next projects in the purge: jac_safe_math, ts_safe, DeepSpace, etc.
