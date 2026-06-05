// File: src/validator/validator.cpp
//
// Implementation of validate(parsed_file).
//
// Organized in three passes per section:
//   1. interpret_fields:    declarations → typed schema, with issues
//   2. assemble_records:    data lines → records (number 0 = sep)
//   3. validate_records:    each record against its schema
//
// Plus a header pass and cross-section check (duplicate names).
//
// Per SPEC v0.6 Section 6.2: report ALL issues found, not just the
// first one. The validator never short-circuits on the first error.

#include "validator/validator.hpp"
#include "parser/line.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <format>
#include <set>
#include <string>
#include <string_view>

namespace jtext {

// ──────────────────────────────────────────────────────────────
//  to_string for enums
// ──────────────────────────────────────────────────────────────

auto to_string(field_type t) -> std::string_view
{
    switch (t) {
        case field_type::string_type: return "String";
        case field_type::number_type: return "Number";
        case field_type::date_type:   return "Date";
    }
    return "unknown";
}

auto to_string(issue_severity s) -> std::string_view
{
    switch (s) {
        case issue_severity::warning: return "warning";
        case issue_severity::error:   return "error";
    }
    return "unknown";
}

auto to_string(issue_kind k) -> std::string_view
{
    switch (k) {
        case issue_kind::field_invalid_declaration:    return "field_invalid_declaration";
        case issue_kind::field_invalid_type:           return "field_invalid_type";
        case issue_kind::field_invalid_length:         return "field_invalid_length";
        case issue_kind::field_invalid_constraint:     return "field_invalid_constraint";
        case issue_kind::field_empty_name:             return "field_empty_name";
        case issue_kind::field_position_out_of_range:  return "field_position_out_of_range";
        case issue_kind::field_position_gap:           return "field_position_gap";
        case issue_kind::field_position_duplicate:     return "field_position_duplicate";
        case issue_kind::record_field_out_of_range:    return "record_field_out_of_range";
        case issue_kind::record_field_duplicate:       return "record_field_duplicate";
        case issue_kind::record_field_out_of_sequence: return "record_field_out_of_sequence";
        case issue_kind::record_missing_required_field: return "record_missing_required_field";
        case issue_kind::record_value_type_mismatch:   return "record_value_type_mismatch";
        case issue_kind::record_value_length_exceeded: return "record_value_length_exceeded";
        case issue_kind::record_hierarchical_value:    return "record_hierarchical_value";
        case issue_kind::header_unknown_field:         return "header_unknown_field";
        case issue_kind::header_invalid_jtext_version: return "header_invalid_jtext_version";
        case issue_kind::section_empty:                return "section_empty";
        case issue_kind::section_duplicate_name:       return "section_duplicate_name";
    }
    return "unknown";
}

auto validation_issue::to_string() const -> std::string
{
    return std::format("[{}] {} (line {}, {}): {}",
                       jtext::to_string(severity),
                       jtext::to_string(kind),
                       line_no,
                       where,
                       message);
}

// ──────────────────────────────────────────────────────────────
//  validation_report methods
// ──────────────────────────────────────────────────────────────

auto validation_report::has_errors() const -> bool
{
    for (const auto& i : issues) {
        if (i.severity == issue_severity::error) return true;
    }
    return false;
}

auto validation_report::has_warnings() const -> bool
{
    for (const auto& i : issues) {
        if (i.severity == issue_severity::warning) return true;
    }
    return false;
}

auto validation_report::error_count() const -> std::size_t
{
    std::size_t n = 0;
    for (const auto& i : issues) {
        if (i.severity == issue_severity::error) ++n;
    }
    return n;
}

auto validation_report::warning_count() const -> std::size_t
{
    std::size_t n = 0;
    for (const auto& i : issues) {
        if (i.severity == issue_severity::warning) ++n;
    }
    return n;
}

auto validation_report::add(validation_issue issue) -> void
{
    issues.push_back(std::move(issue));
}

// ──────────────────────────────────────────────────────────────
//  Small helpers
// ──────────────────────────────────────────────────────────────

namespace {

// Case-insensitive string equality (ASCII only).
auto iequals(std::string_view a, std::string_view b) -> bool
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] + 32) : a[i];
        const auto cb = (b[i] >= 'A' && b[i] <= 'Z') ? static_cast<char>(b[i] + 32) : b[i];
        if (ca != cb) return false;
    }
    return true;
}

