#pragma once

#include <expected>
#include <string>
#include <vector>
#include <optional>
#include <fstream>
#include <print>
#include <format>
#include <source_location>

enum class CaseMode { Insensitive, Sensitive };

struct JTextEntry {
    size_t number = 0;
    char delimiter = '#';
    char level_sep = 0;           // 0 = flat, otherwise separator char
    std::vector<std::string> fields;
    std::string comment;
    bool is_null = false;
    bool is_special_block = false;
};

struct JTextSection {
    std::string name;
    std::vector<JTextEntry> entries;
};

class JTextFile {
public:
    std::string date;
    std::string purpose;
    CaseMode case_mode = CaseMode::Insensitive;
    std::string sql_dialect;
    std::string table_name;
    std::string filename;
    bool auto_id = true;

    std::vector<JTextSection> sections;

    auto write(std::string_view filepath) const -> std::expected<void, std::string>;
    auto read_full(std::string_view filepath) -> std::expected<void, std::string>;
    auto read_section(std::string_view filepath, std::string_view section_name)
        -> std::expected<JTextSection, std::string>;

    auto validate() const -> std::expected<void, std::string>;

    static auto trim(std::string_view sv) -> std::string;

private:
    static void log_error(std::string_view msg, const std::source_location loc = std::source_location::current());

    auto parse_header(std::string_view line) -> bool;
    auto parse_entry(std::string_view line) -> std::expected<JTextEntry, std::string>;
    auto parse_special_block(std::ifstream& in, JTextSection& sec) -> std::expected<void, std::string>;

    auto parse_stream(std::ifstream& in, bool full_read, std::string_view target_section = {})
        -> std::expected<void, std::string>;
};