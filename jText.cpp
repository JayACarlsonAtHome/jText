#include "jText.h"
#include <charconv>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <unordered_set>

namespace fs = std::filesystem;
using namespace std::literals;

// ===================================================================
// Helpers
// ===================================================================

auto JTextFile::trim(std::string_view sv) -> std::string {
    if (sv.empty()) return {};

    auto is_space = [](char c) {
        return std::isspace(static_cast<unsigned char>(c));
    };

    auto first = std::find_if_not(sv.begin(), sv.end(), is_space);
    if (first == sv.end()) return {};

    auto last = std::find_if_not(sv.rbegin(), sv.rend(), is_space).base();
    return std::string(first, last);
}

void JTextFile::log_error(std::string_view msg, const std::source_location loc) {
    std::println(stderr, "Error at {}:{} : {}", loc.file_name(), loc.line(), msg);
}

// ===================================================================
// Header parsing
// ===================================================================

auto JTextFile::parse_header(std::string_view line) -> bool {
    if (line.starts_with("JText File - created "sv)) {
        date = trim(line.substr(21));
        return true;
    }
    if (line.starts_with("Purpose - "sv)) {
        purpose = trim(line.substr(10));
        return true;
    }
    if (line.starts_with("AutoID:"sv)) {
        auto value = trim(line.substr(7));
        if (value == "yes" || value == "YES" || value == "true") {
            auto_id = true;
        } else if (value == "no" || value == "NO" || value == "false") {
            auto_id = false;
        }
        return true;
    }
    if (line.starts_with("Case:"sv)) {
        auto value = trim(line.substr(5));
        if (value == "INSENSITIVE" || value == "insensitive") {
            case_mode = CaseMode::Insensitive;
        } else if (value == "Sensitive" || value == "sensitive") {
            case_mode = CaseMode::Sensitive;
        } else {
            log_error("Unknown Case value: " + std::string(value));
        }
        return true;
    }

    // More robust SQL metadata parsing
    if (line.starts_with("SQL Dialect:"sv)) {
        sql_dialect = trim(line.substr(line.find(':') + 1));
        return true;
    }
    if (line.starts_with("Table Name:"sv)) {
        table_name = trim(line.substr(line.find(':') + 1));
        return true;
    }

    return false;
}
// ===================================================================
// parse_entry - new marker format
// ===================================================================

auto JTextFile::parse_entry(std::string_view line) -> std::expected<JTextEntry, std::string> {
    if (line.size() < 4 || line[1] != '.' || line[2] != ' ') {
        return std::unexpected{"Missing 'N. ' prefix"};
    }

    // Parse entry number
    size_t num = 0;
    auto [ptr, ec] = std::from_chars(line.data(), line.data() + line.size(), num);
    if (ec != std::errc{} || *ptr != '.') {
        return std::unexpected{"Invalid entry number"};
    }

    std::string_view rest(ptr + 2); // after ". "

    JTextEntry e;
    e.number = num;
    e.delimiter = '#';

    // === New Format: #X# ... ===
    if (rest.size() >= 4 && rest[0] == '#' && rest[3] == '#') {
        char level_sep = rest[1];
        bool is_flat = (level_sep == '-');
        e.level_sep = is_flat ? 0 : level_sep;

        std::string_view content = rest.substr(4);
        size_t last_hash = content.rfind('#');
        if (last_hash == std::string_view::npos) {
            return std::unexpected{"Missing final '#' for comment"};
        }

        std::string_view data_part = trim(content.substr(0, last_hash));
        std::string_view comment_part = trim(content.substr(last_hash + 1));

        if (data_part == "NULL" || data_part == "null" || data_part == "Null") {
            e.is_null = true;
            e.fields = {"NULL"};
        } else if (!data_part.empty()) {
            if (!is_flat && level_sep != 0) {
                std::stringstream ss(std::string{data_part});
                std::string token;
                while (std::getline(ss, token, level_sep)) {
                    e.fields.push_back(trim(token));
                }
            } else {
                e.fields.push_back(std::string{data_part});
            }
        }
        e.comment = comment_part;
    }
    // === Clean Field List format: "1. # Name # comment" ===
    else if (rest.starts_with("# ")) {
        std::string_view content = rest.substr(2);
        size_t last_hash = content.rfind('#');
        if (last_hash != std::string_view::npos) {
            e.fields.push_back(trim(content.substr(0, last_hash)));
            e.comment = trim(content.substr(last_hash + 1));
        } else {
            e.fields.push_back(trim(content));
        }
    }
    // Generic fallback
    else {
        size_t last_hash = rest.rfind('#');
        if (last_hash != std::string_view::npos) {
            e.fields.push_back(trim(rest.substr(0, last_hash)));
            e.comment = trim(rest.substr(last_hash + 1));
        } else {
            e.fields.push_back(trim(rest));
        }
    }

    // Case normalization
    if (case_mode == CaseMode::Insensitive && !e.is_null && !e.fields.empty()) {
        for (auto& f : e.fields) {
            std::transform(f.begin(), f.end(), f.begin(), ::toupper);
        }
        if (!e.comment.empty()) {
            std::transform(e.comment.begin(), e.comment.end(), e.comment.begin(), ::toupper);
        }
    }

    return e;
}
// ===================================================================
// Special Block
// ===================================================================

