// File: src/export/xml_export.cpp
//
// The canonical XML export (XML_EXPORT.md). One layout, one escaping, defaults left
// out: equal data gives equal bytes. It exports the data model, not the text form, so
// a comment is named by the line it is on: its record and the field the line starts at.

#include "export/xml_export.hpp"
#include "parser/utf8.hpp"
#include "validator/validator.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <map>
#include <optional>
#include <vector>

namespace jtext {

namespace {

// ── Text ──────────────────────────────────────────────────────

// True when XML 1.0 can hold every character of `s` (XML_EXPORT 3.2): valid UTF-8, and
// none of U+0000-U+0008, U+000B, U+000C, U+000E-U+001F, U+FFFE, U+FFFF.
auto xml_can_hold(std::string_view s) -> bool
{
    for (std::size_t i = 0; i < s.size();) {
        const auto d = decode_utf8(s.substr(i));
        if (!d) return false;
        const char32_t c = d->cp;
        if ((c < 0x20 && c != 0x09 && c != 0x0A && c != 0x0D) || c == 0xFFFE || c == 0xFFFF)
            return false;
        i += d->len;
    }
    return true;
}

// RFC 4648 base64, standard alphabet, padded, no line breaks.
auto base64(std::string_view s) -> std::string
{
    static constexpr std::string_view abc =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < s.size(); i += 3) {
        const auto n = (static_cast<unsigned char>(s[i]) << 16) |
                       (static_cast<unsigned char>(s[i + 1]) << 8) | static_cast<unsigned char>(s[i + 2]);
        out += abc[(n >> 18) & 63]; out += abc[(n >> 12) & 63]; out += abc[(n >> 6) & 63]; out += abc[n & 63];
    }
    if (i + 1 == s.size()) {
        const auto n = static_cast<unsigned char>(s[i]) << 16;
        out += abc[(n >> 18) & 63]; out += abc[(n >> 12) & 63]; out += "==";
    } else if (i + 2 == s.size()) {
        const auto n = (static_cast<unsigned char>(s[i]) << 16) | (static_cast<unsigned char>(s[i + 1]) << 8);
        out += abc[(n >> 18) & 63]; out += abc[(n >> 12) & 63]; out += abc[(n >> 6) & 63]; out += '=';
    }
    return out;
}

// Element text (XML_EXPORT 3.1): & < > and CR escaped, everything else as itself.
auto esc_text(std::string_view s) -> std::string
{
    std::string out;
    for (const char c : s) {
        switch (c) {
        case '&':  out += "&amp;"; break;
        case '<':  out += "&lt;"; break;
        case '>':  out += "&gt;"; break;
        case '\r': out += "&#13;"; break;
        default:   out += c;
        }
    }
    return out;
}

// Attribute value (XML_EXPORT 3.1): also " TAB LF, which a reader would change.
auto esc_attr(std::string_view s) -> std::string
{
    std::string out;
    for (const char c : s) {
        switch (c) {
        case '"':  out += "&quot;"; break;
        case '\t': out += "&#9;"; break;
        case '\n': out += "&#10;"; break;
        default:   out += esc_text(std::string_view(&c, 1));
        }
    }
    return out;
}

// ` name="value"`, or ` name-base64="..."` for a value XML cannot hold.
auto attr(std::string_view name, std::string_view value) -> std::string
{
    if (xml_can_hold(value)) return std::format(" {}=\"{}\"", name, esc_attr(value));
    return std::format(" {}-base64=\"{}\"", name, base64(value));
}

// A comment attribute, or nothing for no comment.
auto comment_attr(std::string_view comment) -> std::string
{
    return comment.empty() ? std::string{} : attr("comment", comment);
}

struct xml_out {
    std::string text;

    void line(int depth, std::string_view s)
    {
        text.append(static_cast<std::size_t>(depth) * 2, ' ');
        text += s;
        text += '\n';
    }

    // <tag attrs>text</tag>, <tag attrs/> when empty, base64 text when XML cannot hold it.
    void element(int depth, std::string_view tag, std::string_view attrs, std::string_view value)
    {
        if (value.empty()) {
            line(depth, std::format("<{}{}/>", tag, attrs));
        } else if (xml_can_hold(value)) {
            line(depth, std::format("<{}{}>{}</{}>", tag, attrs, esc_text(value), tag));
        } else {
            line(depth, std::format("<{}{} encoding=\"base64\">{}</{}>", tag, attrs, base64(value), tag));
        }
    }

