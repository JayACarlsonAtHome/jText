// File: src/parser/close.hpp
//
// Closing a jText file. Every block closes, like XML (SPEC v0.8): a file that
// ends with a block still open (a crash while writing it) or that was written
// before closers were required does not parse. add_missing_closers() puts each
// missing closer where its block ends, so the next read works.

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace jtext {

struct closed_file {
    std::string              text;    // the file with every block closed
    std::vector<std::string> added;   // what was added, and where
    // The file did not end with a newline: its last line may have been cut off
    // mid-write. It is kept as it is; check it.
    bool last_line_unterminated = false;
};

// Inserts "=== End Fields ===" before a Data block, a section's closer before the
// next section, a multiline value's "*]" where it ends, and everything still open at
// the end of the file (the value, then the data or fields block, the section, the
// file). Rewrites a v0.7 "<<< SENTINEL" block as a "[*" value, line for line.
// Changes nothing else. A file already closed comes back unchanged.
auto add_missing_closers(std::string_view text) -> closed_file;

}  // namespace jtext
