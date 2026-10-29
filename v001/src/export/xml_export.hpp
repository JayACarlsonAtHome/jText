// File: src/export/xml_export.hpp
//
// The canonical XML export of a jText file (XML_EXPORT.md): one XML text for one set
// of data, written exactly one way, so two sources holding the same data export to
// byte-identical XML. Every round trip (jText -> database -> jText, ...) is compared
// through it.

#pragma once

#include "parser/section.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace jtext {

// The version of XML_EXPORT.md this exporter writes.
inline constexpr std::string_view kXmlExportVersion = "0.1";

enum class xml_scope : std::uint8_t {
    full,   // header, sections with names, includes, templates, fields, records, comments
    data,   // per section: fields and records with their comments only
};

// The canonical XML of `pf` in `scope`.
auto to_xml(const parsed_file& pf, xml_scope scope) -> std::string;

// A comment of a section, named the way a store that keeps records and values but not
// the layout of lines names it (SPEC 3.2.1, 4.3): the comment on a line by its record
// and the field the line starts at; a comment line by the line it stands before, or as
// standing at the end of its block. One row of a database's <table>_comments table.
struct located_comment {
    std::string   block;          // "fields" or "data"
    std::string   kind;           // "line" or "comment-line"
    std::int64_t  record_id = 0;  // data block: field 1 of the record; fields block: 0
    unsigned      position = 0;   // the field the line starts at; 0: at the end of the block
    unsigned      seq = 0;        // comment lines standing together: 1, 2, ...; a line's comment: 0
    std::string   text;
};

// Every comment of `ps`: the Fields block's first, then the data block's, in file order.
auto section_comments(const parsed_section& ps) -> std::vector<located_comment>;

}  // namespace jtext