    void comments(int depth, const std::vector<std::string>& texts)
    {
        for (const auto& c : texts) element(depth, "comment", "", c);
    }
};

auto iequals(std::string_view a, std::string_view b) -> bool
{
    return std::ranges::equal(a, b, [](char x, char y) {
        return (x >= 'A' && x <= 'Z' ? x + 32 : x) == (y >= 'A' && y <= 'Z' ? y + 32 : y);
    });
}

auto join_levels(const std::vector<std::string>& levels) -> std::string
{
    std::string v;
    for (std::size_t i = 0; i < levels.size(); ++i) v += (i ? "/" : "") + levels[i];
    return v;
}

// U+001F, the v0.7 compact-row null marker, reads as {NULL} (SPEC 2.6.1).
auto value_text(const std::string& v) -> std::string
{
    return v == "\x1F" ? std::string{kNullValue} : v;
}

// ── Header (XML_EXPORT 4.2) ────────────────────────────────────

constexpr std::array<std::string_view, 9> kHeaderKeys{
    "filename", "date", "purpose", "jtext_version", "case_mode",
    "sql_dialect", "table_name", "auto_id", "related_files"};

struct header_item {
    std::string value;
    std::vector<std::string> files;   // related_files
    std::string comment;
    std::vector<std::string> comment_lines;   // standing before it
};

auto normalize(std::size_t key, std::string v) -> std::string
{
    if (kHeaderKeys[key] == "case_mode") {
        if (iequals(v, "insensitive")) return "Insensitive";
        if (iequals(v, "sensitive")) return "Sensitive";
    }
    if (kHeaderKeys[key] == "auto_id") {
        if (iequals(v, "true") || iequals(v, "yes") || v == "1") return "true";
        if (iequals(v, "false") || iequals(v, "no") || v == "0") return "false";
    }
    return v;
}

void write_header(xml_out& x, const parsed_file& pf)
{
    std::array<std::optional<header_item>, kHeaderKeys.size()> items;
    // For each line of the header block, the item it gives (or none).
    std::vector<std::optional<std::size_t>> line_item;

    if (!pf.metadata.empty()) {
        // Light profile: the "#" metadata lines (SPEC 2.0.1).
        static constexpr std::array<std::pair<std::string_view, std::size_t>, 7> kKeys{{
            {"JText File - created ", 1}, {"Purpose - ", 2}, {"jText Version:", 3},
            {"Case:", 4}, {"SQL Dialect:", 5}, {"Table Name:", 6}, {"AutoID:", 7}}};
        for (const auto& m : pf.metadata) {
            std::optional<std::size_t> got;
            for (const auto& [prefix, key] : kKeys) {
                if (!m.starts_with(prefix) || items[key]) continue;
                std::string v = m.substr(prefix.size());
                v.erase(0, v.find_first_not_of(' '));
                v.erase(v.find_last_not_of(' ') + 1);
                items[key] = header_item{normalize(key, v), {}, {}, {}};
                got = key;
                break;
            }
            line_item.push_back(got);
        }
    } else {
        for (const auto& hl : pf.header_lines) {
            std::optional<std::size_t> got;
            if (hl.number >= 1 && hl.number <= kHeaderKeys.size() && !items[hl.number - 1]) {
                const std::size_t key = hl.number - 1;
                header_item it;
                if (kHeaderKeys[key] == "related_files") it.files = hl.values;
                else it.value = normalize(key, join_levels(hl.values));
                it.comment = hl.comment;
                items[key] = std::move(it);
                got = key;
            }
            line_item.push_back(got);
        }
    }

    // A comment line stands before the item of its line, or the next line that gives one.
    std::vector<std::string> end_comments;
    for (const auto& c : pf.header_comments) {
        std::optional<std::size_t> key;
        for (std::size_t l = c.before; l < line_item.size() && !key; ++l) key = line_item[l];
        if (key) items[*key]->comment_lines.push_back(c.text);
        else end_comments.push_back(c.text);
    }

    const bool any = std::ranges::any_of(items, [](const auto& i) { return i.has_value(); });
    if (!any && end_comments.empty()) {
        x.line(1, "<header/>");
        return;
    }
    x.line(1, "<header>");
    for (std::size_t k = 0; k < items.size(); ++k) {
        if (!items[k]) continue;
        const auto& it = *items[k];
        x.comments(2, it.comment_lines);
        if (kHeaderKeys[k] == "related_files") {
            x.line(2, std::format("<related_files{}>", comment_attr(it.comment)));
            for (const auto& f : it.files) x.element(3, "file", "", f);
            x.line(2, "</related_files>");
        } else {
            x.element(2, kHeaderKeys[k], comment_attr(it.comment), it.value);
        }
    }
    x.comments(2, end_comments);
    x.line(1, "</header>");
}

// ── Fields (XML_EXPORT 4.4) ────────────────────────────────────

void write_fields(xml_out& x, const parsed_section& ps, const std::vector<field>& fields)
{
    // Comment lines stand before the declaration of field_lines[before] (by its position).
    std::map<unsigned, std::vector<std::string>> before;
    std::vector<std::string> end_comments;
    for (const auto& c : ps.field_comments) {
        if (c.before < ps.field_lines.size() && !fields.empty())
            before[ps.field_lines[c.before].number].push_back(c.text);
        else
            end_comments.push_back(c.text);
    }
    if (fields.empty() && end_comments.empty()) {
        x.line(2, "<fields/>");
        return;
    }
    x.line(2, "<fields>");
    for (const auto& f : fields) {
        if (auto it = before.find(f.position); it != before.end()) x.comments(3, it->second);
        std::string a = std::format(" position=\"{}\"", f.position) + attr("name", f.name) +
                        std::format(" type=\"{}\"", to_string(f.type));
        if (f.auto_id) a += std::format(" auto=\"Identity({},{})\"", f.auto_id->seed, f.auto_id->step);
        if (f.max_length) a += std::format(" length=\"{}\"", *f.max_length);
        if (f.not_null) a += " not-null=\"true\"";
        if (f.not_empty) a += " not-empty=\"true\"";
        if (f.type == field_type::date_type || f.type == field_type::datetime_type)
            a += attr("null-date", null_date(f));
        a += comment_attr(f.comment);
        x.line(3, std::format("<field{}/>", a));
    }
    x.comments(3, end_comments);
    x.line(2, "</fields>");
}

// ── Records (XML_EXPORT 4.5, 4.6) ──────────────────────────────

struct export_record {
    std::map<unsigned, std::string> values;                      // by position
    std::map<unsigned, std::string> line_comment;               // by the line's first position
    std::map<unsigned, std::vector<std::string>> comment_lines; // standing before that line
};

struct walked_records {
    std::vector<export_record> records;
    std::vector<std::string>   end_comments;   // comment lines at the end of the block
};

// The records as the validator assembles them (a blank line ends one, a line for field
// 1, or one at or before the last field filled, starts the next), with each comment on
// the line it belongs to.
auto walk_records(const parsed_section& ps) -> walked_records
{
    walked_records w;
    std::vector<std::string> pending;
    bool open = false;
    unsigned last = 0;
    std::size_t ci = 0;
    for (std::size_t d = 0; d < ps.data_lines.size(); ++d) {
        for (; ci < ps.data_comments.size() && ps.data_comments[ci].before <= d; ++ci)
            pending.push_back(ps.data_comments[ci].text);
        const auto& pl = ps.data_lines[d];
        if (pl.number == 0) { open = false; continue; }
        if (open && pl.number <= last) open = false;
        if (!open) { w.records.emplace_back(); open = true; last = 0; }
        auto& r = w.records.back();
        if (!pending.empty()) {
            auto& cl = r.comment_lines[pl.number];
            cl.insert(cl.end(), pending.begin(), pending.end());
            pending.clear();
        }
        const std::size_t count = std::max<std::size_t>(pl.values.size(), 1);
        for (std::size_t i = 0; i < pl.values.size(); ++i)
            r.values[static_cast<unsigned>(pl.number + i)] = value_text(pl.values[i]);
        if (!pl.comment.empty()) r.line_comment[pl.number] = pl.comment;
        last = static_cast<unsigned>(pl.number + count - 1);
    }
    for (; ci < ps.data_comments.size(); ++ci) pending.push_back(ps.data_comments[ci].text);
    w.end_comments = std::move(pending);
    return w;
}

void write_records(xml_out& x, const parsed_section& ps, std::size_t field_count)
{
    const auto w = walk_records(ps);
    if (w.records.empty() && w.end_comments.empty()) {
        x.line(2, "<records/>");
        return;
    }
    x.line(2, "<records>");
    for (const auto& r : w.records) {
        x.line(3, "<record>");
        const unsigned last_pos = std::max(static_cast<unsigned>(field_count),
                                           r.values.empty() ? 0u : r.values.rbegin()->first);
        for (unsigned p = 1; p <= last_pos; ++p) {
            if (auto it = r.comment_lines.find(p); it != r.comment_lines.end()) x.comments(4, it->second);
            const auto v = r.values.find(p);
            const auto c = r.line_comment.find(p);
            x.element(4, "value",
                      std::format(" position=\"{}\"", p) +
                          (c == r.line_comment.end() ? std::string{} : comment_attr(c->second)),
                      v == r.values.end() ? std::string_view{} : std::string_view{v->second});
        }
        x.line(3, "</record>");
    }
    x.comments(3, w.end_comments);
    x.line(2, "</records>");
}

}  // anonymous namespace

