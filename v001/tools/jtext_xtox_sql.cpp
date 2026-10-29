// File: tools/jtext_xtox_sql.cpp
//
// Minimal jText processor (first cut).
// Invocation:
//   jtext_xtox_sql <data_path> <base_name> <output_file>
//
// Design philosophy (important):
//   - The tool is intentionally "stupid".
//   - It does NOT try to detect SERIAL, IDENTITY, auto-increment,
//     or any other database-specific column behavior.
//   - It does NOT automatically omit columns.
//   - It simply substitutes {N} placeholders from the templates
//     that are present in the .jtext / .jtFull file.
//   - This keeps the tool (and the whole jText approach) fully
//     database-agnostic. The author controls the exact emitted SQL
//     by writing the appropriate templates inside the jText file.
//
// For example:
//   - If you want to omit an auto-generated id, write a template
//     that does not include that column.
//   - If you want to force a specific id value during migration,
//     include it and use OVERRIDING SYSTEM VALUE (or equivalent).
//
// For now it only handles .jtFull files.
// It loads <data_path>/<base_name>.jtFull, parses + validates it,
// then emits the templates with basic substitution
// and writes the result to <output_file>.

#include <jText/v001.hpp>
#include "parser/section.hpp"
#include "validator/validator.hpp"
#include "export/xml_export.hpp"   // jtext::section_comments

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <string_view>
#include <vector>
#include <chrono>
#include <format>
#include <charconv>
#include <optional>

namespace fs = std::filesystem;

static auto load_file(const fs::path& path) -> std::string
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    return content;
}

// Quote a value as a SQL string literal, escaping embedded single quotes by
// doubling them (standard SQL). Other bytes — including newlines and
// backslashes — are emitted verbatim, which is correct for SQL standard string
// literals (standard_conforming_strings). This is the single choke point for
// turning untrusted text into a safe quoted literal.
static auto sql_string_literal(const std::string& v) -> std::string
{
    std::string out;
    out.reserve(v.size() + 2);
    out += '\'';
    for (char c : v) {
        if (c == '\'') out += '\'';        // double the quote
        out += c;
    }
    out += '\'';
    return out;
}