auto JTextFile::parse_special_block(std::ifstream& in, JTextSection& sec)
    -> std::expected<void, std::string>
{
    std::string data_line, comment_line, closer_line;

    if (!std::getline(in, data_line))
        return std::unexpected{"Missing data line after *** ^^^ ###"};

    if (!std::getline(in, comment_line))
        return std::unexpected{"Missing line after data in special block"};

    if (!std::getline(in, closer_line))
        return std::unexpected{"Missing closer ^^^"};

    JTextEntry e;
    e.is_special_block = true;
    e.fields = {trim(data_line)};

    if (comment_line.starts_with("###")) {
        e.comment = trim(comment_line.substr(3));
    }

    if (trim(closer_line) != "^^^") {
        log_error("Special block missing proper ^^^ closer");
    }

    sec.entries.push_back(std::move(e));
    return {};
}

// ===================================================================
// Main Parser
// ===================================================================

auto JTextFile::parse_stream(std::ifstream& in, bool full_read, std::string_view target_section)
    -> std::expected<void, std::string>
{
    std::string line;
    bool in_header = true;
    std::string cur_section;
    JTextSection* active = nullptr;

    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty()) continue;

        // Skip comment lines in header area
        if (in_header && line.starts_with('#')) {
            continue;
        }

        if (in_header) {
            if (parse_header(line)) {
                continue;
            }
            // Exit header mode when we hit first real section
            if (line == "Field List" || line == "Data Section" ||
                (line.starts_with("-- ") && line.ends_with(" --"))) {
                in_header = false;
                }
        }
        // Section header
        if (line == "Field List" || line == "Data Section" ||
            (line.starts_with("-- ") && line.ends_with(" --"))) {

            cur_section = (line.starts_with("-- "))
                ? trim(line.substr(3, line.size() - 6))
                : line;

            if (full_read || cur_section == target_section) {
                sections.emplace_back();
                active = &sections.back();
                active->name = cur_section;
            }
            continue;
            }

        if (line == "-- EOF --") break;

        // Special block
        if (line.ends_with("*** ^^^ ###")) {
            if (!active) {
                sections.emplace_back();
                active = &sections.back();
                active->name = cur_section.empty() ? "Data Section" : cur_section;
            }
            if (auto res = parse_special_block(in, *active); !res) {
                return res;
            }
            continue;
        }

        // Normal entry
        if (auto res = parse_entry(line); res) {
            if (active) {
                active->entries.push_back(std::move(*res));
            }
        } else {
            log_error(std::format("Invalid entry: {}", line));
        }
    }

    return {};
}
// ===================================================================
// Public API
// ===================================================================

