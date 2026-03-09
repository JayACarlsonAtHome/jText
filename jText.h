#pragma once

#include <expected>
#include <string>
#include <iostream>
#include <vector>
#include <map>
#include <optional>
#include <fstream>
#include <span>
#include <print>
#include <format>
#include <ranges>
#include <algorithm>
#include <source_location>

struct JTextEntry {
    char delimiter = '#';
    std::string value;
    std::string comment;
};

struct JTextSection {
    std::string name;
    std::vector<JTextEntry> entries;
};

class JTextFile {
public:
    std::string date;
    std::string purpose;
    std::vector<JTextSection> sections;

    // Write full file
    auto write(std::string_view filename) const
        -> std::expected<void, std::string>;

    // Read full file
    auto read_full(std::string_view filename)
        -> std::expected<void, std::string>;

    // Read only one section by name
    auto read_section(std::string_view filename, std::string_view section_name)
        -> std::expected<JTextSection, std::string>;

private:
    static auto trim(std::string_view sv) -> std::string;

    static auto parse_entry(std::string_view line)
        -> std::expected<JTextEntry, std::string>;

    static void log_error(std::string_view msg,
                          const std::source_location loc = std::source_location::current());

    auto parse_stream(std::ifstream& in, bool full_read, std::string_view target_section = {})
        -> std::expected<void, std::string>;
};