// A plain SQL identifier: a letter or '_', then letters, digits and '_'; at most 63.
static auto is_safe_identifier(std::string_view s) -> bool
{
    if (s.empty() || s.size() > 63) return false;
    if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    return std::ranges::all_of(s, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
}

// Is the whole string a plain base-10 integer (optionally signed)? Used to
// decide whether a Number field can be emitted unquoted.
static auto is_plain_integer(const std::string& v) -> bool
{
    if (v.empty()) return false;
    long long n = 0;
    const char* first = v.data();
    const char* last  = v.data() + v.size();
    auto [ptr, ec] = std::from_chars(first, last, n);
    return ec == std::errc{} && ptr == last;
}

// Produce the SQL literal for one field value, driven by the field's declared
// TYPE (not by column position). This is schema-agnostic and injection-safe. A file
// holds no nulls (SPEC 2.6.1): {NULL} is the one value that becomes SQL NULL, for
// every type, and an empty value is ''.
//   - String : quoted+escaped.
//   - Number / Integer: a plain integer is emitted unquoted; any other text is
//              quoted+escaped defensively (the validator normally rejects
//              non-numeric values before we get here).
//   - Date / DateTime / GST : quoted+escaped (a date field always holds a date:
//              the validator refuses an empty one before we get here).
//   - Bool   : a Bool value -> TRUE / FALSE (SQL standard boolean literals);
//              anything else quoted+escaped defensively.
static auto sql_literal(const jtext::field& f, const std::string& val) -> std::string
{
    if (val == jtext::kNullValue) return "NULL";
    switch (f.type) {
    case jtext::field_type::bool_type: {
        const auto b = jtext::parse_bool_value(val);
        return b ? (*b ? "TRUE" : "FALSE") : sql_string_literal(val);
    }
    case jtext::field_type::integer_type:
    case jtext::field_type::number_type:
        return is_plain_integer(val) ? val : sql_string_literal(val);
    case jtext::field_type::date_type:
    case jtext::field_type::datetime_type:
    case jtext::field_type::gst_type:
    case jtext::field_type::string_type:
    default:
        return sql_string_literal(val);
    }
}

// Substitute {1}..{N} in the template body with correctly quoted literals.
// Field types come from the validated section schema; value position i maps to
// field i (1-based via {i+1}).
static auto substitute_record(const std::string& tmpl,
                              const jtext::record& rec,
                              const std::vector<jtext::field>& fields) -> std::string
{
    std::string out = tmpl;

    for (std::size_t i = 0; i < rec.values.size(); ++i) {
        static const jtext::field kStringField{};   // past the declared fields: String quoting (safe)
        const jtext::field& f = i < fields.size() ? fields[i] : kStringField;
        std::string placeholder = "{" + std::to_string(i + 1) + "}";
        std::string replacement = sql_literal(f, rec.values[i]);

        std::string::size_type pos = 0;
        while ((pos = out.find(placeholder, pos)) != std::string::npos) {
            out.replace(pos, placeholder.length(), replacement);
            pos += replacement.length();
        }
    }
    return out;
}

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::cerr << "Usage: jtext_xtox_sql <data_path> <base_name> <output_file>\n";
        std::cerr << "Example: jtext_xtox_sql ./samples workshop_tools ./out/workshop_tools.sql\n";
        return 1;
    }

    fs::path data_dir  = argv[1];
    std::string base   = argv[2];
    fs::path out_path  = argv[3];

    // Support multiple layouts for flexibility (including ts_store split logs and .jtFull for debugging):
    // - <data_path>/<base_name>/<base_name>.jtFull (classic)
    // - <data_path>/<base_name>.jtext (flat ts_store style main data file with embedded templates)
    // - <data_path>/<base_name>.jtFull
    fs::path input_file = data_dir / base / (base + ".jtFull");
    if (!fs::exists(input_file)) {
        input_file = data_dir / (base + ".jtext");
    }
    if (!fs::exists(input_file)) {
        input_file = data_dir / base / (base + ".jtext");
    }
    if (!fs::exists(input_file)) {
        input_file = data_dir / (base + ".jtFull");
    }

    std::string content = load_file(input_file);
    if (content.empty()) {
        std::cerr << "ERROR: Could not read " << input_file << "\n";
        return 2;
    }

    auto pf_or_err = jtext::parse_file_structure(content);
    if (!pf_or_err) {
        std::cerr << "PARSE ERROR at line " << pf_or_err.error().line_no
                  << ": " << pf_or_err.error().message << "\n";
        return 3;
    }

    auto result = jtext::validate(*pf_or_err);

    if (result.report.has_errors()) {
        std::cerr << "VALIDATION FAILED (" << result.report.error_count() << " errors):\n";
        for (const auto& issue : result.report.issues) {
            if (issue.severity == jtext::issue_severity::error) {
                std::cerr << "  - " << issue.where << ": " << issue.message << "\n";
            }
        }
        return 4;
    }

    // The header block as SQL comments, so the file runs as written (SPEC 2.1).
    // If the source jText carried table_name, emit the compact Related form.
    std::string rel_table = base;
    auto it = result.file.header.find("table_name");
    if (it != result.file.header.end() && !it->second.empty()) {
        rel_table = it->second;
    }
    std::ostringstream header;
    jText::v001::write_sql_comment_header(header, out_path.string(), "SQL Data File",
                                          rel_table.empty() ? "" : "type=jText table=" + rel_table);
    std::string output_sql = header.str();
    output_sql += "-- Generated from " + std::string(base) + ".jtFull\n";
    output_sql += "-- by jtext_xtox_sql\n";
    output_sql += "-- Note: Identity/auto columns are controlled by the templates in the jText file.\n";

    // Each section is one table (SPEC 2.3), with its own templates: "Create Table" once,
    // "SQL Insert" per record, and its comments in <table>_comments (SPEC 4.3).
    std::size_t inserts = 0;
    for (std::size_t s = 0; s < result.file.sections.size(); ++s) {
        const auto& sec = result.file.sections[s];
        const auto find = [&](std::string_view name) -> const std::string* {
            for (const auto& tpl : sec.templates)
                if (tpl.name == name) return &tpl.body;
            return nullptr;
        };
        const std::string* insert = find("SQL Insert");
        if (!insert) continue;
        // The data table's name names its comments table: the header's table_name for a
        // one-section file (as the templates use it), else the section's name; lower-cased,
        // as PostgreSQL folds an unquoted name.
        const bool one = result.file.sections.size() == 1;
        const bool header_table = one && it != result.file.header.end() && is_safe_identifier(it->second);
        std::string table = header_table ? it->second : is_safe_identifier(sec.name) ? sec.name : "";
        if (table.empty()) {
            std::cerr << "ERROR: section '" << sec.name << "' has no table name usable for its "
                      << "comments table (the header's table_name in a one-section file, or the "
                      << "section name, must be a plain SQL identifier)\n";
            return 5;
        }
        std::ranges::transform(table, table.begin(), [](unsigned char c) { return std::tolower(c); });

        output_sql += std::format("\n-- Section: {}\n", sec.name);
        if (const std::string* create = find("Create Table")) output_sql += *create + "\n";
        output_sql += std::format(
            "CREATE TABLE IF NOT EXISTS {0}_comments (\n"
            "    block     TEXT    NOT NULL,\n"
            "    kind      TEXT    NOT NULL,\n"
            "    record_id INTEGER NOT NULL,\n"
            "    position  INTEGER NOT NULL,\n"
            "    seq       INTEGER NOT NULL,\n"
            "    comment   TEXT    NOT NULL\n"
            ");\n"
            "CREATE TABLE IF NOT EXISTS {0}_fields (\n"
            "    position  INTEGER NOT NULL,\n"
            "    level     INTEGER NOT NULL,\n"
            "    text      TEXT    NOT NULL\n"
            ");\n", table);

        for (const auto& rec : sec.records) {
            // The record id is field 1 of every record (SPEC 2.4.3), so the
            // template's {1} already carries it; no id is synthesized here.
            output_sql += "\n" + substitute_record(*insert, rec, sec.fields);
            ++inserts;
        }
        // Each field's declaration, part by part, as written (SPEC 4.3): what the table's
        // own columns cannot hold (Not Empty, NullDate(...), an auto designation) comes back.
        for (const auto& fl : pf_or_err->sections[s].field_lines)
            for (std::size_t level = 0; level < fl.values.size(); ++level)
                output_sql += std::format("\nINSERT INTO {}_fields (position, level, text) VALUES ({}, {}, {});",
                                          table, fl.number, level + 1, sql_string_literal(fl.values[level]));
        for (const auto& c : jtext::section_comments(pf_or_err->sections[s])) {
            output_sql += std::format(
                "\nINSERT INTO {}_comments (block, kind, record_id, position, seq, comment) "
                "VALUES ({}, {}, {}, {}, {}, {});", table, sql_string_literal(c.block),
                sql_string_literal(c.kind), c.record_id, c.position, c.seq, sql_string_literal(c.text));
        }
        output_sql += "\n";
    }
    if (inserts == 0 && std::ranges::none_of(result.file.sections, [](const auto& sec) {
            return std::ranges::any_of(sec.templates, [](const auto& t) { return t.name == "SQL Insert"; });
        })) {
        std::cerr << "No 'SQL Insert' template found in file.\n";
        return 5;
    }

    // Write output
    fs::create_directories(out_path.parent_path());
    std::ofstream out(out_path);
    if (!out) {
        std::cerr << "ERROR: Could not write to " << out_path << "\n";
        return 6;
    }
    out << output_sql;

    std::cout << "Success. Wrote " << inserts << " INSERT statement(s) to " << out_path << "\n";
    return 0;
}
