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
        case field_type::integer_type: return "Integer";
        case field_type::bool_type:   return "Bool";
        case field_type::datetime_type: return "DateTime";
        case field_type::gst_type:    return "GST";
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
        case issue_kind::field_record_id_invalid:      return "field_record_id_invalid";
        case issue_kind::record_field_out_of_range:    return "record_field_out_of_range";
        case issue_kind::record_field_duplicate:       return "record_field_duplicate";
        case issue_kind::record_field_out_of_sequence: return "record_field_out_of_sequence";
        case issue_kind::record_missing_required_field: return "record_missing_required_field";
        case issue_kind::record_value_type_mismatch:   return "record_value_type_mismatch";
        case issue_kind::record_value_length_exceeded: return "record_value_length_exceeded";
        case issue_kind::record_hierarchical_value:    return "record_hierarchical_value";
        case issue_kind::record_missing_record_id:     return "record_missing_record_id";
        case issue_kind::record_id_not_integer:        return "record_id_not_integer";
        case issue_kind::record_gst_leap_seconds:      return "record_gst_leap_seconds";
        case issue_kind::record_gst_beyond_leap_room:  return "record_gst_beyond_leap_room";
        case issue_kind::header_unknown_field:         return "header_unknown_field";
        case issue_kind::header_missing_jtext_version: return "header_missing_jtext_version";
        case issue_kind::header_older_jtext_version:   return "header_older_jtext_version";
        case issue_kind::header_newer_jtext_version:   return "header_newer_jtext_version";
        case issue_kind::header_invalid_jtext_version: return "header_invalid_jtext_version";
        case issue_kind::template_placeholder_out_of_range: return "template_placeholder_out_of_range";
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
    // Type names vary by database, so each internal type has a family of
    // spellings (SPEC Section 2.4.1). Matched case-insensitively.
    struct family { field_type type; std::array<std::string_view, 9> names; };
    static constexpr std::array<family, 7> families{{
        {field_type::string_type,  {"String", "Text", "Char", "NChar", "VarChar",
                                    "NVarChar", "VarChar2", "NText", "CLOB"}},
        {field_type::integer_type, {"Integer", "Int", "BigInt", "SmallInt", "TinyInt",
                                    "Int2", "Int4", "Int8", "Long"}},
        {field_type::number_type,  {"Number", "Numeric", "Decimal", "Real", "Float",
                                    "Double", "Double Precision"}},
        {field_type::date_type,    {"Date"}},
        {field_type::bool_type,    {"Bool", "Boolean", "Bit", "YesNo"}},
        {field_type::datetime_type, {"DateTime", "DateTime2", "SmallDateTime", "Timestamp",
                                     "TimestampTZ", "DateTimeOffset"}},
        {field_type::gst_type,     {"GST"}},
    }};
    for (const auto& fam : families) {
        for (auto name : fam.names) {
            if (!name.empty() && iequals(s, name)) return fam.type;
        }
    }
    return std::nullopt;
}

auto parse_bool_value(std::string_view s) -> std::optional<bool>
{
    for (auto t : {"Yes", "True", "Y", "T", "1"}) if (iequals(s, t)) return true;
    for (auto f : {"No", "False", "N", "F", "0"}) if (iequals(s, f)) return false;
    return std::nullopt;
}

// ──────────────────────────────────────────────────────────────
//  parse_auto_id_designation / looks_like_integer
// ──────────────────────────────────────────────────────────────

