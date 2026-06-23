#include <jac313/jText/v002.hpp>

#include <iostream>
#include <sstream>
#include <string>

int main() {
    std::ostringstream out;
    jac313::jText::v002::JTextWriter writer(out);

    writer.set_date("2026-06-11");
    writer.set_purpose("jac313::jText::v002 example");
    writer.set_case_mode(jac313::jText::v002::CaseMode::Sensitive);
    writer.write_header();
    writer.begin_section("Users");
    writer.append_row(1, {std::string("Alice"), std::string("30")});
    writer.finalize();

    std::cout << out.str();
    return 0;
}