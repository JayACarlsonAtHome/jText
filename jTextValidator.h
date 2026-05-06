#pragma once

#include "jText.h"
#include <expected>
#include <string>

class JTextValidator {
public:
    static auto validate(const JTextFile& file) -> std::expected<void, std::string>;

private:
    static auto checkFieldList(const JTextFile& file) -> std::expected<void, std::string>;
    static auto checkDataSections(const JTextFile& file) -> std::expected<void, std::string>;
    static auto checkSQLMetadata(const JTextFile& file) -> std::expected<void, std::string>;
};
