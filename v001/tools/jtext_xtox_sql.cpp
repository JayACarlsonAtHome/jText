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

#include "parser/section.hpp"
#include "validator/validator.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
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
// TYPE (not by column position). This is schema-agnostic and injection-safe:
//   - String : missing(nullopt) -> NULL; empty -> '' (preserved); else quoted+escaped.
//   - Number : missing/empty -> NULL; a plain integer is emitted unquoted; any
//              other text is quoted+escaped defensively (the validator normally
//              rejects non-numeric Number values before we get here).
//   - Date   : missing/empty -> NULL; always quoted+escaped (never raw).
static auto sql_literal(jtext::field_type type, const std::optional<std::string>& val) -> std::string
{
    switch (type) {
    case jtext::field_type::number_type:
        if (!val.has_value() || val->empty()) return "NULL";
        return is_plain_integer(*val) ? *val : sql_string_literal(*val);
    case jtext::field_type::date_type:
        if (!val.has_value() || val->empty()) return "NULL";
        return sql_string_literal(*val);
    case jtext::field_type::string_type:
    default:
        if (!val.has_value()) return "NULL";   // missing field
        if (val->empty())     return "''";     // deliberate empty string for Nullable String
        return sql_string_literal(*val);
    }
}

// Substitute {1}..{N} in the template body with correctly quoted literals.
// Field types come from the validated section schema; value position i maps to
// field i (1-based via {i+1}). Positions beyond the declared fields default to
// String quoting (safe).
static auto substitute_record(const std::string& tmpl,
                              const jtext::record& rec,
                              const std::vector<jtext::field>& fields) -> std::string
{
    std::string out = tmpl;

    for (std::size_t i = 0; i < rec.values.size(); ++i) {
        const jtext::field_type type =
            (i < fields.size()) ? fields[i].type : jtext::field_type::string_type;
        std::string placeholder = "{" + std::to_string(i + 1) + "}";
        std::string replacement = sql_literal(type, rec.values[i]);

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

    // Find the "SQL Insert" template (first one for now)
    std::string insert_template_body;
    for (const auto& sec : result.file.sections) {
        for (const auto& tpl : sec.templates) {
            if (tpl.name == "SQL Insert") {
                insert_template_body = tpl.body;
                break;
            }
        }
        if (!insert_template_body.empty()) break;
    }

    if (insert_template_body.empty()) {
        std::cerr << "No 'SQL Insert' template found in file.\n";
        return 5;
    }

    // Emit one INSERT per record using the template
    std::string output_sql;
    auto now = std::chrono::system_clock::now();
    auto today = std::chrono::floor<std::chrono::days>(now);
    std::string date_str = std::format("{:%Y-%m-%d}", today);
    output_sql += "//File:    " + std::string(base) + ".sql\n";
    output_sql += "//Date:    " + date_str + "\n";
    output_sql += "//Purpose: SQL Data File\n";
    // If the source jText carried table_name, emit the compact Related form.
    std::string rel_table = base;
    auto it = result.file.header.find("table_name");
    if (it != result.file.header.end() && !it->second.empty()) {
        rel_table = it->second;
    }
    if (!rel_table.empty()) {
        output_sql += "//Related: type=jText table=" + rel_table + "\n";
    }
    output_sql += "//\n";
    output_sql += "-- Generated from " + std::string(base) + ".jtFull\n";
    output_sql += "-- by jtext_xtox_sql\n";
    output_sql += "-- Note: Identity/auto columns are controlled by the templates in the jText file.\n\n";

    bool first = true;
    for (const auto& sec : result.file.sections) {
        for (const auto& rec : sec.records) {
            if (!first) output_sql += "\n";
            first = false;
            jtext::record temp_rec = rec;
            // ts_store-style logs carry the event id as record_id; surface it as
            // field 1 unless the record already leads with that exact integer.
            // (Parse defensively: a non-numeric leading value must not throw.)
            const bool leads_with_id =
                !rec.values.empty() && rec.values[0].has_value() &&
                is_plain_integer(*rec.values[0]) &&
                std::stoll(*rec.values[0]) == static_cast<long long>(rec.record_id);
            if (rec.record_id > 0 && !leads_with_id) {
                temp_rec.values.insert(temp_rec.values.begin(), std::to_string(rec.record_id));
            }
            output_sql += substitute_record(insert_template_body, temp_rec, sec.fields);
        }
    }

    // Write output
    fs::create_directories(out_path.parent_path());
    std::ofstream out(out_path);
    if (!out) {
        std::cerr << "ERROR: Could not write to " << out_path << "\n";
        return 6;
    }
    out << output_sql;

    std::cout << "Success. Wrote " << result.file.sections[0].records.size()
              << " INSERT statement(s) to " << out_path << "\n";
    return 0;
}
