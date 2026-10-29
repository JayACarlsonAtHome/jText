// File: src/parser/section.cpp
//
// Implementation of the file/section structure parser.
//
// Internally driven by a small state machine that walks through the
// input lines once, dispatching on:
//   - structural markers ("=== ... ===")
//   - blank lines
//   - regular numbered lines (forwarded to parse_line)
//   - multiline values ("[*" opener, "**" lines, "*]" closer)

#include "parser/section.hpp"
#include "parser/line.hpp"
#include "parser/utf8.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <istream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace jtext {

namespace {

// ──────────────────────────────────────────────────────────────
//  Small helpers
// ──────────────────────────────────────────────────────────────

constexpr auto is_ws(char c) -> bool { return c == ' ' || c == '\t'; }

auto trim(std::string_view sv) -> std::string_view
{
    while (!sv.empty() && is_ws(sv.front())) sv.remove_prefix(1);
    while (!sv.empty() && is_ws(sv.back()))  sv.remove_suffix(1);
    return sv;
}

auto strip_eol(std::string_view sv) -> std::string_view
{
    while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r')) {
        sv.remove_suffix(1);
    }
    return sv;
}


auto is_blank(std::string_view sv) -> bool
{
    bool result = false;
    for (char c : sv) {
        if (!is_ws(c) && c != '\n' && c != '\r') return result;
    }
    result = true;
    return result;
}


/*  --This is currently unused -- in ts_store, maybe used somewhere else?
auto is_standard_header_comment(std::string_view sv) -> bool
{
    auto s = trim(strip_eol(sv));
    return s.starts_with("//File:") ||
           s.starts_with("//File Name:") ||
           s.starts_with("//Date:") ||
           s.starts_with("//Purpose:") ||
           s.starts_with("//Purpose -") ||
           s.starts_with("//Related:") ||
           s.starts_with("//Related Database:") ||
           s.starts_with("//Related Table:") ||
           s.starts_with("//Origin Date:") ||
           s.starts_with("//Modified Date:") ||
           s.starts_with("// Related Database") ||
           s.starts_with("// Related Table");
}
*/

auto err(file_error_kind k, std::size_t line_no, std::string msg)
    -> std::unexpected<file_error>
{
    return std::unexpected{file_error{k, line_no, std::move(msg)}};
}

// ──────────────────────────────────────────────────────────────
//  Marker recognition
//
//  A structural marker is a line of the form:
//      [ws]===[ws]<interior>[ws]===[ws]
//  where <interior> is the meaningful content. Returns true and
//  fills interior_out (trimmed) if the shape matches.
//
//  Examples:
//    "=== jText File ==="        → interior = "jText File"
//    "=== Section: tools ==="    → interior = "Section: tools"
//    "  === End Data ===  "      → interior = "End Data"
// ──────────────────────────────────────────────────────────────

}  // anonymous namespace

auto is_structural_marker(std::string_view line, std::string& interior_out) -> bool
{
    auto sv = trim(strip_eol(line));
    constexpr std::string_view tag{"==="};
    if (!sv.starts_with(tag) || !sv.ends_with(tag)) return false;
    if (sv.size() < 2 * tag.size()) return false;
    sv.remove_prefix(tag.size());
    sv.remove_suffix(tag.size());
    interior_out = std::string{trim(sv)};
    return true;
}

namespace {

// ──────────────────────────────────────────────────────────────
//  Recognize specific markers by their interior text.
//  These return std::optional-like results: the section banner
//  and template banner carry a name; the rest are predicates.
// ──────────────────────────────────────────────────────────────

auto marker_is_jtext_file(std::string_view interior) -> bool
{
    return trim(interior) == "jText File";
}

auto marker_is_end_file(std::string_view interior) -> bool
{
    return trim(interior) == "End File";
}

auto marker_is_fields(std::string_view interior) -> bool
{
    return trim(interior) == "Fields";
}

auto marker_is_end_fields(std::string_view interior) -> bool
{
    return trim(interior) == "End Fields";
}

auto marker_is_data(std::string_view interior) -> bool
{
    return trim(interior) == "Data";
}

auto marker_is_end_data(std::string_view interior) -> bool
{
    return trim(interior) == "End Data";
}

auto marker_is_end_section(std::string_view interior) -> bool
{
    return trim(interior) == "End Section";
}

// "Section: <name>" → returns name (trimmed) if recognized, empty optional otherwise.
// We use a plain string return where empty means "not this marker."
// The caller distinguishes by combining with a predicate.
auto extract_section_name(std::string_view interior) -> std::string
{
    constexpr std::string_view prefix{"Section:"};
    auto t = trim(interior);
    if (!t.starts_with(prefix)) return {};
    auto rest = t;
    rest.remove_prefix(prefix.size());
    return std::string{trim(rest)};
}

auto extract_template_name(std::string_view interior) -> std::string
{
    constexpr std::string_view prefix{"Template:"};
    auto t = trim(interior);
    if (!t.starts_with(prefix)) return {};
    auto rest = t;
    rest.remove_prefix(prefix.size());
    return std::string{trim(rest)};
}

auto extract_include(std::string_view interior) -> std::pair<std::string, std::string>
{
    auto t = trim(interior);
    constexpr std::string_view inc_prefix{"<#include#>"};
    if (!t.starts_with(inc_prefix)) return {};
    auto rest = trim(t.substr(inc_prefix.size()));
    size_t colon = rest.find(':');
    if (colon == std::string_view::npos) return {};
    std::string type = std::string{trim(rest.substr(0, colon))};
    std::string path = std::string{trim(rest.substr(colon + 1))};
    return {type, path};
}

}  // anonymous namespace