auto looks_like_integer(std::string_view s) -> bool
{
    std::size_t i = (!s.empty() && (s[0] == '+' || s[0] == '-')) ? 1 : 0;
    if (i == s.size()) return false;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

auto parse_gst_value(std::string_view s) -> std::optional<gst_value>
{
    const auto colon = s.find(':');
    if (colon == std::string_view::npos) return std::nullopt;
    std::string_view ys = s.substr(0, colon);
    const std::string_view ts = s.substr(colon + 1);
    // Ticks are always the full width (zero-padded), digits only.
    if (!looks_like_integer(ys) || ts.size() != kGstTickDigits || !looks_like_integer(ts) ||
        ts.front() == '+' || ts.front() == '-') {
        return std::nullopt;
    }
    if (ys.front() == '+') ys.remove_prefix(1);
    gst_value g;
    auto [yp, yec] = std::from_chars(ys.data(), ys.data() + ys.size(), g.year);
    auto [tp, tec] = std::from_chars(ts.data(), ts.data() + ts.size(), g.ticks);
    if (yec != std::errc{} || yp != ys.data() + ys.size() ||
        tec != std::errc{} || tp != ts.data() + ts.size()) {
        return std::nullopt;
    }
    return g;
}

auto parse_auto_id_designation(std::string_view s) -> std::optional<auto_id_spec>
{
    static constexpr std::array<std::string_view, 9> plain{
        "AutoNumber", "AutoID", "AutoIncrement", "Auto_Increment", "Auto Increment",
        "Identity", "Serial", "SmallSerial", "BigSerial",
    };
    for (auto name : plain) {
        if (iequals(s, name)) return auto_id_spec{};
    }

    // Identity(seed) or Identity(seed,step), spaces allowed around the numbers.
    constexpr std::string_view kIdentity = "Identity(";
    if (s.size() <= kIdentity.size() || !iequals(s.substr(0, kIdentity.size()), kIdentity) ||
        s.back() != ')') {
        return std::nullopt;
    }
    std::string_view args = s.substr(kIdentity.size(), s.size() - kIdentity.size() - 1);
    auto parse_int = [](std::string_view a) -> std::optional<std::int64_t> {
        while (!a.empty() && a.front() == ' ') a.remove_prefix(1);
        while (!a.empty() && a.back() == ' ')  a.remove_suffix(1);
        if (!a.empty() && a.front() == '+') a.remove_prefix(1);
        std::int64_t v = 0;
        auto [p, ec] = std::from_chars(a.data(), a.data() + a.size(), v);
        if (a.empty() || ec != std::errc{} || p != a.data() + a.size()) return std::nullopt;
        return v;
    };
    auto_id_spec spec;
    const auto comma = args.find(',');
    auto seed = parse_int(args.substr(0, comma));
    if (!seed) return std::nullopt;
    spec.seed = *seed;
    if (comma != std::string_view::npos) {
        auto step = parse_int(args.substr(comma + 1));
        if (!step || *step == 0) return std::nullopt;
        spec.step = *step;
    }
    return spec;
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

// Date: STRICT calendar date, exactly YYYY-MM-DD — nothing else.
//
// Date interpretation is notoriously locale/country-dependent (is 03-04-05 in
// March or April? which is the year?), so jText deliberately requires the one
// unambiguous ISO form and rejects everything else: no datetimes, no times, no
// alternate separators, no 2-digit years. The value is also checked as a real
// calendar date (month 1-12, day valid for the month, leap-year-aware), so
// Feb 30 / 2026-13-45 are rejected too.
auto looks_like_date(std::string_view s) -> bool
{
    if (s.size() != 10) return false;
    auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
    if (!is_digit(s[0]) || !is_digit(s[1]) || !is_digit(s[2]) || !is_digit(s[3])) return false;
    if (s[4] != '-') return false;
    if (!is_digit(s[5]) || !is_digit(s[6])) return false;
    if (s[7] != '-') return false;
    if (!is_digit(s[8]) || !is_digit(s[9])) return false;

    const int year  = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
    const int month = (s[5] - '0') * 10 + (s[6] - '0');
    const int day   = (s[8] - '0') * 10 + (s[9] - '0');

    if (month < 1 || month > 12) return false;

    static constexpr int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int max_day = days_in_month[month - 1];
    const bool leap = (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0);
    if (month == 2 && leap) max_day = 29;

    return day >= 1 && day <= max_day;
}

// DateTime: STRICT ISO 8601, in the same spirit as Date (one unambiguous form):
//   YYYY-MM-DD, then 'T' or one space, then HH:MM:SS,
//   then optional fractional seconds (.d to .ddddddddd),
//   then an optional zone: 'Z' or +HH:MM / -HH:MM.
// The date part must be a real calendar date; hours 00-23, minutes and
// seconds 00-59 (no leap second), zone hours 00-23 and minutes 00-59.
auto looks_like_datetime(std::string_view s) -> bool
{
    if (s.size() < 19 || !looks_like_date(s.substr(0, 10))) return false;
    if (s[10] != 'T' && s[10] != ' ') return false;
    auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
    auto two = [&](std::size_t at, int max) -> bool {
        if (!is_digit(s[at]) || !is_digit(s[at + 1])) return false;
        return (s[at] - '0') * 10 + (s[at + 1] - '0') <= max;
    };
    if (!two(11, 23) || s[13] != ':' || !two(14, 59) || s[16] != ':' || !two(17, 59)) return false;

    std::size_t i = 19;
    if (i < s.size() && s[i] == '.') {
        const std::size_t start = ++i;
        while (i < s.size() && is_digit(s[i])) ++i;
        const std::size_t digits = i - start;
        if (digits < 1 || digits > 9) return false;
    }
    if (i == s.size()) return true;                       // no zone
    if (s[i] == 'Z') return i + 1 == s.size();
    if ((s[i] == '+' || s[i] == '-') && i + 6 == s.size()) {
        return two(i + 1, 23) && s[i + 3] == ':' && two(i + 4, 59);
    }
    return false;
}

}  // anonymous namespace

// ──────────────────────────────────────────────────────────────
//  Field-declaration interpretation
//
//  Per SPEC Section 2.4: a field declaration line is one parsed_line
//  whose hierarchy is the name followed by 1 to 4 segments. The
//  segments may come in any order, each kind at most once:
//     type        (a String/Integer/Number/Date/Bool spelling)
//     auto        (AutoID, Identity(seed,step), ... -- implies Integer)
//     length      (a positive integer)
//     constraint  ("Not Null" or "Nullable")
//  A type or an auto designation is required.
//
//  Position comes from the line's number (1..N).
//  Comment is preserved verbatim from the line.
// ──────────────────────────────────────────────────────────────

namespace {

struct interpret_field_result {
    std::optional<field>  fld;     // nullopt if interpretation failed fatally
    bool                  reported_issue = false;
    bool                  declared_nullable = false;  // an explicit "Nullable" segment
};

auto interpret_one_field(
    const parsed_line&   line,
    std::string_view     section_name,
    validation_report&   report)
    -> interpret_field_result
{
    interpret_field_result out;
    const auto where = std::format("section '{}', field {}", section_name, line.number);

    if (line.hierarchy.size() < 2) {
        report_issue(report, issue_severity::error,
                     issue_kind::field_invalid_declaration,
                     0,  // source line not tracked at this layer yet
                     where,
                     std::format("expected at least name/type, got {} segment(s)",
                                 line.hierarchy.size()));
        out.reported_issue = true;
        return out;
    }
    if (line.hierarchy.size() > 5) {
        report_issue(report, issue_severity::error,
                     issue_kind::field_invalid_declaration,
                     0,
                     where,
                     std::format("too many segments: {} (max 5: name plus a type, "
                                 "an auto designation, a length and a constraint)",
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
                     where,
                     "field name is empty");
        out.reported_issue = true;
        // Continue interpreting the rest so we report more issues.
    }

    std::optional<field_type> declared_type;
    bool have_length = false;
    bool have_constraint = false;

    for (std::size_t i = 1; i < line.hierarchy.size(); ++i) {
        const std::string_view seg = line.hierarchy[i];

        if (auto t = parse_field_type(seg); t.has_value() && !declared_type) {
            declared_type = *t;
            continue;
        }
        if (auto a = parse_auto_id_designation(seg); a.has_value() && !f.auto_id) {
            f.auto_id = *a;
            continue;
        }
        if (!have_length && looks_like_integer(seg)) {
            std::size_t n = 0;
            auto [p, ec] = std::from_chars(seg.data(), seg.data() + seg.size(), n);
            if (ec != std::errc{} || p != seg.data() + seg.size() || n == 0) {
                report_issue(report, issue_severity::error,
                             issue_kind::field_invalid_length, 0, where,
                             std::format("length '{}' is not a positive integer", seg));
                out.reported_issue = true;
            } else {
                f.max_length = n;
            }
            have_length = true;
            continue;
        }
        if (!have_constraint && iequals(seg, "Not Null")) {
            f.required = true;
            have_constraint = true;
            continue;
        }
        if (!have_constraint && iequals(seg, "Nullable")) {
            f.required = false;
            out.declared_nullable = true;
            have_constraint = true;
            continue;
        }

        // Unrecognized, or a second segment of a kind already seen.
        const bool type_slot = !declared_type && !f.auto_id;
        report_issue(report, issue_severity::error,
                     type_slot ? issue_kind::field_invalid_type
                               : issue_kind::field_invalid_constraint,
                     0, where,
                     std::format("segment '{}' is not recognized here (expected a type "
                                 "such as String/Text/Integer/Number/Date/DateTime/GST/Bool, an auto "
                                 "designation such as AutoID or Identity(1,1), a length, "
                                 "or 'Not Null' / 'Nullable', each at most once)", seg));
        out.reported_issue = true;
    }

    if (declared_type) {
        f.type = *declared_type;
        if (f.auto_id && f.type != field_type::integer_type &&
            f.type != field_type::number_type) {
            report_issue(report, issue_severity::error,
                         issue_kind::field_invalid_type, 0, where,
                         std::format("an auto designation needs an integer type, not {}",
                                     to_string(f.type)));
            out.reported_issue = true;
        }
    } else if (f.auto_id) {
        f.type = field_type::integer_type;   // the auto designation implies it
    } else if (!out.reported_issue) {
        report_issue(report, issue_severity::error,
                     issue_kind::field_invalid_type, 0, where,
                     "no type given (expected a type such as String/Text/Integer/"
                     "Number/Date/Bool, or an auto designation)");
        out.reported_issue = true;
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
    bool field1_declared_nullable = false;

    for (const auto& fl : sec.field_lines) {
        auto r = interpret_one_field(fl, sec.name, report);
        if (!r.fld) continue;  // already reported, skip
        if (fl.number == 1 && r.declared_nullable) field1_declared_nullable = true;

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

    // Field 1 is the record id (SPEC Section 2.6.4): any name, but always an
    // integer and never null. It is required whether or not it says Not Null.
    if (!fields.empty() && fields[0].position == 1) {
        auto& id = fields[0];
        if (id.type != field_type::integer_type && id.type != field_type::number_type) {
            report_issue(report, issue_severity::error,
                         issue_kind::field_record_id_invalid,
                         0,
                         std::format("section '{}', field 1 ({})", sec.name, id.name),
                         std::format("field 1 is the record id and must be an integer "
                                     "type (Integer, Number with whole values, or an auto "
                                     "designation such as AutoID or Identity(1,1)), not {}",
                                     to_string(id.type)));
        }
        if (field1_declared_nullable) {
            report_issue(report, issue_severity::error,
                         issue_kind::field_record_id_invalid,
                         0,
                         std::format("section '{}', field 1 ({})", sec.name, id.name),
                         "field 1 is the record id and cannot be Nullable");
        }
        id.required = true;
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

    // Field 1 is the record id (SPEC Section 2.6.4); capture it as an integer
    // when it is one. Missing or non-integer ids are reported by
    // validate_record_values.
    auto flush_current = [&]() {
        if (record_open) {
            if (!current.values.empty() && current.values[0] &&
                looks_like_integer(*current.values[0])) {
                std::string_view v = *current.values[0];
                if (!v.empty() && v.front() == '+') v.remove_prefix(1);
                std::int64_t id = 0;
                auto [p, ec] = std::from_chars(v.data(), v.data() + v.size(), id);
                if (ec == std::errc{} && p == v.data() + v.size()) current.record_id = id;
            }
            records.push_back(std::move(current));
            current = record{};
            record_open = false;
            last_pos = 0;
            first_field_in_record = true;
        }
    };

    for (const auto& pl : sec.data_lines) {
        if (pl.number == 0) {
            // Blank-line sentinel: end of current record.
            flush_current();
            continue;
        }

        // Compact row: one data line carries consecutive fields, using a
        // formatter such as |# so the line parser already split the tail into
        // pl.hierarchy (one entry per value). Like every data line, the number
        // before '.' is a FIELD POSITION, the first one on the line (SPEC
        // Section 2.6.3); a whole record is "1. |# <id>|<f2>|...|<fN>".
        // (Per-field data lines have hierarchy.size() == 1.)
        if (pl.hierarchy.size() > 1) {
            // full record in one line
            flush_current();
            current.values.assign(field_count, std::nullopt);
            current.source_line_no = 0;
            record_open = true;
            const std::size_t first = pl.number - 1;   // 0-based position of the first value
            const std::size_t last  = first + pl.hierarchy.size();
            if (field_count > 0 && last > field_count) {
                report_issue(report, issue_severity::error,
                             issue_kind::record_field_out_of_range,
                             0,
                             std::format("section '{}'", sec.name),
                             std::format("compact row starting at field {} carries {} values "
                                         "(through field {}) but the section declares only "
                                         "{} field(s)", pl.number, pl.hierarchy.size(), last,
                                         field_count));
            }
            for (std::size_t fi = 0; fi < pl.hierarchy.size() && first + fi < field_count; ++fi) {
                const auto& hv = pl.hierarchy[fi];
                // We picked '|' as the field separator character for compact
                // rows ("1. |# f1|f2|...").
                // Null is represented by the embedded ASCII Unit Separator (0x1F)
                // so that a null field appears as |\x1F| while empty string is || .
                // This allows the surface to look similar (|| vs || with invisible)
                // but distinguishes null from empty.
                // We also support \N and NULL for compatibility.
                if (hv == "\x1f" || hv == "\\N" || hv == "NULL") {
                    current.values[first + fi] = std::nullopt;
                } else {
                    current.values[first + fi] = hv;  // empty string stays as ""
                }
            }
            flush_current();  // complete row
            continue;
        }

        // Regular data line. Lazy-init the current record's values
        // vector to size field_count (only the first time we see a
        // field for this record).
        if (!record_open) {
            current.values.assign(field_count, std::nullopt);
            current.source_line_no = 0;  // we don't have source line; future work
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

// True when `level` is at least as thorough as `floor` (levels are ordered
// minimal < standard < strict).
constexpr bool level_at_least(validation_level level, validation_level floor) {
    return static_cast<std::uint8_t>(level) >= static_cast<std::uint8_t>(floor);
}

// Validate that every {N} placeholder in a section's template bodies refers to
// a declared field position (1..field_count). {0} and {N>field_count} are
// errors. Non-numeric braces (e.g. {foo}) are literal text, not placeholders,
// and are ignored. Callers skip this for sections whose fields come from an
// include (the resolved field count is unknown there).
auto validate_template_placeholders(
    const std::vector<parsed_template>& templates,
    std::size_t                         field_count,
    std::string_view                    section_name,
    validation_report&                  report) -> void
{
    for (const auto& tpl : templates) {
        const std::string& body = tpl.body;
        for (std::size_t i = 0; i + 1 < body.size(); ++i) {
            if (body[i] != '{') continue;
            std::size_t j = i + 1;
            unsigned long long n = 0;
            while (j < body.size() && body[j] >= '0' && body[j] <= '9') {
                if (n <= field_count + 1) {            // enough to decide range
                    n = n * 10 + static_cast<unsigned>(body[j] - '0');
                }
                ++j;
            }
            // A placeholder is exactly {<digits>}; anything else is literal text.
            if (j == i + 1 || j >= body.size() || body[j] != '}') continue;
            if (n == 0 || n > field_count) {
                report_issue(report, issue_severity::error,
                             issue_kind::template_placeholder_out_of_range,
                             0,
                             std::format("section '{}', template '{}'",
                                         section_name, tpl.name),
                             std::format("placeholder {{{}}} is out of range "
                                         "(section declares {} field(s))",
                                         n, field_count));
            }
            i = j;  // skip past this placeholder
        }
    }
}

auto validate_record_values(
    const std::vector<field>& fields,
    const std::vector<record>& records,
    std::string_view           section_name,
    validation_report&         report,
    validation_level           level) -> void
{
    for (std::size_t ri = 0; ri < records.size(); ++ri) {
        const auto& rec = records[ri];
        for (std::size_t fi = 0; fi < fields.size() && fi < rec.values.size(); ++fi) {
            const auto& fld = fields[fi];
            const auto& v   = rec.values[fi];

            const bool present = v.has_value();
            const bool empty   = present && v->empty();

            // Field 1 is the record id (SPEC Section 2.6.4). Presence is checked at
            // every level; integer-ness (and 64-bit range) at Standard+.
            if (fi == 0 && fld.position == 1) {
                const auto where = std::format("section '{}', record {}, field 1 ({})",
                                               section_name, ri + 1, fld.name);
                if (!present || empty) {
                    report_issue(report, issue_severity::error,
                                 issue_kind::record_missing_record_id, 0, where,
                                 "record id (field 1) is missing or empty; a producer "
                                 "must generate one when the source has none");
                } else if (level_at_least(level, validation_level::standard) &&
                           !rec.record_id.has_value()) {
                    report_issue(report, issue_severity::error,
                                 issue_kind::record_id_not_integer, 0, where,
                                 std::format("record id '{}' is not an integer "
                                             "(or is outside the 64-bit range)", *v));
                }
                continue;
            }

            // Required (Not Null) presence is checked at every level (incl. minimal).
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

            // Per-value type/format/length checks are Standard+ (skipped at minimal).
            if (!level_at_least(level, validation_level::standard)) continue;

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
            case field_type::integer_type: {
                std::string_view iv = *v;
                if (!iv.empty() && iv.front() == '+') iv.remove_prefix(1);
                std::int64_t n = 0;
                auto [p, ec] = std::from_chars(iv.data(), iv.data() + iv.size(), n);
                if (!looks_like_integer(*v) || ec != std::errc{} || p != iv.data() + iv.size()) {
                    report_issue(report, issue_severity::error,
                                 issue_kind::record_value_type_mismatch,
                                 0,
                                 std::format("section '{}', record {}, field {} ({})",
                                             section_name, ri + 1, fld.position, fld.name),
                                 std::format("value '{}' is not a valid Integer "
                                             "(whole number within 64 bits)", *v));
                }
                break;
            }
            case field_type::bool_type:
                if (!parse_bool_value(*v)) {
                    report_issue(report, issue_severity::error,
                                 issue_kind::record_value_type_mismatch,
                                 0,
                                 std::format("section '{}', record {}, field {} ({})",
                                             section_name, ri + 1, fld.position, fld.name),
                                 std::format("value '{}' is not a valid Bool (expected "
                                             "Yes/No, True/False, Y/N, T/F or 1/0)", *v));
                }
                break;
            case field_type::gst_type:
                if (auto g = parse_gst_value(*v); g && g->ticks >= kGstTicksPerEarthYear) {
                    // Valid GST, but past one year: warn at two levels, never block.
                    const bool beyond_room = g->ticks >= kGstTickLimit;
                    report_issue(report, issue_severity::warning,
                                 beyond_room ? issue_kind::record_gst_beyond_leap_room
                                             : issue_kind::record_gst_leap_seconds,
                                 0,
                                 std::format("section '{}', record {}, field {} ({})",
                                             section_name, ri + 1, fld.position, fld.name),
                                 beyond_room
                                     ? std::format("GST '{}' ticks go beyond 1 year and {} leap "
                                                   "seconds (last expected tick {})", *v,
                                                   kGstLeapSecondsRoom, kGstTickLimit - 1)
                                     : std::format("GST '{}' ticks go beyond 1 year: leap seconds "
                                                   "(a year is {} ticks)", *v,
                                                   kGstTicksPerEarthYear));
                } else if (!g) {
                    report_issue(report, issue_severity::error,
                                 issue_kind::record_value_type_mismatch,
                                 0,
                                 std::format("section '{}', record {}, field {} ({})",
                                             section_name, ri + 1, fld.position, fld.name),
                                 std::format("value '{}' is not a valid GST (required format "
                                             "is <year>:<ticks>, year a signed 64-bit integer, "
                                             "ticks exactly {} digits, zero-padded)",
                                             *v, kGstTickDigits));
                }
                break;
            case field_type::datetime_type:
                if (!looks_like_datetime(*v)) {
                    report_issue(report, issue_severity::error,
                                 issue_kind::record_value_type_mismatch,
                                 0,
                                 std::format("section '{}', record {}, field {} ({})",
                                             section_name, ri + 1, fld.position, fld.name),
                                 std::format("value '{}' is not a valid DateTime (required "
                                             "format is YYYY-MM-DDTHH:MM:SS, optional "
                                             ".fraction and Z or +HH:MM zone; a space "
                                             "may replace the T)", *v));
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
                                             "(required format is exactly "
                                             "YYYY-MM-DD)", *v));
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

    return out;
}

// The version the file declares (header line 4, or the light "# jText Version:"),
// checked against the one this library reads.
void check_jtext_version(const parsed_file& pf, validated_file& file, validation_report& report)
{
    const auto declared = declared_version(pf);
    if (!declared.empty()) file.header["jtext_version"] = declared;

    issue_kind kind{};
    switch (check_version(declared)) {
    case version_status::current: return;
    case version_status::missing: kind = issue_kind::header_missing_jtext_version; break;
    case version_status::older:   kind = issue_kind::header_older_jtext_version;   break;
    case version_status::newer:   kind = issue_kind::header_newer_jtext_version;   break;
    case version_status::invalid: kind = issue_kind::header_invalid_jtext_version; break;
    }
    report_issue(report, issue_severity::warning, kind, 0, "header", version_warning(declared));
}

}  // anonymous namespace

// ──────────────────────────────────────────────────────────────
//  validate entry point
// ──────────────────────────────────────────────────────────────

auto validate(const parsed_file& pf, validation_level level) -> validate_result
{
    validate_result vr;

    vr.file.header = interpret_header(pf.header_lines, vr.report);
    check_jtext_version(pf, vr.file, vr.report);

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

        validate_record_values(vs.fields, vs.records, vs.name, vr.report, level);

        // Template {N} placeholder range is a strict-level check. Skip when the
        // section's fields come from an include — the resolved count is unknown.
        if (level_at_least(level, validation_level::strict) && sec.includes.empty()) {
            validate_template_placeholders(vs.templates, fc, vs.name, vr.report);
        }

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

