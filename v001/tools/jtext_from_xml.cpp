// File: tools/jtext_from_xml.cpp
//
// jtext_from_xml - the canonical XML export (XML_EXPORT.md) back into jText: the import
// side of XML interchange. Reads either scope: `full` (header, sections with their names,
// includes and templates) or `data` (fields and records only; each section is named
// "records", or "records_2", ...). Values and comments go back as they were exported, each
// comment on its own line, so exporting the result gives the same XML again.
//
//   jtext_from_xml <in.xml> <out.jtext>
//
// A header's related_files list has no setter in the jText writer; it is reported and left
// out.

#include "common/xml_import.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace fs = std::filesystem;
namespace xi = jtools::xml_import;

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "Usage: jtext_from_xml <in.xml> <out.jtext>\n";
        return 1;
    }
    std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)> doc(xmlReadFile(argv[1], nullptr, XML_PARSE_NONET), xmlFreeDoc);
    const xi::Node* root = doc ? xmlDocGetRootElement(doc.get()) : nullptr;
    if (!xi::name_is(root, "jtext")) {
        std::cerr << "ERROR: " << argv[1] << " is not a jText XML export (no <jtext> root)\n";
        return 2;
    }
    std::ofstream out(argv[2]);
    if (!out) {
        std::cerr << "ERROR: cannot write " << argv[2] << "\n";
        return 3;
    }
    const std::string name = fs::path(argv[2]).filename().string();
    const std::string source = fs::path(argv[1]).filename().string();
    const std::size_t sections = xi::write_jtext(root, out, name, source, "type=xml file=" + source);
    std::cout << "Wrote " << argv[2] << " (" << sections << " section(s))\n";
    if (xi::g_dropped)
        std::cerr << "Note: the header's related_files has no place in the jText writer and was left out\n";
    return 0;
}
