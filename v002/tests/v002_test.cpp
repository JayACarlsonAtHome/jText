#include <jText/v002.hpp>

static_assert(jText::v002::version() == "v002.004", "v002 version");

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
    jText::v002::JTextWriter writer(out);
    writer.set_purpose("jac313 smoke");
    writer.write_header();
    writer.begin_section("T");
    writer.append_row(1, {std::string("jac313")});
    writer.finalize();

    const std::string text = out.str();
    CHECK(text.find("jac313 smoke") != std::string::npos);
    CHECK(text.find("jac313") != std::string::npos);

    CHECK(jText::v002::JTextProfile::Light != jText::v002::JTextProfile::Full);
    CHECK(jText::v002::jtext_line_pad_width_for_count(10) >= 1);

    std::cout << "jText::v002 aliases jText API\n";
    return EXIT_SUCCESS;
}