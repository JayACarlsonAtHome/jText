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
    std::string  body;            // the value's lines, joined with '\n'
    std::string  opener_comment;  // optional comment on the "[*" line
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

// A whole comment line (SPEC 3.2.1): a line of its own, kept where it stands. `text` is
// the line without its leading "#" or "//" and the spaces around it; `before` is the
// index, in its block's lines, of the line it stands before, or the block's size when
// it stands at the end. Blocks: parsed_file::header_lines (light profile: metadata),
// parsed_section::field_lines, parsed_section::data_lines (blank-line sentinels count).
// A comment line between sections stands before the next section's first block; one
// after the file's closer, at the end of the last.
struct comment_line {
    std::size_t before = 0;
    std::string text;
};

struct multiline_body {
    std::size_t  data_line_index = 0;  // index into parsed_section::data_lines
    std::string  body;                 // the value's lines, joined with '\n' (also the opener's values[0])
};

struct parsed_section {
    std::string                    name;
    std::vector<parsed_template>   templates;
    std::vector<parsed_line>       field_lines;
    std::vector<comment_line>      field_comments;   // comment lines in the intro and Fields block
    std::vector<parsed_line>       data_lines;
    std::vector<comment_line>      data_comments;    // comment lines in the data block
    std::vector<multiline_body>    data_multiline_bodies;
    // Support for <#include#> markers (e.g. for ts_store schema/fields sharing)
    // These are recognized in intro state as special markers, not regular entries.
    std::vector<std::pair<std::string, std::string>> includes; // {type, path}

    // Where the section's data lines are in the file: 0-based line indexes, [data_begin,
    // data_end). Full profile: between "=== Data ===" and "=== End Data ===". Light
    // profile: from the first data line to "-- End Name --"; the includes and comments
    // above the first data line stay outside. replace_section_data() rewrites this range.
    bool        has_data_block = false;
    std::size_t data_begin = 0;
    std::size_t data_end = 0;
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
    // Light profile: the "#" metadata lines before the first section, without
    // their "#" ("JText File - created ...", "Purpose - ...", "Case: ...").
    std::vector<std::string>     metadata;
    std::vector<comment_line>    header_comments;   // comment lines in the file header
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
    template_missing_body,          // "=== Template: ===" without a multiline opener
    line_parse_failed,              // parse_line() returned an error
    duplicate_data_block,           // second "=== Data ===" in a section
    io_error,                       // istream error
    missing_closer,                 // a block was opened and never closed (truncated file?)
    section_not_found,              // replace_section_data: no section of that name
    section_ambiguous,              // replace_section_data: the name is used more than once
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
//  Format version (SPEC 2.3)
//
//  kFormatVersion is the jText version this library reads and writes. A file
//  declares its own: full-profile header line 4, or the light profile's
//  "# jText Version:" line. Readers warn when it is missing, older, newer or
//  not a number; writers declare kFormatVersion, so saving a file upgrades it.
//  A newer declaration is never lowered.
// ──────────────────────────────────────────────────────────────

inline constexpr std::string_view kFormatVersion = "0.8";

// A file holds no nulls (SPEC 2.6.1). An author writes this text for "no value"; it is
// read, edited and saved as text, and only SQL output turns it into NULL. A writer
// handed a value it has no text for writes it, and a reader takes the v0.7 compact-row
// null marker (U+001F) as it.
inline constexpr std::string_view kNullValue = "{NULL}";

// What a Date or DateTime field stores where there is no date and its declaration
// gives no NullDate(...) (SPEC 2.6.1).
inline constexpr std::string_view kDefaultNullDate = "1900-01-01";

enum class version_status : std::uint8_t {
    current,   // the file declares kFormatVersion
    missing,   // no version declared (files from before v0.8 often have none)
    older,     // an earlier version: read with today's rules, upgraded on save
    newer,     // a later version: it may hold what this reader does not know
    invalid,   // not a version number ("major.minor")
};

// The version `pf` declares, or "" when it declares none.
auto declared_version(const parsed_file& pf) -> std::string;

auto check_version(std::string_view declared) -> version_status;

// The warning a reader gives for a file declaring `declared`; "" when it is current.
auto version_warning(std::string_view declared) -> std::string;

// The file `text` with one section's data lines replaced by `data` (whole lines, each
// ending in '\n'); every other line stays exactly as it was: the header, templates,
// Fields block, includes, and the other sections. The section is found by name and
// must appear once, with a data block. Comments between the old data lines go with them.
// The one exception is the version: a missing or older one becomes kFormatVersion, as
// the new data is written to today's rules (a newer one is kept).
auto replace_section_data(std::string_view text, std::string_view section, std::string_view data)
    -> std::expected<std::string, file_error>;

// ──────────────────────────────────────────────────────────────
//  Helpers exposed for testing.
//
//  Recognize a structural marker. Returns true if the line is of
//  the form "=== <something> ===" (possibly with leading/trailing
//  whitespace). The interior is returned in `interior` (trimmed).
// ──────────────────────────────────────────────────────────────

auto is_structural_marker(std::string_view line, std::string& interior_out) -> bool;

}  // namespace jtext