// Build an issue and add it to the report.
auto report_issue(
    validation_report& r,
    issue_severity     sev,
    issue_kind         kind,
    std::size_t        line_no,
    std::string        where,
    std::string        message) -> void
{
    r.add(validation_issue{sev, kind, line_no, std::move(where), std::move(message)});
}

}  // anonymous namespace

// ──────────────────────────────────────────────────────────────
//  parse_field_type
// ──────────────────────────────────────────────────────────────

auto parse_field_type(std::string_view s) -> std::optional<field_type>
{
    if (iequals(s, "String")) return field_type::string_type;
    if (iequals(s, "Number")) return field_type::number_type;
    if (iequals(s, "Date"))   return field_type::date_type;
    return std::nullopt;
}

// ──────────────────────────────────────────────────────────────
//  Type-format validators (for record values)
// ──────────────────────────────────────────────────────────────

namespace {

// Number: parseable as integer or decimal. Optional leading sign.
// We accept these patterns:
//   42, -42, +42, 3.14, -3.14, 0, 0.0, 1e10, -1.5e-3
auto looks_like_number(std::string_view s) -> bool
{
    if (s.empty()) return false;
    std::size_t i = 0;
    if (s[0] == '+' || s[0] == '-') i = 1;
    bool has_digit = false;
    bool has_dot   = false;
    for (; i < s.size(); ++i) {
        if (s[i] >= '0' && s[i] <= '9') { has_digit = true; continue; }
        if (s[i] == '.' && !has_dot)    { has_dot = true; continue; }
        if ((s[i] == 'e' || s[i] == 'E') && has_digit && i + 1 < s.size()) {
            std::size_t j = i + 1;
            if (s[j] == '+' || s[j] == '-') ++j;
            if (j >= s.size()) return false;
            for (; j < s.size(); ++j) {
                if (s[j] < '0' || s[j] > '9') return false;
            }
            return true;
        }
        return false;
    }
    return has_digit;
}

// Date: ISO 8601 format. Accept either:
//   YYYY-MM-DD
//   YYYY-MM-DDTHH:MM:SS  (with optional .frac and timezone Z or ±HH:MM)
// We do not validate that the date is a real calendar date (Feb 30
// would pass here); structural shape is sufficient for now. A future
// pass can tighten if needed.
auto looks_like_date(std::string_view s) -> bool
{
    if (s.size() < 10) return false;
    // YYYY-MM-DD prefix
    auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
    if (!is_digit(s[0]) || !is_digit(s[1]) || !is_digit(s[2]) || !is_digit(s[3])) return false;
    if (s[4] != '-') return false;
    if (!is_digit(s[5]) || !is_digit(s[6])) return false;
    if (s[7] != '-') return false;
    if (!is_digit(s[8]) || !is_digit(s[9])) return false;
    if (s.size() == 10) return true;
    // Datetime: 'T' then HH:MM:SS plus optional fragments
    if (s[10] != 'T' && s[10] != ' ') return false;
    if (s.size() < 19) return false;
    if (!is_digit(s[11]) || !is_digit(s[12])) return false;
    if (s[13] != ':') return false;
    if (!is_digit(s[14]) || !is_digit(s[15])) return false;
    if (s[16] != ':') return false;
    if (!is_digit(s[17]) || !is_digit(s[18])) return false;
    // We don't strictly validate the rest (fractional, timezone).
    return true;
}

}  // anonymous namespace

