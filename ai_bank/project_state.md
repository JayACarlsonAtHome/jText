# jText — Project State (Carry-Forward Document)

**Last updated:** end of Session 5 (validator) + v0.6 cleanup
**Spec version:** v0.6 (draft)
**Test count:** 122 tests / 377 assertions, only 2 intentional meta-failures
**Author:** Jay Carlson — github.com/JayACarlsonAtHome

---

## 1. What This Document Is

This is a **carry-forward briefing**: a self-contained snapshot of the jText project's state, designed to be loaded into a fresh Claude session so we can pick up work without re-deriving decisions.

**Read order when starting a new session:**
1. This document — gives you the shape of the whole project
2. `SPEC.md` (in the repo root) — the authoritative format specification
3. `src/validator/validator.hpp` — the type vocabulary used by the validator and what the emitter will consume next
4. `src/parser/section.hpp` — the type vocabulary the validator consumes

Then ask Jay what session is next; he'll tell you whether it's a cleanup pass or the next functional area.

---

## 2. Project Description

**jText** is a structured plain-text record format — "the format CSV should have been." It pairs with **ts_store** (Jay's multithreaded ordered logger, ~15M ops/sec) but is also a standalone format.

The format is designed around two principles:

> **Principle 1:** A jText file may be saved in any state. A jText file may only be used when it is valid.
>
> **Principle 2:** A jText file is a faithful representation of the data.

"Used" means produced as output, used as input to another tool, or processed in a way that depends on its correctness. "Saved" can happen at any time, in any state — that's what makes it editable like text rather than locked behind a parser-strict gate.

**Key innovation: per-line bookends.** Every non-structural line declares its own formatter `<bookend><separator><bookend>` (e.g., `#?#`, `#/#`, `?-?`). The bookend appearing with whitespace on both sides is that line's comment marker. The separator is that line's hierarchy delimiter. This eliminates escaping problems — if your data contains `#`, you pick a different formatter for that line. No backslashes, no quoting, no nested escape soup.

**Format characteristics:**
- 1–30 fields is the sweet spot
- Three types only: String, Number, Date (deliberately small)
- Constraints: Not Null or Nullable (default Nullable)
- Templates embedded in the file (e.g., a Create Table SQL template, a per-row Insert template) — emission-only `{N}` substitution
- Multi-file relational datasets via `related_files` header field (declared in spec; implementation deferred)
- Sidecar `.jCheck` files (xxh3-128 hash + validation cache; implementation deferred)
- UTF-8 only, no BOM emitted, BOM tolerated on read

---

## 3. Repository Structure

Repo: `~/git/jText`, branch: `dev-work`

```
jText/
├── CMakeLists.txt              # top-level
├── SPEC.md                     # v0.6 — the source of truth
├── README.md                   # public-facing introduction
├── LICENSE                     # Apache-2.0
├── .gitignore
├── ai_bank/                    # AI carry-forward docs (this directory)
│   └── project_state.md        # this document
├── src/
│   ├── CMakeLists.txt          # builds jtext_core static library
│   ├── parser/
│   │   ├── line.hpp            # 174 lines — line grammar types
│   │   ├── line.cpp            # ~420 lines — line parser
│   │   ├── section.hpp         # 175 lines — file/section types
│   │   └── section.cpp         # ~620 lines — section parser
│   └── validator/
│       ├── validator.hpp       # 239 lines — typed metadata + report
│       └── validator.cpp       # 741 lines — three-pass validator
└── tests/
    ├── CMakeLists.txt
    ├── framework/              # bespoke test framework (~1400 lines)
    │   ├── CMakeLists.txt
    │   ├── concepts.hpp        # type-classification concepts
    │   ├── type_name.hpp       # demangled type names
    │   ├── format_value.hpp    # concept-dispatched value rendering
    │   ├── assertion.hpp       # test_eq, test_true, test_false
    │   ├── runner.hpp/.cpp     # suite, run(argc, argv)
    │   └── reporter.hpp/.cpp   # report file + terminal summary
    └── manual/
        ├── CMakeLists.txt
        ├── main.cpp            # registers all categories
        └── categories/
            ├── meta_basic.hpp      # 14 framework self-tests
            ├── parser_basic.hpp    # 48 line-grammar tests
            ├── section_basic.hpp   # 25 section-parser tests
            └── validator_basic.hpp # 34 validator tests
```

**Total:** roughly 1900 lines of production code, 1900 lines of tests, plus the spec and README.

---

## 4. What's Implemented (vs SPEC v0.6)

| Spec section | Topic                        | Status                                                |
|--------------|------------------------------|-------------------------------------------------------|
| 1            | Philosophy                   | Documented; principles drive all design               |
| 2.1          | UTF-8 encoding               | BOM-skip on read; UTF-8 emit assumed                  |
| 2.2          | File header                  | Parsed structurally; interpreted by validator (1-9)   |
| 2.3          | Section layout               | Fully implemented; Fields-required-before-Data rule   |
| 2.4          | Fields block                 | Parsed and interpreted into typed `field` records     |
| 2.5          | Templates                    | Recognized; bodies captured verbatim. Substitution = Session 6 |
| 2.6          | Data block                   | Parsed; records assembled from blank-line separators  |
| 2.6.1        | Empty vs. missing            | `optional<string>` preserves the distinction          |
| 3            | Line grammar                 | Complete                                              |
| 4            | Multi-file datasets          | **Declared in spec, not implemented**                 |
| 5            | Multiline blocks             | Skeleton: opener detected, body captured raw          |
| 6.1          | Structural validation        | Complete; all rules checked at parse or validate time |
| 6.2          | Semantic validation          | Complete; all issues collected, never short-circuit   |
| 6.3          | Operations (Save/Use/Write)  | Documented; validate() implemented, write() = Session 6 |
| 7            | jCheck sidecar               | **Declared in spec, not implemented**                 |

---

## 5. Session History

**Session 1 — Test framework foundation.** Built the bespoke test framework: `concepts.hpp`, `type_name.hpp`, `format_value.hpp`, `assertion.hpp`. Concept-dispatched rendering, `std::source_location` for assertion tracking, explicit registration via `suite::add(name, lambda)`. C++23 throughout. Result: 14 framework self-tests passing, 2 intentional failures to prove the FAIL path works.

**Session 2 — Runner + reporter + CLI.** Added `runner.{hpp,cpp}` and `reporter.{hpp,cpp}`. Test suite runs all tests in registration order, prints per-test output to stdout (controlled by `--verbose`/`--quiet`), writes `test_report.log` with full assertion details. Flags: `--verbose`, `--quiet`, `--no-report`, `--report=PATH`, `--help`.

**Session 3 — Line parser.** Implemented SPEC Section 3 (line grammar). Pure function `parse_line(string_view) -> expected<parsed_line, parse_error>`. Handles formatters, hierarchies (leading/trailing cosmetic), comment markers (bookend with whitespace on both sides), multiline opener detection (`<<< sentinel`). 38 tests covering every rule. 60 tests total.

**v0.5 cleanup.** Collapsed `hierarchy_internal_empty` + `hierarchy_all_empty` into single `hierarchy_empty_segment` (no meaningful interpretation of an empty hierarchy level regardless of position). Added `to_string(parse_error_kind)`. Added `has_adl_to_string` concept to test framework so enum failures show readable names instead of memory addresses. 62 tests total.

**Session 4 — Section/file parser.** Implemented SPEC Section 2. State-machine-based parser over input lines: recognizes `=== Section: ===`, `=== Fields ===`, `=== Data ===`, `=== Template: ===` markers and their End counterparts. Crude multiline-body consumption (exact-match closer sentinel, captures verbatim). 24 new tests. 86 tests total.

**v0.6 cleanup.** Made Fields-block-required-before-Data-block a structural error (was lenient before, deferring to validator — wrong decision; line numbers in Data have no referent without Fields). Pluralization fix in `runner.cpp` and `reporter.cpp` (`1 assertion` not `1 assertions`). SPEC v0.5 → v0.6. Two new tests. 88 tests total.

**Session 5 — Validator.** Implemented SPEC Section 6. Three-pass per section: `interpret_fields` → `assemble_records` → `validate_record_values`. Plus `interpret_header` (canonical 1–9 position assignment) and cross-section duplicate-name check. Single `validate(parsed_file) -> validate_result` entry point returning `validated_file` + `validation_report`. Section parser modified to preserve blank lines as sentinel `parsed_line` entries (`number == 0`); validator uses these for record-boundary detection. 34 new validator tests + 2 existing section tests updated. 122 tests total.

---

## 6. Build Environment

**Target machine:** Jay's Linux Mint 22 laptop. **GCC 14**, **C++23**, **CMake**, **CLion** via JetBrains Toolbox. Build directory: `cmake-build-debug/`. CLion handles all building; no manual `cmake` invocations needed.

**Claude's sandbox:** GCC 13.3 — lacks `<print>`. When testing in the sandbox, the polyfill `/tmp/print_polyfill.hpp` provides `std::println` with a `FILE*` overload. **All shipped files use real `#include <print>`** — polyfill is sed-restored before delivery. Never let polyfill references ship.

**Compilation flags:** `-Wall -Wextra -Wpedantic`, code compiles with zero warnings. Standard library only, no third-party dependencies.

**Delivery method:** **Heredoc-based shell scripts**, not tar. Earlier sessions had tar extraction issues — files silently not overwritten by `tar -xzf`. Heredoc scripts bypass extraction entirely (each `cat > path <<'EOF' ... EOF` writes the file directly with guaranteed content).

---

## 7. Code Style and Conventions

C++23 with explicit `cxx_std_23`, `CXX_STANDARD_REQUIRED ON`, `CXX_EXTENSIONS OFF`.

- `#pragma once` everywhere
- Trailing return types: `auto func() -> Type`
- 4-space indent, K&R braces
- `snake_case` for functions and variables, including types in new code (e.g., `parsed_line`, `validated_file`)
- Older `PascalCase` exists in some places but new types use `snake_case` matching std-lib style
- `jtext::` namespace for production, `jtext::test::` for the test framework
- `std::expected<T, E>` for fallible operations (parser layer)
- Validator does NOT use `std::expected` — it returns `validate_result` containing both the parsed structure and a report; validation is a "produce report" operation, not a "succeed or fail" operation
- `std::source_location` for assertion file/line tracking
- No macros in the test framework
- ADL `to_string(EnumKind)` free functions on every error/issue enum — framework's `format_value` uses them automatically for readable failure output
- `[[nodiscard]]` not used pervasively yet — open question whether to retrofit
- Comments are documentation, not decoration; long doc-block headers on hpp files explain the public API and reasoning

---

## 8. Design Decisions Worth Preserving

These are decisions made during the design conversations that aren't fully captured in the code or spec, but matter for future sessions:

**The per-line bookend mechanic is sacred.** Every other format wraps strings in delimiters and then has to escape those delimiters when they appear in data. jText puts the delimiter choice on the author of *each line*. The author picks one that doesn't collide with their data. Compromising this — adding fallback escaping or special-case unicode handling — defeats the whole point.

**Emission-only substitution.** The reader is liberal: it accepts varying whitespace, multi-blank lines between records, missing optional end markers, trailing newlines, CRLF, leading whitespace before line numbers. The writer is canonical: one space between formatter and data, exactly one blank line between records, end markers always present. Future enhancements that violate this principle (escape codes, format extensions, etc.) are rejected.

**Faithful representation.** When the data is read and immediately written back unchanged, the result should be byte-identical to a canonical normalization of the input — never lossy. This is why we preserve empty-vs-missing for String fields (per SPEC 2.6.1) and why comments survive a round-trip.

**No `std::expected` in the validator.** Parser layer (line, section) uses `std::expected<T, E>` because structural errors mean "I can't continue, here's why." Validator does not — semantic errors are "found a problem, here's the full list, I kept going." Returning all errors at once is core to the spec's principle that validators report everything, never short-circuit.

**Field positions are contiguous (no gaps).** Decided in Session 5. A field declared at position 1, 2, 5 (gap) is a structural error because positions reference each other (in data lines, in templates). Tolerating gaps creates ambiguity.

**Records are separated by blank lines, not by field-number reset.** Decided in Session 5. Blank-line separators are visually clear and prevent a class of human error (forgetting the blank line — would silently merge records under a number-reset rule, errors loudly under blank-line rule). Within a record, field numbers must be strictly increasing.

**"Required" vs "Not Null".** Internal code uses `field::required` boolean because it expresses behavior clearly (`if (field.required && !value) error()`). User-facing messages and spec language use "Not Null" because that's the spec's vocabulary. There's pending cleanup (v0.7 list) to bring error messages and `issue_kind` names in line with spec vocabulary.

**Crude multiline-body consumer.** Templates and multiline data fields use exact-match closer detection (a line whose *entire* content equals the sentinel). If a body could contain the sentinel literally, the author picks a different sentinel — same author-controls-delimiters principle as the line grammar.

**The validator interprets the structure into a typed `validated_file` *and* produces a report.** Both come back from `validate()` simultaneously. A file with errors still produces a partial `validated_file` (everything the validator could interpret) — the report tells the caller whether to trust it.

---

## 9. Pending Cleanup (v0.7)

Not blocking anything; pick up when convenient:

1. **"required" → "Not Null" vocabulary sweep.** Internal `field::required` boolean is fine; user-facing strings should use spec vocabulary. Affected:
   - `issue_kind::record_missing_required_field` → rename to `record_missing_not_null_field` (or similar; preserve consistency with other `record_*` names)
   - Error message in `validator.cpp` for that issue: `"required (Not Null) field is missing or empty"` → `"Not Null field is missing or empty"`
   - Test names in `validator_basic.hpp`: `"records: missing required field is rejected"` etc.
   - Documentation comments mentioning "required"
   - `record` struct's docstring in `validator.hpp` mentions "required"

2. **Stricter date validation.** Currently `looks_like_date()` checks shape only — `2026-02-30` passes structurally but is not a real calendar date. If we want calendar validity, add it. Open question whether the spec should mandate this. Current behavior is permissive.

3. **`[[nodiscard]]` audit.** Functions returning `std::expected` or `validate_result` are good candidates. Not done pervasively.

---

## 10. What's Planned Next

**Session 6 — Emitter.** The most exciting next step. Walks a `validated_file`, processes each section's templates by substituting `{N}` placeholders with field values from each record. Per SPEC Section 2.5:

- **Section-level templates** (e.g., "Create Table") emit once per section, before any record templates
- **Per-record templates** (e.g., "SQL Insert") emit once per record, with `{N}` replaced by field N's value
- Type-aware quoting:
  - **String** → single-quoted, embedded single quotes doubled (`'O''Brien'`)
  - **Number** → raw value, unquoted
  - **Date** → single-quoted (`'2026-05-12'`)
  - **Missing or empty value in Nullable field** → emit `NULL` (per SPEC 2.6.1)
  - **Missing in Not Null field** → already an error caught by validator; emitter should never see this
- Multiple templates per section: emit all of them, in declaration order

API shape (tentative):

```cpp
struct emit_result {
    std::string text;
    emit_report report;   // probably small; mostly success/failure
};

auto emit(const validated_file& vf) -> emit_result;
```

The emitter produces a string. Whether to also have a file-writing convenience is a design call when we get there.

**Session 7 — `.jCheck` sidecar.** Cache validation results in a sidecar file co-located with the .jText source. The sidecar contains an xxh3-128 hash of the source plus the validation status. On open: hash the source, compare to sidecar; if matches and sidecar says valid, skip re-validation. SPEC Section 7 documents this.

**Session 8+ — Multi-file datasets.** SPEC Section 4. The `related_files` header field declares a set of related .jText files (e.g., `customers.jtext`, `orders.jtext`, `order_items.jtext`). The validator should be able to validate the set as a unit, checking cross-file references.

**Beyond:** a CLI tool wrapping everything (`jtext validate file.jtext`, `jtext emit file.jtext --template "SQL Insert"`, etc.).

---

## 11. Open Questions / Future Decisions

These came up during design but didn't need resolving yet. They'll surface when the relevant session happens:

- **Should `jtext_version` mismatches be errors or warnings?** Currently a missing/malformed version is a warning. A version newer than what the tool supports is undefined.

- **What's the canonical line-ending on write?** LF assumed; not explicitly speced.

- **What about hierarchical data values in records?** Currently a warning (`record_hierarchical_value`), value is joined with `/`. Open whether this should be an error, or whether the hierarchy should be preserved as a vector somewhere.

- **Comments on data lines — preserve in emitted output?** Per SPEC 1.2 emission-only substitution, comments are author intent and should round-trip. Implementation not yet present.

- **`Number` precision.** Currently `looks_like_number()` accepts integer/decimal/scientific. SPEC doesn't distinguish between integers and floats. Open whether SQL emission should preserve the distinction (an `INTEGER`-typed column with value `1.5` — error in some SQL dialects).

---

## 12. Conversational Conventions

When working with Jay across sessions, these patterns hold:

- **Sessions are sized to be verifiable end-to-end.** A session ends when there's a clean commit point with passing tests. Don't accumulate uncommitted work across multiple "sessions" in one chat.

- **Cleanup passes are valuable.** Don't accumulate technical debt; sweep it before it compounds. Cleanups have been done after Sessions 3 (v0.5), 4 (v0.6), and pending after Session 5 (v0.7).

- **Heredoc installers, not tar.** Earlier tar deliveries failed silently — files not overwritten. Always deliver via shell script that writes files via heredocs.

- **End-to-end verify before delivering.** Before handing Jay an installer, run it in a fresh sandbox copy and confirm the resulting state builds clean and runs.

- **Sandbox compilation uses polyfill; never ship it.** The polyfill at `/tmp/print_polyfill.hpp` provides `std::println` for GCC 13. Sed-restore `#include <print>` in all delivered files; verify no polyfill references remain.

- **The SPEC is the source of truth.** When the code says one thing and the spec says another, fix one or the other — they shouldn't drift.

- **Be careful with "required" — see Pending Cleanup #1.**

---

## 13. How to Start a New Session

Minimum needed to pick up:

1. Load this document (`ai_bank/project_state.md`) into the new chat
2. Also include `SPEC.md` so the format definition is in context
3. Include `src/validator/validator.hpp` and `src/parser/section.hpp` for the type vocabulary
4. State the goal: e.g., "I'd like to do Session 6, the emitter."

That gives a new Claude session enough to start. After the session, update this document with what changed before ending the chat.

---

*End of carry-forward document.*
