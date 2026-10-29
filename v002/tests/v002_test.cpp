#include <jText/v002.hpp>

static_assert(jText::v002::version() == "v002.027", "v002 version");

#include "parser/close.hpp"
#include "parser/line.hpp"
#include "parser/section.hpp"
#include "export/xml_export.hpp"
#include <csignal>
#include <cstdlib>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <source_location>
#include <string_view>

// A failed check says what and where, and ends the test.
void check(bool ok, std::string_view what, std::source_location at = std::source_location::current()) {
    if (ok) return;
    std::cerr << "FAIL: " << what << " at " << at.file_name() << ":" << at.line() << "\n";
    std::exit(EXIT_FAILURE);
}

namespace {

using jText::v002::JTextEntry;
using jText::v002::JTextWriter;

JTextEntry compact(std::optional<std::string> id, std::string value) {
    JTextEntry e;
    e.number = 1;
    e.level_sep = '|';
    e.fields = {std::move(id), std::move(value)};
    return e;
}

// The record ids (first value) of every "1. |▌ " line in `text`, in order.
std::vector<std::string> ids_in(const std::string& text) {
    std::vector<std::string> ids;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        constexpr std::string_view kRow = "1. |\xE2\x96\x8C ";
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
    jText::v002::JTextWriter writer(out);
    writer.set_purpose("jac313 smoke");
    writer.write_header();
    writer.begin_section("T");
    writer.append_row(1, {std::string("jac313")});
    writer.finalize();

    const std::string text = out.str();
    check(text.find("jac313 smoke") != std::string::npos, "text.find(\"jac313 smoke\") != std::string::npos");
    check(text.find("jac313") != std::string::npos, "text.find(\"jac313\") != std::string::npos");

    check(jText::v002::JTextProfile::Light != jText::v002::JTextProfile::Full, "jText::v002::JTextProfile::Light != jText::v002::JTextProfile::Full");
    check(jText::v002::jtext_line_pad_width_for_count(10) >= 1, "jText::v002::jtext_line_pad_width_for_count(10) >= 1");

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
        check(w.last_record_id() == 12, "w.last_record_id() == 12");
        w.begin_section("B");                           // ids restart per section
        w.append_entry(compact(std::nullopt, "f"));   // -> 1
        w.finalize();
        const auto ids = ids_in(o.str());
        check((ids == std::vector<std::string>{"1", "2", "10", "11", "12", "1"}), "(ids == std::vector<std::string>{\"1\", \"2\", \"10\", \"11\", \"12\", \"1\"})");
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
        check((ids_in(o.str()) == std::vector<std::string>{"100", "105", "110"}), "(ids_in(o.str()) == std::vector<std::string>{\"100\", \"105\", \"110\"})");
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
        check((ids_in(o.str()) == std::vector<std::string>{"-1", "-7", "-8"}), "(ids_in(o.str()) == std::vector<std::string>{\"-1\", \"-7\", \"-8\"})");
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
        check(!w.last_record_id().has_value(), "!w.last_record_id().has_value()");
        // A field given no value is written as the text {NULL} (SPEC 2.6.1).
        check(o.str().find("2. |\xE2\x96\x8C {NULL}\n") != std::string::npos, "a field given no value is written as {NULL}");
    }

    {
        // Like a database: Insensitive is for comparing; stored values keep their case.
        using namespace jText::v002;
        const auto path = std::filesystem::temp_directory_path() / "jtext_case_kept_test.jtext";
        {
            std::ofstream fo(path);
            JTextWriter w(fo);
            w.set_case_mode(CaseMode::Insensitive);
            w.write_header();
            w.begin_section("Tools");
            w.append_row(1, {std::string("Hammer"), std::string("Mixed Case")});
            w.finalize();
        }
        JTextFile f;
        check(f.read_full(path.string()).has_value(), "f.read_full(path.string()).has_value()");
        check(f.case_mode == CaseMode::Insensitive, "f.case_mode == CaseMode::Insensitive");
        bool kept = false;
        for (const auto& sec : f.sections)
            for (const auto& e : sec.entries)
                for (const auto& fld : e.fields)
                    if (fld && *fld == "Hammer") kept = true;
        check(kept, "kept");
        std::filesystem::remove(path);
        check(text_equal("Hammer", "HAMMER", CaseMode::Insensitive), "text_equal(\"Hammer\", \"HAMMER\", CaseMode::Insensitive)");
        check(!text_equal("Hammer", "HAMMER", CaseMode::Sensitive), "!text_equal(\"Hammer\", \"HAMMER\", CaseMode::Sensitive)");
        check(text_equal("Hammer", "Hammer", CaseMode::Sensitive), "text_equal(\"Hammer\", \"Hammer\", CaseMode::Sensitive)");
        check(!text_equal("Hammer", "Hammers", CaseMode::Insensitive), "!text_equal(\"Hammer\", \"Hammers\", CaseMode::Insensitive)");
    }

    {
        // A row's bookend can be custom (|^) so a value may contain " # ". The writer
        // must emit that bookend, and use it for the comment marker, on both the
        // direct and the auto-batched path.
        using namespace jText::v002;
        for (const std::size_t batch : {std::size_t{0}, std::size_t{10}}) {
            const auto path = std::filesystem::temp_directory_path() / "jtext_bookend_test.jtext";
            {
                std::ofstream fo(path);
                JTextWriter w(fo);
                w.set_auto_batch_size(batch);
                w.write_header();
                w.begin_section("Langs");
                JTextEntry e;
                e.number = 1;
                e.delimiter = '^';
                e.level_sep = '|';
                e.fields = {std::string("6"), std::string("C # sharp")};
                e.comment = "safe comment";
                w.append_entry(e);
                w.finalize();
            }
            std::ifstream fi(path);
            const std::string text((std::istreambuf_iterator<char>(fi)), std::istreambuf_iterator<char>());
            check(text.find("1. |^ 6|C # sharp ^ safe comment") != std::string::npos, "text.find(\"1. |^ 6|C # sharp ^ safe comment\") != std::string::npos");
            JTextFile f;
            check(f.read_full(path.string()).has_value(), "f.read_full(path.string()).has_value()");
            bool found = false;
            for (const auto& sec : f.sections)
                for (const auto& r : sec.entries)
                    if (r.fields.size() == 2 && r.fields[1] && *r.fields[1] == "C # sharp"
                        && r.comment == "safe comment") found = true;
            check(found, "found");
            std::filesystem::remove(path);
        }
    }

    {
        // Left on the default comment character (U+258C), a row whose data contains it
        // gets a punctuation comment character that is not in its data, on every writer
        // path. A '#' in the data no longer matters.
        using namespace jText::v002;
        struct Case { std::vector<std::string> fields; std::string line; };
        const std::vector<Case> cases = {
            {{"1", "C # sharp"}, "1. |\xE2\x96\x8C 1|C # sharp \xE2\x96\x8C note"},
            {{"2", "a \xE2\x96\x8C b"}, "1. |# 2|a \xE2\x96\x8C b # note"},
            {{"3", "a \xE2\x96\x8C b # c"}, "1. |^ 3|a \xE2\x96\x8C b # c ^ note"},
            {{"4", "plain"}, "1. |\xE2\x96\x8C 4|plain \xE2\x96\x8C note"},
        };
        for (const int path_kind : {0, 1, 2}) {  // append_row, append_row batched, append_entry
            const auto path = std::filesystem::temp_directory_path() / "jtext_auto_bookend_test.jtext";
            {
                std::ofstream fo(path);
                JTextWriter w(fo);
                w.set_auto_batch_size(path_kind == 1 ? 10 : 0);
                w.write_header();
                w.begin_section("Auto");
                for (const auto& c : cases) {
                    if (path_kind == 2) {
                        JTextEntry e;
                        e.number = 1;
                        e.level_sep = '|';
                        for (const auto& f : c.fields) e.fields.emplace_back(f);
                        e.comment = "note";
                        w.append_entry(e);
                    } else {
                        w.append_row(1, c.fields, "note", '|');
                    }
                }
                w.finalize();
            }
            std::ifstream fi(path);
            const std::string text((std::istreambuf_iterator<char>(fi)), std::istreambuf_iterator<char>());
            for (const auto& c : cases) check(text.find(c.line) != std::string::npos, "text.find(c.line) != std::string::npos");
            JTextFile f;
            check(f.read_full(path.string()).has_value(), "f.read_full(path.string()).has_value()");
            std::size_t matched = 0;
            for (const auto& sec : f.sections)
                for (const auto& r : sec.entries)
                    for (const auto& c : cases)
                        if (r.fields.size() == 2 && r.fields[0] && *r.fields[0] == c.fields[0]
                            && r.fields[1] && *r.fields[1] == c.fields[1] && r.comment == "note") ++matched;
            check(matched == cases.size(), "matched == cases.size()");
            std::filesystem::remove(path);
        }
    }

