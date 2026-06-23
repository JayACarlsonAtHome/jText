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

// Public jText API now lives under jac313::jText::v002 (migrated in-tree; the
// internal parser/validator stay in namespace jtext). The legacy global-namespace
// API is gone — consumers use jac313::jText::v002 (or `using namespace`).
//
// The declarations live in jText.api.inc (an include-free fragment). This textual
// header makes std visible via the includes above, then pastes the fragment inside
// the namespace; the gcc import-std module includes the SAME fragment under
// `import std;`. Keep declarations in the fragment.
namespace jac313::jText::v002 {

#include "jText.api.inc"

} // namespace jac313::jText::v002