// ──────────────────────────────────────────────────────────────
//  to_string for file_error_kind
// ──────────────────────────────────────────────────────────────

auto to_string(file_error_kind k) -> std::string_view
{
    switch (k) {
        case file_error_kind::missing_magic_line:           return "missing_magic_line";
        case file_error_kind::missing_fields_block:         return "missing_fields_block";
        case file_error_kind::unexpected_marker:            return "unexpected_marker";
        case file_error_kind::template_missing_body:        return "template_missing_body";
        case file_error_kind::line_parse_failed:            return "line_parse_failed";
        case file_error_kind::duplicate_data_block:         return "duplicate_data_block";
        case file_error_kind::io_error:                     return "io_error";
        case file_error_kind::missing_closer:                return "missing_closer";
        case file_error_kind::section_not_found:             return "section_not_found";
        case file_error_kind::section_ambiguous:             return "section_ambiguous";
    }
    return "unknown";
}

auto file_error::to_string() const -> std::string
{
    return std::format("[line {}] {}: {}", line_no, jtext::to_string(kind), message);
}

// ──────────────────────────────────────────────────────────────
//  Core parser implementation
//
//  Driven by indexed access to a vector of lines so the multiline
//  block consumer can look ahead without re-architecting the loop.
// ──────────────────────────────────────────────────────────────

