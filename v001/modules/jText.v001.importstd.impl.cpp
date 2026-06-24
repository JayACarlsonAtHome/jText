// Module-native jText implementation unit — attached to jText.v001 so its
// definitions have module linkage matching the interface unit's declarations (an
// implementation unit implicitly imports its primary interface). Mirrors jText.cpp's
// textual front-end over the SAME impl fragment; std comes from `import std;` instead
// of textual #includes, and the file-scope fs alias / std::literals are reproduced.
//
// Self-contained: the public API uses no jtext:: internals (parser/validator), so
// this links nothing — jtext_core is for the textual path only.
//
// Global module fragment: `import std;` exports the std:: library but NOT the global
// C names the impl uses (the `stderr` macro, global `::toupper`). Pull those few via
// the C headers textually — gcc permits textual std/C in the GMF alongside import std.
module;
#include <cstdio>    // stderr (C macro; not exported by `import std;`)
#include <ctype.h>   // ::toupper (used global-qualified in the impl fragment)

module jText.v001;

import std;

namespace fs = std::filesystem;
using namespace std::literals;

namespace jText::v001 {
#include "jText.impl.inc"
}
