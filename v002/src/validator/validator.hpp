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
//  SPEC Section 2.4.1: a deliberately small set of internal types.
//  Each is written with any of a family of spellings, because type
//  names vary by database (Text / NChar / VarChar are all String;
//  Int / BigInt are Integer; ...). See parse_field_type.
// ──────────────────────────────────────────────────────────────

enum class field_type : std::uint8_t {
    string_type,
    number_type,
    date_type,
    integer_type,   // whole numbers only (64-bit)
    bool_type,      // Yes/No, True/False, Y/N, T/F, 1/0
    datetime_type,  // YYYY-MM-DD[T| ]HH:MM:SS[.fraction][Z|+HH:MM|-HH:MM]
    gst_type,       // Galactic Standard Time: <earth_approximate_year>:<earth_approximate_year_ticks>
};

// Stable, readable names for diagnostics.
auto to_string(field_type t) -> std::string_view;

// Parse a type token, case-insensitive, by family (SPEC Section 2.4.1):
//   String:  String, Text, Char, NChar, VarChar, NVarChar, VarChar2, NText, CLOB
//   Integer: Integer, Int, BigInt, SmallInt, TinyInt, Int2, Int4, Int8, Long
//   Number:  Number, Numeric, Decimal, Real, Float, Double, Double Precision
//   Date:    Date
//   Bool:    Bool, Boolean, Bit, YesNo
//   DateTime: DateTime, DateTime2, SmallDateTime, Timestamp, TimestampTZ, DateTimeOffset
//   GST:     GST
// Returns nullopt for unrecognized tokens.
auto parse_field_type(std::string_view s) -> std::optional<field_type>;

// Bool values (case-insensitive): Yes/No, True/False, Y/N, T/F, 1/0.
// Returns the truth value, or nullopt if `s` is not a Bool value.
auto parse_bool_value(std::string_view s) -> std::optional<bool>;

// ──────────────────────────────────────────────────────────────
//  auto_id_spec / parse_auto_id_designation
//
//  SPEC Section 2.4.3: the record id (field 1) may be designated as
//  auto-generated. The designation names vary by database, so any of
//  these spellings is accepted (case-insensitive):
//    AutoNumber, AutoID, AutoIncrement, Auto_Increment, Identity,
//    Identity(seed,step), Serial, SmallSerial, BigSerial
//  All of them mean "an integer the producer assigns": seed is the
//  first id, step the increment (both default to 1).
// ──────────────────────────────────────────────────────────────

struct auto_id_spec {
    std::int64_t seed = 1;
    std::int64_t step = 1;
};

auto parse_auto_id_designation(std::string_view s) -> std::optional<auto_id_spec>;

// True when `s` is an integer: optional sign, then one or more digits, and
// nothing else (no decimal point, no exponent). The record id must be one.
auto looks_like_integer(std::string_view s) -> bool;

// Galactic Standard Time (SPEC Section 2.4.1), written "<year>:<ticks>":
//   year  = earth_approximate_year, a signed 64-bit integer
//   ticks = earth_approximate_year_ticks, 10 ps ticks since the start of that
//           year, always zero-padded to the full kGstTickDigits (19) digits.
//           Any 19-digit value is a valid GST. The validator warns (never
//           errors) at two levels: ticks past one year (leap seconds in use),
//           and ticks past one year plus kGstLeapSecondsRoom (3) leap seconds.
// e.g. "2026:0000000000000000001" is 10 picoseconds into earth approximate year 2026.
inline constexpr std::uint64_t kGstTicksPerEarthYear = 3'155'695'200'000'000'000ULL;
inline constexpr std::uint64_t kGstTicksPerSecond    = 100'000'000'000ULL;   // 10 ps ticks
inline constexpr std::uint64_t kGstLeapSecondsRoom   = 3;
inline constexpr std::uint64_t kGstTickLimit =
    kGstTicksPerEarthYear + kGstLeapSecondsRoom * kGstTicksPerSecond;   // exclusive
inline constexpr std::size_t   kGstTickDigits = 19;   // digits in kGstTickLimit - 1

struct gst_value {
    std::int64_t  year  = 0;
    std::uint64_t ticks = 0;
};

auto parse_gst_value(std::string_view s) -> std::optional<gst_value>;

// ──────────────────────────────────────────────────────────────
//  field
//
//  Typed metadata for one field, interpreted from a single
//  declaration line in the Fields block.
//
//  Per SPEC Section 2.4 the syntax is:
//    <name> / <segment> [ / <segment> ... ]   (up to 4 segments)
//  where the segments, in any order and each at most once, are a type,
//  an auto designation, a length, the constraints "Not Null" and "Not Empty"
//  (or "Nullable", the default), and, for Date/DateTime, NullDate(YYYY-MM-DD).
//  A type or an auto designation is required.
// ──────────────────────────────────────────────────────────────

