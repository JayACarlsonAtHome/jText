// File: tools/ods/ods.hpp
//
// An OpenDocument spreadsheet (.ods) as sheets of cells (ODS.md): written and read
// through libzip (the archive) and libxml2 (content.xml). Never CSV.

#pragma once

#include <expected>
#include <string>
#include <vector>

namespace ods {

enum class cell_kind { empty, text, number, date };

struct Cell {
    cell_kind kind = cell_kind::empty;
    std::string text;   // a text cell's text; a number's office:value; a date's office:date-value
};

struct Sheet {
    std::string name;
    std::vector<std::vector<Cell>> rows;
};

// Writes `sheets` as an OpenDocument 1.3 spreadsheet at `path`. A text cell's text may
// hold line breaks (each line a paragraph), runs of spaces and tabs; it must not hold a
// character XML cannot (the caller refuses those).
auto write(const std::string& path, const std::vector<Sheet>& sheets) -> std::expected<void, std::string>;

// The sheets of the spreadsheet at `path`, repeated cells and rows expanded, trailing
// empty cells and empty rows left out.
auto read(const std::string& path) -> std::expected<std::vector<Sheet>, std::string>;

// True when XML 1.0 can hold every character of `s` (XML_EXPORT.md 3.2).
auto xml_can_hold(const std::string& s) -> bool;

}  // namespace ods