    {
        // The row scan must find a '|' or a U+258C at every position of every length.
        using namespace jText::v002;
        for (std::size_t len = 1; len <= 24; ++len) {
            for (std::size_t at = 0; at < len; ++at) {
                std::string pipe(len, 'x'), mark(len, 'x');
                pipe[at] = '|';
                mark.replace(at, 1, "\xE2\x96\x8C");
                std::ostringstream o;
                JTextWriter w(o);
                w.set_bad_row_policy(BadRowPolicy::Skip);
                w.write_header();
                w.begin_section("Scan");
                w.append_row(1, {std::string("1"), std::string(len, 'x')}, "", '|');
                w.append_row(1, {std::string("2"), pipe}, "", '|');
                w.append_row(1, {std::string("3"), mark}, "", '|');
                w.finalize();
                check(w.skipped_rows() == 1, "w.skipped_rows() == 1");
                check(o.str().find("1. |\xE2\x96\x8C 1|") != std::string::npos, "o.str().find(\"1. |\\xE2\\x96\\x8C 1|\") != std::string::npos");
                check(o.str().find("1. |# 3|" + mark) != std::string::npos, "o.str().find(\"1. |# 3|\" + mark) != std::string::npos");
            }
        }
    }

    {
        // With U+258C as the separator, the comment character must not also be U+258C
        // (the two formatter characters must differ): the writer falls back to '#'.
        using namespace jText::v002;
        std::ostringstream o;
        JTextWriter w(o);
        w.write_header();
        w.begin_section("CommentSep");
        w.append_row(1, {std::string("1"), std::string("a")}, "", U'\u258C');
        w.finalize();
        check(o.str().find("1. \xE2\x96\x8C# 1\xE2\x96\x8C" "a") != std::string::npos, "o.str().find(\"1. \\xE2\\x96\\x8C# 1\\xE2\\x96\\x8C\" \"a\") != std::string::npos");
    }

    {
        // One line grammar for every reader. In a data row every separator splits and
        // empty values are kept (the writer's exact inverse); in a declaration's levels an empty
        // level is an error.
        auto fields = jtext::parse_line("1. |# a||b|", jtext::split_rule::multifield);
        check(fields.has_value(), "fields.has_value()");
        check((fields->values == std::vector<std::string>{"a", "", "b", ""}), "(fields->values == std::vector<std::string>{\"a\", \"\", \"b\", \"\"})");
        check(!jtext::parse_line("1. |# a||b|").has_value(), "!jtext::parse_line(\"1. |# a||b|\").has_value()");

        using namespace jText::v002;
        const auto path = std::filesystem::temp_directory_path() / "jtext_one_parser_test.jtext";
        {
            std::ofstream fo(path);
            fo << "=== jText File ===\n 1. ?# one parser\n\n=== Section: s ===\n\n=== Fields ===\n"
               << " 1. /# id/Identity(1,1)/Not Null\n 2. /# a/String\n 3. /# b/String\n 4. /# c/String\n"
               << "=== End Fields ===\n\n=== Data ===\n"
               << "1. |# 1|x|\n\n"
               << "1. |# 2||y|\x1F\n\n"
               << "1. |# 3|a|b|c # first # second\n\n"
               << "1. \u25AE\u258C 4\u25AEa|b # c \u258C v0.8 formatter\n\n"
               << "=== End Data ===\n=== End Section ===\n=== End File ===\n";
        }
        JTextFile f;
        check(f.read_full(path.string()).has_value(), "f.read_full(path.string()).has_value()");
        check(f.sections.size() == 1, "f.sections.size() == 1");
        const auto& rows = f.sections[0].entries;
        check(rows.size() == 4, "rows.size() == 4");
        check(rows[0].fields.size() == 3 && rows[0].fields[2] && rows[0].fields[2]->empty(), "rows[0].fields.size() == 3 && rows[0].fields[2] && rows[0].fields[2]->empty()");
        check(rows[1].fields.size() == 4 && rows[1].fields[1]->empty() && *rows[1].fields[3] == "{NULL}",
              "an empty value is empty; the v0.7 marker U+001F reads as {NULL}");
        check(rows[2].fields.size() == 4 && *rows[2].fields[3] == "c" && rows[2].comment == "first # second", "rows[2].fields.size() == 4 && *rows[2].fields[3] == \"c\" && rows[2].comment == \"first # second\"");
        check(rows[3].fields.size() == 2 && *rows[3].fields[1] == "a|b # c" && rows[3].comment == "v0.8 formatter", "rows[3].fields.size() == 2 && *rows[3].fields[1] == \"a|b # c\" && rows[3].comment == \"v0.8 formatter\"");
        check(rows[3].level_sep == U'\u25AE' && rows[3].delimiter == U'\u258C', "rows[3].level_sep == U'\\u25AE' && rows[3].delimiter == U'\\u258C'");
        std::filesystem::remove(path);
    }

    {
        // Writer to reader: empty strings come back empty, and a field given no value
        // comes back as the text {NULL} (SPEC 2.6.1).
        using namespace jText::v002;
        const auto path = std::filesystem::temp_directory_path() / "jtext_round_trip_test.jtext";
        {
            std::ofstream fo(path);
            JTextWriter w(fo);
            w.write_header();
            w.begin_section("Trip");
            JTextEntry e;
            e.number = 1;
            e.level_sep = '|';
            e.fields = {std::string("1"), std::string(""), std::nullopt, std::string("x"), std::string("")};
            w.append_entry(e);
            w.finalize();
        }
        JTextFile f;
        check(f.read_full(path.string()).has_value(), "f.read_full(path.string()).has_value()");
        const auto& r = f.sections.at(0).entries.at(0);
        check(r.fields.size() == 5, "r.fields.size() == 5");
        check(r.fields[1] && r.fields[1]->empty(), "r.fields[1] && r.fields[1]->empty()");
        check(r.fields[2] && *r.fields[2] == "{NULL}", "no value reads back as {NULL}");
        check(r.fields[3] && *r.fields[3] == "x", "r.fields[3] && *r.fields[3] == \"x\"");
        check(r.fields[4] && r.fields[4]->empty(), "r.fields[4] && r.fields[4]->empty()");
        std::filesystem::remove(path);
    }

    {
        // No separator given (level_sep 0): the writer joins with U+25AE, or with a
        // character not in the data when the data holds U+25AE. Never with a NUL byte.
        using namespace jText::v002;
        std::ostringstream o;
        JTextWriter w(o);
        w.write_header();
        w.begin_section("Flat");
        w.append_row(1, {std::string("Hammer"), std::string("Mixed Case")});
        w.append_row(1, {std::string("2026-10-28")});
        w.append_row(1, {std::string("a\xE2\x96\xAE"), std::string("b")});
        w.finalize();
        check(o.str().find("1. \xE2\x96\xAE\xE2\x96\x8C Hammer\xE2\x96\xAEMixed Case") != std::string::npos, "o.str().find(\"1. \\xE2\\x96\\xAE\\xE2\\x96\\x8C Hammer\\xE2\\x96\\xAEMixed Case\") != std::string::npos");
        check(o.str().find("1. \xE2\x96\xAE\xE2\x96\x8C 2026-10-28") != std::string::npos, "o.str().find(\"1. \\xE2\\x96\\xAE\\xE2\\x96\\x8C 2026-10-28\") != std::string::npos");
        check(o.str().find("1. ^\xE2\x96\x8C a\xE2\x96\xAE^b") != std::string::npos, "o.str().find(\"1. ^\\xE2\\x96\\x8C a\\xE2\\x96\\xAE^b\") != std::string::npos");
        check(o.str().find('\0') == std::string::npos, "o.str().find('\\0') == std::string::npos");
    }