auto to_xml(const parsed_file& pf, xml_scope scope) -> std::string
{
    const auto vr = validate(pf, validation_level::minimal);   // the fields, interpreted
    xml_out x;
    x.text = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    x.line(0, std::format("<jtext format=\"{}\" export=\"{}\" scope=\"{}\" xml:space=\"preserve\">",
                          kFormatVersion, kXmlExportVersion, scope == xml_scope::full ? "full" : "data"));
    if (scope == xml_scope::full) write_header(x, pf);

    for (std::size_t s = 0; s < pf.sections.size(); ++s) {
        const auto& ps = pf.sections[s];
        const auto& fields = vr.file.sections.at(s).fields;
        x.line(1, scope == xml_scope::full ? std::format("<section{}>", attr("name", ps.name))
                                           : std::string{"<section>"});
        if (scope == xml_scope::full) {
            for (const auto& [kind, path] : ps.includes) x.element(2, "include", attr("kind", kind), path);
            for (const auto& t : ps.templates)
                x.element(2, "template", attr("name", t.name) + comment_attr(t.opener_comment), t.body);
        }
        write_fields(x, ps, fields);
        write_records(x, ps, fields.size());
        x.line(1, "</section>");
    }
    x.line(0, "</jtext>");
    return std::move(x.text);
}

