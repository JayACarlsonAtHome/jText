// File: tools/jtext_from_ods.cpp
//
// jtext_from_ods - an OpenDocument spreadsheet back into jText (ODS.md). Each data sheet
// is a section; its companion "<name> (jText)" gives the field declarations and the
// comments. A sheet without one (made by hand) takes its field names from row 1: field 1
// an Integer (the record id), the rest String.
//
//   jtext_from_ods <in.ods> <out.jtext>

#include "common/record_lines.hpp"
#include "ods/ods.hpp"
#include "validator/validator.hpp"   // parse_field_type, null_date

#include <charconv>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kCompanion = " (jText)";

auto cell_text(const std::vector<ods::Cell>& row, std::size_t i) -> std::string
{
    return i < row.size() ? row[i].text : std::string{};
}

auto number(const std::string& s) -> std::int64_t
{
    std::int64_t n = 0;
    std::from_chars(s.data(), s.data() + s.size(), n);
    return n;
}

// The null date of a declaration that is a Date or DateTime field, or "" for any other.
auto null_date_of(const std::vector<std::string>& levels) -> std::string
{
    jtext::field f;
    bool date = false;
    for (std::size_t i = 1; i < levels.size(); ++i) {
        const auto& l = levels[i];
        if (auto t = jtext::parse_field_type(l)) {
            f.type = *t;
            date = *t == jtext::field_type::date_type || *t == jtext::field_type::datetime_type;
        } else if (l.size() > 10 && l.starts_with("NullDate(") && l.ends_with(')')) {
            f.null_date = l.substr(9, l.size() - 10);
        }
    }
    return date ? jtext::null_date(f) : std::string{};
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "Usage: jtext_from_ods <in.ods> <out.jtext>\n";
        return 1;
    }
    const auto sheets = ods::read(argv[1]);
    if (!sheets) {
        std::cerr << "ERROR: " << sheets.error() << "\n";
        return 2;
    }
    std::map<std::string, const ods::Sheet*> companions;
    for (const auto& s : *sheets)
        if (s.name.ends_with(kCompanion)) companions[s.name.substr(0, s.name.size() - kCompanion.size())] = &s;

    std::ofstream out(argv[2]);
    if (!out) {
        std::cerr << "ERROR: cannot write " << argv[2] << "\n";
        return 3;
    }
    const std::string name = fs::path(argv[2]).filename().string();
    const std::string source = fs::path(argv[1]).filename().string();
    jText::v001::write_file_comment_header(out, name, "jText Data File", "type=ods file=" + source);
    jText::v001::JTextWriter w(out);
    w.set_profile(jText::v001::JTextProfile::Full);
    w.set_filename(name);
    w.set_purpose("Imported from " + source);
    w.write_header();

    std::size_t sections = 0;
    for (const auto& sheet : *sheets) {
        if (sheet.name.ends_with(kCompanion) || sheet.rows.empty()) continue;
        ++sections;

        // The declarations and comments, from the companion; or String fields from row 1.
        std::vector<jtools::Declaration> fields;
        std::vector<jtext::located_comment> comments;
        if (auto it = companions.find(sheet.name); it != companions.end()) {
            std::map<std::size_t, jtools::Declaration> by_position;
            for (const auto& row : it->second->rows) {
                const std::string kind = cell_text(row, 0);
                if (kind == "field") {
                    jtools::Declaration d{{}, cell_text(row, 2)};
                    for (std::size_t i = 3; i < row.size(); ++i) d.levels.push_back(row[i].text);
                    by_position[static_cast<std::size_t>(number(cell_text(row, 1)))] = std::move(d);
                } else if (kind == "comment") {
                    comments.push_back({cell_text(row, 1), cell_text(row, 2), number(cell_text(row, 3)),
                                        static_cast<unsigned>(number(cell_text(row, 4))),
                                        static_cast<unsigned>(number(cell_text(row, 5))), cell_text(row, 6)});
                }
            }
            for (auto& [_, d] : by_position) fields.push_back(std::move(d));
        } else {
            // Field 1 is the record id, always an integer (SPEC 2.4.3); the rest are text.
            for (const auto& c : sheet.rows[0]) fields.push_back({{c.text, fields.empty() ? "Integer" : "String"}, {}});
        }
        const auto grouped = jtools::group_comments(comments);
        std::vector<std::string> null_dates;
        for (const auto& d : fields) null_dates.push_back(null_date_of(d.levels));

        w.begin_section(sheet.name);
        jtools::write_fields(w, fields, grouped.fields);
        w.begin_data();
        static const jtools::LineComments kNone;
        for (std::size_t r = 1; r < sheet.rows.size(); ++r) {
            std::vector<std::string> row;
            for (std::size_t i = 0; i < fields.size(); ++i) {
                std::string v = cell_text(sheet.rows[r], i);
                // A date field always holds a date (SPEC 2.6.1).
                if (!null_dates[i].empty() && (v.empty() || v == jtext::kNullValue)) v = null_dates[i];
                row.push_back(std::move(v));
            }
            const auto it = grouped.records.find(jtools::record_id(row));
            jtools::write_record(w, row, it == grouped.records.end() ? kNone : it->second);
        }
        for (const auto& t : grouped.data_end) w.write_comment_line(t);
    }
    w.finalize();
    std::cout << "Wrote " << argv[2] << " (" << sections << " section(s))\n";
    return 0;
}
