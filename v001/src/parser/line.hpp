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
//   - parse_value_line  a line inside a multiline value ("**", "*]")
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

#include "utf8.hpp"

namespace jtext {

// ──────────────────────────────────────────────────────────────
//  formatter
//
//  The live marker is two characters: splice, then comment.
//  A longer run of marker characters is accepted. The reader keeps
//  the right two and drops what is in front. Those two must differ.
//  [] is raw data (see raw_data()). Each marker character is one Unicode
//  code point whose General Category is Punctuation or Symbol (SPEC 3.1):
//  any printable non-alphanumeric ASCII character, or one such as U+25AE.
//
//  stand_in is set equal to the comment character and is unused.
// ──────────────────────────────────────────────────────────────

struct formatter {
    char32_t bookend   = 0;
    char32_t separator = 0;
    char32_t stand_in  = 0;

    constexpr auto operator==(const formatter&) const -> bool = default;

    // [] is raw data: no splice, no comment. The rest of the line is the value.
    constexpr auto raw_data() const -> bool {
        return separator == U'[' && bookend == U']';
    }

    // [* opens a multiline value (SPEC v0.8 Part C): "** " lines, then "*]".
    constexpr auto multiline() const -> bool {
        return separator == U'[' && bookend == U'*';
    }

    // The two live characters: splice, then comment.
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
    multiline_opener,   // "<num>. [* # comment" — the value's "**" lines follow
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
//    - fmt: [*
//    - comment: the opener's optional comment (after '#')
//    - hierarchy: empty from parse_line; the file parser fills in the one
//      value its "**" lines make
// ──────────────────────────────────────────────────────────────

struct parsed_line {
    line_kind                 kind        = line_kind::data;
    unsigned                  number      = 0;
    formatter                 fmt;
    std::vector<std::string>  hierarchy;
    std::string               comment;
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
    formatter_too_short,                 // fewer than 2 chars where the marker goes
    formatter_bookends_mismatch,         // retained; no longer raised
    formatter_separator_equals_bookend,  // the right two characters are the same
    formatter_invalid_char,              // non-ASCII, alphanumeric, or whitespace
    hierarchy_empty_segment,             // hierarchy contains an empty segment
    multiline_missing_sentinel,          // retained; no longer raised ("<<<" is gone)
    multiline_sentinel_invalid,          // retained; no longer raised
    invalid_utf8,                        // the formatter's bytes are not UTF-8
    formatter_reserved,                  // '[' splice other than [] and [*
    multiline_opener_text,               // text after "[*" that is not a "#" comment
    value_line_invalid,                  // "**" without a space, or text after "*]"
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

//  split_rule
//
//  How the splice character divides the data (SPEC 2.6.3, 3.3).
//    hierarchy - levels of one value: a leading or trailing separator is
//                cosmetic, an empty level is an error. Fields-block lines,
//                file-header lines.
//    fields    - the values of a compact row: every separator splits, an
//                empty value ("||", or a trailing "|") is an empty string.
//                Data-block lines. This is the exact inverse of the writer.
enum class split_rule : std::uint8_t { hierarchy, fields };

auto parse_line(std::string_view line, split_rule rule = split_rule::hierarchy)
    -> std::expected<parsed_line, parse_error>;

//  Multiline values (SPEC v0.8 Part C)
//
//     6. [* # stack trace          the opener (parse_line: multiline_opener)
//        ** Traceback ...          a line of the value
//        **                        an empty line of the value
//        *]                        the closer, alone
//
//  One space after "**" belongs to the marker; the rest of the line is the
//  value's line, verbatim. Leading spaces and tabs before "**" and "*]" are
//  alignment and are ignored.

enum class value_line_kind : std::uint8_t {
    content,   // "** text" or "**"
    closer,    // "*]"
    other,     // anything else: not part of a value
};

struct value_line {
    value_line_kind kind = value_line_kind::other;
    std::string     text;   // for content: the value's line
};

auto parse_value_line(std::string_view line) -> std::expected<value_line, parse_error>;

// A template's opener: a bare "[*" (no line number), with an optional "# comment".
// Returns the comment.
auto parse_bare_value_opener(std::string_view line) -> std::expected<std::string, parse_error>;

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

constexpr auto is_valid_formatter_char(char32_t cp) -> bool
{
    // One code point whose General Category is Punctuation or Symbol: never a
    // letter, digit, mark, space, control or format character. In ASCII that is
    // exactly the printable non-alphanumeric characters.
    return is_punctuation_or_symbol(cp);
}

}  // namespace jtext
