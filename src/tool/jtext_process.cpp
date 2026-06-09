// File: src/tool/jtext_process.cpp
//
// Minimal jText processor (first cut).
// Invocation:
//   jtext_process <data_path> <base_name> <output_file>
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

namespace fs = std::filesystem;

static auto load_file(const fs::path& path) -> std::string
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    return content;
}

// Produce the SQL literal for one field value (prototype, sample-specific).
// This function is deliberately simple and does no database-specific magic.
// The author controls quoting, NULL handling, and whether identity columns
// are included via the templates they write in the jText file.
static auto sql_literal(std::size_t pos1, const std::optional<std::string>& val) -> std::string
{
    if (!val.has_value()) {
        return "NULL";                     // missing field
    }
    const std::string& v = *val;

    if (pos1 == 1) {
        // id - Number, Not Null
        return v.empty() ? "NULL" : v;
    }
    if (pos1 == 4) {
        // acquired - Date, Nullable
        if (v.empty()) return "NULL";
        return "'" + v + "'";
    }
    // All others are String (Nullable or Not Null)
    if (v.empty()) {
        return "''";                       // deliberate empty string for Nullable String
    }
    // Escape single quotes by doubling
    std::string esc;
    for (char c : v) {
        esc += c;
        if (c == '\'') esc += '\'';
    }
    return "'" + esc + "'";
}

// Substitute {1}..{N} in the template body with correctly quoted literals.
static auto substitute_record(const std::string& tmpl,
                              const jtext::record& rec) -> std::string
{
    std::string out = tmpl;

    for (std::size_t i = 0; i < rec.values.size(); ++i) {
        std::string placeholder = "{" + std::to_string(i + 1) + "}";
        std::string replacement = sql_literal(i + 1, rec.values[i]);

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
        std::cerr << "Usage: jtext_process <data_path> <base_name> <output_file>\n";
        std::cerr << "Example: jtext_process ./samples workshop_tools ./out/workshop_tools.sql\n";
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
    output_sql += "-- by jtext_process\n";
    output_sql += "-- Note: Identity/auto columns are controlled by the templates in the jText file.\n\n";

    bool first = true;
    for (const auto& sec : result.file.sections) {
        for (const auto& rec : sec.records) {
            if (!first) output_sql += "\n";
            first = false;
            jtext::record temp_rec = rec;
            if (rec.record_id > 0 && (rec.values.empty() || !rec.values[0].has_value() || std::stoll(*rec.values[0]) != static_cast<long long>(rec.record_id))) {
                temp_rec.values.insert(temp_rec.values.begin(), std::to_string(rec.record_id));
            }
            output_sql += substitute_record(insert_template_body, temp_rec);
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