    {
        // SPEC v0.8: a formatter character is any Unicode punctuation or symbol.
        check(jtext::is_valid_formatter_char(U'\u25AE') && jtext::is_valid_formatter_char(U'\u258C'), "jtext::is_valid_formatter_char(U'\\u25AE') && jtext::is_valid_formatter_char(U'\\u258C')");
        check(jtext::is_valid_formatter_char(U'|') && jtext::is_valid_formatter_char(U'\u20AC'), "jtext::is_valid_formatter_char(U'|') && jtext::is_valid_formatter_char(U'\\u20AC')");   // | and the euro sign
        check(!jtext::is_valid_formatter_char(U'a') && !jtext::is_valid_formatter_char(U'\u00E9'), "!jtext::is_valid_formatter_char(U'a') && !jtext::is_valid_formatter_char(U'\\u00E9')"); // letters
        check(!jtext::is_valid_formatter_char(U'7') && !jtext::is_valid_formatter_char(U' '), "!jtext::is_valid_formatter_char(U'7') && !jtext::is_valid_formatter_char(U' ')");
        check(!jtext::is_valid_formatter_char(U'\u00A0') && !jtext::is_valid_formatter_char(U'\u200D'), "!jtext::is_valid_formatter_char(U'\\u00A0') && !jtext::is_valid_formatter_char(U'\\u200D')"); // NBSP, ZWJ
        check(!jtext::is_valid_formatter_char(U'\u0301'), "!jtext::is_valid_formatter_char(U'\\u0301')");                                            // combining mark

        auto both = [](std::string_view line) {
            return std::pair{jtext::parse_line(line, jtext::split_rule::multifield), jtext::parse_line(line)};
        };
        auto [f, h] = both("1. \u25AE\u258C 1\u25AEcmd | grep T\u25AElaunch # 3 \u258C a note");
        check(f && h, "f && h");
        check((f->values == std::vector<std::string>{"1", "cmd | grep T", "launch # 3"}), "(f->values == std::vector<std::string>{\"1\", \"cmd | grep T\", \"launch # 3\"})");
        check(f->comment == "a note" && h->values == f->values, "f->comment == \"a note\" && h->values == f->values");
        check(f->fmt.separator == U'\u25AE' && f->fmt.bookend == U'\u258C', "f->fmt.separator == U'\\u25AE' && f->fmt.bookend == U'\\u258C'");

        const auto empty = jtext::parse_line("1. \u25AE\u258C a\u25AE\u25AEb\u25AE", jtext::split_rule::multifield);
        check(empty && (empty->values == std::vector<std::string>{"a", "", "b", ""}), "empty && (empty->values == std::vector<std::string>{\"a\", \"\", \"b\", \"\"})");

        const auto legacy = jtext::parse_line("1. \u258C\u25AE\u258C x\u25AEy");    // old three-character form
        check(legacy && legacy->fmt.separator == U'\u25AE' && legacy->fmt.bookend == U'\u258C', "legacy && legacy->fmt.separator == U'\\u25AE' && legacy->fmt.bookend == U'\\u258C'");

        auto fails = [](std::string_view line, jtext::parse_error_kind kind) {
            const auto r = jtext::parse_line(line);
            return !r && r.error().kind == kind;
        };
        check(fails("1. \u25AEa x", jtext::parse_error_kind::formatter_too_short), "fails(\"1. \\u25AEa x\", jtext::parse_error_kind::formatter_too_short)");          // letter
        check(fails("1. \u25AE\u0301 x", jtext::parse_error_kind::formatter_too_short), "fails(\"1. \\u25AE\\u0301 x\", jtext::parse_error_kind::formatter_too_short)");    // combining mark
        check(fails("1. \u25AE\u200D x", jtext::parse_error_kind::formatter_too_short), "fails(\"1. \\u25AE\\u200D x\", jtext::parse_error_kind::formatter_too_short)");    // zero-width joiner
        check(fails("1. \u25AE\u25AE x", jtext::parse_error_kind::formatter_separator_equals_bookend), "fails(\"1. \\u25AE\\u25AE x\", jtext::parse_error_kind::formatter_separator_equals_bookend)");
        check(fails("1. \xE2\x96 x", jtext::parse_error_kind::invalid_utf8), "fails(\"1. \\xE2\\x96 x\", jtext::parse_error_kind::invalid_utf8)");                // truncated
        check(fails("1. \xC0\xBC| x", jtext::parse_error_kind::invalid_utf8), "fails(\"1. \\xC0\\xBC| x\", jtext::parse_error_kind::invalid_utf8)");               // overlong '<'

        // The writer emits the code points it is given, and the reader gets them back.
        using namespace jText::v002;
        const auto path = std::filesystem::temp_directory_path() / "jtext_unicode_formatter_test.jtext";
        std::size_t skipped = 0;
        {
            std::ofstream fo(path);
            JTextWriter w(fo);
            w.set_bad_row_policy(BadRowPolicy::Skip);
            w.write_header();
            w.begin_section("Unicode");
            JTextEntry e;
            e.number = 1;
            e.delimiter = U'\u258C';
            e.level_sep = U'\u25AE';
            e.fields = {std::string("1"), std::string("a|b # c"), std::string("x")};
            e.comment = "note";
            w.append_entry(e);
            w.append_row(1, {std::string("2"), std::string("has \u25AE inside")}, "", U'\u25AE');   // bad row
            w.finalize();
            skipped = w.skipped_rows();
        }
        check(skipped == 1, "skipped == 1");
        std::ifstream fi(path);
        const std::string text((std::istreambuf_iterator<char>(fi)), std::istreambuf_iterator<char>());
        check(text.find("1. \u25AE\u258C 1\u25AEa|b # c\u25AEx \u258C note") != std::string::npos, "text.find(\"1. \\u25AE\\u258C 1\\u25AEa|b # c\\u25AEx \\u258C note\") != std::string::npos");
        JTextFile file;
        check(file.read_full(path.string()).has_value(), "file.read_full(path.string()).has_value()");
        const auto& r = file.sections.at(0).entries.at(0);
        check(r.fields.size() == 3 && *r.fields[1] == "a|b # c" && r.comment == "note", "r.fields.size() == 3 && *r.fields[1] == \"a|b # c\" && r.comment == \"note\"");
        check(r.level_sep == U'\u25AE' && r.delimiter == U'\u258C', "r.level_sep == U'\\u25AE' && r.delimiter == U'\\u258C'");
        std::filesystem::remove(path);
    }

    {
        // Data rows live inside a Data block. A row after the section's includes with no
        // "=== Data ===" banner is a structural error; inside the banner, any formatter works.
        const auto banner_less = jtext::parse_file_structure(
            "=== jText File ===\n 1. ?# no banner\n\n=== Section: s ===\n"
            "=== <#include#> Fields: f.jtFlds ===\n"
            "1. |# 1|a|b\n"
            "=== End Section ===\n=== End File ===\n");
        check(!banner_less.has_value(), "!banner_less.has_value()");
        const auto top_level = jtext::parse_file_structure(
            "=== <#include#> Fields: f.jtFlds ===\n1. |# 1|a|b\n");
        check(!top_level.has_value(), "!top_level.has_value()");

        const auto file = jtext::parse_file_structure(
            "=== jText File ===\n 1. ?# banner\n\n=== Section: s ===\n"
            "=== Fields ===\n 1. /# id/Identity(1,1)/Not Null\n 2. /# a/String\n 3. /# b/String\n"
            "=== End Fields ===\n=== Data ===\n"
            "1. |# 1|a|b\n\n"
            "1. \u25AE\u258C 2\u25AEc|d\u25AEe\n"
            "=== End Data ===\n=== End Section ===\n=== End File ===\n");
        check(file.has_value(), "file.has_value()");
        check(file->sections.size() == 1, "file->sections.size() == 1");
        const auto& lines = file->sections[0].data_lines;
        std::size_t rows = 0;
        for (const auto& pl : lines) if (pl.number != 0) ++rows;
        check(rows == 2, "rows == 2");
        check(lines.back().values == (std::vector<std::string>{"2", "c|d", "e"}), "lines.back().values == (std::vector<std::string>{\"2\", \"c|d\", \"e\"})");
    }

    {
        // Every block closes, like XML. The writer closes each section and the file,
        // and both readers see exactly the sections that were written.
        using namespace jText::v002;
        const auto path = std::filesystem::temp_directory_path() / "jtext_closers_test.jtext";
        {
            std::ofstream fo(path);
            JTextWriter w(fo);
            w.write_header();
            w.begin_section("A");
            w.append_row(1, {std::string("1"), std::string("x")}, "", '|');
            w.begin_section("B");
            w.append_row(1, {std::string("1"), std::string("y")}, "", '|');
            w.finalize();
        }
        std::ifstream fi(path);
        const std::string text((std::istreambuf_iterator<char>(fi)), std::istreambuf_iterator<char>());
        check(text.find("-- A --") < text.find("-- End A --"), "text.find(\"-- A --\") < text.find(\"-- End A --\")");
        check(text.find("-- End A --") < text.find("-- B --"), "text.find(\"-- End A --\") < text.find(\"-- B --\")");
        check(text.find("-- B --") < text.find("-- End B --"), "text.find(\"-- B --\") < text.find(\"-- End B --\")");
        check(text.find("-- End B --") < text.find("-- EOF --"), "text.find(\"-- End B --\") < text.find(\"-- EOF --\")");
        const auto strict = jtext::parse_file_structure(text);
        check(strict.has_value() && strict->sections.size() == 2, "strict.has_value() && strict->sections.size() == 2");
        JTextFile f;
        check(f.read_full(path.string()).has_value(), "f.read_full(path.string()).has_value()");
        check(f.sections.size() == 2, "f.sections.size() == 2");
        std::filesystem::remove(path);
    }