auto JTextFile::read_full(std::string_view filepath) -> std::expected<void, std::string> {
    std::ifstream in(std::string{filepath});
    if (!in) return std::unexpected{"Cannot open file: " + std::string(filepath)};

    this->filename = std::string{filepath};   // ← store it

    return parse_stream(in, true);
}

auto JTextFile::read_section(std::string_view filepath, std::string_view section_name)
    -> std::expected<JTextSection, std::string>
{
    std::ifstream in(std::string{filepath});
    if (!in) {
        return std::unexpected{"Cannot open file: " + std::string(filepath)};
    }

    sections.clear();  // reuse internal storage

    auto res = parse_stream(in, false, section_name);
    if (!res) {
        return std::unexpected{res.error()};
    }

    if (sections.empty() || sections[0].name != section_name) {
        return std::unexpected{std::format("Section '{}' not found", section_name)};
    }

    JTextSection result = std::move(sections[0]);
    sections.clear();
    return result;
}

auto JTextFile::write(std::string_view filepath) const -> std::expected<void, std::string> {
    std::string target = std::string{filepath};
    if (target.empty() && !filename.empty()) {
        target = filename;
    }
    if (target.empty()) {
        return std::unexpected{"No filename provided"};
    }

    std::ofstream out(target);
    if (!out) return std::unexpected{"Cannot open file for writing: " + target};

    // === Clean # comment-style header ===
    out << std::format("# JText File - created {}\n", date);
    out << std::format("# Filename: {}\n", target);
    out << std::format("# Purpose - {}\n", purpose);
    out << std::format("# Case: {}\n",
        case_mode == CaseMode::Insensitive ? "INSENSITIVE" : "Sensitive");

    if (!sql_dialect.empty()) {
        out << std::format("# SQL Dialect: {}\n", sql_dialect);
    }
    if (!table_name.empty()) {
        out << std::format("# Table Name: {}\n", table_name);
    }
    if (!sql_dialect.empty()) {
        out << std::format("# AutoID: {}\n", auto_id ? "yes" : "no");
    }

    if (!sql_dialect.empty()) {
        out << "# Note: Field types are user-defined and not validated. "
               "The SQL generator will use them as provided.\n";
    }

    out << "\n";

    // === Sections ===
    for (const auto& sec : sections) {
        if (sec.name == "Field List" || sec.name == "Data Section") {
            out << sec.name << '\n';
        } else {
            out << std::format("-- {} --\n", sec.name);
        }

        if (sec.name == "Field List") {
            // Aligned Field List
            size_t max_len = 0;
            for (const auto& e : sec.entries) {
                if (!e.fields.empty()) {
                    max_len = std::max(max_len, e.fields[0].length());
                }
            }

            for (const auto& e : sec.entries) {
                out << std::format("{}. # ", e.number);
                std::string name = e.fields.empty() ? "" : e.fields[0];
                out << name;
                out << std::string(max_len + 5 - name.length(), ' ');

                if (!e.comment.empty()) {
                    out << "# " << e.comment;
                } else {
                    out << "# Field type not validated, used as is by SQL Generator";
                }
                out << '\n';
            }
        } else {
            for (const auto& e : sec.entries) {
                if (e.is_special_block) {
                    out << "*** ^^^ ###\n";
                    out << e.fields[0] << '\n';
                    if (!e.comment.empty()) out << "### " << e.comment << '\n';
                    out << "^^^" << '\n';
                    continue;
                }

                out << std::format("{}. ", e.number);

                char sep = (e.level_sep == 0) ? '-' : e.level_sep;
                out << "#" << sep << "# ";

                for (size_t i = 0; i < e.fields.size(); ++i) {
                    if (i > 0) out << e.level_sep;
                    out << e.fields[i];
                }

                if (!e.comment.empty()) {
                    out << " # " << e.comment;
                }
                out << '\n';
            }
        }
        out << '\n';
    }

    out << "-- EOF --\n";
    return {};
}

