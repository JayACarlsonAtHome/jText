// File: src/parser/line.cpp
//
// Implementation of the line-grammar parser per SPEC Section 3.
//
// Parsing strategy is hand-written, left-to-right:
//
//   1. Skip leading whitespace
//   2. Read digits → line number (1..99)
//   3. Require period
//   4. Skip whitespace
//   5. Detect multiline opener "<<<" — short-circuit if seen
//   6. Otherwise: read 3 chars → formatter, validate
//   7. Skip whitespace
//   8. Scan rest of line for the comment marker (bookend + whitespace
//      on both sides). Everything before = data, after = comment.
//   9. Split data on the separator if present → hierarchy

#include "line.hpp"

#include <cctype>
#include <charconv>
#include <format>
#include <string_view>

namespace jtext {

namespace {

// ──────────────────────────────────────────────────────────────
//  Small helpers
// ──────────────────────────────────────────────────────────────

constexpr auto is_ws(char c) -> bool
{
    return c == ' ' || c == '\t';
}

constexpr auto is_digit(char c) -> bool
{
    return c >= '0' && c <= '9';
}

// Trim trailing newline (or CR+LF) from a view.
constexpr auto strip_eol(std::string_view sv) -> std::string_view
{
    while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r')) {
        sv.remove_suffix(1);
    }
    return sv;
}

// Trim leading whitespace from a view; return the count consumed.
auto skip_ws(std::string_view& sv) -> std::size_t
{
    std::size_t n = 0;
    while (!sv.empty() && is_ws(sv.front())) {
        sv.remove_prefix(1);
        ++n;
    }
    return n;
}

// Trim trailing whitespace from a view.
auto rtrim(std::string_view sv) -> std::string_view
{
    while (!sv.empty() && is_ws(sv.back())) {
        sv.remove_suffix(1);
    }
    return sv;
}

// Trim both sides.
auto trim(std::string_view sv) -> std::string_view
{
    while (!sv.empty() && is_ws(sv.front())) sv.remove_prefix(1);
    while (!sv.empty() && is_ws(sv.back()))  sv.remove_suffix(1);
    return sv;
}

// Build a parse_error.
auto err(parse_error_kind k, std::size_t col, std::string msg)
    -> std::unexpected<parse_error>
{
    return std::unexpected{parse_error{k, col, std::move(msg)}};
}

// ──────────────────────────────────────────────────────────────
//  Find the comment marker in `data_region`.
//
//  The marker is the bookend character with whitespace immediately
//  on both sides (or end-of-line on the right). Scan left-to-right
//  and return the position of the marker character, or npos if not
//  found.
//
//  `data_region` is the portion of the line AFTER the formatter
//  and its trailing whitespace. The bookend is `b`.
// ──────────────────────────────────────────────────────────────

auto find_comment_marker(std::string_view data_region, char b) -> std::size_t
{
    for (std::size_t i = 0; i < data_region.size(); ++i) {
        if (data_region[i] != b) continue;

        // Need whitespace (or start-of-region) on the left.
        const bool left_ok = (i == 0) || is_ws(data_region[i - 1]);
        if (!left_ok) continue;

        // Need whitespace or end-of-region on the right.
        const bool right_ok =
            (i + 1 == data_region.size()) || is_ws(data_region[i + 1]);
        if (!right_ok) continue;

        return i;
    }
    return std::string_view::npos;
}

// ──────────────────────────────────────────────────────────────
//  Split `data` on `sep` per SPEC Section 3.3:
//
//    - leading separator → cosmetic, ignored
//    - trailing separator → cosmetic, ignored
//    - any empty segment anywhere → ERROR (hierarchy_empty_segment)
//      including the "all separators" case like "///"
//
//  If `data` contains no `sep`, returns a single-element vector
//  with the value, which may be empty (legitimate "empty data" case
//  per SPEC Section 2.6.1).
//
//  `col_base` is the column in the original line where `data`
//  starts, used for error reporting.
// ──────────────────────────────────────────────────────────────

auto split_hierarchy(std::string_view data, char sep, std::size_t col_base)
    -> std::expected<std::vector<std::string>, parse_error>
{
    // No separator in data → flat single value.
    if (data.find(sep) == std::string_view::npos) {
        return std::vector<std::string>{std::string{data}};
    }

    // Has separator → parse as hierarchy. Strip ONE leading and ONE
    // trailing separator (cosmetic), then split the remainder.
    auto core = data;
    if (!core.empty() && core.front() == sep) core.remove_prefix(1);
    if (!core.empty() && core.back()  == sep) core.remove_suffix(1);

    std::vector<std::string> segments;
    std::size_t start = 0;
    for (std::size_t cursor = 0; cursor < core.size(); ++cursor) {
        if (core[cursor] == sep) {
            const auto piece = trim(core.substr(start, cursor - start));
            if (piece.empty()) {
                return err(parse_error_kind::hierarchy_empty_segment,
                           col_base + start,
                           "hierarchy contains an empty segment");
            }
            segments.emplace_back(piece);
            start = cursor + 1;
        }
    }
    // Final segment (the part after the last separator, or the whole
    // core if no separators remain after stripping leading/trailing).
    const auto piece = trim(core.substr(start));
    if (piece.empty()) {
        return err(parse_error_kind::hierarchy_empty_segment,
                   col_base + start,
                   "hierarchy contains an empty segment");
    }
    segments.emplace_back(piece);
    return segments;
}

}  // anonymous namespace