namespace {

// Read a multiline value's lines from `start` (just past its "[*" opener) through
// its "*]" closer. Returns the value, its lines joined with '\n', and the index of the
// line after the closer. A line that is not "**" or "*]" means the value was never
// closed: a missing closer, as at the end of the file.
auto consume_value(const std::vector<std::string>& lines, std::size_t start)
    -> std::expected<std::pair<std::string, std::size_t>, file_error>
{
    std::string value;
    bool first = true;
    for (std::size_t i = start; i < lines.size(); ++i) {
        auto vl = parse_value_line(lines[i]);
        if (!vl) {
            return err(file_error_kind::line_parse_failed, i + 1,
                       std::format("line parse failed: {}", vl.error().to_string()));
        }
        if (vl->kind == value_line_kind::closer) return std::pair{std::move(value), i + 1};
        if (vl->kind == value_line_kind::other) {
            return err(file_error_kind::missing_closer, i + 1,
                       "multiline value is not closed: expected '*]'");
        }
        if (!first) value.push_back('\n');
        value += vl->text;
        first = false;
    }
    return err(file_error_kind::missing_closer, lines.size() + 1,
               "multiline value is not closed: expected '*]' before the end of the file");
}

// Parse a single data line, returning the parsed_line. On line-parse
// failure, wraps the parse_error in a file_error pointing at the
// 1-based source line.
auto parse_one_line(std::string_view raw, std::size_t line_no,
                    split_rule rule = split_rule::levels)
    -> std::expected<parsed_line, file_error>
{
    auto r = parse_line(raw, rule);
    if (!r) {
        return err(file_error_kind::line_parse_failed, line_no,
                   std::format("line parse failed: {}",
                               r.error().to_string()));
    }
    return *r;
}

// Section-builder state machine.
//
// reading_intro: between section banner and the first Fields/Data block.
//   Accepts: template banners, blank lines, the Fields or Data banner.
// reading_fields: inside the Fields block.
//   Accepts: data lines, blank lines, End Fields, Data banner.
// reading_data: inside the Data block.
//   Accepts: data lines (including multiline openers), blank lines,
//            End Data, End Section, Section banner, End File.
// after_data: between End Data and End Section.
//   Accepts: End Section, Section banner, End File, blank lines.
enum class section_state {
    reading_intro,
    reading_fields,
    reading_data,
    after_data,
};

// Build one section from the line stream starting at `cursor` (just
// past a "=== Section: <name> ===" banner). On return, `cursor`
// points at the line AFTER the section's last consumed line (which
// may be a Section banner of the next section, an End File marker,
// or one past the end).
auto build_one_section(
    const std::vector<std::string>& lines,
    std::size_t&                    cursor,
    std::string                     name)
    -> std::expected<parsed_section, file_error>
{
    parsed_section sec;
    sec.name = std::move(name);
    section_state state = section_state::reading_intro;
    bool fields_block_seen = false;

    // Strip a trailing blank sentinel from data_lines (used at section
    // exit points). Blank lines at the very end of a data block are
    // visual whitespace, not record separators with nothing on the
    // far side.
    auto trim_trailing_blank = [&]() {
        if (!sec.data_lines.empty() && sec.data_lines.back().number == 0) {
            sec.data_lines.pop_back();
        }
    };

    // Every block closes, like XML: a block left open is a missing closer, which is
    // what a truncated file looks like.
    auto missing = [&](std::string_view closer, std::size_t line_no) {
        return err(file_error_kind::missing_closer, line_no,
                   std::format("section '{}' is not closed: expected '{}'{}", sec.name, closer,
                               line_no > lines.size() ? " before the end of the file" : ""));
    };
    auto open_closer = [&]() -> std::string_view {
        if (state == section_state::reading_fields) return "=== End Fields ===";
        if (state == section_state::reading_data) return "=== End Data ===";
        return "=== End Section ===";
    };

    while (cursor < lines.size()) {
        const auto& raw = lines[cursor];
        const auto stripped = strip_eol(raw);
        const auto line_no = cursor + 1;  // 1-based

        // Skip comment lines (# or //) inside the section (for robustness with
        // ts_store samples, explanatory comments, etc.). They are already skipped
        // at file header level.
        if (stripped.starts_with('#') || stripped.starts_with("//")) {
            ++cursor;
            continue;
        }

        // Blank lines: in the data block they're significant — they
        // separate records. We preserve them as sentinel parsed_line
        // entries (number == 0) so the validator can detect record
        // boundaries. In other states (intro, fields, after_data),
        // blank lines are ignored.
        if (is_blank(stripped)) {
            if (state == section_state::reading_data) {
                // Only emit one blank sentinel between consecutive blanks
                // (consecutive blanks compress to one record-separator).
                if (sec.data_lines.empty() || sec.data_lines.back().number != 0) {
                    parsed_line blank{};
                    blank.kind   = line_kind::data;
                    blank.number = 0;
                    sec.data_lines.push_back(blank);
                }
            }
            ++cursor;
            continue;
        }

        std::string interior;
        const bool is_marker = is_structural_marker(stripped, interior);

        if (is_marker) {
            // Section-terminating markers: applicable in any state.
            if (!extract_section_name(interior).empty() || marker_is_end_file(interior)) {
                return missing(open_closer(), line_no);
            }
            if (marker_is_end_section(interior)) {
                if (state == section_state::reading_fields || state == section_state::reading_data) {
                    return missing(open_closer(), line_no);
                }
                ++cursor;
                trim_trailing_blank();
                return sec;
            }

            // State-specific markers
            if (state == section_state::reading_intro) {
                if (auto tname = extract_template_name(interior); !tname.empty()) {
                    // Next non-blank line MUST be a multiline opener.
                    ++cursor;
                    while (cursor < lines.size() && is_blank(strip_eol(lines[cursor]))) {
                        ++cursor;
                    }
                    if (cursor >= lines.size()) {
                        return err(file_error_kind::template_missing_body, line_no,
                                   std::format("template '{}' missing body", tname));
                    }
                    // A template's body is a multiline value with a bare "[*" opener.
                    auto opener_comment = parse_bare_value_opener(lines[cursor]);
                    if (!opener_comment) {
                        return err(file_error_kind::template_missing_body, cursor + 1,
                                   std::format("template '{}' expected '[*' to open its "
                                               "body: {}", tname, opener_comment.error().to_string()));
                    }
                    ++cursor;  // advance past opener
                    auto body_or_err = consume_value(lines, cursor);
                    if (!body_or_err) return std::unexpected{body_or_err.error()};
                    parsed_template tpl;
                    tpl.name           = std::move(tname);
                    tpl.body           = std::move(body_or_err->first);
                    tpl.opener_comment = std::move(*opener_comment);
                    sec.templates.push_back(std::move(tpl));
                    cursor = body_or_err->second;  // past the closer line
                    continue;
                }
                if (auto [inc_type, inc_path] = extract_include(interior); !inc_type.empty()) {
                    sec.includes.emplace_back(inc_type, inc_path);
                    ++cursor;
                    continue;
                }
                if (marker_is_fields(interior)) {
                    state = section_state::reading_fields;
                    fields_block_seen = true;
                    ++cursor;
                    continue;
                }
                if (marker_is_data(interior)) {
                    if (!fields_block_seen) {
                        return err(file_error_kind::missing_fields_block, line_no,
                                   std::format("section '{}' has a Data block "
                                               "but no Fields block; every "
                                               "section must declare its fields "
                                               "before its data", sec.name));
                    }
                    state = section_state::reading_data;
                    sec.has_data_block = true;
                    sec.data_begin = cursor + 1;
                    ++cursor;
                    continue;
                }
                return err(file_error_kind::unexpected_marker, line_no,
                           std::format("unexpected marker '=== {} ===' in section "
                                       "intro", interior));
            }

            if (state == section_state::reading_fields) {
                if (marker_is_end_fields(interior)) {
                    state = section_state::reading_intro;  // back to intro for Data
                    ++cursor;
                    continue;
                }
                if (marker_is_data(interior)) {
                    return missing("=== End Fields ===", line_no);
                }
                if (auto [inc_type, inc_path] = extract_include(interior); !inc_type.empty()) {
                    sec.includes.emplace_back(inc_type, inc_path);
                    ++cursor;
                    continue;
                }
                return err(file_error_kind::unexpected_marker, line_no,
                           std::format("unexpected marker '=== {} ===' in fields "
                                       "block", interior));
            }

            if (state == section_state::reading_data) {
                if (marker_is_end_data(interior)) {
                    trim_trailing_blank();
                    sec.data_end = cursor;
                    state = section_state::after_data;
                    ++cursor;
                    continue;
                }
                if (marker_is_data(interior)) {
                    return err(file_error_kind::duplicate_data_block, line_no,
                               "second '=== Data ===' marker in same section");
                }
                return err(file_error_kind::unexpected_marker, line_no,
                           std::format("unexpected marker '=== {} ===' in data "
                                       "block", interior));
            }

            if (state == section_state::after_data) {
                return err(file_error_kind::unexpected_marker, line_no,
                           std::format("unexpected marker '=== {} ===' after data "
                                       "block ended", interior));
            }
        }
        else {
            // Non-marker, non-blank line: parse as a regular line.
            // Fields-block lines are declarations ("id/Identity(1,1)/Not Null"): levels.
            // Everything else here is a data row: values.
            auto pl_or_err = parse_one_line(stripped, line_no,
                state == section_state::reading_fields ? split_rule::levels : split_rule::multifield);
            if (!pl_or_err) return std::unexpected{pl_or_err.error()};
            const auto& pl = *pl_or_err;

            switch (state) {
            case section_state::reading_intro:
                // A data line belongs inside a Data block (or a Fields block): the
                // SPEC requires the banner, and there is no banner-less form.
                return err(file_error_kind::unexpected_marker, line_no,
                           "data line before '=== Fields ===' or '=== Data ===' "
                           "in section");
            case section_state::reading_fields:
                sec.field_lines.push_back(pl);
                ++cursor;
                break;
            case section_state::reading_data:
                if (pl.kind == line_kind::multiline_opener) {
                    // Capture body; record the opener as a parsed_line
                    // and remember its body separately.
                    const auto idx = sec.data_lines.size();
                    sec.data_lines.push_back(pl);
                    ++cursor;
                    auto body_or_err = consume_value(lines, cursor);
                    if (!body_or_err) return std::unexpected{body_or_err.error()};
                    sec.data_lines[idx].values = {body_or_err->first};   // the field's value
                    sec.data_multiline_bodies.push_back(
                        multiline_body{idx, std::move(body_or_err->first)});
                    cursor = body_or_err->second;
                } else {
                    sec.data_lines.push_back(pl);
                    ++cursor;
                }
                break;
            case section_state::after_data:
                return err(file_error_kind::unexpected_marker, line_no,
                           "data line after '=== End Data ===' but before section end");
            }
        }
    }

    // Reached the end of the file inside the section: it was never closed.
    return missing(open_closer(), lines.size() + 1);
}

// ──────────────────────────────────────────────────────────────
//  Light profile (SPEC 2.0.1)
//
//    # JText File - created ...        the magic line, then # metadata
//    -- Name --                        a section
//    # Fields: path.jtFlds             its field-list include
//     1. |# ...                        its rows
//    -- End Name --                    the section's closer (names match)
//    -- EOF --                         the file's closer
//
//  Every block closes, like the full profile: a section without its
//  "-- End Name --", or a file without "-- EOF --", is a missing closer.
// ──────────────────────────────────────────────────────────────

constexpr std::string_view kLightMagic = "# JText File - created";

// The text between "-- " and " --", or empty when the line is not a light banner.
auto light_banner(std::string_view line) -> std::string_view
{
    const auto t = trim(line);
    if (t.size() < 6 || !t.starts_with("-- ") || !t.ends_with(" --")) return {};
    return trim(t.substr(3, t.size() - 6));
}

auto build_light_section(const std::vector<std::string>& lines, std::size_t& cursor,
                         std::string name)
    -> std::expected<parsed_section, file_error>
{
    parsed_section sec;
    sec.name = std::move(name);
    sec.has_data_block = true;
    bool data_seen = false;
    const std::string closer = std::format("-- End {} --", sec.name);
    auto trim_trailing_blank = [&]() {
        if (!sec.data_lines.empty() && sec.data_lines.back().number == 0) sec.data_lines.pop_back();
    };

    while (cursor < lines.size()) {
        const auto stripped = strip_eol(lines[cursor]);
        const auto line_no = cursor + 1;
        const auto t = trim(stripped);

        if (is_blank(stripped)) {
            // Blank lines separate records (as in a full-profile Data block).
            if (!sec.data_lines.empty() && sec.data_lines.back().number != 0) {
                parsed_line blank{};
                blank.kind = line_kind::data;
                blank.number = 0;
                sec.data_lines.push_back(blank);
            }
            ++cursor;
            continue;
        }
        if (t.starts_with("# Fields:")) {
            sec.includes.emplace_back("Fields", std::string{trim(t.substr(9))});
            ++cursor;
            continue;
        }
        if (t.starts_with('#') || t.starts_with("//")) {
            ++cursor;
            continue;
        }
        if (const auto banner = light_banner(stripped); !banner.empty()) {
            if (banner.starts_with("End ") && trim(banner.substr(4)) == sec.name) {
                sec.data_end = cursor;
                if (!data_seen) sec.data_begin = cursor;
                ++cursor;
                trim_trailing_blank();
                return sec;
            }
            if (banner.starts_with("End ")) {
                return err(file_error_kind::unexpected_marker, line_no,
                           std::format("'-- {} --' does not close section '{}'", banner, sec.name));
            }
            return err(file_error_kind::missing_closer, line_no,
                       std::format("section '{}' is not closed: expected '{}'", sec.name, closer));
        }
        std::string interior;
        if (is_structural_marker(stripped, interior)) {
            return err(file_error_kind::unexpected_marker, line_no,
                       std::format("full-profile marker '=== {} ===' in a light-profile file",
                                   interior));
        }

        auto pl_or_err = parse_one_line(stripped, line_no, split_rule::multifield);
        if (!pl_or_err) return std::unexpected{pl_or_err.error()};
        const auto pl = *pl_or_err;
        if (!data_seen) {
            data_seen = true;
            sec.data_begin = cursor;
        }
        sec.data_lines.push_back(pl);
        ++cursor;
        if (pl.kind == line_kind::multiline_opener) {
            auto body_or_err = consume_value(lines, cursor);
            if (!body_or_err) return std::unexpected{body_or_err.error()};
            sec.data_lines.back().values = {body_or_err->first};   // the field's value
            sec.data_multiline_bodies.push_back(
                multiline_body{sec.data_lines.size() - 1, std::move(body_or_err->first)});
            cursor = body_or_err->second;
        }
    }
    return err(file_error_kind::missing_closer, lines.size() + 1,
               std::format("section '{}' is not closed: expected '{}' before the end of the file",
                           sec.name, closer));
}

// `cursor` is at the magic line.
auto build_light_file(const std::vector<std::string>& lines, std::size_t cursor)
    -> std::expected<parsed_file, file_error>
{
    parsed_file out;
    bool in_header = true;   // the "#" metadata block, before the first section

    while (cursor < lines.size()) {
        const auto stripped = strip_eol(lines[cursor]);
        const auto line_no = cursor + 1;
        const auto t = trim(stripped);
        if (in_header && t.starts_with('#')) {
            out.metadata.emplace_back(trim(t.substr(1)));
            ++cursor;
            continue;
        }
        if (is_blank(stripped) || t.starts_with('#') || t.starts_with("//")) {
            ++cursor;
            continue;
        }
        in_header = false;
        const auto banner = light_banner(stripped);
        if (banner == "EOF") {
            // The file is closed: only blank lines and comments may follow.
            for (++cursor; cursor < lines.size(); ++cursor) {
                const auto rest = trim(strip_eol(lines[cursor]));
                if (!rest.empty() && !rest.starts_with("//") && !rest.starts_with('#')) {
                    return err(file_error_kind::unexpected_marker, cursor + 1,
                               "content after '-- EOF --'");
                }
            }
            return out;
        }
        if (banner.empty() || banner.starts_with("End ")) {
            return err(file_error_kind::unexpected_marker, line_no,
                       banner.empty()
                           ? std::string{"line outside any section in a light-profile file"}
                           : std::format("'-- {} --' without an open section", banner));
        }
        ++cursor;
        auto sec_or_err = build_light_section(lines, cursor, std::string{banner});
        if (!sec_or_err) return std::unexpected{sec_or_err.error()};
        out.sections.push_back(std::move(*sec_or_err));
    }
    return err(file_error_kind::missing_closer, lines.size() + 1,
               "the file is not closed: expected '-- EOF --' before the end of the file");
}

// Build a parsed_file from a flat vector of input lines.
auto build_from_lines(const std::vector<std::string>& lines)
    -> std::expected<parsed_file, file_error>
{
    parsed_file out;
    std::size_t cursor = 0;
    bool opened_file = false;   // "=== jText File ===" seen: "=== End File ===" must close it

    // The file is closed: only blank lines and comments may follow.
    auto after_end_file = [&]() -> std::expected<parsed_file, file_error> {
        for (; cursor < lines.size(); ++cursor) {
            const auto rest = trim(strip_eol(lines[cursor]));
            if (!rest.empty() && !rest.starts_with("//") && !rest.starts_with('#')) {
                return err(file_error_kind::unexpected_marker, cursor + 1,
                           "content after '=== End File ==='");
            }
        }
        return std::move(out);
    };

    // 1. Find the magic line. Skip leading blank lines and comments (// or #), but not
    // the light profile's own "# JText File - created" magic line.
    while (cursor < lines.size()) {
        const auto stripped = strip_eol(lines[cursor]);
        auto s = trim(stripped);
        if (s.starts_with(kLightMagic)) break;
        if (is_blank(stripped) ||
            s.starts_with("//") ||
            s.starts_with("#")) {
            ++cursor;
            continue;
        }
        break;
    }

    if (cursor >= lines.size()) {
        return err(file_error_kind::missing_magic_line, 0,
                   "file is empty or contains only whitespace");
    }

    {
        const auto& first = lines[cursor];
        const auto first_stripped = strip_eol(first);
        std::string interior;
        if (first_stripped.find("=== Section:") != std::string_view::npos) {
            // ts_store style top-level section (with our includes/templates) without an explicit
            // jText File magic line. Proceed with empty header; the header-read loop will
            // immediately see the section banner and stop. This supports the ts_store logs.
        } else if (is_structural_marker(first, interior) && marker_is_jtext_file(interior)) {
            ++cursor;  // consumed the === magic
            opened_file = true;
        } else if (trim(first_stripped).starts_with(kLightMagic)) {
            return build_light_file(lines, cursor);
        } else {
            return err(file_error_kind::missing_magic_line, cursor + 1,
                       std::format("first non-blank line must be "
                                   "'=== jText File ===' or start with '# JText File - created ' "
                                   "or a top-level '=== Section: ', got: {}",
                                   first_stripped));
        }
    }

    // 2. Read header lines until first Section banner (or End File).
    while (cursor < lines.size()) {
        const auto stripped = strip_eol(lines[cursor]);
        const auto line_no = cursor + 1;

        if (is_blank(stripped)) {
            ++cursor;
            continue;
        }

        if (stripped.starts_with('#') || stripped.starts_with("//")) {
            // skip legacy # header and any // comments in header area
            ++cursor;
            continue;
        }

        std::string interior;
        if (is_structural_marker(stripped, interior)) {
            if (!extract_section_name(interior).empty()) break;
            if (marker_is_end_file(interior)) {
                // File has header only, no sections.
                ++cursor;
                return after_end_file();
            }
            return err(file_error_kind::unexpected_marker, line_no,
                       std::format("unexpected marker '=== {} ===' in file header",
                                   interior));
        }

        auto pl_or_err = parse_one_line(stripped, line_no);
        if (!pl_or_err) return std::unexpected{pl_or_err.error()};
        out.header_lines.push_back(*pl_or_err);
        ++cursor;
    }

    // 3. Read sections.
    while (cursor < lines.size()) {
        const auto stripped = strip_eol(lines[cursor]);
        const auto line_no = cursor + 1;

        if (is_blank(stripped)) {
            ++cursor;
            continue;
        }

        std::string interior;
        if (!is_structural_marker(stripped, interior)) {
            return err(file_error_kind::unexpected_marker, line_no,
                       "data line outside any section");
        }

        if (marker_is_end_file(interior)) {
            ++cursor;
            return after_end_file();
        }

        auto name = extract_section_name(interior);
        if (name.empty()) {
            return err(file_error_kind::unexpected_marker, line_no,
                       std::format("expected '=== Section: ... ===' or "
                                   "'=== End File ===', got '=== {} ==='",
                                   interior));
        }

        ++cursor;  // past the section banner
        auto sec_or_err = build_one_section(lines, cursor, std::move(name));
        if (!sec_or_err) return std::unexpected{sec_or_err.error()};
        out.sections.push_back(std::move(*sec_or_err));
    }

    if (opened_file) {
        return err(file_error_kind::missing_closer, lines.size() + 1,
                   "the file is not closed: expected '=== End File ===' before the end of the file");
    }
    return out;
}

}  // anonymous namespace