    {
        // A block left open is a missing closer: a cut-off file does not read.
        auto kind = [](std::string_view text) {
            const auto r = jtext::parse_file_structure(text);
            return r ? std::string("ok") : std::string(jtext::to_string(r.error().kind));
        };
        const std::string full =
            "=== jText File ===\n 1. ?# t\n\n=== Section: s ===\n=== Fields ===\n"
            " 1. /# id/Identity(1,1)\n 2. /# v/String\n=== End Fields ===\n=== Data ===\n"
            "1. |# 1|a\n\n1. |# 2|b\n";
        const std::string closed = full + "=== End Data ===\n=== End Section ===\n=== End File ===\n";
        check(kind(closed) == "ok", "kind(closed) == \"ok\"");
        check(kind(full) == "missing_closer", "kind(full) == \"missing_closer\"");
        check(kind(full + "=== End Data ===\n=== End Section ===\n") == "missing_closer", "kind(full + \"=== End Data ===\\n=== End Section ===\\n\") == \"missing_closer\"");
        check(kind(closed + "1. |# 3|c\n") == "unexpected_marker", "kind(closed + \"1. |# 3|c\\n\") == \"unexpected_marker\"");          // after the file closed
        const std::string light = "# JText File - created x\n\n-- A --\n1. |# 1|a\n";
        check(kind(light + "-- End A --\n-- EOF --\n") == "ok", "kind(light + \"-- End A --\\n-- EOF --\\n\") == \"ok\"");
        check(kind(light) == "missing_closer", "kind(light) == \"missing_closer\"");
        check(kind(light + "-- End B --\n-- EOF --\n") == "unexpected_marker", "kind(light + \"-- End B --\\n-- EOF --\\n\") == \"unexpected_marker\""); // closes the wrong section

        // add_missing_closers puts each where its block ends; the next read works.
        auto cut = jtext::add_missing_closers(full);
        check(cut.added.size() == 3 && !cut.last_line_unterminated, "cut.added.size() == 3 && !cut.last_line_unterminated");
        check(cut.text == closed, "cut.text == closed");
        check(kind(cut.text) == "ok", "kind(cut.text) == \"ok\"");
        const auto again = jtext::add_missing_closers(cut.text);
        check(again.added.empty() && again.text == cut.text, "again.added.empty() && again.text == cut.text");

        // Written before closers were required: End Fields before Data, a section
        // closed before the next one, the file at the end.
        const std::string old =
            "=== jText File ===\n 1. ?# old\n\n"
            "=== Section: a ===\n=== Fields ===\n 1. /# id/Identity(1,1)\n=== Data ===\n1. |# 1\n\n"
            "=== Section: b ===\n=== Fields ===\n 1. /# id/Identity(1,1)\n=== Data ===\n1. |# 1\n";
        const auto fixed = jtext::add_missing_closers(old);
        check(fixed.added.size() == 7, "fixed.added.size() == 7");
        check(kind(fixed.text) == "ok", "kind(fixed.text) == \"ok\"");
        const auto old_light = jtext::add_missing_closers(
            "# JText File - created x\n-- A --\n1. |# 1|a\n\n-- B --\n1. |# 1|b");
        check(old_light.last_line_unterminated && old_light.added.size() == 3, "old_light.last_line_unterminated && old_light.added.size() == 3");
        check(kind(old_light.text) == "ok", "kind(old_light.text) == \"ok\"");

        // Cut off inside a multiline value: its "*]" comes first, aligned with its lines.
        const auto ml = jtext::add_missing_closers(
            "# JText File - created x\n-- A --\n 1. [* # note\n    ** line one\n");
        check(ml.added.size() == 3 && ml.added[0].find("'*]'") != std::string::npos, "ml.added.size() == 3 && ml.added[0].find(\"'*]'\") != std::string::npos");
        check(ml.text.find("    ** line one\n    *]\n-- End A --\n-- EOF --\n") != std::string::npos, "ml.text.find(\" ** line one\\n *]\\n-- End A --\\n-- EOF --\\n\") != std::string::npos");
        check(kind(ml.text) == "ok", "kind(ml.text) == \"ok\"");

        // A v0.7 "<<< SENTINEL" block is rewritten as a [* value, line for line: in a
        // data row it keeps its number; a template's opener is a bare "[*".
        const auto old_ml = jtext::add_missing_closers(
            "# JText File - created x\n-- A --\n 2. <<< END  # note\nline one\n\n  indented\nEND\n"
            "-- End A --\n-- EOF --\n");
        check(old_ml.text.find(" 2. [* # note\n    ** line one\n    **\n    **   indented\n    *]\n")
              != std::string::npos, "old_ml.text.find(\" 2. [* # note\\n ** line one\\n **\\n ** indented\\n *]\\n\") != std::string::npos");
        check(kind(old_ml.text) == "ok", "kind(old_ml.text) == \"ok\"");
        const auto old_tpl = jtext::add_missing_closers(
            "=== jText File ===\n 1. ?# t\n\n=== Section: s ===\n=== Template: T ===\n"
            " 1. <<< !!!T!!!\nINSERT {1};\n!!!T!!!\n=== Fields ===\n 1. /# id/Identity(1,1)\n"
            "=== End Fields ===\n=== Data ===\n1. |# 1\n=== End Data ===\n=== End Section ===\n"
            "=== End File ===\n");
        check(old_tpl.text.find("=== Template: T ===\n[*\n** INSERT {1};\n*]\n") != std::string::npos, "old_tpl.text.find(\"=== Template: T ===\\n[*\\n** INSERT {1};\\n*]\\n\") != std::string::npos");
        check(kind(old_tpl.text) == "ok", "kind(old_tpl.text) == \"ok\"");
    }

    {
        // Multiline values (SPEC 5): "[*" opens, "** " lines, a bare "*]".
        using VK = jtext::value_line_kind;
        auto vl = [](std::string_view line) { return jtext::parse_value_line(line); };
        check(vl("    ** text")->kind == VK::content && vl("    ** text")->text == "text", "vl(\" ** text\")->kind == VK::content && vl(\" ** text\")->text == \"text\"");
        check(vl("**   indented")->text == "  indented", "vl(\"** indented\")->text == \" indented\"");     // one space is the marker's
        check(vl("**")->kind == VK::content && vl("**")->text.empty(), "vl(\"**\")->kind == VK::content && vl(\"**\")->text.empty()");
        check(vl("** ")->text.empty(), "vl(\"** \")->text.empty()");
        check(vl("** trailing  ")->text == "trailing  ", "vl(\"** trailing \")->text == \"trailing \"");     // verbatim
        check(vl("\t*]")->kind == VK::closer && vl("*]  ")->kind == VK::closer, "vl(\"\\t*]\")->kind == VK::closer && vl(\"*] \")->kind == VK::closer");
        check(vl(" 7. |# x")->kind == VK::other, "vl(\" 7. |# x\")->kind == VK::other");
        check(!vl("**x").has_value() && !vl("*] more").has_value(), "!vl(\"**x\").has_value() && !vl(\"*] more\").has_value()");

        auto kind = [](std::string_view text) {
            const auto r = jtext::parse_file_structure(text);
            return r ? std::string("ok") : std::string(jtext::to_string(r.error().kind));
        };
        const std::string head =
            "=== jText File ===\n 1. ?# ml\n\n=== Section: s ===\n=== Template: Insert ===\n"
            "[* # one statement\n** INSERT INTO t VALUES ({1}, {2});\n*]\n"
            "=== Fields ===\n 1. /# id/Identity(1,1)\n 2. /# trace/String\n=== End Fields ===\n"
            "=== Data ===\n 1. |# 42\n 2. [* # from worker 3\n    ** Traceback:\n    **\n"
            "    **   File \"main.py\" | # 12\n";
        const std::string tail = "=== End Data ===\n=== End Section ===\n=== End File ===\n";
        const auto file = jtext::parse_file_structure(head + "    *]\n" + tail);
        check(file.has_value(), "file.has_value()");
        check(file->sections[0].templates[0].body == "INSERT INTO t VALUES ({1}, {2});", "file->sections[0].templates[0].body == \"INSERT INTO t VALUES ({1}, {2});\"");
        check(file->sections[0].templates[0].opener_comment == "one statement", "file->sections[0].templates[0].opener_comment == \"one statement\"");
        const auto& value = file->sections[0].data_lines.back();
        check(value.kind == jtext::line_kind::multiline_opener && value.comment == "from worker 3", "value.kind == jtext::line_kind::multiline_opener && value.comment == \"from worker 3\"");
        check(value.values == (std::vector<std::string>{"Traceback:\n\n  File \"main.py\" | # 12"}), "value.values == (std::vector<std::string>{\"Traceback:\\n\\n File \\\"main.py\\\" | # 12\"})");
        check(kind(head) == "missing_closer", "kind(head) == \"missing_closer\"");                      // end of file
        check(kind(head + tail) == "missing_closer", "kind(head + tail) == \"missing_closer\"");               // a marker
        check(kind(head + "\n    *]\n" + tail) == "missing_closer", "kind(head + \"\\n *]\\n\" + tail) == \"missing_closer\""); // a blank line is not "**"

        // The writer: a field with a newline becomes an aligned [* value, the row
        // breaking around it; its '|' and '#' are not separators or comments there.
        using namespace jText::v002;
        for (const std::size_t batch : {std::size_t{0}, std::size_t{10}}) {
            const auto path = std::filesystem::temp_directory_path() / "jtext_multiline_test.jtext";
            {
                std::ofstream fo(path);
                JTextWriter w(fo);
                w.set_auto_batch_size(batch);
                w.write_header();
                w.begin_section("Trace");
                w.append_row(1, {std::string("1"), std::string("line one | # x\n  indented\n\nlast"),
                                 std::string("tail")}, "note", '|');
                w.finalize();
            }
            std::ifstream fi(path);
            const std::string text((std::istreambuf_iterator<char>(fi)), std::istreambuf_iterator<char>());
            check(text.find(" 1. |\xE2\x96\x8C 1 \xE2\x96\x8C note\n"
                            " 2. [*\n"
                            "    ** line one | # x\n"
                            "    **   indented\n"
                            "    **\n"
                            "    ** last\n"
                            "    *]\n"
                            " 3. |\xE2\x96\x8C tail\n") != std::string::npos, "text.find(\" 1. |\\xE2\\x96\\x8C 1 \\xE2\\x96\\x8C note\\n\" \" 2. [*\\n\" \" ** line one | # x\\n\" \" ** indented\\n\" \" **\\n\" \" ** last\\n\" \" *]\\n\" \" 3. |\\xE2\\x96\\x8C tail\\n\") != std::string::npos");
            JTextFile f;
            check(f.read_full(path.string()).has_value(), "f.read_full(path.string()).has_value()");
            const auto& rows = f.sections.at(0).entries;
            check(rows.size() == 3, "rows.size() == 3");
            check(rows[0].comment == "note" && rows[1].number == 2, "rows[0].comment == \"note\" && rows[1].number == 2");
            check(rows[1].fields.size() == 1 && *rows[1].fields[0] == "line one | # x\n  indented\n\nlast", "rows[1].fields.size() == 1 && *rows[1].fields[0] == \"line one | # x\\n indented\\n\\nlast\"");
            check(rows[2].number == 3 && *rows[2].fields[0] == "tail", "rows[2].number == 3 && *rows[2].fields[0] == \"tail\"");

            // Written back by JTextFile, the file is the same.
            const auto again = std::filesystem::temp_directory_path() / "jtext_multiline_again.jtext";
            check(f.write(again.string()).has_value(), "f.write(again.string()).has_value()");
            std::ifstream fa(again);
            const std::string text2((std::istreambuf_iterator<char>(fa)), std::istreambuf_iterator<char>());
            check(text2.find(" 2. [*\n    ** line one | # x\n    **   indented\n    **\n    ** last\n    *]\n")
                  != std::string::npos, "text2.find(\" 2. [*\\n ** line one | # x\\n ** indented\\n **\\n ** last\\n *]\\n\") != std::string::npos");
            std::filesystem::remove(path);
            std::filesystem::remove(again);
        }
    }

