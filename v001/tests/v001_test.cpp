#include <jText/v001.hpp>

static_assert(jText::v001::version() == "v001.007", "v001 version");

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            return EXIT_FAILURE; \
        } \
    } while (0)

namespace {

using jText::v001::JTextEntry;
using jText::v001::JTextWriter;

JTextEntry compact(std::optional<std::string> id, std::string value) {
    JTextEntry e;
    e.number = 1;
    e.level_sep = '|';
    e.fields = {std::move(id), std::move(value)};
    return e;
}

// The record ids (first value) of every "1. #|# " line in `text`, in order.
std::vector<std::string> ids_in(const std::string& text) {
    std::vector<std::string> ids;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        constexpr std::string_view kRow = "1. #|# ";
        const auto at = line.find(kRow);
        if (at == std::string::npos) continue;
        const auto start = at + kRow.size();
        ids.push_back(line.substr(start, line.find('|', start) - start));
    }
    return ids;
}

}  // namespace

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

    // ─── record id generation (SPEC 2.6.4): field 1 is made when the source has none ───
    {
        std::ostringstream o;
        JTextWriter w(o);
        w.write_header();
        w.begin_section("A");
        w.append_entry(compact(std::nullopt, "a"));   // null   -> 1
        w.append_entry(compact(std::string{}, "b"));  // empty  -> 2
        w.append_entry(compact("10", "c"));           // given  -> 10 (noted)
        w.append_entry(compact(std::nullopt, "d"));   // continues past 10 -> 11
        w.append_row(1, {std::string{}, "e"}, "", '|');  // append_row path -> 12
        CHECK(w.last_record_id() == 12);
        w.begin_section("B");                           // ids restart per section
        w.append_entry(compact(std::nullopt, "f"));   // -> 1
        w.finalize();
        const auto ids = ids_in(o.str());
        CHECK((ids == std::vector<std::string>{"1", "2", "10", "11", "12", "1"}));
    }
    {
        // Identity(100,5), with auto-batching on (the batched path must agree).
        std::ostringstream o;
        JTextWriter w(o);
        w.set_auto_batch_size(2);
        w.write_header();
        w.begin_section("I");
        w.set_record_id_start(100, 5);
        for (int i = 0; i < 3; ++i) w.append_entry(compact(std::nullopt, "x"));
        w.finalize();
        CHECK((ids_in(o.str()) == std::vector<std::string>{"100", "105", "110"}));
    }
    {
        // Identity(-1,-1): a negative step counts down and continues below the smallest id.
        std::ostringstream o;
        JTextWriter w(o);
        w.write_header();
        w.begin_section("N");
        w.set_record_id_start(-1, -1);
        w.append_entry(compact(std::nullopt, "x"));   // -1
        w.append_entry(compact("-7", "y"));           // given
        w.append_entry(compact(std::nullopt, "z"));   // -8
        w.finalize();
        CHECK((ids_in(o.str()) == std::vector<std::string>{"-1", "-7", "-8"}));
    }
    {
        // Lines for other field positions are never touched.
        std::ostringstream o;
        JTextWriter w(o);
        w.write_header();
        w.begin_section("P");
        JTextEntry f2;
        f2.number = 2;
        f2.level_sep = '|';
        f2.fields = {std::nullopt};
        w.append_entry(f2);
        w.finalize();
        CHECK(!w.last_record_id().has_value());
        CHECK(o.str().find("2. #|# \x1F") != std::string::npos);
    }

    std::cout << "jText::v001 aliases jText API\n";
    return EXIT_SUCCESS;
}