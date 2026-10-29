// File: tools/common/xml_import.hpp
//
// The canonical XML export (XML_EXPORT.md) back into jText, shared by jtext_from_xml and
// jtext_from_html (which reads its tables into that same XML first). Values and comments
// go back as they were exported, each comment on its own line.

#pragma once

#include "common/record_lines.hpp"
#include "parser/section.hpp"   // jtext::kDefaultNullDate

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <charconv>
#include <format>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace jtools::xml_import {


using Node = xmlNode;

inline auto name_is(const Node* n, std::string_view name) -> bool
{
    return n && n->type == XML_ELEMENT_NODE && name == reinterpret_cast<const char*>(n->name);
}

inline auto attr(const Node* n, const char* name) -> std::optional<std::string>
{
    xmlChar* v = xmlGetProp(n, reinterpret_cast<const xmlChar*>(name));
    if (!v) return std::nullopt;
    std::string out(reinterpret_cast<const char*>(v));
    xmlFree(v);
    return out;
}

inline auto attr_or(const Node* n, const char* name, std::string fallback = {}) -> std::string
{
    return attr(n, name).value_or(std::move(fallback));
}

// RFC 4648 base64 (XML_EXPORT.md 3.2: a value or comment XML cannot hold).
inline auto unbase64(std::string_view s) -> std::string
{
    auto digit = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    unsigned buf = 0;
    int bits = 0;
    for (const char c : s) {
        const int d = digit(c);
        if (d < 0) continue;   // '=' padding
        buf = (buf << 6) | static_cast<unsigned>(d);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

// An element's text: its content, decoded when it is base64.
inline auto text_of(const Node* n) -> std::string
{
    xmlChar* v = xmlNodeGetContent(n);
    std::string out = v ? reinterpret_cast<const char*>(v) : "";
    if (v) xmlFree(v);
    return attr_or(n, "encoding") == "base64" ? unbase64(out) : out;
}

// The comment on an element's line: `comment`, or `comment-base64`.
inline auto line_comment(const Node* n) -> std::string
{
    if (auto c = attr(n, "comment")) return *c;
    if (auto c = attr(n, "comment-base64")) return unbase64(*c);
    return {};
}

inline auto number(const std::string& s) -> std::size_t
{
    std::size_t n = 0;
    std::from_chars(s.data(), s.data() + s.size(), n);
    return n;
}

// A <field> back as its declaration's levels: name, the type (or its auto designation),
// length, Not Null / Nullable, Not Empty, NullDate(...) when not the default.
inline auto declaration_of(const Node* f) -> jtools::Declaration
{
    jtools::Declaration d;
    const std::size_t position = number(attr_or(f, "position"));
    d.levels.push_back(attr_or(f, "name"));
    const auto type = attr_or(f, "type", "String");
    if (auto a = attr(f, "auto")) d.levels.push_back(*a);
    else d.levels.push_back(type);
    if (auto l = attr(f, "length")) d.levels.push_back(*l);
    d.levels.emplace_back(attr_or(f, "not-null") == "true" ? "Not Null" : "Nullable");
    if (position != 1 && attr_or(f, "not-empty") == "true") d.levels.emplace_back("Not Empty");
    if (auto nd = attr(f, "null-date"); nd && *nd != jtext::kDefaultNullDate)
        d.levels.push_back(std::format("NullDate({})", *nd));
    d.comment = line_comment(f);
    return d;
}

inline std::size_t g_dropped = 0;   // header parts the writer has no place for

// A header element's line number in the full profile (SPEC 2.3); 0: none.
inline auto header_line(const Node* h) -> std::size_t
{
    static constexpr std::string_view kNames[] = {"filename", "date", "purpose", "jtext_version",
                                                  "case_mode", "sql_dialect", "table_name", "auto_id"};
    for (std::size_t i = 0; i < std::size(kNames); ++i)
        if (name_is(h, kNames[i])) return i + 1;
    return 0;
}

inline void header_of(const Node* header, jText::v002::JTextWriter& w)
{
    std::vector<std::string> pending;   // comment lines waiting for the line they stand before
    for (const Node* h = header ? header->children : nullptr; h; h = h->next) {
        if (h->type != XML_ELEMENT_NODE) continue;
        if (name_is(h, "comment")) {
            pending.push_back(text_of(h));
            continue;
        }
        const std::size_t line = header_line(h);
        if (line == 0) {
            ++g_dropped;   // related_files
            continue;
        }
        for (const auto& t : pending) w.add_header_comment_line(line, t);
        pending.clear();
        if (auto c = line_comment(h); !c.empty()) w.set_header_comment(line, c);
        const std::string v = text_of(h);
        if (name_is(h, "filename")) w.set_filename(v);
        else if (name_is(h, "date")) w.set_date(v);
        else if (name_is(h, "purpose")) w.set_purpose(v);
        else if (name_is(h, "case_mode"))
            w.set_case_mode(v == "Sensitive" ? jText::v002::CaseMode::Sensitive : jText::v002::CaseMode::Insensitive);
        else if (name_is(h, "sql_dialect")) w.set_sql_dialect(v);
        else if (name_is(h, "table_name")) w.set_table_name(v);
        else if (name_is(h, "auto_id")) w.set_auto_id(v == "true");
    }
    for (const auto& t : pending) w.add_header_comment_line(0, t);   // after the last line
}

inline void section_of(const Node* sec, jText::v002::JTextWriter& w, std::size_t index, bool full)
{
    std::string name = full ? attr_or(sec, "name") : std::string{};
    if (name.empty()) name = index == 1 ? std::string("records") : std::format("records_{}", index);
    w.begin_section(name);

    const Node* fields = nullptr;
    const Node* records = nullptr;
    for (const Node* c = sec->children; c; c = c->next) {
        if (name_is(c, "include")) w.write_include(attr_or(c, "kind"), text_of(c));
        else if (name_is(c, "template")) w.write_template(attr_or(c, "name"), text_of(c), line_comment(c));
        else if (name_is(c, "fields")) fields = c;
        else if (name_is(c, "records")) records = c;
    }

    // Fields: each declaration with the comment lines before it, then those at the end.
    std::vector<jtools::Declaration> decls;
    jtools::LineComments field_comments;
    std::vector<std::string> pending;
    for (const Node* f = fields ? fields->children : nullptr; f; f = f->next) {
        if (name_is(f, "comment")) pending.push_back(text_of(f));
        else if (name_is(f, "field")) {
            decls.push_back(declaration_of(f));
            if (!pending.empty()) field_comments.before[static_cast<unsigned>(decls.size())] = std::move(pending);
            pending.clear();
        }
    }
    field_comments.end = std::move(pending);
    jtools::write_fields(w, decls, field_comments);

    // Records: every declared position's value, each line's comment and comment lines.
    w.begin_data();
    std::vector<std::string> data_end;
    for (const Node* r = records ? records->children : nullptr; r; r = r->next) {
        if (name_is(r, "comment")) {
            data_end.push_back(text_of(r));
            continue;
        }
        if (!name_is(r, "record")) continue;
        for (auto& t : data_end) w.write_comment_line(t);   // comment lines between records
        data_end.clear();
        std::vector<std::string> row;
        jtools::LineComments lc;
        std::vector<std::string> before;
        for (const Node* v = r->children; v; v = v->next) {
            if (name_is(v, "comment")) {
                before.push_back(text_of(v));
                continue;
            }
            if (!name_is(v, "value")) continue;
            const auto p = static_cast<unsigned>(number(attr_or(v, "position")));
            if (p == 0) continue;
            if (row.size() < p) row.resize(p);
            row[p - 1] = text_of(v);
            if (auto c = line_comment(v); !c.empty()) lc.line[p] = c;
            if (!before.empty()) lc.before[p] = std::move(before);
            before.clear();
        }
        jtools::write_record(w, row, lc);
    }
    for (const auto& t : data_end) w.write_comment_line(t);
}


// The jText of a canonical XML export whose root is `root`, written to `out`. A data-scope
// export gets `name` and "Imported from <source>" as its header. Returns the sections written.
inline auto write_jtext(const Node* root, std::ostream& out, const std::string& name, const std::string& source,
                        const std::string& related) -> std::size_t
{
    const bool full = attr_or(root, "scope") == "full";
    jText::v002::write_file_comment_header(out, name, "jText Data File", related);
    jText::v002::JTextWriter w(out);
    w.set_profile(jText::v002::JTextProfile::Full);
    if (full) {
        w.set_exact_header(true);   // the header declares what the exported file did
    } else {
        w.set_filename(name);
        w.set_purpose("Imported from " + source);
    }
    std::size_t sections = 0;
    for (const Node* c = root->children; c; c = c->next) {
        if (name_is(c, "header")) header_of(c, w);
        else if (name_is(c, "section")) {
            if (sections == 0) w.write_header();
            section_of(c, w, ++sections, full);
        }
    }
    if (sections == 0) w.write_header();
    w.finalize();
    return sections;
}

}  // namespace jtools::xml_import