    {
        // A file of configurations (dev, test, prod): replacing one section's data
        // leaves every other line of the file exactly as it was (the files declare the
        // current version; an older one is upgraded, tested with the versions below).
        auto section = [](std::string_view name, std::string_view rows) {
            return std::string("=== Section: ") + std::string(name) + " ===\n"
                   "=== Template: Insert ===\n[*\n** INSERT {1};\n*]\n"
                   "=== Fields ===\n 1. /# id/Identity(1,1)\n 2. /# host/String\n=== End Fields ===\n"
                   "=== Data ===\n" + std::string(rows) + "=== End Data ===\n=== End Section ===\n\n";
        };
        const std::string head = "=== jText File ===\n 1. ?# configs\n 4. ?# 0.8\n\n";
        const std::string dev = section("dev", " 1. |# 1|dev.local\n");
        const std::string prod = section("prod", " 1. |# 1|prod.example\n# keep me\n");
        const std::string full = head + dev + section("test", " 1. |# 1|test.local\n\n 1. |# 2|test2\n")
                                 + prod + "=== End File ===\n";
        const auto replaced = jtext::replace_section_data(full, "test", " 1. |# 1|qa.local\n");
        check(replaced.has_value(), "replaced.has_value()");
        check(*replaced == head + dev + section("test", " 1. |# 1|qa.local\n") + prod + "=== End File ===\n", "*replaced == head + dev + section(\"test\", \" 1. |# 1|qa.local\\n\") + prod + \"=== End File ===\\n\"");
        const auto back = jtext::parse_file_structure(*replaced);
        check(back.has_value() && back->sections.size() == 3, "back.has_value() && back->sections.size() == 3");
        check(back->sections[1].data_lines.back().values == (std::vector<std::string>{"1", "qa.local"}), "back->sections[1].data_lines.back().values == (std::vector<std::string>{\"1\", \"qa.local\"})");

        const std::string light =
            "# JText File - created x\n# jText Version: 0.8\n\n"
            "-- dev --\n# Fields: cfg.jtFlds\n 1. |# 1|dev.local\n-- End dev --\n\n"
            "-- test --\n# Fields: cfg.jtFlds\n// note above the rows\n 1. |# 1|test.local\n-- End test --\n\n"
            "-- prod --\n 1. |# 1|prod.example\n-- End prod --\n\n-- EOF --";   // no final newline
        const auto light_replaced = jtext::replace_section_data(light, "test", " 1. |# 1|qa.local\n 1. |# 2|qa2\n");
        check(light_replaced.has_value(), "light_replaced.has_value()");
        check(*light_replaced ==
              "# JText File - created x\n# jText Version: 0.8\n\n"
              "-- dev --\n# Fields: cfg.jtFlds\n 1. |# 1|dev.local\n-- End dev --\n\n"
              "-- test --\n# Fields: cfg.jtFlds\n// note above the rows\n 1. |# 1|qa.local\n 1. |# 2|qa2\n-- End test --\n\n"
              "-- prod --\n 1. |# 1|prod.example\n-- End prod --\n\n-- EOF --", "*light_replaced == \"# JText File - created x\\n# jText Version: 0.8\\n\\n\" \"-- dev --\\n# Fields: cfg.jtFlds\\n 1. |# 1|dev.local\\n-- End dev --\\n\\n\" \"-- test --\\n# Fields: cfg.jtFlds\\n// note above the rows\\n 1. |# 1|qa.local\\n 1. |# 2|qa2\\n-- End test --\\n\\n\" \"-- prod --\\n 1. |# 1|prod.example\\n-- End prod --\\n\\n-- EOF --\"");
        check(jtext::parse_file_structure(*light_replaced).has_value(), "jtext::parse_file_structure(*light_replaced).has_value()");

        // An empty light section takes its first rows right before its closer.
        const auto empty = jtext::replace_section_data(
            "# JText File - created x\n# jText Version: 0.8\n-- new --\n-- End new --\n-- EOF --\n", "new", " 1. |# 1|a\n");
        check(empty && *empty == "# JText File - created x\n# jText Version: 0.8\n-- new --\n 1. |# 1|a\n-- End new --\n-- EOF --\n", "empty && *empty == \"# JText File - created x\\n# jText Version: 0.8\\n-- new --\\n 1. |# 1|a\\n-- End new --\\n-- EOF --\\n\"");

        auto kind = [](const std::expected<std::string, jtext::file_error>& r) {
            return r ? std::string("ok") : std::string(jtext::to_string(r.error().kind));
        };
        check(kind(jtext::replace_section_data(full, "staging", "")) == "section_not_found", "kind(jtext::replace_section_data(full, \"staging\", \"\")) == \"section_not_found\"");
        check(kind(jtext::replace_section_data(head + dev + dev + "=== End File ===\n", "dev", "")) == "section_ambiguous", "kind(jtext::replace_section_data(head + dev + dev + \"=== End File ===\\n\", \"dev\", \"\")) == \"section_ambiguous\"");
        check(kind(jtext::replace_section_data(full.substr(0, full.size() - 17), "test", "")) == "missing_closer", "kind(jtext::replace_section_data(full.substr(0, full.size() - 17), \"test\", \"\")) == \"missing_closer\"");
    }

    {
        // //File: holds the file's name only: a path would go stale when the file moves.
        using namespace jText::v002;
        const auto dir = std::filesystem::temp_directory_path() / "jtext_file_header_test";
        std::filesystem::create_directories(dir);
        const auto path = dir / "named.jtext";
        {
            JTextWriter w(path.string());
            w.write_header();
            w.finalize();
        }
        std::ifstream fi(path);
        std::string first;
        std::getline(fi, first);
        check(first == "//File:    named.jtext", "first == \"//File: named.jtext\"");
        std::filesystem::remove_all(dir);
    }