// ──────────────────────────────────────────────────────────────
//  Field-declaration interpretation
//
//  Per SPEC v0.6 Section 2.4: a field declaration line is one
//  parsed_line whose hierarchy has the segments:
//     [name, type] | [name, type, length] | [name, type, length, constraints]
//
//  Position comes from the line's number (1..N).
//  Comment is preserved verbatim from the line.
// ──────────────────────────────────────────────────────────────

namespace {

struct interpret_field_result {
    std::optional<field>  fld;     // nullopt if interpretation failed fatally
    bool                  reported_issue = false;
};

auto interpret_one_field(
    const parsed_line&   line,
    std::string_view     section_name,
    validation_report&   report)
    -> interpret_field_result
{
    interpret_field_result out;

    if (line.hierarchy.size() < 2) {
        report_issue(report, issue_severity::error,
                     issue_kind::field_invalid_declaration,
                     0,  // source line not tracked at this layer yet
                     std::format("section '{}', field {}", section_name, line.number),
                     std::format("expected at least name/type, got {} segment(s)",
                                 line.hierarchy.size()));
        out.reported_issue = true;
        return out;
    }
    if (line.hierarchy.size() > 4) {
        report_issue(report, issue_severity::error,
                     issue_kind::field_invalid_declaration,
                     0,
                     std::format("section '{}', field {}", section_name, line.number),
                     std::format("too many segments: {} (max 4: "
                                 "name/type/length/constraints)",
                                 line.hierarchy.size()));
        out.reported_issue = true;
        return out;
    }

    field f;
    f.position = line.number;
    f.name     = line.hierarchy[0];
    f.comment  = line.comment;

    if (f.name.empty()) {
        report_issue(report, issue_severity::error,
                     issue_kind::field_empty_name,
                     0,
                     std::format("section '{}', field {}", section_name, line.number),
                     "field name is empty");
        out.reported_issue = true;
        // Continue interpreting the rest so we report more issues.
    }

    if (auto t = parse_field_type(line.hierarchy[1]); t.has_value()) {
        f.type = *t;
    } else {
        report_issue(report, issue_severity::error,
                     issue_kind::field_invalid_type,
                     0,
                     std::format("section '{}', field {}", section_name, line.number),
                     std::format("type '{}' is not recognized (expected "
                                 "String, Number, or Date)",
                                 line.hierarchy[1]));
        out.reported_issue = true;
    }

    // Optional length and/or constraint.
    // The 3rd segment is length if it parses as a positive integer;
    // otherwise it's a constraint.
    auto interpret_length = [&](std::string_view seg) -> bool {
        std::size_t n = 0;
        auto [p, ec] = std::from_chars(seg.data(), seg.data() + seg.size(), n);
        if (ec != std::errc{} || p != seg.data() + seg.size() || n == 0) {
            return false;
        }
        f.max_length = n;
        return true;
    };

    auto interpret_constraint = [&](std::string_view seg) -> bool {
        if (iequals(seg, "Not Null")) { f.required = true;  return true; }
        if (iequals(seg, "Nullable")) { f.required = false; return true; }
        return false;
    };

    if (line.hierarchy.size() >= 3) {
        const auto& seg3 = line.hierarchy[2];
        if (!interpret_length(seg3)) {
            // Maybe it's a constraint with no length.
            if (!interpret_constraint(seg3)) {
                report_issue(report, issue_severity::error,
                             issue_kind::field_invalid_length,
                             0,
                             std::format("section '{}', field {}", section_name, line.number),
                             std::format("segment '{}' is neither a positive "
                                         "integer length nor a recognized "
                                         "constraint", seg3));
                out.reported_issue = true;
            }
        }
    }
    if (line.hierarchy.size() >= 4) {
        const auto& seg4 = line.hierarchy[3];
        if (!interpret_constraint(seg4)) {
            report_issue(report, issue_severity::error,
                         issue_kind::field_invalid_constraint,
                         0,
                         std::format("section '{}', field {}", section_name, line.number),
                         std::format("constraint '{}' is not recognized "
                                     "(expected 'Not Null' or 'Nullable')", seg4));
            out.reported_issue = true;
        }
    }

    out.fld = std::move(f);
    return out;
}

// Walk the field_lines and produce a sorted vector<field> with
// position checks. Reports gaps, duplicates, etc.
auto interpret_fields(
    const parsed_section& sec,
    validation_report&    report) -> std::vector<field>
{
    std::vector<field> fields;
    fields.reserve(sec.field_lines.size());

    // Track which positions have been seen, in declaration order.
    // We accept declarations in any order; we sort at the end and
    // then verify contiguity 1..N.
    std::set<unsigned> seen_positions;

    for (const auto& fl : sec.field_lines) {
        auto r = interpret_one_field(fl, sec.name, report);
        if (!r.fld) continue;  // already reported, skip

        if (seen_positions.contains(fl.number)) {
            report_issue(report, issue_severity::error,
                         issue_kind::field_position_duplicate,
                         0,
                         std::format("section '{}', field {}", sec.name, fl.number),
                         std::format("position {} declared more than once", fl.number));
            continue;
        }
        seen_positions.insert(fl.number);
        fields.push_back(*r.fld);
    }

    // Sort by position.
    std::sort(fields.begin(), fields.end(),
              [](const field& a, const field& b) { return a.position < b.position; });

    // Check contiguity: positions must be 1, 2, 3, ..., N.
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const auto expected = static_cast<unsigned>(i + 1);
        if (fields[i].position != expected) {
            report_issue(report, issue_severity::error,
                         issue_kind::field_position_gap,
                         0,
                         std::format("section '{}'", sec.name),
                         std::format("field positions must be contiguous from 1; "
                                     "expected position {} at index {}, got {}",
                                     expected, i, fields[i].position));
            break;  // one report; the rest of the analysis can still proceed
        }
    }

