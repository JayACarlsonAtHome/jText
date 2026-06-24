// File: src/parser/line.hpp
//
// Line-grammar types and parser entry point.
//
// Implements SPEC Section 3: the line grammar
//
//   <padding><number>.<ws><formatter><ws><data>[<ws><comment-marker><ws><comment>]
//
// Public API:
//   - formatter         3-char formatter declaration
//   - parsed_line       successful parse result
//   - parse_error       parse failure details
//   - line_kind         what kind of line this is (data, multiline_opener)
//   - parse_line(sv)    main entry point
//
// The parser is a pure function: no I/O, no global state. The same input
// always produces the same output.

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace jtext {

// ──────────────────────────────────────────────────────────────
//  formatter
//
//  The 3-character declaration that opens every non-structural line.
//  Shape: <bookend><separator><bookend>
//
//  Invariants enforced by validate_formatter():
//    - bookend == bookend (two outer chars identical)
//    - bookend != separator
//    - both are printable ASCII, non-alphanumeric, non-whitespace
// ──────────────────────────────────────────────────────────────

struct formatter {
    char bookend   = '\0';
    char separator = '\0';

    constexpr auto operator==(const formatter&) const -> bool = default;

    // Render as a 3-char string for debug output.
    auto to_string() const -> std::string;
};

// ──────────────────────────────────────────────────────────────
//  line_kind
//
//  Distinguishes the two kinds of lines the line parser recognizes.
//  Multiline block bodies are handled at a higher layer; the line
//  parser only flags that a multiline opener was seen.
// ──────────────────────────────────────────────────────────────

enum class line_kind : std::uint8_t {
    data,               // a regular numbered data line
    multiline_opener,   // "<num>. <<< <sentinel>" — body handled separately
};

// ──────────────────────────────────────────────────────────────
//  parsed_line
//
//  A successful parse. The interpretation depends on `kind`:
//
//  kind == data:
//    - number: 1..99
//    - fmt: the line's declared formatter
//    - hierarchy: parsed segments (1 or more) of the data
//      - flat data has hierarchy.size() == 1, hierarchy[0] is the value
//      - empty data has hierarchy.size() == 1, hierarchy[0] is ""
//      - hierarchical data has hierarchy.size() == N levels
//    - comment: text after the comment marker (without leading/trailing ws)
//      - empty string when no comment was present
//
//  kind == multiline_opener:
//    - number: 1..99
//    - sentinel: the chosen sentinel string (after "<<<")
//    - comment: optional trailing comment on the opener line
//    - fmt and hierarchy are unused (default-constructed)
// ──────────────────────────────────────────────────────────────

struct parsed_line {
    line_kind                 kind        = line_kind::data;
    unsigned                  number      = 0;
    formatter                 fmt;
    std::vector<std::string>  hierarchy;
    std::string               comment;
    std::string               sentinel;   // for kind == multiline_opener
};

// ──────────────────────────────────────────────────────────────
//  parse_error_kind
//
//  Categorical reasons a line can fail to parse. Keep this stable
//  so consumers can switch on it.
//
//  Note: hierarchy_empty_segment covers all cases where the data
//  syntactically claims to be hierarchical but contains an empty
//  segment somewhere — there is no meaningful interpretation of
//  an empty hierarchy level, regardless of where it appears.
// ──────────────────────────────────────────────────────────────

enum class parse_error_kind : std::uint8_t {
    empty_line,                          // line is empty or whitespace-only
    missing_period,                      // number not followed by '.'
    invalid_line_number,                 // 0, > 99, or not a number
    missing_formatter,                   // line ends after the period
    formatter_too_short,                 // fewer than 3 chars where formatter goes
    formatter_bookends_mismatch,         // bookend1 != bookend2
    formatter_separator_equals_bookend,  // bookend == separator
    formatter_invalid_char,              // non-ASCII, alphanumeric, or whitespace
    hierarchy_empty_segment,             // hierarchy contains an empty segment
    multiline_missing_sentinel,          // "<<<" with no following sentinel
    multiline_sentinel_invalid,          // sentinel too short / has whitespace
};

// Render a parse_error_kind as a stable, human-readable string.
// Used by tests and report output to show meaningful enum names
// instead of internal numeric values or memory addresses.
auto to_string(parse_error_kind k) -> std::string_view;

// ──────────────────────────────────────────────────────────────
//  parse_error
//
//  Carries enough context to produce a useful error message:
//    - kind: categorical
//    - column: 0-based column where the problem was detected
//    - message: human-readable description
// ──────────────────────────────────────────────────────────────

struct parse_error {
    parse_error_kind  kind    = parse_error_kind::empty_line;
    std::size_t       column  = 0;
    std::string       message;

    auto to_string() const -> std::string;
};

// ──────────────────────────────────────────────────────────────
//  parse_line
//
//  Parse a single line. Trailing newline (if any) is tolerated.
//  Leading whitespace before the line number is allowed (used for
//  visual padding of single-digit numbers).
//
//  Returns parsed_line on success, parse_error on failure.
// ──────────────────────────────────────────────────────────────

auto parse_line(std::string_view line) -> std::expected<parsed_line, parse_error>;

// ──────────────────────────────────────────────────────────────
//  validate_formatter
//
//  Standalone validator for a 3-char string_view. Used internally
//  by parse_line and exposed so tests (and consumers) can validate
//  a formatter independently.
// ──────────────────────────────────────────────────────────────

auto validate_formatter(std::string_view three_chars)
    -> std::expected<formatter, parse_error>;

// ──────────────────────────────────────────────────────────────
//  Character-class helpers used by the formatter validator.
//  Exposed because they're useful for tests and other parsers
//  in later sessions.
// ──────────────────────────────────────────────────────────────

constexpr auto is_valid_formatter_char(char c) -> bool
{
    // Must be printable ASCII (0x21..0x7E), non-alphanumeric, non-whitespace.
    const auto u = static_cast<unsigned char>(c);
    if (u < 0x21 || u > 0x7E) return false;            // unprintable or non-ASCII
    if ((u >= '0' && u <= '9') ||
        (u >= 'A' && u <= 'Z') ||
        (u >= 'a' && u <= 'z')) return false;          // alphanumeric
    return true;
}

}  // namespace jtext