struct field {
    unsigned                       position    = 0;  // 1..N (from line number)
    std::string                    name;
    field_type                     type        = field_type::string_type;
    std::optional<std::size_t>     max_length;       // String length cap if declared
    bool                           not_null    = false;  // Not Null: never {NULL}
    bool                           not_empty   = false;  // Not Empty: never ""
    // Date/DateTime: the date that stands for "no date" (SPEC 2.6.1); see null_date().
    std::string                    null_date;
    std::string                    comment;          // inline comment on the field line
    // Set when the declaration carries an auto designation (Section 2.4.3);
    // only meaningful on field 1, the record id.
    std::optional<auto_id_spec>    auto_id;
};

// ──────────────────────────────────────────────────────────────
//  record
//
//  One assembled record. values[i] is the text of field position i+1
//  (values[0] is field 1, the record id). A file holds no nulls (SPEC
//  2.6.1): a field whose line is missing, or has no data, is "", and
//  {NULL} is the text "{NULL}" (kNullValue), as written.
// ──────────────────────────────────────────────────────────────

struct record {
    std::size_t                              source_line_no = 0;
    std::vector<std::string>                 values;
    // Field 1, the mandatory record id (SPEC Section 2.6.4), as an integer.
    // nullopt when field 1 is missing or not an integer (both reported).
    std::optional<std::int64_t>              record_id;
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
    field_invalid_constraint,         // segment not a type, length or constraint, or repeated
    field_empty_name,                 // name is empty
    field_position_out_of_range,      // position not in 1..N or duplicate
    field_position_gap,               // positions are not contiguous from 1
    field_position_duplicate,         // same position declared twice
    field_record_id_invalid,          // field 1 is not an integer type, or is Nullable
    field_invalid_null_date,          // NullDate(...) not a date, or on a non-date field

    // Records
    record_field_out_of_range,        // record references field > field_count
    record_field_duplicate,           // same field appears twice in one record
    record_field_out_of_sequence,     // field number not strictly increasing
    record_null_not_allowed,          // Not Null field holds {NULL}
    record_empty_not_allowed,         // Not Empty field is empty or missing
    record_date_missing,              // Date/DateTime field is empty or {NULL}
    record_value_type_mismatch,       // value not parseable as declared type
    record_value_length_exceeded,     // String value longer than max_length
    record_missing_record_id,         // field 1 (the record id) absent or empty
    record_id_not_integer,            // field 1 value is not an integer
    record_gst_leap_seconds,          // Warning 1: GST ticks past one year (leap seconds)
    record_gst_beyond_leap_room,      // Warning 2: GST ticks past one year + 3 leap seconds

    // Header
    header_unknown_field,             // header line number > recognized range
    header_missing_jtext_version,     // no jtext_version declared
    header_older_jtext_version,       // jtext_version older than jtext::kFormatVersion
    header_newer_jtext_version,       // jtext_version newer than jtext::kFormatVersion
    header_invalid_jtext_version,     // jtext_version not a version number

    // Templates
    template_placeholder_out_of_range, // {N} references field 0 or > field_count

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
//  validation_level
//
//  Layered validation: callers choose how much checking they want,
//  trading thoroughness for speed. Higher levels are supersets of
//  lower ones.
//
//   - minimal:  structural integrity + Not Null / Not Empty / dates present.
//               No per-value parsing — the fast path for trusted data.
//   - standard: minimal + per-value type/format checks (Number,
//               strict YYYY-MM-DD Date) + String length caps.
//   - strict:   standard + template {N} placeholder range checks.
//               Maximum scrutiny; use for critical/financial data.
//
//  Advisory warnings (empty section, duplicate name, jtext_version,
//  GST past a year) are reported at every level and never gate
//  has_errors().
// ──────────────────────────────────────────────────────────────

enum class validation_level : std::uint8_t {
    minimal,
    standard,
    strict,
};

// ──────────────────────────────────────────────────────────────
//  validate
//
//  The entry point. Walks the parsed_file, interprets fields,
//  assembles records, validates everything, collects all issues.
//  `level` defaults to strict, so existing callers are unchanged and
//  get the most thorough checking (fail-closed).
// ──────────────────────────────────────────────────────────────

auto validate(const parsed_file& pf,
              validation_level   level = validation_level::strict) -> validate_result;

// The date a Date or DateTime field stores where there is none (SPEC 2.6.1): its
// NullDate(...), else kDefaultNullDate ("1900-01-01"); a DateTime gets " 00:00:00".
auto null_date(const field& f) -> std::string;

}  // namespace jtext

