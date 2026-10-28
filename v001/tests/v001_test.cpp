#include <jText/v001.hpp>

static_assert(jText::v001::version() == "v001.014", "v001 version");

#include "parser/close.hpp"
#include "parser/line.hpp"
#include "parser/section.hpp"
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
        CHECK(o.str().find("2. |\xE2\x96\x8C \x1F") != std::string::npos);
    }

    {
        // Like a database: Insensitive is for comparing; stored values keep their case.
        using namespace jText::v001;
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
        CHECK(f.read_full(path.string()).has_value());
        CHECK(f.case_mode == CaseMode::Insensitive);
        bool kept = false;
        for (const auto& sec : f.sections)
            for (const auto& e : sec.entries)
                for (const auto& fld : e.fields)
                    if (fld && *fld == "Hammer") kept = true;
        CHECK(kept);
        std::filesystem::remove(path);
        CHECK(text_equal("Hammer", "HAMMER", CaseMode::Insensitive));
        CHECK(!text_equal("Hammer", "HAMMER", CaseMode::Sensitive));
        CHECK(text_equal("Hammer", "Hammer", CaseMode::Sensitive));
        CHECK(!text_equal("Hammer", "Hammers", CaseMode::Insensitive));
    }

    {
        // A row's bookend can be custom (|^) so a value may contain " # ". The writer
        // must emit that bookend, and use it for the comment marker, on both the
        // direct and the auto-batched path.
        using namespace jText::v001;
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
            CHECK(text.find("1. |^ 6|C # sharp ^ safe comment") != std::string::npos);
            JTextFile f;
            CHECK(f.read_full(path.string()).has_value());
            bool found = false;
            for (const auto& sec : f.sections)
                for (const auto& r : sec.entries)
                    if (r.fields.size() == 2 && r.fields[1] && *r.fields[1] == "C # sharp"
                        && r.comment == "safe comment") found = true;
            CHECK(found);
            std::filesystem::remove(path);
        }
    }

    {
        // Left on the default comment character (U+258C), a row whose data contains it
        // gets a punctuation comment character that is not in its data, on every writer
        // path. A '#' in the data no longer matters.
        using namespace jText::v001;
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
            for (const auto& c : cases) CHECK(text.find(c.line) != std::string::npos);
            JTextFile f;
            CHECK(f.read_full(path.string()).has_value());
            std::size_t matched = 0;
            for (const auto& sec : f.sections)
                for (const auto& r : sec.entries)
                    for (const auto& c : cases)
                        if (r.fields.size() == 2 && r.fields[0] && *r.fields[0] == c.fields[0]
                            && r.fields[1] && *r.fields[1] == c.fields[1] && r.comment == "note") ++matched;
            CHECK(matched == cases.size());
            std::filesystem::remove(path);
        }
    }

    {
        // The row scan must find a '|' or a U+258C at every position of every length.
        using namespace jText::v001;
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
                CHECK(w.skipped_rows() == 1);
                CHECK(o.str().find("1. |\xE2\x96\x8C 1|") != std::string::npos);
                CHECK(o.str().find("1. |# 3|" + mark) != std::string::npos);
            }
        }
    }

    {
        // With U+258C as the separator, the comment character must not also be U+258C
        // (the two formatter characters must differ): the writer falls back to '#'.
        using namespace jText::v001;
        std::ostringstream o;
        JTextWriter w(o);
        w.write_header();
        w.begin_section("CommentSep");
        w.append_row(1, {std::string("1"), std::string("a")}, "", U'\u258C');
        w.finalize();
        CHECK(o.str().find("1. \xE2\x96\x8C# 1\xE2\x96\x8C" "a") != std::string::npos);
    }

    {
        // One line grammar for every reader. In a data row every separator splits and
        // empty values are kept (the writer's exact inverse); in a hierarchy an empty
        // level is an error.
        auto fields = jtext::parse_line("1. |# a||b|", jtext::split_rule::fields);
        CHECK(fields.has_value());
        CHECK((fields->hierarchy == std::vector<std::string>{"a", "", "b", ""}));
        CHECK(!jtext::parse_line("1. |# a||b|").has_value());

        using namespace jText::v001;
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
        CHECK(f.read_full(path.string()).has_value());
        CHECK(f.sections.size() == 1);
        const auto& rows = f.sections[0].entries;
        CHECK(rows.size() == 4);
        CHECK(rows[0].fields.size() == 3 && rows[0].fields[2] && rows[0].fields[2]->empty());
        CHECK(rows[1].fields.size() == 4 && rows[1].fields[1]->empty() && !rows[1].fields[3].has_value());
        CHECK(rows[2].fields.size() == 4 && *rows[2].fields[3] == "c" && rows[2].comment == "first # second");
        CHECK(rows[3].fields.size() == 2 && *rows[3].fields[1] == "a|b # c" && rows[3].comment == "v0.8 formatter");
        CHECK(rows[3].level_sep == U'\u25AE' && rows[3].delimiter == U'\u258C');
        std::filesystem::remove(path);
    }

    {
        // Writer to reader: empty strings and nulls come back as written.
        using namespace jText::v001;
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
        CHECK(f.read_full(path.string()).has_value());
        const auto& r = f.sections.at(0).entries.at(0);
        CHECK(r.fields.size() == 5);
        CHECK(r.fields[1] && r.fields[1]->empty());
        CHECK(!r.fields[2].has_value());
        CHECK(r.fields[3] && *r.fields[3] == "x");
        CHECK(r.fields[4] && r.fields[4]->empty());
        std::filesystem::remove(path);
    }

    {
        // No separator given (level_sep 0): the writer joins with U+25AE, or with a
        // character not in the data when the data holds U+25AE. Never with a NUL byte.
        using namespace jText::v001;
        std::ostringstream o;
        JTextWriter w(o);
        w.write_header();
        w.begin_section("Flat");
        w.append_row(1, {std::string("Hammer"), std::string("Mixed Case")});
        w.append_row(1, {std::string("2026-10-28")});
        w.append_row(1, {std::string("a\xE2\x96\xAE"), std::string("b")});
        w.finalize();
        CHECK(o.str().find("1. \xE2\x96\xAE\xE2\x96\x8C Hammer\xE2\x96\xAEMixed Case") != std::string::npos);
        CHECK(o.str().find("1. \xE2\x96\xAE\xE2\x96\x8C 2026-10-28") != std::string::npos);
        CHECK(o.str().find("1. ^\xE2\x96\x8C a\xE2\x96\xAE^b") != std::string::npos);
        CHECK(o.str().find('\0') == std::string::npos);
    }

    {
        // SPEC v0.8: a formatter character is any Unicode punctuation or symbol.
        CHECK(jtext::is_valid_formatter_char(U'\u25AE') && jtext::is_valid_formatter_char(U'\u258C'));
        CHECK(jtext::is_valid_formatter_char(U'|') && jtext::is_valid_formatter_char(U'\u20AC'));   // | and the euro sign
        CHECK(!jtext::is_valid_formatter_char(U'a') && !jtext::is_valid_formatter_char(U'\u00E9')); // letters
        CHECK(!jtext::is_valid_formatter_char(U'7') && !jtext::is_valid_formatter_char(U' '));
        CHECK(!jtext::is_valid_formatter_char(U'\u00A0') && !jtext::is_valid_formatter_char(U'\u200D')); // NBSP, ZWJ
        CHECK(!jtext::is_valid_formatter_char(U'\u0301'));                                            // combining mark

        auto both = [](std::string_view line) {
            return std::pair{jtext::parse_line(line, jtext::split_rule::fields), jtext::parse_line(line)};
        };
        auto [f, h] = both("1. \u25AE\u258C 1\u25AEcmd | grep T\u25AElaunch # 3 \u258C a note");
        CHECK(f && h);
        CHECK((f->hierarchy == std::vector<std::string>{"1", "cmd | grep T", "launch # 3"}));
        CHECK(f->comment == "a note" && h->hierarchy == f->hierarchy);
        CHECK(f->fmt.separator == U'\u25AE' && f->fmt.bookend == U'\u258C');

        const auto empty = jtext::parse_line("1. \u25AE\u258C a\u25AE\u25AEb\u25AE", jtext::split_rule::fields);
        CHECK(empty && (empty->hierarchy == std::vector<std::string>{"a", "", "b", ""}));

        const auto legacy = jtext::parse_line("1. \u258C\u25AE\u258C x\u25AEy");    // old three-character form
        CHECK(legacy && legacy->fmt.separator == U'\u25AE' && legacy->fmt.bookend == U'\u258C');

        auto fails = [](std::string_view line, jtext::parse_error_kind kind) {
            const auto r = jtext::parse_line(line);
            return !r && r.error().kind == kind;
        };
        CHECK(fails("1. \u25AEa x", jtext::parse_error_kind::formatter_too_short));          // letter
        CHECK(fails("1. \u25AE\u0301 x", jtext::parse_error_kind::formatter_too_short));    // combining mark
        CHECK(fails("1. \u25AE\u200D x", jtext::parse_error_kind::formatter_too_short));    // zero-width joiner
        CHECK(fails("1. \u25AE\u25AE x", jtext::parse_error_kind::formatter_separator_equals_bookend));
        CHECK(fails("1. \xE2\x96 x", jtext::parse_error_kind::invalid_utf8));                // truncated
        CHECK(fails("1. \xC0\xBC| x", jtext::parse_error_kind::invalid_utf8));               // overlong '<'

        // The writer emits the code points it is given, and the reader gets them back.
        using namespace jText::v001;
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
        CHECK(skipped == 1);
        std::ifstream fi(path);
        const std::string text((std::istreambuf_iterator<char>(fi)), std::istreambuf_iterator<char>());
        CHECK(text.find("1. \u25AE\u258C 1\u25AEa|b # c\u25AEx \u258C note") != std::string::npos);
        JTextFile file;
        CHECK(file.read_full(path.string()).has_value());
        const auto& r = file.sections.at(0).entries.at(0);
        CHECK(r.fields.size() == 3 && *r.fields[1] == "a|b # c" && r.comment == "note");
        CHECK(r.level_sep == U'\u25AE' && r.delimiter == U'\u258C');
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
        CHECK(!banner_less.has_value());
        const auto top_level = jtext::parse_file_structure(
            "=== <#include#> Fields: f.jtFlds ===\n1. |# 1|a|b\n");
        CHECK(!top_level.has_value());

        const auto file = jtext::parse_file_structure(
            "=== jText File ===\n 1. ?# banner\n\n=== Section: s ===\n"
            "=== Fields ===\n 1. /# id/Identity(1,1)/Not Null\n 2. /# a/String\n 3. /# b/String\n"
            "=== End Fields ===\n=== Data ===\n"
            "1. |# 1|a|b\n\n"
            "1. \u25AE\u258C 2\u25AEc|d\u25AEe\n"
            "=== End Data ===\n=== End Section ===\n=== End File ===\n");
        CHECK(file.has_value());
        CHECK(file->sections.size() == 1);
        const auto& lines = file->sections[0].data_lines;
        std::size_t rows = 0;
        for (const auto& pl : lines) if (pl.number != 0) ++rows;
        CHECK(rows == 2);
        CHECK(lines.back().hierarchy == (std::vector<std::string>{"2", "c|d", "e"}));
    }

    {
        // Every block closes, like XML. The writer closes each section and the file,
        // and both readers see exactly the sections that were written.
        using namespace jText::v001;
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
        CHECK(text.find("-- A --") < text.find("-- End A --"));
        CHECK(text.find("-- End A --") < text.find("-- B --"));
        CHECK(text.find("-- B --") < text.find("-- End B --"));
        CHECK(text.find("-- End B --") < text.find("-- EOF --"));
        const auto strict = jtext::parse_file_structure(text);
        CHECK(strict.has_value() && strict->sections.size() == 2);
        JTextFile f;
        CHECK(f.read_full(path.string()).has_value());
        CHECK(f.sections.size() == 2);
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
        CHECK(kind(closed) == "ok");
        CHECK(kind(full) == "missing_closer");
        CHECK(kind(full + "=== End Data ===\n=== End Section ===\n") == "missing_closer");
        CHECK(kind(closed + "1. |# 3|c\n") == "unexpected_marker");          // after the file closed
        const std::string light = "# JText File - created x\n\n-- A --\n1. |# 1|a\n";
        CHECK(kind(light + "-- End A --\n-- EOF --\n") == "ok");
        CHECK(kind(light) == "missing_closer");
        CHECK(kind(light + "-- End B --\n-- EOF --\n") == "unexpected_marker"); // closes the wrong section

        // add_missing_closers puts each where its block ends; the next read works.
        auto cut = jtext::add_missing_closers(full);
        CHECK(cut.added.size() == 3 && !cut.last_line_unterminated);
        CHECK(cut.text == closed);
        CHECK(kind(cut.text) == "ok");
        const auto again = jtext::add_missing_closers(cut.text);
        CHECK(again.added.empty() && again.text == cut.text);

        // Written before closers were required: End Fields before Data, a section
        // closed before the next one, the file at the end.
        const std::string old =
            "=== jText File ===\n 1. ?# old\n\n"
            "=== Section: a ===\n=== Fields ===\n 1. /# id/Identity(1,1)\n=== Data ===\n1. |# 1\n\n"
            "=== Section: b ===\n=== Fields ===\n 1. /# id/Identity(1,1)\n=== Data ===\n1. |# 1\n";
        const auto fixed = jtext::add_missing_closers(old);
        CHECK(fixed.added.size() == 7);
        CHECK(kind(fixed.text) == "ok");
        const auto old_light = jtext::add_missing_closers(
            "# JText File - created x\n-- A --\n1. |# 1|a\n\n-- B --\n1. |# 1|b");
        CHECK(old_light.last_line_unterminated && old_light.added.size() == 3);
        CHECK(kind(old_light.text) == "ok");

        // Cut off inside a multiline value: its "*]" comes first, aligned with its lines.
        const auto ml = jtext::add_missing_closers(
            "# JText File - created x\n-- A --\n 1. [* # note\n    ** line one\n");
        CHECK(ml.added.size() == 3 && ml.added[0].find("'*]'") != std::string::npos);
        CHECK(ml.text.find("    ** line one\n    *]\n-- End A --\n-- EOF --\n") != std::string::npos);
        CHECK(kind(ml.text) == "ok");

        // A v0.7 "<<< SENTINEL" block is rewritten as a [* value, line for line: in a
        // data row it keeps its number; a template's opener is a bare "[*".
        const auto old_ml = jtext::add_missing_closers(
            "# JText File - created x\n-- A --\n 2. <<< END  # note\nline one\n\n  indented\nEND\n"
            "-- End A --\n-- EOF --\n");
        CHECK(old_ml.text.find(" 2. [* # note\n    ** line one\n    **\n    **   indented\n    *]\n")
              != std::string::npos);
        CHECK(kind(old_ml.text) == "ok");
        const auto old_tpl = jtext::add_missing_closers(
            "=== jText File ===\n 1. ?# t\n\n=== Section: s ===\n=== Template: T ===\n"
            " 1. <<< !!!T!!!\nINSERT {1};\n!!!T!!!\n=== Fields ===\n 1. /# id/Identity(1,1)\n"
            "=== End Fields ===\n=== Data ===\n1. |# 1\n=== End Data ===\n=== End Section ===\n"
            "=== End File ===\n");
        CHECK(old_tpl.text.find("=== Template: T ===\n[*\n** INSERT {1};\n*]\n") != std::string::npos);
        CHECK(kind(old_tpl.text) == "ok");
    }

    {
        // Multiline values (SPEC v0.8 Part C): "[*" opens, "** " lines, a bare "*]".
        using VK = jtext::value_line_kind;
        auto vl = [](std::string_view line) { return jtext::parse_value_line(line); };
        CHECK(vl("    ** text")->kind == VK::content && vl("    ** text")->text == "text");
        CHECK(vl("**   indented")->text == "  indented");     // one space is the marker's
        CHECK(vl("**")->kind == VK::content && vl("**")->text.empty());
        CHECK(vl("** ")->text.empty());
        CHECK(vl("** trailing  ")->text == "trailing  ");     // verbatim
        CHECK(vl("\t*]")->kind == VK::closer && vl("*]  ")->kind == VK::closer);
        CHECK(vl(" 7. |# x")->kind == VK::other);
        CHECK(!vl("**x").has_value() && !vl("*] more").has_value());

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
        CHECK(file.has_value());
        CHECK(file->sections[0].templates[0].body == "INSERT INTO t VALUES ({1}, {2});");
        CHECK(file->sections[0].templates[0].opener_comment == "one statement");
        const auto& value = file->sections[0].data_lines.back();
        CHECK(value.kind == jtext::line_kind::multiline_opener && value.comment == "from worker 3");
        CHECK(value.hierarchy == (std::vector<std::string>{"Traceback:\n\n  File \"main.py\" | # 12"}));
        CHECK(kind(head) == "missing_closer");                      // end of file
        CHECK(kind(head + tail) == "missing_closer");               // a marker
        CHECK(kind(head + "\n    *]\n" + tail) == "missing_closer"); // a blank line is not "**"

        // The writer: a field with a newline becomes an aligned [* value, the row
        // breaking around it; its '|' and '#' are not separators or comments there.
        using namespace jText::v001;
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
            CHECK(text.find(" 1. |\xE2\x96\x8C 1 \xE2\x96\x8C note\n"
                            " 2. [*\n"
                            "    ** line one | # x\n"
                            "    **   indented\n"
                            "    **\n"
                            "    ** last\n"
                            "    *]\n"
                            " 3. |\xE2\x96\x8C tail\n") != std::string::npos);
            JTextFile f;
            CHECK(f.read_full(path.string()).has_value());
            const auto& rows = f.sections.at(0).entries;
            CHECK(rows.size() == 3);
            CHECK(rows[0].comment == "note" && rows[1].number == 2);
            CHECK(rows[1].fields.size() == 1 && *rows[1].fields[0] == "line one | # x\n  indented\n\nlast");
            CHECK(rows[2].number == 3 && *rows[2].fields[0] == "tail");

            // Written back by JTextFile, the file is the same.
            const auto again = std::filesystem::temp_directory_path() / "jtext_multiline_again.jtext";
            CHECK(f.write(again.string()).has_value());
            std::ifstream fa(again);
            const std::string text2((std::istreambuf_iterator<char>(fa)), std::istreambuf_iterator<char>());
            CHECK(text2.find(" 2. [*\n    ** line one | # x\n    **   indented\n    **\n    ** last\n    *]\n")
                  != std::string::npos);
            std::filesystem::remove(path);
            std::filesystem::remove(again);
        }
    }

    {
        // A file of configurations (dev, test, prod): replacing one section's data
        // leaves every other line of the file exactly as it was.
        auto section = [](std::string_view name, std::string_view rows) {
            return std::string("=== Section: ") + std::string(name) + " ===\n"
                   "=== Template: Insert ===\n[*\n** INSERT {1};\n*]\n"
                   "=== Fields ===\n 1. /# id/Identity(1,1)\n 2. /# host/String\n=== End Fields ===\n"
                   "=== Data ===\n" + std::string(rows) + "=== End Data ===\n=== End Section ===\n\n";
        };
        const std::string head = "=== jText File ===\n 1. ?# configs\n\n";
        const std::string dev = section("dev", " 1. |# 1|dev.local\n");
        const std::string prod = section("prod", " 1. |# 1|prod.example\n# keep me\n");
        const std::string full = head + dev + section("test", " 1. |# 1|test.local\n\n 1. |# 2|test2\n")
                                 + prod + "=== End File ===\n";
        const auto replaced = jtext::replace_section_data(full, "test", " 1. |# 1|qa.local\n");
        CHECK(replaced.has_value());
        CHECK(*replaced == head + dev + section("test", " 1. |# 1|qa.local\n") + prod + "=== End File ===\n");
        const auto back = jtext::parse_file_structure(*replaced);
        CHECK(back.has_value() && back->sections.size() == 3);
        CHECK(back->sections[1].data_lines.back().hierarchy == (std::vector<std::string>{"1", "qa.local"}));

        const std::string light =
            "# JText File - created x\n\n"
            "-- dev --\n# Fields: cfg.jtFlds\n 1. |# 1|dev.local\n-- End dev --\n\n"
            "-- test --\n# Fields: cfg.jtFlds\n// note above the rows\n 1. |# 1|test.local\n-- End test --\n\n"
            "-- prod --\n 1. |# 1|prod.example\n-- End prod --\n\n-- EOF --";   // no final newline
        const auto light_replaced = jtext::replace_section_data(light, "test", " 1. |# 1|qa.local\n 1. |# 2|qa2\n");
        CHECK(light_replaced.has_value());
        CHECK(*light_replaced ==
              "# JText File - created x\n\n"
              "-- dev --\n# Fields: cfg.jtFlds\n 1. |# 1|dev.local\n-- End dev --\n\n"
              "-- test --\n# Fields: cfg.jtFlds\n// note above the rows\n 1. |# 1|qa.local\n 1. |# 2|qa2\n-- End test --\n\n"
              "-- prod --\n 1. |# 1|prod.example\n-- End prod --\n\n-- EOF --");
        CHECK(jtext::parse_file_structure(*light_replaced).has_value());

        // An empty light section takes its first rows right before its closer.
        const auto empty = jtext::replace_section_data(
            "# JText File - created x\n-- new --\n-- End new --\n-- EOF --\n", "new", " 1. |# 1|a\n");
        CHECK(empty && *empty == "# JText File - created x\n-- new --\n 1. |# 1|a\n-- End new --\n-- EOF --\n");

        auto kind = [](const std::expected<std::string, jtext::file_error>& r) {
            return r ? std::string("ok") : std::string(jtext::to_string(r.error().kind));
        };
        CHECK(kind(jtext::replace_section_data(full, "staging", "")) == "section_not_found");
        CHECK(kind(jtext::replace_section_data(head + dev + dev + "=== End File ===\n", "dev", "")) == "section_ambiguous");
        CHECK(kind(jtext::replace_section_data(full.substr(0, full.size() - 17), "test", "")) == "missing_closer");
    }

    {
        // //File: holds the file's name only: a path would go stale when the file moves.
        using namespace jText::v001;
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
        CHECK(first == "//File:    named.jtext");
        std::filesystem::remove_all(dir);
    }

    {
        // end_record() ends a record with a blank line, so the next field-1 row starts a
        // new record, on the direct and the auto-batched path. set_filename() gives a
        // writer attached to a stream its header line 1, the file name only.
        using namespace jText::v001;
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
            w.finalize();
            const std::string text = o.str();
            CHECK(text.find(" 1. \xE2\x96\xAE\xE2\x96\x8C conn.jtext\n") != std::string::npos);
            CHECK(text.find("1\xE2\x96\xAE" "a\n\n") != std::string::npos);
            const auto file = jtext::parse_file_structure(text);
            CHECK(file.has_value());
            const auto& lines = file->sections.at(0).data_lines;
            CHECK(lines.size() == 3 && lines[1].number == 0);   // row, record end, row
            bool name = false;
            for (const auto& hl : file->header_lines)
                if (hl.number == 1 && hl.hierarchy == std::vector<std::string>{"conn.jtext"}) name = true;
            CHECK(name);
        }
    }

    {
        // The full profile, written by the writer: header lines, sections with includes,
        // templates, a Fields block and a Data block, every block closed by finalize().
        using namespace jText::v001;
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
        CHECK(text.starts_with("=== jText File ===\n"));
        CHECK(text.ends_with("=== End Data ===\n=== End Section ===\n\n=== End File ===\n"));
        const auto file = jtext::parse_file_structure(text);
        CHECK(file.has_value());
        CHECK(file->sections.size() == 2);
        const auto& s = file->sections[0];
        CHECK(s.name == "events" && s.includes.size() == 2 && s.includes[0].first == "Schema");
        CHECK(s.templates.size() == 1 && s.templates[0].body == "INSERT INTO events VALUES ({1}, {2});");
        std::size_t rows = 0;
        for (const auto& pl : s.data_lines) if (pl.number != 0) ++rows;
        CHECK(rows == 3);   // the multiline record is two lines: fields 1 and 2
        CHECK(s.data_lines.front().hierarchy == (std::vector<std::string>{"1", "a|b # c"}));
        bool purpose = false, version = false;
        for (const auto& hl : file->header_lines) {
            if (hl.number == 3 && hl.hierarchy == std::vector<std::string>{"event log # main"}) purpose = true;
            if (hl.number == 4 && hl.hierarchy == std::vector<std::string>{"0.8"}) version = true;
        }
        CHECK(purpose && version);

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
        CHECK(f.read_full(path.string()).has_value());
        CHECK(f.sections.size() == 1 && f.sections[0].entries.size() == 1);
        std::filesystem::remove(path);
    }

    {
        // Skip: a field holding the row's separator is reported and left out, takes no
        // record id, and only bad rows of a batch are dropped. A row with a separator
        // not in its data is written and reads back whole.
        using namespace jText::v001;
        for (const std::size_t batch : {std::size_t{0}, std::size_t{10}}) {
            const auto path = std::filesystem::temp_directory_path() / "jtext_skip_test.jtext";
            std::size_t skipped = 0;
            {
                std::ofstream fo(path);
                JTextWriter w(fo);
                CHECK(w.bad_row_policy() == BadRowPolicy::Terminate);   // the default
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
            CHECK(skipped == 3);
            std::ifstream fi(path);
            const std::string text((std::istreambuf_iterator<char>(fi)), std::istreambuf_iterator<char>());
            CHECK(text.find("1. |\xE2\x96\x8C 1|ok") != std::string::npos);    // the skipped row took no id
            CHECK(text.find("a|b") == std::string::npos);
            CHECK(text.find("8|first") != std::string::npos);        // the good row of the batch
            CHECK(text.find("1. ~\xE2\x96\x8C 7~x|y") != std::string::npos);
            JTextFile f;
            CHECK(f.read_full(path.string()).has_value());
            bool whole = false;
            for (const auto& sec : f.sections)
                for (const auto& r : sec.entries)
                    if (r.fields.size() == 2 && r.fields[1] && *r.fields[1] == "x|y") whole = true;
            CHECK(whole);
            std::filesystem::remove(path);
        }
    }

    {
        // Terminate (the default): the process ends, the reason is on stderr, and the
        // rows before the bad one are in the file. Run in a child so the test survives.
        using namespace jText::v001;
        for (const std::size_t batch : {std::size_t{0}, std::size_t{10}}) {
            const auto path = std::filesystem::temp_directory_path() / "jtext_terminate_test.jtext";
            int err_pipe[2];
            CHECK(pipe(err_pipe) == 0);
            const pid_t child = fork();
            CHECK(child >= 0);
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
            CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
            CHECK(err.find("jText ERROR: cannot write a row: field 2 contains the separator '|'") != std::string::npos);
            CHECK(err.find("section: Launch") != std::string::npos);
            CHECK(err.find("value:   \"a|b\"") != std::string::npos);
            CHECK(err.find(path.string()) != std::string::npos);
            std::ifstream fi(path);
            const std::string text((std::istreambuf_iterator<char>(fi)), std::istreambuf_iterator<char>());
            CHECK(text.find("1|before") != std::string::npos);
            CHECK(text.find("a|b") == std::string::npos);
            CHECK(text.find("after") == std::string::npos);
            std::filesystem::remove(path);
        }
    }

    std::cout << "jText::v001 aliases jText API\n";
    return EXIT_SUCCESS;
}