auto JTextFile::validate() const -> std::expected<void, std::string> {
    std::vector<std::string> checks;
    size_t field_count = 0;
    bool has_field_list = false;
    bool has_data_section = false;
    bool field_list_before_data = true;

    std::println("=== JText Validation Check ===");

    // 1. Field List Exists?
    for (const auto& sec : sections) {
        if (sec.name == "Field List") {
            has_field_list = true;
            field_count = sec.entries.size();
            break;
        }
    }
    checks.push_back(has_field_list ? "✓ 1. Field List exists" : "✗ 1. No Field List found");

    if (!has_field_list) {
        for (const auto& s : checks) std::println("{}", s);
        return std::unexpected{"Validation failed: No Field List section."};
    }

    // 2. Field List has unique names and numbers
    std::unordered_set<std::string> name_set;
    std::vector<bool> number_set(field_count + 1, false);
    bool field_list_valid = true;

    for (const auto& sec : sections) {
        if (sec.name != "Field List") continue;

        for (const auto& e : sec.entries) {
            std::string name = trim(e.fields.empty() ? "" : e.fields[0]);

            if (name.empty()) {
                checks.push_back("✗ 2. Empty field name in Field List");
                field_list_valid = false;
            }
            if (name_set.contains(name)) {
                checks.push_back("✗ 2. Duplicate field name: " + name);
                field_list_valid = false;
            }
            name_set.insert(name);

            if (e.number == 0 || e.number > field_count) {
                checks.push_back(std::format("✗ 2. Invalid field number {} in Field List", e.number));
                field_list_valid = false;
            } else if (number_set[e.number]) {
                checks.push_back(std::format("✗ 2. Duplicate field number {}", e.number));
                field_list_valid = false;
            }
            number_set[e.number] = true;
        }
    }
    checks.push_back(field_list_valid ? "✓ 2. Field List has unique names and numbers"
                                      : "✗ 2. Field List has errors");

    if (!field_list_valid) {
        for (const auto& s : checks) std::println("{}", s);
        return std::unexpected{"Validation failed in Field List."};
    }

    // 3. Field List appears before any Data Section
    for (const auto& sec : sections) {
        if (sec.name == "Field List") break;
        if (sec.name == "Data Section") {
            field_list_before_data = false;
            break;
        }
    }
    checks.push_back(field_list_before_data ? "✓ 3. Field List appears before Data Section"
                                             : "✗ 3. Field List must appear before Data Section");

    // 4. Data Section (optional)
    for (const auto& sec : sections) {
        if (sec.name == "Data Section") {
            has_data_section = true;
            size_t last_num = 0;

            for (const auto& e : sec.entries) {
                if (e.number == 0) {
                    checks.push_back("✗ 4. Data entry with number 0");
                }
                if (e.number <= last_num) {
                    checks.push_back(std::format("✗ 4. Data numbers not strictly increasing ({} after {})",
                                                 e.number, last_num));
                }
                if (e.number > last_num + 1) {
                    std::println(stderr, "Warning: Skipped field number(s) in Data Section ({} after {})",
                                 e.number, last_num);
                }
                if (e.fields.size() > field_count) {
                    checks.push_back(std::format("✗ 4. Entry {} has too many fields ({})",
                                                 e.number, e.fields.size()));
                }
                last_num = e.number;
            }
        }
    }

    checks.push_back(has_data_section ? "✓ 4. Data Section present"
                                      : "✓ 4. No Data Section (optional - OK)");

    // Print all checks
    for (const auto& s : checks) {
        std::println("{}", s);
    }

    std::println("✅ Validation passed.");
    return {};
}
