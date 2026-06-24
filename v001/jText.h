#pragma once

#include <expected>
#include <string>
#include <vector>
#include <optional>
#include <fstream>
#include <print>
#include <format>
#include <source_location>
#include <cstddef>
#include <span>

// Public jText API now lives under jText::v001 (migrated in-tree; the
// internal parser/validator stay in namespace jtext). The legacy global-namespace
// API is gone — consumers use jText::v001 (or `using namespace`).
//
// The declarations live in jText.api.inc (an include-free fragment). This textual
// header makes std visible via the includes above, then pastes the fragment inside
// the namespace; the gcc import-std module includes the SAME fragment under
// `import std;`. Keep declarations in the fragment.
namespace jText::v001 {

#include "jText.api.inc"

} // namespace jText::v001