// ──────────────────────────────────────────────────────────────
//  Public entry points
// ──────────────────────────────────────────────────────────────

auto parse_file_structure(std::istream& in)
    -> std::expected<parsed_file, file_error>
{
    if (!in) {
        return err(file_error_kind::io_error, 0, "input stream not readable");
    }

    std::vector<std::string> lines;
    std::string buf;
    while (std::getline(in, buf)) {
        lines.push_back(std::move(buf));
        buf.clear();
    }
    if (in.bad()) {
        return err(file_error_kind::io_error, lines.size(), "I/O error during read");
    }
    return build_from_lines(lines);
}

auto parse_file_structure(std::string_view text)
    -> std::expected<parsed_file, file_error>
{
    // Hand-split on '\n' to avoid the istream allocation overhead and
    // to handle text without a trailing newline cleanly.
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            lines.emplace_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    if (start < text.size()) {
        lines.emplace_back(text.substr(start));
    }
    return build_from_lines(lines);
}

// ──────────────────────────────────────────────────────────────
//  Format version
// ──────────────────────────────────────────────────────────────

namespace {

constexpr std::string_view kLightVersionKey = "jText Version:";

// "0.8" -> {0, 8}; nullopt when the text is not dot-separated numbers.
auto version_parts(std::string_view v) -> std::optional<std::vector<unsigned>>
{
    std::vector<unsigned> parts;
    while (true) {
        const auto dot = v.find('.');
        const auto part = v.substr(0, dot);
        unsigned n = 0;
        const auto [end, ec] = std::from_chars(part.data(), part.data() + part.size(), n);
        if (part.empty() || ec != std::errc{} || end != part.data() + part.size()) return std::nullopt;
        parts.push_back(n);
        if (dot == std::string_view::npos) return parts;
        v.remove_prefix(dot + 1);
    }
}

// Makes the version declared in `head` (the file's lines before the replaced data)
// kFormatVersion when it is missing, older or not a number. A file with no header
// at all (a ts_store log opening on "=== Section:") is left as it is.
void upgrade_version(std::vector<std::string>& head, const parsed_file& pf)
{
    const auto status = check_version(declared_version(pf));
    if (status == version_status::current || status == version_status::newer) return;

    if (!pf.metadata.empty()) {   // light: "# jText Version: 0.8", after "# Purpose"
        const auto line = std::format("# {} {}", kLightVersionKey, kFormatVersion);
        std::optional<std::size_t> after;
        for (std::size_t i = 0; i < head.size(); ++i) {
            const auto t = trim(strip_eol(head[i]));
            if (!after) {
                if (t.starts_with(kLightMagic)) after = i;
                continue;
            }
            if (is_blank(t) || t.starts_with("//")) continue;
            if (!t.starts_with('#')) break;
            const auto key = trim(t.substr(1));
            if (key.starts_with(kLightVersionKey)) { head[i] = line; return; }
            if (key.starts_with("Purpose - ")) after = i;
        }
        if (after) head.insert(head.begin() + static_cast<std::ptrdiff_t>(*after + 1), line);
        return;
    }

    // Full: header line 4, kept in order between lines 1-3 and 5-9.
    std::optional<std::size_t> at;
    std::string fmt = "\u25AE\u258C";
    for (std::size_t i = 0; i < head.size(); ++i) {
        const auto stripped = strip_eol(head[i]);
        std::string interior;
        if (!at) {
            if (is_structural_marker(stripped, interior) && marker_is_jtext_file(interior)) at = i + 1;
            continue;
        }
        const auto t = trim(stripped);
        if (is_blank(t) || t.starts_with('#') || t.starts_with("//")) continue;
        if (is_structural_marker(stripped, interior)) break;
        const auto pl = parse_line(stripped);
        if (!pl) continue;
        fmt = pl->fmt.to_string();
        if (pl->number < 4) { at = i + 1; continue; }
        if (pl->number > 4) break;
        // Line 4 itself: same indent, formatter and comment, today's version.
        auto line = std::format("{}4. {} {}", stripped.substr(0, stripped.find_first_not_of(" \t")),
                                fmt, kFormatVersion);
        if (!pl->comment.empty()) line += std::format(" {} {}", encode_utf8(pl->fmt.bookend), pl->comment);
        head[i] = std::move(line);
        return;
    }
    if (at) head.insert(head.begin() + static_cast<std::ptrdiff_t>(*at),
                        std::format(" 4. {} {}", fmt, kFormatVersion));
}

}  // anonymous namespace

