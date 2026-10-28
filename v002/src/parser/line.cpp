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
//   5. (formerly "<<<"; a multiline value now opens with the formatter [*)
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

auto find_comment_marker(std::string_view data_region, std::string_view b) -> std::size_t
{
    // A comment is this character with a space on each side.
    const std::string needle = std::string{' '} + std::string{b} + ' ';
    const auto at = data_region.find(needle);
    return at == std::string_view::npos ? at : at + 1;
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

auto split_hierarchy(std::string_view data, std::string_view sep, std::size_t col_base)
    -> std::expected<std::vector<std::string>, parse_error>
{
    // No separator in data → flat single value.
    if (data.find(sep) == std::string_view::npos) {
        return std::vector<std::string>{std::string{data}};
    }

    // Has separator → parse as hierarchy. Strip ONE leading and ONE
    // trailing separator (cosmetic), then split the remainder.
    auto core = data;
    std::size_t offset = 0;
    if (core.starts_with(sep)) { core.remove_prefix(sep.size()); offset = sep.size(); }
    if (core.ends_with(sep))   core.remove_suffix(sep.size());

    std::vector<std::string> segments;
    std::size_t start = 0;
    for (;;) {
        const auto at = core.find(sep, start);
        const auto piece = trim(core.substr(start, at == std::string_view::npos ? std::string_view::npos : at - start));
        if (piece.empty()) {
            return err(parse_error_kind::hierarchy_empty_segment,
                       col_base + offset + start,
                       "hierarchy contains an empty segment");
        }
        segments.emplace_back(piece);
        if (at == std::string_view::npos) break;
        start = at + sep.size();
    }
    return segments;
}

// Split a compact row's values on `sep`. Every separator splits and an
// empty value is kept, so "a||b|" is ["a", "", "b", ""]: what the writer
// joined comes back as it was. Values are trimmed (SPEC 3.4).
auto split_fields(std::string_view data, std::string_view sep) -> std::vector<std::string>
{
    std::vector<std::string> values;
    std::size_t start = 0;
    for (;;) {
        const auto at = data.find(sep, start);
        values.emplace_back(trim(data.substr(start, at == std::string_view::npos ? std::string_view::npos : at - start)));
        if (at == std::string_view::npos) break;
        start = at + sep.size();
    }
    return values;
}

// After "[*" and its whitespace: nothing, or a comment introduced by '#'.
auto opener_comment(std::string_view rest, std::size_t col)
    -> std::expected<std::string, parse_error>
{
    if (rest.empty()) return std::string{};
    if (rest.front() != '#') {
        return err(parse_error_kind::multiline_opener_text, col,
                   "after '[*' only a comment, introduced by '#', may follow");
    }
    return std::string{trim(rest.substr(1))};
}

}  // anonymous namespace

// ──────────────────────────────────────────────────────────────
//  Multiline values
// ──────────────────────────────────────────────────────────────

auto parse_value_line(std::string_view raw) -> std::expected<value_line, parse_error>
{
    auto sv = strip_eol(raw);
    const auto col = skip_ws(sv);
    value_line out;
    if (sv.starts_with("**")) {
        const auto rest = sv.substr(2);
        if (!rest.empty() && rest.front() != ' ') {
            return err(parse_error_kind::value_line_invalid, col + 2,
                       "'**' must be followed by a space before the value's text");
        }
        out.kind = value_line_kind::content;
        if (!rest.empty()) out.text = std::string{rest.substr(1)};
        return out;
    }
    if (sv.starts_with("*]")) {
        if (!trim(sv.substr(2)).empty()) {
            return err(parse_error_kind::value_line_invalid, col + 2,
                       "nothing may follow '*]': it closes the value on a line of its own");
        }
        out.kind = value_line_kind::closer;
        return out;
    }
    return out;
}

auto parse_bare_value_opener(std::string_view raw) -> std::expected<std::string, parse_error>
{
    auto sv = strip_eol(raw);
    auto col = skip_ws(sv);
    if (!sv.starts_with("[*")) {
        return err(parse_error_kind::missing_formatter, col, "expected '[*'");
    }
    sv.remove_prefix(2);
    col += 2;
    if (!sv.empty() && !is_ws(sv.front())) {
        return err(parse_error_kind::multiline_opener_text, col,
                   "after '[*' only a comment, introduced by '#', may follow");
    }
    col += skip_ws(sv);
    return opener_comment(sv, col);
}

// ──────────────────────────────────────────────────────────────
//  formatter::to_string
// ──────────────────────────────────────────────────────────────