    {
        // The light profile declares its jText version in the "#" block, as the full profile
        // does on header line 4: the writer's formatters are non-ASCII (SPEC v0.8, 2.3).
        using namespace jText::v002;
        std::ostringstream o;
        JTextWriter w(o);
        w.write_header();
        w.begin_section("s");
        w.append_row(1, {std::string("1"), std::string("a")});
        w.end_record();
        w.finalize();
        const std::string text = o.str();
        check(text.find("# Purpose - \n# jText Version: 0.8\n# Case: ") != std::string::npos,
              "light header declares jText Version 0.8");
        check(jtext::parse_file_structure(text).has_value(), "the light file parses");
    }

    {
        // Comment lines are data (SPEC 3.2.1): a line of its own, kept where it stands, in
        // the header, the intro and Fields block, the data block (before a record, inside
        // one, at the end), between sections and after the file's closer.
        const std::string full =
            "//File:    c.jtext\n//\n"                         // the file's // block: not a comment
            "=== jText File ===\n// about the header\n 1. ?# c.jtext\n 4. ?# 0.8\n# header end\n\n"
            "=== Section: s ===\n// intro\n=== Fields ===\n 1. /# id/Integer\n# about a\n 2. /# a/String\n"
            "=== End Fields ===\n=== Data ===\n# first record\n 1. ?# 1\n// inside\n 2. ?# x # the line's own\n\n"
            "# second\n 1. |# 2|y\n# data end\n=== End Data ===\n=== End Section ===\n"
            "# between\n=== Section: t ===\n=== Fields ===\n 1. /# id/Integer\n=== End Fields ===\n"
            "=== Data ===\n 1. ?# 1\n=== End Data ===\n=== End Section ===\n=== End File ===\n# after\n";
        auto pf = jtext::parse_file_structure(full);
        check(pf.has_value(), "the commented full file parses");
        const auto texts = [](const std::vector<jtext::comment_line>& v) {
            std::vector<std::pair<std::size_t, std::string>> out;
            for (const auto& c : v) out.emplace_back(c.before, c.text);
            return out;
        };
        using P = std::vector<std::pair<std::size_t, std::string>>;
        check(texts(pf->header_comments) == P{{0, "about the header"}, {2, "header end"}},
              "header comment lines, before line 1 and at the end");
        const auto& s0 = pf->sections.at(0);
        check(texts(s0.field_comments) == P{{0, "intro"}, {1, "about a"}}, "intro and Fields comment lines");
        // data_lines: [0] 1.  [1] 2.  [2] blank  [3] 1. |#
        check(texts(s0.data_comments) == P{{0, "first record"}, {1, "inside"}, {3, "second"}, {4, "data end"}},
              "data comment lines: before a record, inside one, before the next, at the end");
        check(s0.data_lines.at(1).comment == "the line's own" &&
              s0.data_lines.at(1).values == std::vector<std::string>{"x"},
              "a line's own comment stays on its line");
        check(texts(pf->sections.at(1).field_comments) == P{{0, "between"}}, "a comment line between sections");
        check(texts(pf->sections.at(1).data_comments) == P{{1, "after"}}, "a comment line after the closer");

        const std::string light =
            "# JText File - created 2026-01-01\n# jText Version: 0.8\n// header note\n\n"
            "-- s --\n# Fields: f.jtFlds\n# top\n 1. |# 1|a\n# middle\n 1. |# 2|b\n# end of s\n-- End s --\n"
            "# between\n-- t --\n 1. |# 1|c\n-- End t --\n-- EOF --\n# after\n";
        auto lf = jtext::parse_file_structure(light);
        check(lf.has_value(), "the commented light file parses");
        check(texts(lf->header_comments) == P{{2, "header note"}}, "a light header comment line");
        check(lf->sections.at(0).includes.size() == 1, "# Fields: is an include, not a comment");
        check(texts(lf->sections.at(0).data_comments) == P{{0, "top"}, {1, "middle"}, {2, "end of s"}},
              "light data comment lines");
        check(texts(lf->sections.at(1).data_comments) == P{{0, "between"}, {1, "after"}},
              "light: between sections, and after -- EOF --");

        // JTextFile keeps them through a read, a write and a read.
        using namespace jText::v002;
        const auto path = std::filesystem::temp_directory_path() / "jtext_comment_lines_test.jtext";
        { std::ofstream fo(path); fo << light; }
        JTextFile f;
        check(f.read_full(path.string()).has_value(), "JTextFile reads the commented file");
        const auto& c0 = f.sections.at(0).comments;
        check(c0.size() == 3 && c0[0].before == 0 && c0[0].text == "top" && c0[1].before == 1 &&
              c0[1].text == "middle" && c0[2].before == 2 && c0[2].text == "end of s",
              "JTextFile places comment lines among its entries");
        check(f.write(path.string()).has_value(), "JTextFile writes it");
        JTextFile g;
        check(g.read_full(path.string()).has_value(), "and reads it again");
        const auto& g0 = g.sections.at(0).comments;
        check(g0.size() == 3 && g0[0].text == "top" && g0[1].before == 1 && g0[2].before == 2 &&
              g.header_comments == std::vector<std::string>{"header note"} &&
              g.sections.at(1).comments.size() == 2,
              "the comment lines survive the save, where they stood");
        std::filesystem::remove(path);
    }

    {
        // The canonical XML export (XML_EXPORT.md): one byte form for one set of data.
        const auto xml = [](const std::string& text, jtext::xml_scope scope) {
            auto pf = jtext::parse_file_structure(text);
            check(pf.has_value(), "the file to export parses");
            return jtext::to_xml(*pf, scope);
        };
        const std::string fields =
            "=== Fields ===\n 1. /# id/Integer\n 2. /# name/VarChar/20/Not Null\n 3. /# d/Date # when\n=== End Fields ===\n";
        const auto full_file = [&](const std::string& data) {
            return "=== jText File ===\n 1. ?# x.jtext\n 4. ?# 0.8\n 5. ?# yes\n\n=== Section: s ===\n" + fields +
                   "=== Data ===\n" + data + "=== End Data ===\n=== End Section ===\n=== End File ===\n";
        };

        // The exact bytes, so the layout cannot drift.
        const std::string golden =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<jtext format=\"0.8\" export=\"0.1\" scope=\"data\" xml:space=\"preserve\">\n"
            "  <section>\n"
            "    <fields>\n"
            "      <field position=\"1\" name=\"id\" type=\"Integer\" not-null=\"true\" not-empty=\"true\"/>\n"
            "      <field position=\"2\" name=\"name\" type=\"String\" length=\"20\" not-null=\"true\"/>\n"
            "      <field position=\"3\" name=\"d\" type=\"Date\" null-date=\"1900-01-01\" comment=\"when\"/>\n"
            "    </fields>\n"
            "    <records>\n"
            "      <record>\n"
            "        <comment>first</comment>\n"
            "        <value position=\"1\" comment=\"row note\">1</value>\n"
            "        <value position=\"2\">a &amp; b &lt;c&gt; &#13;x</value>\n"
            "        <value position=\"3\">2026-01-01</value>\n"
            "      </record>\n"
            "      <record>\n"
            "        <value position=\"1\">2</value>\n"
            "        <value position=\"2\" encoding=\"base64\">AWI=</value>\n"
            "        <value position=\"3\"/>\n"
            "      </record>\n"
            "      <comment>the end</comment>\n"
            "    </records>\n"
            "  </section>\n"
            "</jtext>\n";
        const std::string rows =
            "# first\n 1. ▮▌ 1▮a & b <c> \rx▮2026-01-01 ▌ row note\n 1. |# 2|\x01" "b|\n# the end\n";
        check(xml(full_file(rows), jtext::xml_scope::data) == golden, "the data-scope export, byte for byte");

        // The same data written one line per field exports the same bytes.
        const std::string vertical =
            "// first\n 1. ?# 1 # row note\n 2. ?# a & b <c> \rx\n 3. ?# 2026-01-01\n\n"
            " 1. ?# 2\n 2. |# \x01" "b\n\n// the end\n";
        check(xml(full_file(vertical), jtext::xml_scope::data) == golden,
              "rows and per-field lines with the same data and comments export the same");

        // A comment belongs to its line: on a line that starts at field 2, it is on value 2.
        const auto split = xml(full_file(" 1. ?# 1\n 2. |# n|2026-01-01 # on two\n"), jtext::xml_scope::data);
        check(split.find("<value position=\"2\" comment=\"on two\">n</value>") != std::string::npos,
              "a line's comment is on the value where the line starts");

        // full scope: header (normalized), section name, attribute escaping.
        const auto full = xml(full_file(" 1. ?# 1 # say \"hi\"\n"), jtext::xml_scope::full);
        check(full.find("  <header>\n    <filename>x.jtext</filename>\n    <jtext_version>0.8</jtext_version>\n"
                        "    <case_mode>yes</case_mode>\n  </header>\n  <section name=\"s\">\n") != std::string::npos,
              "full scope: header in the canonical order, then the named section");
        check(full.find("comment=\"say &quot;hi&quot;\"") != std::string::npos, "a quote in an attribute");

        // U+001F reads as {NULL}; NULL is text.
        const auto nul = xml(full_file(" 1. |# 1|\x1F|\n 1. |# 2|NULL|2026-01-01\n"), jtext::xml_scope::data);
        check(nul.find("<value position=\"2\">{NULL}</value>") != std::string::npos &&
              nul.find("<value position=\"2\">NULL</value>") != std::string::npos,
              "U+001F exports as {NULL}, and NULL as NULL");
    }

