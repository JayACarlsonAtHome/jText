// File: src/validator/validator.hpp
//
// Validates a parsed_file (output of the section parser) against the
// semantic rules of SPEC v0.6.
//
// What this layer does:
//   - Interprets field declaration lines (name/type/length/constraint)
//     into typed field metadata
//   - Validates the field schema (positions 1..N contiguous, types
//     recognized, constraints recognized)
//   - Assembles data lines into records, using blank-line sentinels
//     as record separators
//   - Validates each record (required fields present, field numbers
//     in order, no duplicates, values type-compatible)
//   - Validates the file header (recognized fields, well-known values)
//   - Returns the validated structure AND a report of all issues
//
// Per SPEC v0.6 Section 6.3: validate() does not write anything; it
// only reads and reports. Callers consume the report to decide
// whether the data is trustworthy.
//
// Public API:
//   - field_type:           enum (String, Number, Date)
//   - field:                typed field metadata
//   - record:               assembled record (field-position → value)
//   - validated_section:    section with typed schema and records
//   - validated_file:       file with typed header + validated sections
//   - issue_severity, issue_kind, validation_issue: report contents
//   - validation_report:    full set of issues with helper accessors
//   - validate_result:      validated structure + report
//   - validate(parsed_file): the entry point

#pragma once

#include "parser/section.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace jtext {

// ──────────────────────────────────────────────────────────────
//  field_type
//
//  SPEC v0.6 Section 2.4.1 vocabulary: String, Number, Date.
//  Deliberately small.
// ──────────────────────────────────────────────────────────────

enum class field_type : std::uint8_t {
    string_type,
    number_type,
    date_type,
};

// Stable, readable names for diagnostics.
auto to_string(field_type t) -> std::string_view;

// Parse a type token (case-insensitive: "String", "Number", "Date").
// Returns nullopt for unrecognized tokens.
auto parse_field_type(std::string_view s) -> std::optional<field_type>;

// ──────────────────────────────────────────────────────────────
//  field
//
//  Typed metadata for one field, interpreted from a single
//  declaration line in the Fields block.
//
//  Per SPEC v0.6 Section 2.4 the syntax is:
//    <name> / <type> [ / <length> [ / <constraints> ] ]
//  where <constraints> is "Not Null" or "Nullable" (default).
// ──────────────────────────────────────────────────────────────

struct field {
    unsigned                       position    = 0;  // 1..N (from line number)
    std::string                    name;
    field_type                     type        = field_type::string_type;
    std::optional<std::size_t>     max_length;       // String length cap if declared
    bool                           required    = false;  // Not Null
    std::string                    comment;          // inline comment on the field line
};

// ──────────────────────────────────────────────────────────────
//  record
//
//  One assembled record. values[i] is the value for field position
//  i+1 (so values[0] holds field 1). Each value is either:
//    - nullopt: the field was not present in this record (missing)
//    - empty string "": the field appeared with no data (empty)
//    - non-empty string: the field's data value (single-segment
//      hierarchies; multi-segment hierarchies are encoded as a
//      string with the original separator preserved? — for now,
//      only single-segment values land here; multi-segment data
//      values are encoded as the joined string with '/' separator
//      and that's flagged by the field's hierarchical_value bit)
//
//  Per SPEC 2.6.1, the distinction between empty ("") and missing
//  (nullopt) is preserved for String fields. For Number and Date
//  fields, both states are semantically equivalent (NULL).
// ──────────────────────────────────────────────────────────────

struct record {
    std::size_t                              source_line_no = 0;
    std::vector<std::optional<std::string>>  values;
    std::size_t                              record_id = 0;  // for ts_store style logs (the event id from the data line number)
};

// ──────────────────────────────────────────────────────────────
//  validated_section
// ──────────────────────────────────────────────────────────────

struct validated_section {
    std::string                  name;
    std::vector<field>           fields;     // ordered by position (1..N)
    std::vector<record>          records;
    std::vector<parsed_template> templates;  // unchanged from parser
    // includes copied from parser (for tools that want to process ts_store-style includes)
    std::vector<std::pair<std::string, std::string>> includes;
};

// ──────────────────────────────────────────────────────────────
//  validated_file
// ──────────────────────────────────────────────────────────────

