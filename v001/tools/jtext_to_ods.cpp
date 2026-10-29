// File: tools/jtext_to_ods.cpp
//
// jtext_to_ods - a jText file as an OpenDocument spreadsheet (ODS.md): per section a data
// sheet (field names, then one row per record) and a companion sheet "<name> (jText)"
// with the field declarations and the comments, so jtext_from_ods gets all of it back.
//
//   jtext_to_ods <file> <out.ods>

#include "export/xml_export.hpp"   // jtext::section_comments
#include "ods/ods.hpp"
#include "parser/section.hpp"
#include "validator/validator.hpp"

#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

auto text(std::string s) -> ods::Cell { return {ods::cell_kind::text, std::move(s)}; }

// A plain decimal number, as office:value takes it: -12, 3.50, 1e3 (no '+', no spaces).
auto plain_number(const std::string& s) -> bool
{
    std::size_t i = s.starts_with('-') ? 1 : 0;
    const auto digits = [&] {
        const auto from = i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        return i > from;
    };
    if (!digits()) return false;
    if (i < s.size() && s[i] == '.') { ++i; if (!digits()) return false; }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
        if (!digits()) return false;
    }
    return i == s.size();
}

// A value as its cell (ODS.md 3.1).
auto cell(const jtext::field& f, const std::string& v) -> ods::Cell
{
    if (v.empty()) return {};
    if ((f.type == jtext::field_type::integer_type || f.type == jtext::field_type::number_type) && plain_number(v))
        return {ods::cell_kind::number, v};
    if (f.type == jtext::field_type::date_type && jtext::looks_like_date(v))
        return {ods::cell_kind::date, v};
    return text(v);
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "Usage: jtext_to_ods <file> <out.ods>\n";
        return 1;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "ERROR: cannot read " << argv[1] << "\n";
        return 2;
    }
    const auto pf = jtext::parse_file_structure(in);
    if (!pf) {
        std::cerr << "ERROR: " << argv[1] << ": " << pf.error().to_string() << "\n";
        return 2;
    }
    const auto vr = jtext::validate(*pf, jtext::validation_level::minimal);

    std::vector<ods::Sheet> sheets;
    for (std::size_t s = 0; s < pf->sections.size(); ++s) {
        const auto& ps = pf->sections[s];
        const auto& vs = vr.file.sections.at(s);
        if (ps.name.find_first_of("[]*?:/\\") != std::string::npos) {
            std::cerr << "ERROR: section '" << ps.name << "': a sheet name cannot hold [ ] * ? : / \\\n";
            return 3;
        }
        if (vs.fields.empty()) {
            std::cerr << "ERROR: section '" << ps.name << "' declares no fields (an include the tool "
                      << "cannot read?); a sheet needs its columns\n";
            return 3;
        }

        // The data sheet: field names, then one row per record.
        ods::Sheet data{ps.name, {}};
        std::vector<ods::Cell> names;
        for (const auto& f : vs.fields) names.push_back(text(f.name));
        data.rows.push_back(std::move(names));
        for (std::size_t r = 0; r < vs.records.size(); ++r) {
            std::vector<ods::Cell> row;
            for (std::size_t i = 0; i < vs.fields.size() && i < vs.records[r].values.size(); ++i) {
                const auto& v = vs.records[r].values[i];
                if (!ods::xml_can_hold(v)) {
                    std::cerr << std::format("ERROR: section '{}', record {}, field {}: the value holds a "
                                             "character a spreadsheet cannot store\n", ps.name, r + 1, i + 1);
                    return 3;
                }
                row.push_back(cell(vs.fields[i], v));
            }
            data.rows.push_back(std::move(row));
        }

        // The companion sheet: declarations and comments (ODS.md 3.2).
        ods::Sheet meta{ps.name + " (jText)", {}};
        meta.rows.push_back({text("jText"), text(ps.name), text(std::string{jtext::kFormatVersion})});
        for (const auto& fl : ps.field_lines) {
            std::vector<ods::Cell> row{text("field"), text(std::to_string(fl.number)), text(fl.comment)};
            for (const auto& level : fl.values) row.push_back(text(level));
            meta.rows.push_back(std::move(row));
        }
        for (const auto& c : jtext::section_comments(ps)) {
            if (!ods::xml_can_hold(c.text)) {
                std::cerr << "ERROR: section '" << ps.name << "': a comment holds a character a "
                          << "spreadsheet cannot store\n";
                return 3;
            }
            meta.rows.push_back({text("comment"), text(c.block), text(c.kind), text(std::to_string(c.record_id)),
                                 text(std::to_string(c.position)), text(std::to_string(c.seq)), text(c.text)});
        }
        sheets.push_back(std::move(data));
        sheets.push_back(std::move(meta));
    }

    if (auto w = ods::write(argv[2], sheets); !w) {
        std::cerr << "ERROR: " << w.error() << "\n";
        return 4;
    }
    std::cout << "Wrote " << argv[2] << " (" << sheets.size() / 2 << " section(s))\n";
    return 0;
}