    {
        // No nulls in a file (SPEC 2.6.1): the reader keeps NULL and {NULL} as text and
        // reads the v0.7 marker U+001F as {NULL}; the writer writes {NULL} for a field
        // given no value, and it reads back as that text.
        using namespace jText::v002;
        const auto path = std::filesystem::temp_directory_path() / "jtext_null_text_test.jtext";
        {
            std::ofstream fo(path);
            fo << "# JText File - created 2026-01-01\n# jText Version: 0.8\n\n-- s --\n"
                  " 1. |# 1|NULL|\x1F|{NULL}|\n-- End s --\n-- EOF --\n";
        }
        JTextFile f;
        check(f.read_full(path.string()).has_value(), "the null-text file reads");
        const auto& e = f.sections.at(0).entries.at(0);
        check(e.fields.size() == 5 && *e.fields[1] == "NULL" && *e.fields[2] == "{NULL}" &&
              *e.fields[3] == "{NULL}" && e.fields[4]->empty(),
              "NULL stays text, U+001F reads as {NULL}, an empty value is empty");

        std::ostringstream o;
        JTextWriter w(o);
        w.write_header();
        w.begin_section("s");
        JTextEntry row;
        row.number = 1;
        row.fields = {std::string("1"), std::nullopt, std::string("NULL")};
        w.append_entry(row);
        w.end_record();
        w.finalize();
        auto pf = jtext::parse_file_structure(o.str());
        check(pf && pf->sections.at(0).data_lines.at(0).values ==
                        (std::vector<std::string>{"1", "{NULL}", "NULL"}),
              "the writer writes {NULL} for no value and keeps NULL as text");
        std::filesystem::remove(path);
    }

    {
        // A value that begins with the comment character and a space would read back as a
        // comment (SPEC 3.2), so the writer picks another comment character for that row.
        using namespace jText::v002;
        std::ostringstream o;
        JTextWriter w(o);
        w.write_header();
        w.begin_section("s");
        w.append_row(2, {std::string("\xE2\x96\x8C lead")});
        w.append_row(3, {std::string("# also")});
        w.end_record();
        w.finalize();
        auto pf = jtext::parse_file_structure(o.str());
        check(pf.has_value(), "the leading-comment file parses");
        const auto& lines = pf->sections.at(0).data_lines;
        check(lines.at(0).values == std::vector<std::string>{"\xE2\x96\x8C lead"} &&
              lines.at(1).values == std::vector<std::string>{"# also"},
              "values that begin with a comment character read back as written");
    }

    {
        // The format version (SPEC 2.3): a reader warns when it is missing, older, newer
        // or not a number; saving upgrades a missing or older one and never lowers a newer.
        using jtext::version_status;
        check(jtext::check_version("0.8") == version_status::current, "0.8 is current");
        check(jtext::check_version(" 0.8.0 ") == version_status::current, "0.8.0 is current");
        check(jtext::check_version("") == version_status::missing, "no version is missing");
        check(jtext::check_version("0.6") == version_status::older, "0.6 is older");
        check(jtext::check_version("0.10") == version_status::newer, "0.10 is newer (numbers, not text)");
        check(jtext::check_version("1") == version_status::newer, "1 is newer");
        check(jtext::check_version("0..8") == version_status::invalid, "0..8 is invalid");
        check(jtext::check_version("v0.8") == version_status::invalid, "v0.8 is invalid");
        check(jtext::version_warning("0.8").empty(), "no warning for the current version");
        check(jtext::version_warning("0.6").find("upgrades it to 0.8") != std::string::npos,
              "the older-version warning says saving upgrades it");

        const std::string light =
            "# JText File - created 2026-01-01\n# Purpose - p\n# Case: Sensitive\n\n"
            "-- s --\n\n1. |# 1|a\n\n-- End s --\n-- EOF --\n";
        auto pf = jtext::parse_file_structure(light);
        check(pf && jtext::declared_version(*pf).empty(), "the old light file declares no version");
        auto up = jtext::replace_section_data(light, "s", "1. |# 1|b\n");
        check(up && up->find("# Purpose - p\n# jText Version: 0.8\n# Case: Sensitive\n") != std::string::npos,
              "a light section save adds the version after Purpose");
        pf = jtext::parse_file_structure(*up);
        check(pf && jtext::declared_version(*pf) == "0.8", "the saved light file declares 0.8");

        const std::string full_head = "=== jText File ===\n 2. |# 2026-01-01\n";
        const std::string full_tail =
            " 5. |# Sensitive\n\n=== Section: s ===\n=== Fields ===\n=== End Fields ===\n"
            "=== Data ===\n1. |# 1|a\n=== End Data ===\n=== End Section ===\n=== End File ===\n";
        up = jtext::replace_section_data(full_head + " 4. |# 0.6 # old\n" + full_tail, "s", "1. |# 1|b\n");
        check(up && up->find(" 2. |# 2026-01-01\n 4. |# 0.8 # old\n 5. |# ") != std::string::npos,
              "a full section save upgrades line 4, keeping its formatter and comment");
        up = jtext::replace_section_data(full_head + full_tail, "s", "1. |# 1|b\n");
        check(up && up->find(" 2. |# 2026-01-01\n 4. |# 0.8\n 5. |# ") != std::string::npos,
              "a full section save adds a missing line 4 in its place");
        up = jtext::replace_section_data(full_head + " 4. |# 0.9\n" + full_tail, "s", "1. |# 1|b\n");
        check(up && up->find(" 4. |# 0.9\n") != std::string::npos, "a newer version is never lowered");

        // JTextFile: the read notes the warning; its write declares 0.8, so the file is upgraded.
        using namespace jText::v002;
        const auto path = std::filesystem::temp_directory_path() / "jtext_version_test.jtext";
        { std::ofstream fo(path); fo << light; }
        JTextFile f;
        check(f.read_full(path.string()).has_value(), "the old light file reads");
        check(f.jtext_version.empty() && f.warnings.size() == 1, "its read warns once: no version");
        check(f.write(path.string()).has_value(), "it writes back");
        JTextFile g;
        check(g.read_full(path.string()).has_value(), "the written file reads");
        check(g.jtext_version == "0.8" && g.warnings.empty(), "the written file is 0.8, with no warning");
        std::filesystem::remove(path);
    }

    {
        // end_record(): a record that is one multifield row from field 1 needs no blank
        // line (the next row's "1." starts the next record); a record written one line
        // per field ends with one. On the direct and the auto-batched path.
        // set_filename() gives a writer attached to a stream its header line 1, the file
        // name only.
        using namespace jText::v002;
        for (const std::size_t batch : {std::size_t{0}, std::size_t{10}}) {
            std::ostringstream o;
            JTextWriter w(o);
            w.set_auto_batch_size(batch);
            w.set_profile(JTextProfile::Full);
            w.set_filename("some/dir/conn.jtext");
            w.write_header();
            w.begin_section("s");
            w.begin_data();
            w.append_row(1, {std::string("1"), std::string("a")});
            w.end_record();
            w.append_row(1, {std::string("2"), std::string("b")});
            w.end_record();
            w.append_row(1, {std::string("3")});      // a record written one line per field
            w.append_row(2, {std::string("c")});
            w.end_record();
            w.append_row(1, {std::string("4"), std::string("d")});
            w.end_record();
            w.finalize();
            const std::string text = o.str();
            check(text.find(" 1. \xE2\x96\xAE\xE2\x96\x8C conn.jtext\n") != std::string::npos, "header line 1 is the file name");
            check(text.find("1\xE2\x96\xAE" "a\n 1. ") != std::string::npos, "rows follow one another without a blank line");
            check(text.find(" c\n\n 1. ") != std::string::npos, "a per-field record ends with a blank line");
            check(text.find("4\xE2\x96\xAE" "d\n=== End Data") != std::string::npos, "no blank line after the last row");
            const auto file = jtext::parse_file_structure(text);
            check(file.has_value(), "file.has_value()");
            const auto& lines = file->sections.at(0).data_lines;
            check(lines.size() == 6 && lines[4].number == 0, "row, row, 1., 2., record end, row");
            bool name = false;
            for (const auto& hl : file->header_lines)
                if (hl.number == 1 && hl.values == std::vector<std::string>{"conn.jtext"}) name = true;
            check(name, "name");
        }
    }

