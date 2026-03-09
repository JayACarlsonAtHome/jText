#include "jText.h"
#include <charconv>     // from_chars
#include <filesystem>   // for file existence (C++17 but useful)

namespace fs = std::filesystem;
using namespace std::literals;

// Helpers
auto JTextFile::trim(std::string_view sv) -> std::string {
    auto is_space = [](char c) { return std::isspace(static_cast<unsigned char>(c)); };
    auto first = std::ranges::find_if_not(sv, is_space);
    if (first == sv.end()) return {};
    auto last = std::ranges::find_if_not(sv | std::views::reverse, is_space).base();
    return std::string{first, last};
}

void JTextFile::log_error(std::string_view msg, const std::source_location loc) {
    std::println(stderr, "Error at {}:{} : {}",
                 loc.file_name(), loc.line(), msg);
}

auto JTextFile::parse_entry(std::string_view line) -> std::expected<JTextEntry, std::string> {
    if (line.size() < 4 || line[1] != '.' || line[2] != ' ') {
        return std::unexpected{"Missing or malformed number + dot + space"};
    }

    // Skip "N. "
    auto content = line.substr(3);

    if (content.empty()) {
        return std::unexpected{"No delimiter after N. "};
    }

    char delim = content[0];
    if (content.size() < 2 || content[1] != ' ') {
        return std::unexpected{std::format("Missing space after delimiter '{}'", delim)};
    }

    auto rest = content.substr(2);
    auto second_pos = rest.find(delim);

    std::string value, comment;
    if (second_pos != std::string_view::npos) {
        value  = trim(rest.substr(0, second_pos));
        comment = trim(rest.substr(second_pos + 1));
    } else {
        value = trim(rest);
    }

    return JTextEntry{delim, std::move(value), std::move(comment)};
}

// Write
auto JTextFile::write(std::string_view filename) const
    -> std::expected<void, std::string> {
    std::ofstream out{std::string{filename}};
    if (!out) {
        return std::unexpected{std::format("Cannot open file for writing: {}", filename)};
    }

    out << std::format("JText File - created {}\n", date);
    out << std::format("Purpose - {}\n\n", purpose);

    for (const auto& sec : sections) {
        if (sec.name == "Field List" || sec.name == "Data Section") {
            out << sec.name << '\n';
        } else {
            out << std::format("-- {} --\n", sec.name);
        }

        for (size_t i = 0; i < sec.entries.size(); ++i) {
            const auto& e = sec.entries[i];
            out << std::format("{}. {} {} ", i + 1, e.delimiter, e.value);

            if (!e.comment.empty()) {
                out << std::format("   {}{}", e.delimiter, e.comment);
            }
            out << '\n';
        }
        out << '\n';
    }

    out << "-- EOF --\n";
    return {};
}

// Read full
auto JTextFile::read_full(std::string_view filename)
    -> std::expected<void, std::string> {
    std::ifstream in{std::string{filename}};
    if (!in) {
        return std::unexpected{std::format("Cannot open file: {}", filename)};
    }
    return parse_stream(in, true);
}

// Read one section
auto JTextFile::read_section(std::string_view filename, std::string_view section_name)
    -> std::expected<JTextSection, std::string> {
    std::ifstream in{std::string{filename}};
    if (!in) {
        return std::unexpected{std::format("Cannot open file: {}", filename)};
    }

    sections.clear();  // reuse internal storage temporarily
    auto res = parse_stream(in, false, section_name);
    if (!res) return std::unexpected{res.error()};

    if (sections.empty()) {
        return std::unexpected{std::format("Section '{}' not found", section_name)};
    }

    auto sec = std::move(sections[0]);
    sections.clear();
    return sec;
}

// Core parser
auto JTextFile::parse_stream(std::ifstream& in, bool full_read, std::string_view target_section)
    -> std::expected<void, std::string> {
    std::string line;
    bool in_header = true;
    std::string cur_section;
    JTextSection* active = nullptr;

    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty()) continue;

        if (in_header) {
            if (line.starts_with("JText File - created "sv)) {
                date = trim(line.substr(21)).data();
                continue;
            }
            if (line.starts_with("Purpose - "sv)) {
                purpose = trim(line.substr(10)).data();
                in_header = false;
                continue;
            }
        }

        // Section headers
        if (line == "Field List" || line == "Data Section") {
            cur_section = line;
        } else if (line.starts_with("-- "sv) && line.ends_with(" --"sv)) {
            cur_section = trim(line.substr(3, line.size() - 6));
        } else if (line == "-- EOF --") {
            break;
        } else {
            // Assume entry line
            auto entry_res = parse_entry(line);
            if (!entry_res) {
                log_error(std::format("Invalid entry line: {}", line));
                continue;
            }

            if (!active || cur_section != active->name) {
                if (!full_read && !cur_section.empty() && cur_section != target_section) {
                    continue;  // skip until target
                }
                if (!full_read && cur_section == target_section) {
                    sections.emplace_back();
                    active = &sections.back();
                    active->name = cur_section;
                } else if (full_read) {
                    sections.emplace_back();
                    active = &sections.back();
                    active->name = cur_section;
                }
            }

            if (active) {
                active->entries.push_back(std::move(*entry_res));
            }
            continue;
        }

        // New section start
        if (full_read || cur_section == target_section) {
            sections.emplace_back();
            active = &sections.back();
            active->name = cur_section;
        }
    }

    if (!full_read && (sections.empty() || sections[0].name != target_section)) {
        return std::unexpected{std::format("Target section '{}' not found", target_section)};
    }

    return {};
}
