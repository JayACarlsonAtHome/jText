// File: tools/common/record_lines.hpp
//
// Writing a table that comes back from a store that keeps records and values but not the
// layout of lines (a database, a spreadsheet) as jText again, its comments each on its own
// line (SPEC 3.2.1, 4.3). Shared by jtext_from_sql and jtext_from_ods.

#pragma once

#include <jText/v002.hpp>          // JTextWriter
#include "export/xml_export.hpp"   // jtext::located_comment

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace jtools {

// The comments of one record (or of the Fields block), by the field their line starts at.
struct LineComments {
    std::map<unsigned, std::string> line;                  // the comment on the line
    std::map<unsigned, std::vector<std::string>> before;   // comment lines before it
    std::vector<std::string> end;                          // comment lines at the block's end
};

struct SectionComments {
    LineComments fields;
    std::map<std::int64_t, LineComments> records;   // by record id
    std::vector<std::string> data_end;              // comment lines at the end of the data
};

// `comments` in the order stores keep them: by block, record, position, seq.
inline SectionComments group_comments(const std::vector<jtext::located_comment>& comments)
{
    SectionComments g;
    for (const auto& c : comments) {
        const bool in_fields = c.block == "fields";
        if (!in_fields && c.position == 0) { g.data_end.push_back(c.text); continue; }
        auto& lc = in_fields ? g.fields : g.records[c.record_id];
        if (c.position == 0) lc.end.push_back(c.text);
        else if (c.kind == "line") lc.line[c.position] = c.text;
        else lc.before[c.position].push_back(c.text);
    }
    return g;
}

// One field declaration line: its levels, and the comment on it.
struct Declaration {
    std::vector<std::string> levels;
    std::string comment;
};

// The Fields block: each declaration with the comment lines before it, then those at its
// end. A declaration is split on '/' unless a level holds one; then the writer picks.
inline void write_fields(jText::v002::JTextWriter& w, const std::vector<Declaration>& fields,
                         const LineComments& lc)
{
    w.begin_fields();
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const auto p = static_cast<unsigned>(i + 1);
        if (auto it = lc.before.find(p); it != lc.before.end())
            for (const auto& t : it->second) w.write_comment_line(t);
        const bool slash = std::ranges::any_of(fields[i].levels,
                                               [](const std::string& l) { return l.find('/') != std::string::npos; });
        w.append_row(p, fields[i].levels, fields[i].comment, slash ? 0 : U'/');
    }
    for (const auto& t : lc.end) w.write_comment_line(t);
    w.end_fields();
}

// One record: a line from field 1, and a new line wherever one of its comments' lines
// starts, each comment back on its line.
inline void write_record(jText::v002::JTextWriter& w, const std::vector<std::string>& row, const LineComments& lc)
{
    std::vector<unsigned> starts{1};
    for (const auto& [p, _] : lc.line) starts.push_back(p);
    for (const auto& [p, _] : lc.before) starts.push_back(p);
    std::ranges::sort(starts);
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
    for (std::size_t s = 0; s < starts.size() && starts[s] <= row.size(); ++s) {
        const unsigned from = starts[s];
        const unsigned to = s + 1 < starts.size() ? starts[s + 1] : static_cast<unsigned>(row.size() + 1);
        if (auto it = lc.before.find(from); it != lc.before.end())
            for (const auto& t : it->second) w.write_comment_line(t);
        jText::v002::JTextEntry e;
        e.number = from;
        for (unsigned p = from; p < to && p <= row.size(); ++p) e.fields.emplace_back(row[p - 1]);
        if (auto it = lc.line.find(from); it != lc.line.end()) e.comment = it->second;
        w.append_entry(e);
    }
    w.end_record();
}

// The record id of a row: its field 1 as an integer (0 when it is none).
inline std::int64_t record_id(const std::vector<std::string>& row)
{
    std::int64_t id = 0;
    if (!row.empty()) std::from_chars(row[0].data(), row[0].data() + row[0].size(), id);
    return id;
}

}  // namespace jtools