    {
        // The full profile, written by the writer: header lines, sections with includes,
        // templates, a Fields block and a Data block, every block closed by finalize().
        using namespace jText::v002;
        std::ostringstream o;
        {
            JTextWriter w(o);
            w.set_profile(JTextProfile::Full);
            w.set_purpose("event log # main");
            w.write_header();
            w.begin_section("events");
            w.write_include("Schema", "../includes/main.jschma");
            w.write_template("SQL Insert", "INSERT INTO events VALUES ({1}, {2});\n");
            w.begin_fields();
            w.write_include("Fields", "../includes/main.jtFlds");
            w.begin_data();
            JTextEntry e;
            e.number = 1;
            e.delimiter = U'\u258C';
            e.level_sep = U'\u25AE';
            e.fields = {std::string("1"), std::string("a|b # c")};
            w.append_entry(e);
            e.fields = {std::string("2"), std::string("line one\nline two")};
            w.append_entry(e);
            w.begin_section("second");
            w.begin_fields();
            w.end_fields();
            w.begin_data();
            w.append_row(1, {std::string("1"), std::string("x")}, "", U'|');
            w.finalize();
        }
        const std::string text = o.str();
        check(text.starts_with("=== jText File ===\n"), "text.starts_with(\"=== jText File ===\\n\")");
        check(text.ends_with("=== End Data ===\n=== End Section ===\n\n=== End File ===\n"), "text.ends_with(\"=== End Data ===\\n=== End Section ===\\n\\n=== End File ===\\n\")");
        const auto file = jtext::parse_file_structure(text);
        check(file.has_value(), "file.has_value()");
        check(file->sections.size() == 2, "file->sections.size() == 2");
        const auto& s = file->sections[0];
        check(s.name == "events" && s.includes.size() == 2 && s.includes[0].first == "Schema", "s.name == \"events\" && s.includes.size() == 2 && s.includes[0].first == \"Schema\"");
        check(s.templates.size() == 1 && s.templates[0].body == "INSERT INTO events VALUES ({1}, {2});", "s.templates.size() == 1 && s.templates[0].body == \"INSERT INTO events VALUES ({1}, {2});\"");
        std::size_t rows = 0;
        for (const auto& pl : s.data_lines) if (pl.number != 0) ++rows;
        check(rows == 3, "rows == 3");   // the multiline record is two lines: fields 1 and 2
        check(s.data_lines.front().values == (std::vector<std::string>{"1", "a|b # c"}), "s.data_lines.front().values == (std::vector<std::string>{\"1\", \"a|b # c\"})");
        bool purpose = false, version = false;
        for (const auto& hl : file->header_lines) {
            if (hl.number == 3 && hl.values == std::vector<std::string>{"event log # main"}) purpose = true;
            if (hl.number == 4 && hl.values == std::vector<std::string>{"0.8"}) version = true;
        }
        check(purpose && version, "purpose && version");

        // JTextFile reads it, and a writer dropped without finalize() still closes it.
        const auto path = std::filesystem::temp_directory_path() / "jtext_full_profile_test.jtext";
        {
            JTextWriter w(path.string());
            w.set_profile(JTextProfile::Full);
            w.write_header();
            w.begin_section("only");
            w.begin_data();
            w.append_row(1, {std::string("7"), std::string("z")}, "", U'|');
        }
        JTextFile f;
        check(f.read_full(path.string()).has_value(), "f.read_full(path.string()).has_value()");
        check(f.sections.size() == 1 && f.sections[0].entries.size() == 1, "f.sections.size() == 1 && f.sections[0].entries.size() == 1");
        std::filesystem::remove(path);
    }

    {
        // Skip: a field holding the row's separator is reported and left out, takes no
        // record id, and only bad rows of a batch are dropped. A row with a separator
        // not in its data is written and reads back whole.
        using namespace jText::v002;
        for (const std::size_t batch : {std::size_t{0}, std::size_t{10}}) {
            const auto path = std::filesystem::temp_directory_path() / "jtext_skip_test.jtext";
            std::size_t skipped = 0;
            {
                std::ofstream fo(path);
                JTextWriter w(fo);
                check(w.bad_row_policy() == BadRowPolicy::Terminate, "w.bad_row_policy() == BadRowPolicy::Terminate");   // the default
                w.set_bad_row_policy(BadRowPolicy::Skip);
                w.set_auto_batch_size(batch);
                w.write_header();
                w.begin_section("Pipes");
                w.append_row(1, {std::string(""), std::string("a|b")}, "", '|');
                w.append_row(1, {std::string(""), std::string("ok")}, "", '|');

                JTextEntry bad;
                bad.number = 1;
                bad.level_sep = '|';
                bad.fields = {std::string("7"), std::string("x|y")};
                w.append_entry(bad);

                JTextEntry good = bad;
                good.fields = {std::string("8"), std::string("first")};
                w.append_entries(std::vector<JTextEntry>{good, bad});

                JTextEntry tilde = bad;
                tilde.level_sep = '~';
                w.append_entry(tilde);
                w.finalize();
                skipped = w.skipped_rows();
            }
            check(skipped == 3, "skipped == 3");
            std::ifstream fi(path);
            const std::string text((std::istreambuf_iterator<char>(fi)), std::istreambuf_iterator<char>());
            check(text.find("1. |\xE2\x96\x8C 1|ok") != std::string::npos, "text.find(\"1. |\\xE2\\x96\\x8C 1|ok\") != std::string::npos");    // the skipped row took no id
            check(text.find("a|b") == std::string::npos, "text.find(\"a|b\") == std::string::npos");
            check(text.find("8|first") != std::string::npos, "text.find(\"8|first\") != std::string::npos");        // the good row of the batch
            check(text.find("1. ~\xE2\x96\x8C 7~x|y") != std::string::npos, "text.find(\"1. ~\\xE2\\x96\\x8C 7~x|y\") != std::string::npos");
            JTextFile f;
            check(f.read_full(path.string()).has_value(), "f.read_full(path.string()).has_value()");
            bool whole = false;
            for (const auto& sec : f.sections)
                for (const auto& r : sec.entries)
                    if (r.fields.size() == 2 && r.fields[1] && *r.fields[1] == "x|y") whole = true;
            check(whole, "whole");
            std::filesystem::remove(path);
        }
    }

    {
        // Terminate (the default): the process ends, the reason is on stderr, and the
        // rows before the bad one are in the file. Run in a child so the test survives.
        using namespace jText::v002;
        for (const std::size_t batch : {std::size_t{0}, std::size_t{10}}) {
            const auto path = std::filesystem::temp_directory_path() / "jtext_terminate_test.jtext";
            int err_pipe[2];
            check(pipe(err_pipe) == 0, "pipe(err_pipe) == 0");
            const pid_t child = fork();
            check(child >= 0, "child >= 0");
            if (child == 0) {
                const rlimit no_core{0, 0};
                setrlimit(RLIMIT_CORE, &no_core);
                dup2(err_pipe[1], STDERR_FILENO);
                close(err_pipe[0]);
                JTextWriter w{path.string()};
                w.set_auto_batch_size(batch);
                w.write_header();
                w.begin_section("Launch");
                w.append_row(1, {std::string("1"), std::string("before")}, "", '|');
                w.append_row(1, {std::string("2"), std::string("a|b")}, "", '|');
                w.append_row(1, {std::string("3"), std::string("after")}, "", '|');
                w.finalize();
                _exit(0);   // not reached
            }
            close(err_pipe[1]);
            std::string err;
            char buf[512];
            for (ssize_t n; (n = read(err_pipe[0], buf, sizeof buf)) > 0;) err.append(buf, static_cast<std::size_t>(n));
            close(err_pipe[0]);
            int status = 0;
            waitpid(child, &status, 0);
            check(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT, "WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT");
            check(err.find("jText ERROR: cannot write a row: field 2 contains the separator '|'") != std::string::npos, "err.find(\"jText ERROR: cannot write a row: field 2 contains the separator '|'\") != std::string::npos");
            check(err.find("section: Launch") != std::string::npos, "err.find(\"section: Launch\") != std::string::npos");
            check(err.find("value:   \"a|b\"") != std::string::npos, "err.find(\"value: \\\"a|b\\\"\") != std::string::npos");
            check(err.find(path.string()) != std::string::npos, "err.find(path.string()) != std::string::npos");
            std::ifstream fi(path);
            const std::string text((std::istreambuf_iterator<char>(fi)), std::istreambuf_iterator<char>());
            check(text.find("1|before") != std::string::npos, "text.find(\"1|before\") != std::string::npos");
            check(text.find("a|b") == std::string::npos, "text.find(\"a|b\") == std::string::npos");
            check(text.find("after") == std::string::npos, "text.find(\"after\") == std::string::npos");
            std::filesystem::remove(path);
        }
    }

    std::cout << "jText::v002 aliases jText API\n";
    return EXIT_SUCCESS;
}