struct validated_file {
    // Keyed by recognized header field name (lower-case canonical:
    // "filename", "date", "purpose", "jtext_version", "case_mode",
    // "sql_dialect", "table_name", "auto_id", "related_files").
    // Unrecognized line numbers do not appear here; they're
    // surfaced via the report as warnings.
    std::map<std::string, std::string>  header;

    std::vector<validated_section>      sections;
};

// ──────────────────────────────────────────────────────────────
//  issue_severity
//
//  Errors block "write" per SPEC 6.3; warnings do not.
// ──────────────────────────────────────────────────────────────

enum class issue_severity : std::uint8_t {
    warning,
    error,
};

auto to_string(issue_severity s) -> std::string_view;

// ──────────────────────────────────────────────────────────────
//  issue_kind
//
//  Categorical taxonomy. Stable identifiers callers can switch on.
//  Each kind has an implicit severity (encoded in the validator)
//  but the issue carries the severity explicitly for clarity.
// ──────────────────────────────────────────────────────────────

enum class issue_kind : std::uint8_t {
    // Field schema
    field_invalid_declaration,        // declaration line has wrong shape
    field_invalid_type,               // type token not String/Number/Date
    field_invalid_length,             // length not a positive integer
    field_invalid_constraint,         // constraint token not Not Null/Nullable
    field_empty_name,                 // name is empty
    field_position_out_of_range,      // position not in 1..N or duplicate
    field_position_gap,               // positions are not contiguous from 1
    field_position_duplicate,         // same position declared twice

    // Records
    record_field_out_of_range,        // record references field > field_count
    record_field_duplicate,           // same field appears twice in one record
    record_field_out_of_sequence,     // field number not strictly increasing
    record_missing_required_field,    // Not Null field absent or empty
    record_value_type_mismatch,       // value not parseable as declared type
    record_value_length_exceeded,     // String value longer than max_length
    record_hierarchical_value,        // value is a hierarchy (>1 segment) in a record line

    // Header
    header_unknown_field,             // header line number > recognized range
    header_invalid_jtext_version,     // jtext_version not parseable

    // Whole-section
    section_empty,                    // section has no records (only warning)
    section_duplicate_name,           // two sections with the same name
};

auto to_string(issue_kind k) -> std::string_view;

// ──────────────────────────────────────────────────────────────
//  validation_issue
// ──────────────────────────────────────────────────────────────

struct validation_issue {
    issue_severity   severity = issue_severity::error;
    issue_kind       kind     = issue_kind::field_invalid_declaration;
    std::size_t      line_no  = 0;        // 1-based source line; 0 if not applicable
    std::string      where;               // e.g. "section 'Tools', field 3"
    std::string      message;

    auto to_string() const -> std::string;
};

// ──────────────────────────────────────────────────────────────
//  validation_report
// ──────────────────────────────────────────────────────────────

struct validation_report {
    std::vector<validation_issue> issues;

    auto has_errors()   const -> bool;
    auto has_warnings() const -> bool;
    auto error_count()  const -> std::size_t;
    auto warning_count() const -> std::size_t;

    auto add(validation_issue issue) -> void;
};

// ──────────────────────────────────────────────────────────────
//  validate_result
//
//  Both the validated structure and the report. Per SPEC 6.3,
//  validation does not modify input or write output; it produces
//  a structured interpretation and an issues list.
// ──────────────────────────────────────────────────────────────

struct validate_result {
    validated_file     file;
    validation_report  report;
};

// ──────────────────────────────────────────────────────────────
//  validate
//
//  The entry point. Walks the parsed_file, interprets fields,
//  assembles records, validates everything, collects all issues.
// ──────────────────────────────────────────────────────────────

auto validate(const parsed_file& pf) -> validate_result;

// In-memory numbered template substitution (e.g. "INSERT ... VALUES ({1}, {2})").
// values[0] replaces {1}, etc. Useful for "in memory procedure" processing of
// records against templates (e.g. ts_store roundtrips to SQL) without materializing
// full .jtFull or .sql files on disk. For debugging the .jtFull is still supported.
std::string apply_numbered_template(std::string_view body,
                                    const std::vector<std::optional<std::string>>& values);

}  // namespace jtext

