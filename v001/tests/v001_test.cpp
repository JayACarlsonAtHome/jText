#include <jText/v001.hpp>

static_assert(jText::v001::version() == "v001.004", "v001 version");

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            return EXIT_FAILURE; \
        } \
    } while (0)

int main() {
    std::ostringstream out;
    jText::v001::JTextWriter writer(out);
    writer.set_purpose("jac313 smoke");
    writer.write_header();
    writer.begin_section("T");
    writer.append_row(1, {std::string("jac313")});
    writer.finalize();

    const std::string text = out.str();
    CHECK(text.find("jac313 smoke") != std::string::npos);
    CHECK(text.find("jac313") != std::string::npos);

    CHECK(jText::v001::JTextProfile::Light != jText::v001::JTextProfile::Full);
    CHECK(jText::v001::jtext_line_pad_width_for_count(10) >= 1);

    std::cout << "jText::v001 aliases jText API\n";
    return EXIT_SUCCESS;
}