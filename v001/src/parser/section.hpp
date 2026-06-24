// File: src/parser/section.hpp
//
// File and section structure parser.
//
// Implements SPEC v0.5 Section 2: file header and section layout.
//
// This session handles the structural skeleton: the "=== ..." marker
// lines, the grouping of numbered lines into header / fields / data /
// templates. It does NOT interpret field declarations into typed
// metadata, assemble data lines into records, or validate semantic
// correctness — those are jobs for the validator in a later session.
//
// Multiline blocks (template bodies, multiline data fields) are
// consumed crudely: this session finds the matching sentinel and
// captures everything between as a raw body string. Detailed parsing
// of multiline contents is also deferred.
//
// Public API:
//   - parsed_template, parsed_section, parsed_file: structural types
//   - file_error_kind, file_error:                  error reporting
//   - parse_file_structure(istream):                stream entry point
//   - parse_file_structure(string_view):            string entry point (testing)

#pragma once

#include "parser/line.hpp"

#include <cstdint>
#include <expected>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace jtext {

// ──────────────────────────────────────────────────────────────
//  parsed_template
//
//  One template block within a section. The body is captured
//  verbatim as the text between the opener and closer sentinels
//  (exclusive on both ends), with original whitespace and newlines
//  preserved. Detailed parsing of the body — recognizing {N}
//  placeholders, separating literal text from substitution
//  positions — is deferred to a later session.
// ──────────────────────────────────────────────────────────────

struct parsed_template {
    std::string  name;            // from "=== Template: <name> ==="
    std::string  sentinel;        // chosen sentinel for the body
    std::string  body;            // verbatim text between sentinels
    std::string  opener_comment;  // optional comment on the opener line
};

// ──────────────────────────────────────────────────────────────
//  parsed_section
//
//  Everything between a "=== Section: <name> ===" banner and the
//  next section banner or end-of-file. Note that:
//
//   - templates appear in declaration order
//   - field_lines and data_lines are NOT semantically interpreted
//     at this stage; they are simply the parsed_line records
//     produced by parse_line() for each non-structural line in
//     the respective block
//   - blank lines within the data block ARE preserved as sentinel
//     entries (parsed_line with number == 0). They mark record
//     boundaries for the validator. Consecutive blanks compress
//     to one sentinel; trailing blanks at the end of the data
//     block are stripped (visual whitespace, not boundaries).
//   - data_lines may contain parsed_line records where kind ==
//     multiline_opener; in those cases, the corresponding entry
//     in multiline_bodies (indexed by position) holds the raw body
// ──────────────────────────────────────────────────────────────

struct multiline_body {
    std::size_t  data_line_index = 0;  // index into parsed_section::data_lines
    std::string  body;                 // verbatim text between sentinels
};

struct parsed_section {
    std::string                    name;
    std::vector<parsed_template>   templates;
    std::vector<parsed_line>       field_lines;
    std::vector<parsed_line>       data_lines;
    std::vector<multiline_body>    data_multiline_bodies;
    // Support for <#include#> markers (e.g. for ts_store schema/fields sharing)
    // These are recognized in intro state as special markers, not regular entries.
    std::vector<std::pair<std::string, std::string>> includes; // {type, path}
};

// ──────────────────────────────────────────────────────────────
//  parsed_file
//
//  The top-level structural representation of a jText file.
//  header_lines are the numbered lines between the magic line
//  "=== jText File ===" and the first "=== Section: ===".
// ──────────────────────────────────────────────────────────────

struct parsed_file {
    std::vector<parsed_line>     header_lines;
    std::vector<parsed_section>  sections;
};

// ──────────────────────────────────────────────────────────────
//  file_error_kind
//
//  Categorical reasons file-structure parsing can fail. These
//  cover structural problems that prevent further parsing;
//  semantic issues (e.g., a field reference outside the field
//  range) are deferred to the validator.
// ──────────────────────────────────────────────────────────────

enum class file_error_kind : std::uint8_t {
    missing_magic_line,             // first non-blank line is not "=== jText File ==="
    missing_fields_block,           // section reached Data without a Fields block first
    unexpected_marker,              // a "=== Foo ===" marker out of place
    unknown_section_state,           // line not consumable in current state
    template_missing_body,          // "=== Template: ===" without a multiline opener
    multiline_unterminated,         // sentinel not found before EOF
    multiline_sentinel_empty_body,  // closer sentinel immediately after opener
    line_parse_failed,              // parse_line() returned an error
    duplicate_data_block,           // second "=== Data ===" in a section
    unterminated_section,           // section ended unexpectedly (EOF in fields)
    io_error,                       // istream error
};

// Render a file_error_kind as a stable, human-readable string.
auto to_string(file_error_kind k) -> std::string_view;

// ──────────────────────────────────────────────────────────────
//  file_error
//
//  Carries enough context to produce a useful error message:
//    - kind: categorical
//    - line_no: 1-based line in the source file
//    - message: human-readable description, may include nested
//      parse_error details if line_parse_failed
// ──────────────────────────────────────────────────────────────

struct file_error {
    file_error_kind  kind     = file_error_kind::missing_magic_line;
    std::size_t      line_no  = 0;
    std::string      message;

    auto to_string() const -> std::string;
};

// ──────────────────────────────────────────────────────────────
//  parse_file_structure
//
//  Two overloads, sharing the same core logic:
//    - istream version: reads line-by-line, suitable for files
//    - string_view version: parses from in-memory text, convenient
//      for testing
//
//  Returns a parsed_file on success, file_error on failure.
// ──────────────────────────────────────────────────────────────

auto parse_file_structure(std::istream& in)
    -> std::expected<parsed_file, file_error>;

auto parse_file_structure(std::string_view text)
    -> std::expected<parsed_file, file_error>;

// ──────────────────────────────────────────────────────────────
//  Helpers exposed for testing.
//
//  Recognize a structural marker. Returns true if the line is of
//  the form "=== <something> ===" (possibly with leading/trailing
//  whitespace). The interior is returned in `interior` (trimmed).
// ──────────────────────────────────────────────────────────────

auto is_structural_marker(std::string_view line, std::string& interior_out) -> bool;

}  // namespace jtext

