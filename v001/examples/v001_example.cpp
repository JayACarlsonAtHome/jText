#include <jText/v001.hpp>

#include <iostream>
#include <sstream>
#include <string>

int main() {
    std::ostringstream out;
    jText::v001::JTextWriter writer(out);

    writer.set_date("2026-06-11");
    writer.set_purpose("jText::v001 example");
    writer.set_case_mode(jText::v001::CaseMode::Sensitive);
    writer.write_header();
    writer.begin_section("Users");
    writer.append_row(1, {std::string("Alice"), std::string("30")});
    writer.finalize();

    std::cout << out.str();
    return 0;
}