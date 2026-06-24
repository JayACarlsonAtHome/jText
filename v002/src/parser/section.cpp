// File: src/parser/section.cpp
//
// Implementation of the file/section structure parser.
//
// Internally driven by a small state machine that walks through the
// input lines once, dispatching on:
//   - structural markers ("=== ... ===")
//   - blank lines
//   - regular numbered lines (forwarded to parse_line)
//   - multiline-opener lines (consume until matching sentinel)

#include "parser/section.hpp"
#include "parser/line.hpp"

#include <cctype>
#include <format>
#include <istream>
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
        case file_error_kind::unknown_section_state:        return "unknown_section_state";
        case file_error_kind::template_missing_body:        return "template_missing_body";
        case file_error_kind::multiline_unterminated:       return "multiline_unterminated";
        case file_error_kind::multiline_sentinel_empty_body: return "multiline_sentinel_empty_body";
        case file_error_kind::line_parse_failed:            return "line_parse_failed";
        case file_error_kind::duplicate_data_block:         return "duplicate_data_block";
        case file_error_kind::unterminated_section:         return "unterminated_section";
        case file_error_kind::io_error:                     return "io_error";
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

// Consume a multiline block starting at lines[start] which must
// be a parsed multiline_opener. Walk forward looking for a line
// whose entire content matches the sentinel. Capture everything
// between (exclusive) as the body. Returns the index AFTER the
// closer line on success.
auto consume_multiline_body(
    const std::vector<std::string>& lines,
    std::size_t                     start,
    std::string_view                sentinel)
    -> std::expected<std::pair<std::string, std::size_t>, file_error>
{
    std::string body;
    bool first = true;

    for (std::size_t i = start; i < lines.size(); ++i) {
        const auto stripped = strip_eol(lines[i]);
        if (stripped == sentinel) {
            // Closer found. Body excludes the closer line itself.
            return std::pair{std::move(body), i + 1};
        }
        if (!first) body.push_back('\n');
        body.append(std::string{stripped});
        first = false;
    }

    return err(file_error_kind::multiline_unterminated, start,
               std::format("multiline block opened but sentinel '{}' "
                           "never found", sentinel));
}