// ──────────────────────────────────────────────────────────────
//  formatter::to_string
// ──────────────────────────────────────────────────────────────

auto formatter::to_string() const -> std::string
{
    return std::string{bookend, separator, bookend};
}

// ──────────────────────────────────────────────────────────────
//  parse_error_kind to_string
//
//  Stable, human-readable names. Used by report output and tests
//  so failures show meaningful identifiers instead of raw addresses.
// ──────────────────────────────────────────────────────────────

auto to_string(parse_error_kind k) -> std::string_view
{
    switch (k) {
        case parse_error_kind::empty_line:                         return "empty_line";
        case parse_error_kind::missing_period:                     return "missing_period";
        case parse_error_kind::invalid_line_number:                return "invalid_line_number";
        case parse_error_kind::missing_formatter:                  return "missing_formatter";
        case parse_error_kind::formatter_too_short:                return "formatter_too_short";
        case parse_error_kind::formatter_bookends_mismatch:        return "formatter_bookends_mismatch";
        case parse_error_kind::formatter_separator_equals_bookend: return "formatter_separator_equals_bookend";
        case parse_error_kind::formatter_invalid_char:             return "formatter_invalid_char";
        case parse_error_kind::hierarchy_empty_segment:            return "hierarchy_empty_segment";
        case parse_error_kind::multiline_missing_sentinel:         return "multiline_missing_sentinel";
        case parse_error_kind::multiline_sentinel_invalid:         return "multiline_sentinel_invalid";
    }
    return "unknown";
}

// ──────────────────────────────────────────────────────────────
//  parse_error::to_string
// ──────────────────────────────────────────────────────────────

auto parse_error::to_string() const -> std::string
{
    return std::format("[col {}] {}: {}", column, jtext::to_string(kind), message);
}

// ──────────────────────────────────────────────────────────────
//  validate_formatter
// ──────────────────────────────────────────────────────────────

auto validate_formatter(std::string_view three_chars)
    -> std::expected<formatter, parse_error>
{
    if (three_chars.size() < 3) {
        return err(parse_error_kind::formatter_too_short, 0,
                   std::format("formatter must be exactly 3 characters, got {}",
                               three_chars.size()));
    }
    const char b1 = three_chars[0];
    const char s  = three_chars[1];
    const char b2 = three_chars[2];

    if (!is_valid_formatter_char(b1)) {
        return err(parse_error_kind::formatter_invalid_char, 0,
                   std::format("formatter bookend '{}' is not a valid character "
                               "(must be printable ASCII, non-alphanumeric, "
                               "non-whitespace)", b1));
    }
    if (!is_valid_formatter_char(s)) {
        return err(parse_error_kind::formatter_invalid_char, 1,
                   std::format("formatter separator '{}' is not a valid character "
                               "(must be printable ASCII, non-alphanumeric, "
                               "non-whitespace)", s));
    }
    if (!is_valid_formatter_char(b2)) {
        return err(parse_error_kind::formatter_invalid_char, 2,
                   std::format("formatter bookend '{}' is not a valid character",
                               b2));
    }
    if (b1 != b2) {
        return err(parse_error_kind::formatter_bookends_mismatch, 2,
                   std::format("formatter bookends differ: '{}' and '{}'", b1, b2));
    }
    if (b1 == s) {
        return err(parse_error_kind::formatter_separator_equals_bookend, 1,
                   std::format("formatter bookend and separator are both '{}'; "
                               "they must differ", b1));
    }
    return formatter{b1, s};
}

// ──────────────────────────────────────────────────────────────
//  parse_line
// ──────────────────────────────────────────────────────────────