auto section_comments(const parsed_section& ps) -> std::vector<located_comment>
{
    std::vector<located_comment> out;
    auto lines = [&](std::string_view block, std::int64_t record, unsigned position,
                     const std::vector<std::string>& texts) {
        unsigned seq = 0;
        for (const auto& t : texts)
            out.push_back({std::string{block}, "comment-line", record, position, ++seq, t});
    };

    // Fields block: comment lines stand before the declaration of field_lines[before].
    std::map<unsigned, std::vector<std::string>> before;
    std::vector<std::string> fields_end;
    for (const auto& c : ps.field_comments) {
        if (c.before < ps.field_lines.size()) before[ps.field_lines[c.before].number].push_back(c.text);
        else fields_end.push_back(c.text);
    }
    for (const auto& fl : ps.field_lines) {
        if (auto it = before.find(fl.number); it != before.end()) lines("fields", 0, fl.number, it->second);
        if (!fl.comment.empty()) out.push_back({"fields", "line", 0, fl.number, 0, fl.comment});
    }
    lines("fields", 0, 0, fields_end);

    // Data block, record by record.
    const auto w = walk_records(ps);
    for (const auto& r : w.records) {
        std::int64_t id = 0;
        if (auto v = r.values.find(1); v != r.values.end()) {
            std::string_view t = v->second;
            if (t.starts_with('+')) t.remove_prefix(1);
            std::from_chars(t.data(), t.data() + t.size(), id);
        }
        for (const auto& [p, texts] : r.comment_lines) lines("data", id, p, texts);
        for (const auto& [p, text] : r.line_comment) out.push_back({"data", "line", id, p, 0, text});
    }
    lines("data", 0, 0, w.end_comments);
    return out;
}

}  // namespace jtext
