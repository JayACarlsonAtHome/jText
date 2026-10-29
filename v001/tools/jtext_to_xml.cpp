// File: tools/jtext_to_xml.cpp
//
// jtext_to_xml - the canonical XML export of a jText file (XML_EXPORT.md). Two files
// that hold the same data export to the same bytes, so every round trip compares
// through it.
//
//   jtext_to_xml [--data] <file> [<out.xml>]
//
// --data exports only fields, records and their comments (the scope for round trips
// through a database). Without <out.xml> the XML goes to stdout.

#include "export/xml_export.hpp"
#include "parser/section.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

int main(int argc, char** argv)
{
    std::vector<std::string_view> args(argv + 1, argv + argc);
    auto scope = jtext::xml_scope::full;
    if (!args.empty() && args.front() == "--data") {
        scope = jtext::xml_scope::data;
        args.erase(args.begin());
    }
    if (args.empty() || args.size() > 2) {
        std::cerr << "Usage: jtext_to_xml [--data] <file> [<out.xml>]\n";
        return 1;
    }

    std::ifstream in{std::string(args[0])};
    if (!in) {
        std::cerr << "ERROR: cannot read " << args[0] << "\n";
        return 2;
    }
    const auto pf = jtext::parse_file_structure(in);
    if (!pf) {
        std::cerr << "ERROR: " << args[0] << ": " << pf.error().to_string() << "\n";
        return 2;
    }
    const std::string xml = jtext::to_xml(*pf, scope);

    if (args.size() == 1) {
        std::cout << xml;
        return 0;
    }
    std::ofstream out{std::string(args[1]), std::ios::binary};
    if (!(out << xml)) {
        std::cerr << "ERROR: cannot write " << args[1] << "\n";
        return 3;
    }
    return 0;
}