// Parse a single data line, returning the parsed_line. On line-parse
// failure, wraps the parse_error in a file_error pointing at the
// 1-based source line.
auto parse_one_line(std::string_view raw, std::size_t line_no)
    -> std::expected<parsed_line, file_error>
{
    auto r = parse_line(raw);
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

    while (cursor < lines.size()) {
        const auto& raw = lines[cursor];
        const auto stripped = strip_eol(raw);
        const auto line_no = cursor + 1;  // 1-based

        // ts_store / debug trailer
        if (stripped.find("-- EOF") != std::string::npos || stripped.find("EOF --") != std::string::npos) {
            ++cursor;
            trim_trailing_blank();
            return sec;
        }

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
            if (auto next_name = extract_section_name(interior); !next_name.empty()) {
                // New section begins → end of current section (implicit).
                // Do NOT advance cursor; caller will see this banner.
                trim_trailing_blank();
                return sec;
            }
            if (marker_is_end_file(interior)) {
                // End of file → end of current section (implicit).
                trim_trailing_blank();
                return sec;
            }
            if (marker_is_end_section(interior)) {
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
                    auto opener_or_err = parse_one_line(lines[cursor], cursor + 1);
                    if (!opener_or_err) return std::unexpected{opener_or_err.error()};
                    const auto& opener = *opener_or_err;
                    if (opener.kind != line_kind::multiline_opener) {
                        return err(file_error_kind::template_missing_body, cursor + 1,
                                   std::format("template '{}' expected multiline "
                                               "opener, got data line", tname));
                    }
                    ++cursor;  // advance past opener
                    auto body_or_err = consume_multiline_body(lines, cursor, opener.sentinel);
                    if (!body_or_err) {
                        auto e = body_or_err.error();
                        e.line_no = cursor;
                        return std::unexpected{std::move(e)};
                    }
                    parsed_template tpl;
                    tpl.name           = std::move(tname);
                    tpl.sentinel       = opener.sentinel;
                    tpl.body           = std::move(body_or_err->first);
                    tpl.opener_comment = opener.comment;
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
                    state = section_state::reading_data;
                    ++cursor;
                    continue;
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
            auto pl_or_err = parse_one_line(stripped, line_no);
            if (!pl_or_err) return std::unexpected{pl_or_err.error()};
            const auto& pl = *pl_or_err;

            switch (state) {
            case section_state::reading_intro:
                // Loose lines in intro before fields/data — accept
                // them as field lines? No: the SPEC requires Fields
                // banner. We treat this as unexpected.
                // However, for ts_store bare style (includes/templates at top, then bare
                // compact data rows "N. #|# f1|f2|..." without Data banner), allow the
                // compact data lines to be collected directly into data_lines.
                if (stripped.find(" #|# ") != std::string::npos) {
                    sec.data_lines.push_back(pl);
                    ++cursor;
                    continue;
                }
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
                    auto body_or_err = consume_multiline_body(lines, cursor, pl.sentinel);
                    if (!body_or_err) {
                        auto e = body_or_err.error();
                        e.line_no = line_no;
                        return std::unexpected{std::move(e)};
                    }
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

    // Reached EOF.
    // It is legal to end here in reading_data or after_data. It is
    // semantically odd to end in reading_intro/reading_fields, but
    // structurally permissible (no actual section content). We let
    // this through; validator can warn.
    trim_trailing_blank();
    return sec;
}

// Build a parsed_file from a flat vector of input lines.
auto build_from_lines(const std::vector<std::string>& lines)
    -> std::expected<parsed_file, file_error>
{
    parsed_file out;
    std::size_t cursor = 0;

    // 1. Find and consume the magic line.
    // Skip leading blank lines and any header comment lines (// or # style, standard or legacy).
    // This makes ts_store logs (which start with our // header then # from writer) parseable,
    // while still supporting the === jText File magic.
    while (cursor < lines.size()) {
        const auto stripped = strip_eol(lines[cursor]);
        auto s = trim(stripped);
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
        } else if (first_stripped.starts_with("# JText File - created ")) {
            // legacy # magic from writer; do not advance, header parsing will handle the # block
        } else if (first_stripped.find("=== <#include#>") != std::string_view::npos ||
                   first_stripped.find("=== Template:") != std::string_view::npos) {
            // ts_store bare style: top-level includes + templates (no explicit jText File or
            // Section banner). The includes bring schema/fields, templates for SQL etc, followed
            // by bare data rows in compact "N. #|# f1|f2|..." form. Accept here so header loop
            // can see it; we will auto-start an implicit section for the content.
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
                return out;
            }
            if (interior.find("#include") != std::string::npos ||
                interior.find("Template:") != std::string::npos) {
                // ts_store bare style at file header level: includes + templates come before any
                // Section banner (or instead of one). Back up so the sections logic (or implicit
                // section creation) will pick up this content as the start of the data.
                --cursor;
                break;
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

    // ts_store bare support (after possible --cursor in header for include/template):
    // If we have top-level includes/templates or bare data (no Section banner ever emitted
    // by ts_store split logs), auto-create one implicit section "ts_store" and let
    // build_one_section consume the includes, templates, and following bare "N. #|# ..." rows.
    if (out.sections.empty() && cursor < lines.size()) {
        // skip blanks to see what is next
        std::size_t probe = cursor;
        while (probe < lines.size() && is_blank(strip_eol(lines[probe]))) ++probe;
        if (probe < lines.size()) {
            std::string inter;
            auto s = strip_eol(lines[probe]);
            if (is_structural_marker(s, inter)) {
                if (inter.find("#include") != std::string::npos ||
                    inter.find("Template:") != std::string::npos) {
                    std::string impl_name = "ts_store";
                    auto sec_or_err = build_one_section(lines, cursor, impl_name);
                    if (!sec_or_err) return std::unexpected{sec_or_err.error()};
                    out.sections.push_back(std::move(*sec_or_err));
                }
            } else if (s.find(" #|# ") != std::string::npos) {
                // bare data rows with no preceding include/template even
                std::string impl_name = "ts_store";
                auto sec_or_err = build_one_section(lines, cursor, impl_name);
                if (!sec_or_err) return std::unexpected{sec_or_err.error()};
                out.sections.push_back(std::move(*sec_or_err));
            }
        }
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
            return out;
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

}  // namespace jtext