auto formatter::to_string() const -> std::string
{
    return encode_utf8(separator) + encode_utf8(bookend);
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
        case parse_error_kind::formatter_separator_equals_bookend: return "formatter_separator_equals_bookend";
        case parse_error_kind::formatter_invalid_char:             return "formatter_invalid_char";
        case parse_error_kind::hierarchy_empty_segment:            return "hierarchy_empty_segment";
        case parse_error_kind::invalid_utf8:                       return "invalid_utf8";
        case parse_error_kind::formatter_reserved:                 return "formatter_reserved";
        case parse_error_kind::multiline_opener_text:              return "multiline_opener_text";
        case parse_error_kind::value_line_invalid:                 return "value_line_invalid";
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

auto validate_formatter(std::string_view marker)
    -> std::expected<formatter, parse_error>
{
    // The marker is counted in code points, not bytes (SPEC 3.1).
    char32_t prev = 0, last = 0;
    std::size_t count = 0, prev_at = 0, last_at = 0;
    for (std::size_t i = 0; i < marker.size();) {
        const auto d = decode_utf8(marker.substr(i));
        if (!d) {
            return err(parse_error_kind::invalid_utf8, i, "formatter is not valid UTF-8");
        }
        if (!is_valid_formatter_char(d->cp)) {
            return err(parse_error_kind::formatter_invalid_char, i,
                       std::format("formatter character '{}' (U+{:04X}) is not valid "
                                   "(must be Unicode punctuation or a symbol)",
                                   marker.substr(i, d->len), static_cast<std::uint32_t>(d->cp)));
        }
        prev = last; prev_at = last_at;
        last = d->cp; last_at = i;
        ++count;
        i += d->len;
    }
    if (count < 2) {
        return err(parse_error_kind::formatter_too_short, 0,
                   std::format("formatter must be at least 2 characters, got {}", count));
    }
    // Keep the right two characters. A longer opener drops what is in front.
    if (prev == U'[' && last != U']' && last != U'*') {
        return err(parse_error_kind::formatter_reserved, prev_at,
                   "'[' as the splicing character is reserved: only [] (raw data) "
                   "and [* (a multiline value)");
    }
    if (prev == last) {
        return err(parse_error_kind::formatter_separator_equals_bookend, prev_at,
                   std::format("formatter separator '{}' matches the comment "
                               "character; they must differ", encode_utf8(prev)));
    }
    formatter fmt;
    fmt.stand_in  = last;
    fmt.separator = prev;
    fmt.bookend   = last;
    return fmt;
}

// ──────────────────────────────────────────────────────────────
//  parse_line
// ──────────────────────────────────────────────────────────────

auto parse_line(std::string_view raw, split_rule rule)
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
    if (number == 0 || number > 99) {
        return err(parse_error_kind::invalid_line_number, col,
                   std::format("line number must be 1..99, got {}", number));
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

    // 6. Marker run. Keep the right two characters: splice, then comment.
    std::size_t marker_len = 0;
    std::size_t marker_chars = 0;
    while (marker_len < sv.size()) {
        const auto d = decode_utf8(sv.substr(marker_len));
        if (!d) {
            return err(parse_error_kind::invalid_utf8, col + marker_len,
                       "the formatter is not valid UTF-8");
        }
        if (!is_valid_formatter_char(d->cp)) break;
        marker_len += d->len;
        ++marker_chars;
    }
    if (marker_chars < 2) {
        return err(parse_error_kind::formatter_too_short, col,
                   "expected a marker of at least 2 characters after the line number");
    }
    const std::string_view fmt_view = sv.substr(0, marker_len);
    auto fmt_or_err = validate_formatter(fmt_view);
    if (!fmt_or_err) {
        auto e = fmt_or_err.error();
        e.column += col;
        return std::unexpected{std::move(e)};
    }
    const formatter fmt = *fmt_or_err;
    sv.remove_prefix(marker_len);
    col += marker_len;

    // 7. Whitespace between formatter and data. If the line ends here
    // (empty data with no comment), that's the "explicitly empty"
    // case from SPEC 2.6.1 — still valid.
    col += skip_ws(sv);

    // [* opens a multiline value: an optional comment, and its lines follow.
    if (fmt.multiline()) {
        auto comment = opener_comment(sv, col);
        if (!comment) return std::unexpected{std::move(comment.error())};
        parsed_line out;
        out.kind    = line_kind::multiline_opener;
        out.number  = number;
        out.fmt     = fmt;
        out.comment = std::move(*comment);
        return out;
    }

    // [] means the rest of the line is one value. No splice, no comment.
    if (fmt.raw_data()) {
        parsed_line out;
        out.kind      = line_kind::data;
        out.number    = number;
        out.fmt       = fmt;
        out.hierarchy = {std::string{sv}};
        return out;
    }

    // 8. Scan the rest for the comment marker.
    const auto data_region_col = col;
    const std::string bookend_text = encode_utf8(fmt.bookend);
    const std::string separator_text = encode_utf8(fmt.separator);
    const auto marker_pos = find_comment_marker(sv, bookend_text);

    std::string_view data_part;
    std::string_view comment_part;

    if (marker_pos == std::string_view::npos) {
        data_part = sv;
    } else {
        data_part    = sv.substr(0, marker_pos);
        comment_part = sv.substr(marker_pos + bookend_text.size());
    }

    // Strip trailing whitespace from the data part (it ends at the
    // whitespace preceding the comment marker, or at end of line).
    data_part = rtrim(data_part);

    // 9. Split data on the separator (if present).
    parsed_line out;
    if (rule == split_rule::fields) {
        out.hierarchy = split_fields(data_part, separator_text);
    } else {
        auto hier_or_err = split_hierarchy(data_part, separator_text, data_region_col);
        if (!hier_or_err) {
            return std::unexpected{std::move(hier_or_err.error())};
        }
        out.hierarchy = std::move(*hier_or_err);
    }
    out.kind      = line_kind::data;
    out.number    = number;
    out.fmt       = fmt;
    out.comment   = std::string{trim(comment_part)};
    return out;
}

}  // namespace jtext