auto declared_version(const parsed_file& pf) -> std::string
{
    for (const auto& hl : pf.header_lines) {
        if (hl.number != 4) continue;
        std::string v;
        for (const auto& level : hl.values) {
            if (!v.empty()) v.push_back('/');
            v += level;
        }
        return v;
    }
    for (const auto& m : pf.metadata) {
        const std::string_view sv{m};
        if (sv.starts_with(kLightVersionKey)) return std::string{trim(sv.substr(kLightVersionKey.size()))};
    }
    return {};
}

auto check_version(std::string_view declared) -> version_status
{
    declared = trim(declared);
    if (declared.empty()) return version_status::missing;
    auto have = version_parts(declared);
    if (!have) return version_status::invalid;
    auto want = *version_parts(kFormatVersion);
    const auto n = std::max(have->size(), want.size());
    have->resize(n);
    want.resize(n);
    if (*have == want) return version_status::current;
    return *have < want ? version_status::older : version_status::newer;
}

auto version_warning(std::string_view declared) -> std::string
{
    switch (check_version(declared)) {
    case version_status::current:
        return {};
    case version_status::missing:
        return std::format("the file declares no jText version; it is read as {0}, "
                           "and saving it declares {0}", kFormatVersion);
    case version_status::older:
        return std::format("the file is jText {0}, older than this reader's {1}; it is read "
                           "with {1} rules, and saving it upgrades it to {1}",
                           trim(declared), kFormatVersion);
    case version_status::newer:
        return std::format("the file is jText {}, newer than this reader's {}; "
                           "parts of it may be misread", trim(declared), kFormatVersion);
    case version_status::invalid:
        return std::format("the file's jText version '{0}' is not a version number; it is "
                           "read as {1}, and saving it declares {1}", trim(declared), kFormatVersion);
    }
    return {};
}

auto replace_section_data(std::string_view text, std::string_view section, std::string_view data)
    -> std::expected<std::string, file_error>
{
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }

    auto parsed = parse_file_structure(text);
    if (!parsed) return std::unexpected{parsed.error()};
    const parsed_section* found = nullptr;
    for (const auto& sec : parsed->sections) {
        if (sec.name != section) continue;
        if (found) {
            return err(file_error_kind::section_ambiguous, 0,
                       std::format("section '{}' appears more than once", section));
        }
        found = &sec;
    }
    if (!found || !found->has_data_block) {
        return err(file_error_kind::section_not_found, 0,
                   found ? std::format("section '{}' has no data block", section)
                         : std::format("no section '{}'", section));
    }

    std::vector<std::string> head(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(found->data_begin));
    upgrade_version(head, *parsed);

    std::string out;
    for (const auto& line : head) { out += line; out += '\n'; }
    out += data;
    if (!data.empty() && data.back() != '\n') out += '\n';
    for (std::size_t i = found->data_end; i < lines.size(); ++i) { out += lines[i]; out += '\n'; }
    if (!text.empty() && text.back() != '\n' && !out.empty()) out.pop_back();
    return out;
}

}  // namespace jtext