auto parse_line(std::string_view raw)
    -> std::expected<parsed_line, parse_error>
{
    const auto line = strip_eol(raw);

    // The "column" we report in errors is into the original line as
    // received. We track consumed_chars as a running offset.
    std::size_t col = 0;
    std::string_view sv = line;

    // 1. Skip leading whitespace (visual padding for line numbers).
    col += skip_ws(sv);

    if (sv.empty()) {
        return err(parse_error_kind::empty_line, col,
                   "line is empty or whitespace-only");
    }

    // 2. Read digits → line number.
    const auto digits_start = sv.data();
    while (!sv.empty() && is_digit(sv.front())) {
        sv.remove_prefix(1);
    }
    const auto digits_len = static_cast<std::size_t>(sv.data() - digits_start);

    if (digits_len == 0) {
        return err(parse_error_kind::invalid_line_number, col,
                   "expected a line number at start of line");
    }

    unsigned number = 0;
    auto [ptr, ec] = std::from_chars(digits_start, digits_start + digits_len,
                                     number);
    if (ec != std::errc{}) {
        return err(parse_error_kind::invalid_line_number, col,
                   std::format("invalid line number '{}'",
                               std::string_view{digits_start, digits_len}));
    }
    col += digits_len;

    // 3. Require period.
    if (sv.empty() || sv.front() != '.') {
        return err(parse_error_kind::missing_period, col,
                   "expected '.' after line number");
    }
    sv.remove_prefix(1);
    col += 1;

    // 4. Whitespace between period and formatter.
    col += skip_ws(sv);

    if (sv.empty()) {
        return err(parse_error_kind::missing_formatter, col,
                   "line ends before formatter");
    }

    // 5. Detect multiline opener.
    if (sv.starts_with("<<<")) {
        sv.remove_prefix(3);
        col += 3;
        col += skip_ws(sv);

        if (sv.empty()) {
            return err(parse_error_kind::multiline_missing_sentinel, col,
                       "expected sentinel after '<<<'");
        }

        // Sentinel is everything up to the next whitespace.
        // Anything after the sentinel + whitespace is treated as
        // an optional inline comment for the opener line.
        const auto sentinel_start = sv.data();
        while (!sv.empty() && !is_ws(sv.front())) {
            sv.remove_prefix(1);
        }
        const auto sentinel_len =
            static_cast<std::size_t>(sv.data() - sentinel_start);

        const std::string_view sentinel{sentinel_start, sentinel_len};
        if (sentinel.size() < 3) {
            return err(parse_error_kind::multiline_sentinel_invalid,
                       col,
                       std::format("multiline sentinel must be at least 3 "
                                   "characters, got '{}'", sentinel));
        }

        // Optional trailing text after whitespace = treated as a comment.
        col += sentinel_len;
        col += skip_ws(sv);
        const auto comment_text = trim(sv);

        parsed_line out;
        out.kind     = line_kind::multiline_opener;
        out.number   = number;
        out.sentinel = std::string{sentinel};
        out.comment  = std::string{comment_text};
        return out;
    }

    // 6. Standard data line: read 3 chars → formatter.
    if (sv.size() < 3) {
        return err(parse_error_kind::formatter_too_short, col,
                   "expected 3-character formatter after line number");
    }
    const std::string_view fmt_view = sv.substr(0, 3);
    auto fmt_or_err = validate_formatter(fmt_view);
    if (!fmt_or_err) {
        auto e = fmt_or_err.error();
        e.column += col;
        return std::unexpected{std::move(e)};
    }
    const formatter fmt = *fmt_or_err;
    sv.remove_prefix(3);
    col += 3;

    // 7. Whitespace between formatter and data. If the line ends here
    // (empty data with no comment), that's the "explicitly empty"
    // case from SPEC 2.6.1 — still valid.
    col += skip_ws(sv);

    // 8. Scan the rest for the comment marker.
    const auto data_region_col = col;
    const auto marker_pos = find_comment_marker(sv, fmt.bookend);

    std::string_view data_part;
    std::string_view comment_part;

    if (marker_pos == std::string_view::npos) {
        data_part = sv;
    } else {
        data_part    = sv.substr(0, marker_pos);
        comment_part = sv.substr(marker_pos + 1);
    }

    // Strip trailing whitespace from the data part (it ends at the
    // whitespace preceding the comment marker, or at end of line).
    data_part = rtrim(data_part);

    // 9. Split data on the separator (if present).
    auto hier_or_err = split_hierarchy(data_part, fmt.separator, data_region_col);
    if (!hier_or_err) {
        return std::unexpected{std::move(hier_or_err.error())};
    }

    parsed_line out;
    out.kind      = line_kind::data;
    out.number    = number;
    out.fmt       = fmt;
    out.hierarchy = std::move(*hier_or_err);
    out.comment   = std::string{trim(comment_part)};
    return out;
}

}  // namespace jtext