    return fields;
}

// ──────────────────────────────────────────────────────────────
//  Record assembly
//
//  Per SPEC v0.6: records are separated by blank lines within the
//  data block. The section parser preserved blanks as sentinel
//  parsed_line entries with number == 0.
//
//  Within a single record, field numbers must be strictly
//  increasing (1, 2, 5 is OK; 1, 2, 2 is not; 1, 3, 2 is not).
//  Field positions outside 1..N are errors.
// ──────────────────────────────────────────────────────────────

auto encode_value(const parsed_line& line) -> std::string
{
    // The hierarchy was parsed by parse_line. For typical record
    // lines the hierarchy is one element. If it's multiple, join
    // with '/' and let the validator flag it as suspicious for
    // record values.
    if (line.hierarchy.size() == 1) return line.hierarchy[0];
    std::string out;
    for (std::size_t i = 0; i < line.hierarchy.size(); ++i) {
        if (i > 0) out.push_back('/');
        out.append(line.hierarchy[i]);
    }
    return out;
}

auto assemble_records(
    const parsed_section& sec,
    std::size_t           field_count,
    validation_report&    report) -> std::vector<record>
{
    std::vector<record> records;
    record current;
    bool record_open = false;
    unsigned last_pos = 0;
    bool first_field_in_record = true;

    auto flush_current = [&]() {
        if (record_open) {
            records.push_back(std::move(current));
            current = record{};
            record_open = false;
            last_pos = 0;
            first_field_in_record = true;
        }
    };

    bool ts_style = false;
    if (!sec.data_lines.empty() && sec.data_lines[0].number > field_count) {
      ts_style = true;
    }
    unsigned ts_field_pos = 0;
    std::size_t current_ts_record_id = 0;

    for (const auto& pl : sec.data_lines) {
        if (pl.number == 0) {
            // Blank-line sentinel: end of current record.
            flush_current();
            if (ts_style) ts_field_pos = 0;
            continue;
        }

        // ts_store compact full-row format support: one data line per record,
        // using formatter #|# (or similar) so the line parser already split the
        // tail into pl.hierarchy (one entry per field value). The pl.number is
        // the record id. Handle this regardless of the old ts_style heuristic
        // (which was for per-field lines repeating the record id).
        // If hierarchy has >1 entry, this pl came from a multi-field compact line
        // (standard per-field data lines have hierarchy.size()==1).
        if (pl.hierarchy.size() > 1) {
            // full record in one line
            flush_current();
            current.values.assign(field_count, std::nullopt);
            current.source_line_no = 0;
            current.record_id = pl.number;
            record_open = true;
            for (size_t fi = 0; fi < pl.hierarchy.size() && fi < field_count; ++fi) {
                const auto& hv = pl.hierarchy[fi];
                // We picked '|' as the field separator character for compact
                // ts_store-style data rows ("N. #|# f1|f2|...").
                // Null is represented by the embedded ASCII Unit Separator (0x1F)
                // so that a null field appears as |\x1F| while empty string is || .
                // This allows the surface to look similar (|| vs || with invisible)
                // but distinguishes null from empty.
                // We also support \N and NULL for compatibility.
                if (hv == "\x1f" || hv == "\\N" || hv == "NULL") {
                    current.values[fi] = std::nullopt;
                } else {
                    current.values[fi] = hv;  // empty string stays as ""
                }
            }
            flush_current();  // complete row
            continue;
        }

        if (ts_style) {
            if (!record_open || pl.number != current_ts_record_id) {
                flush_current();
                current.values.assign(field_count, std::nullopt);
                current.source_line_no = 0;
                current.record_id = pl.number;
                record_open = true;
                current_ts_record_id = pl.number;
                ts_field_pos = 0;
                last_pos = 0;
                first_field_in_record = true;
            }
            ts_field_pos++;
            if (ts_field_pos > field_count) {
                report_issue(report, issue_severity::error,
                             issue_kind::record_field_out_of_range,
                             0,
                             std::format("section '{}'", sec.name),
                             std::format("data line for record id {} references field {} but the "
                                         "section declares only {} field(s)",
                                         pl.number, ts_field_pos, field_count));
                continue;
            }
            current.values[ts_field_pos - 1] = encode_value(pl);
            last_pos = ts_field_pos;
            first_field_in_record = false;
            continue;
        }

        // Regular data line. Lazy-init the current record's values
        // vector to size field_count (only the first time we see a
        // field for this record).
        if (!record_open) {
            current.values.assign(field_count, std::nullopt);
            current.source_line_no = 0;  // we don't have source line; future work
            current.record_id = 0;
            record_open = true;
            first_field_in_record = true;
            last_pos = 0;
        }

        // Validate the field position.
        if (pl.number > field_count) {
            report_issue(report, issue_severity::error,
                         issue_kind::record_field_out_of_range,
                         0,
                         std::format("section '{}'", sec.name),
                         std::format("data line references field {} but the "
                                     "section declares only {} field(s)",
                                     pl.number, field_count));
            // Continue past — don't write to values[].
            continue;
        }

        // Strict increase check.
        if (!first_field_in_record && pl.number <= last_pos) {
            if (pl.number == last_pos) {
                report_issue(report, issue_severity::error,
                             issue_kind::record_field_duplicate,
                             0,
                             std::format("section '{}'", sec.name),
                             std::format("field {} appears more than once in the "
                                         "same record", pl.number));
            } else {
                report_issue(report, issue_severity::error,
                             issue_kind::record_field_out_of_sequence,
                             0,
                             std::format("section '{}'", sec.name),
                             std::format("field {} appears after field {} in the "
                                         "same record; field numbers must be "
                                         "strictly increasing", pl.number, last_pos));
            }
            continue;
        }

        // Multi-segment hierarchies in records are flagged.
        if (pl.hierarchy.size() > 1) {
            report_issue(report, issue_severity::warning,
                         issue_kind::record_hierarchical_value,
                         0,
                         std::format("section '{}'", sec.name),
                         std::format("data line for field {} contains a "
                                     "hierarchical value with {} segments; "
                                     "stored as joined string", pl.number,
                                     pl.hierarchy.size()));
        }

        current.values[pl.number - 1] = encode_value(pl);
        last_pos = pl.number;
        first_field_in_record = false;
    }

    flush_current();

    if (records.empty()) {
        report_issue(report, issue_severity::warning,
                     issue_kind::section_empty,
                     0,
                     std::format("section '{}'", sec.name),
                     "section contains no records");
    }

    return records;
}

