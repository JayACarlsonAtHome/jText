// File: tools/jtext_close.cpp
//
// jtext_close - add the closers a jText file is missing, so it reads again.
//
//   jtext_close FILE...           close each file in place, listing what was added
//   jtext_close --check FILE...   only list what is missing (exit 1 if anything is)
//
// Every block closes, like XML. A file cut off mid-write (a crash) or written
// before closers were required is missing some; each is put where its block ends.
// Nothing else in the file changes. Exit: 0 done, 1 closers missing (--check), 2 error.

#include "parser/close.hpp"
#include "parser/section.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <print>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

int main(int argc, char** argv)
{
    std::vector<std::string> files;
    bool check = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--check") check = true;
        else files.emplace_back(a);
    }
    if (files.empty()) {
        std::println(stderr, "usage: jtext_close [--check] FILE...");
        return 2;
    }

    int rc = 0;
    for (const auto& path : files) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            std::println(stderr, "{}: cannot open", path);
            rc = 2;
            continue;
        }
        const std::string text{std::istreambuf_iterator<char>(in), {}};
        in.close();

        const auto closed = jtext::add_missing_closers(text);
        if (closed.last_line_unterminated)
            std::println("{}: the last line has no newline; it may have been cut off - check it", path);
        if (closed.added.empty()) {
            std::println("{}: already closed", path);
            continue;
        }
        for (const auto& a : closed.added) std::println("{}: {}", path, a);
        if (check) {
            if (rc == 0) rc = 1;
            continue;
        }

        const std::string tmp = path + ".closing";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out << closed.text;
            if (!out) {
                std::println(stderr, "{}: cannot write {}", path, tmp);
                rc = 2;
                continue;
            }
        }
        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            std::println(stderr, "{}: cannot replace the file: {}", path, ec.message());
            rc = 2;
            continue;
        }
        if (const auto parsed = jtext::parse_file_structure(closed.text); !parsed)
            std::println("{}: closed, but it still does not parse: line {}: {}", path,
                         parsed.error().line_no, parsed.error().message);
        else
            std::println("{}: closed", path);
    }
    return rc;
}
