#include "jTextValidator.h"
#include <unordered_set>
#include <print>

auto JTextValidator::validate(const JTextFile& file) -> std::expected<void, std::string> {
    std::println("=== JText Validation Check ===");

    auto res1 = checkFieldList(file);
    if (!res1) return res1;

    auto res2 = checkDataSections(file);
    if (!res2) return res2;

    auto res3 = checkSQLMetadata(file);
    if (!res3) return res3;

    std::println("✅ Validation passed.");
    return {};
}

// ===================================================================

auto JTextValidator::checkFieldList(const JTextFile& file) -> std::expected<void, std::string> {
    bool has_field_list = false;
    size_t field_count = 0;
    std::unordered_set<std::string> name_set;
    std::vector<bool> number_set;

    for (const auto& sec : file.sections) {
        if (sec.name == "Field List") {
            has_field_list = true;
            field_count = sec.entries.size();
            number_set.resize(field_count + 1, false);

            for (const auto& e : sec.entries) {
                std::string name = JTextFile::trim(e.fields.empty() ? "" : e.fields[0]);

                if (name.empty()) {
                    return std::unexpected{std::format("✗ 2. Empty field name at position {}", e.number)};
                }
                if (name_set.contains(name)) {
                    return std::unexpected{std::format("✗ 2. Duplicate field name: {}", name)};
                }
                name_set.insert(name);

                if (e.number == 0 || e.number > field_count) {
                    return std::unexpected{std::format("✗ 2. Invalid field number {} in Field List", e.number)};
                }
                if (number_set[e.number]) {
                    return std::unexpected{std::format("✗ 2. Duplicate field number {}", e.number)};
                }
                number_set[e.number] = true;
            }
            break;
        }
    }

    if (!has_field_list) {
        return std::unexpected{"✗ 1. No Field List found"};
    }

    return {};
}

// ===================================================================

auto JTextValidator::checkDataSections(const JTextFile& file) -> std::expected<void, std::string> {
    size_t field_count = 0;
    for (const auto& sec : file.sections) {
        if (sec.name == "Field List") {
            field_count = sec.entries.size();
            break;
        }
    }

    for (const auto& sec : file.sections) {
        if (sec.name != "Data Section") continue;

        size_t last_num = 0;
        for (const auto& e : sec.entries) {
            if (e.number == 0) {
                return std::unexpected{"✗ 4. Data entry with number 0"};
            }
            if (e.number <= last_num) {
                return std::unexpected{std::format("✗ 4. Data numbers not strictly increasing ({} after {})",
                                                   e.number, last_num)};
            }
            if (e.number > last_num + 1) {
                std::println(stderr, "Warning: Skipped field number(s) in Data Section ({} after {})",
                             e.number, last_num);
            }
            if (e.fields.size() > field_count) {
                return std::unexpected{std::format("✗ 4. Entry {} has too many fields ({})",
                                                   e.number, e.fields.size())};
            }
            last_num = e.number;
        }
    }
    return {};
}

// ===================================================================

auto JTextValidator::checkSQLMetadata(const JTextFile& file) -> std::expected<void, std::string> {
    bool has_sql_dialect = !file.sql_dialect.empty();
    bool has_table_name = !file.table_name.empty();

    if (has_sql_dialect) {
        if (!has_table_name) {
            return std::unexpected{"✗ SQL Dialect is specified but Table Name is missing.\n"
                                   "   (Table Name is required when using SQL Dialect)"};
        }
        // Note: We do NOT validate the actual SQL types — user responsibility
    }

    return {};
}