// ──────────────────────────────────────────────────────────────
//  Record value validation
// ──────────────────────────────────────────────────────────────

auto validate_record_values(
    const std::vector<field>& fields,
    const std::vector<record>& records,
    std::string_view           section_name,
    validation_report&         report) -> void
{
    for (std::size_t ri = 0; ri < records.size(); ++ri) {
        const auto& rec = records[ri];
        for (std::size_t fi = 0; fi < fields.size() && fi < rec.values.size(); ++fi) {
            const auto& fld = fields[fi];
            const auto& v   = rec.values[fi];

            const bool present = v.has_value();
            const bool empty   = present && v->empty();

            if (fld.required) {
                if (!present || empty) {
                    report_issue(report, issue_severity::error,
                                 issue_kind::record_missing_required_field,
                                 0,
                                 std::format("section '{}', record {}, field {} ({})",
                                             section_name, ri + 1, fld.position, fld.name),
                                 "required (Not Null) field is missing or empty");
                    continue;
                }
            }

            if (!present || empty) continue;  // Nullable + absent/empty: NULL per spec

            // Type checks (only when there's a value to check).
            switch (fld.type) {
            case field_type::string_type:
                if (fld.max_length.has_value() && v->size() > *fld.max_length) {
                    report_issue(report, issue_severity::error,
                                 issue_kind::record_value_length_exceeded,
                                 0,
                                 std::format("section '{}', record {}, field {} ({})",
                                             section_name, ri + 1, fld.position, fld.name),
                                 std::format("value length {} exceeds declared "
                                             "max_length {}",
                                             v->size(), *fld.max_length));
                }
                break;
            case field_type::number_type:
                if (!looks_like_number(*v)) {
                    report_issue(report, issue_severity::error,
                                 issue_kind::record_value_type_mismatch,
                                 0,
                                 std::format("section '{}', record {}, field {} ({})",
                                             section_name, ri + 1, fld.position, fld.name),
                                 std::format("value '{}' is not a valid Number", *v));
                }
                break;
            case field_type::date_type:
                if (!looks_like_date(*v)) {
                    report_issue(report, issue_severity::error,
                                 issue_kind::record_value_type_mismatch,
                                 0,
                                 std::format("section '{}', record {}, field {} ({})",
                                             section_name, ri + 1, fld.position, fld.name),
                                 std::format("value '{}' is not a valid Date "
                                             "(expected YYYY-MM-DD or "
                                             "YYYY-MM-DDTHH:MM:SS)", *v));
                }
                break;
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────
//  Header interpretation
//
//  Per SPEC v0.6 Section 2.2, file-header fields are addressed by
//  line number with the canonical assignment:
//     1: filename, 2: date, 3: purpose, 4: jtext_version,
//     5: case_mode, 6: sql_dialect, 7: table_name, 8: auto_id,
//     9: related_files
//  Line numbers beyond 9 are unknown — a warning, not an error.
// ──────────────────────────────────────────────────────────────

auto interpret_header(
    const std::vector<parsed_line>& header_lines,
    validation_report&              report) -> std::map<std::string, std::string>
{
    static constexpr std::array<std::string_view, 10> recognized{
        "",                // unused (positions are 1-based)
        "filename",
        "date",
        "purpose",
        "jtext_version",
        "case_mode",
        "sql_dialect",
        "table_name",
        "auto_id",
        "related_files",
    };

    std::map<std::string, std::string> out;
    for (const auto& hl : header_lines) {
        const auto pos = hl.number;
        if (pos < 1 || pos >= recognized.size()) {
            report_issue(report, issue_severity::warning,
                         issue_kind::header_unknown_field,
                         0, "header",
                         std::format("line {} is not a recognized header field "
                                     "(positions 1-9 are defined; ignoring)",
                                     pos));
            continue;
        }
        const auto& key = recognized[pos];
        // Header values are single-segment except for related_files,
        // which is a hierarchy of filenames. Both cases get joined
        // with '/' separator when stored.
        std::string value;
        if (hl.hierarchy.size() == 1) {
            value = hl.hierarchy[0];
        } else {
            for (std::size_t i = 0; i < hl.hierarchy.size(); ++i) {
                if (i > 0) value.push_back('/');
                value.append(hl.hierarchy[i]);
            }
        }
        out[std::string{key}] = std::move(value);
    }

    // jtext_version sanity check.
    if (auto it = out.find("jtext_version"); it != out.end()) {
        const auto& v = it->second;
        // Expected shape: a major.minor decimal or just digits.
        // We accept any non-empty value with at least one digit; a
        // stricter form is possible later.
        bool has_digit = false;
        for (char c : v) if (c >= '0' && c <= '9') { has_digit = true; break; }
        if (!has_digit) {
            report_issue(report, issue_severity::warning,
                         issue_kind::header_invalid_jtext_version,
                         0, "header",
                         std::format("jtext_version '{}' does not look like a "
                                     "version number", v));
        }
    }

    return out;
}

}  // anonymous namespace

// ──────────────────────────────────────────────────────────────
//  validate entry point
// ──────────────────────────────────────────────────────────────

auto validate(const parsed_file& pf) -> validate_result
{
    validate_result vr;

    vr.file.header = interpret_header(pf.header_lines, vr.report);

    std::set<std::string> section_names_seen;

    for (const auto& sec : pf.sections) {
        // Duplicate section name check (warning per SPEC 2.3).
        if (section_names_seen.contains(sec.name)) {
            report_issue(vr.report, issue_severity::warning,
                         issue_kind::section_duplicate_name,
                         0, std::format("section '{}'", sec.name),
                         std::format("section name '{}' appears more than once",
                                     sec.name));
        } else {
            section_names_seen.insert(sec.name);
        }

        validated_section vs;
        vs.name      = sec.name;
        vs.templates = sec.templates;
        vs.includes  = sec.includes;
        vs.fields    = interpret_fields(sec, vr.report);

        const auto fc = vs.fields.size();
        vs.records   = assemble_records(sec, fc, vr.report);

        validate_record_values(vs.fields, vs.records, vs.name, vr.report);

        vr.file.sections.push_back(std::move(vs));
    }

    return vr;
}

// Simple in-memory substitution for numbered placeholders {1}, {2}, ...
// Replaces {N} with the corresponding value (or "NULL" for missing).
// This is the core of the "in memory procedure" for template-driven processing
// (e.g. turning ts_store jText records + template into SQL statements on the fly
// without writing a full .jtFull or .sql file). The caller is responsible for
// any SQL-specific quoting/escaping if needed.
std::string apply_numbered_template(std::string_view body,
                                    const std::vector<std::optional<std::string>>& values)
{
    std::string out(body);
    for (std::size_t i = 0; i < values.size(); ++i) {
        std::string placeholder = "{" + std::to_string(i + 1) + "}";
        std::string replacement = values[i].has_value() ? *values[i] : "NULL";

        std::string::size_type pos = 0;
        while ((pos = out.find(placeholder, pos)) != std::string::npos) {
            out.replace(pos, placeholder.length(), replacement);
            pos += replacement.length();
        }
    }
    return out;
}

}  // namespace jtext

