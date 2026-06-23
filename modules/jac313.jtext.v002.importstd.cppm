// Module-native jText interface unit — `import std;` front-end (gcc pilot, opt-in
// via JAC313_JTEXT_IMPORT_STD). A single interface unit exports the full public API
// under jac313::jText::v002 from the shared declaration fragment (jText.api.inc);
// the out-of-line definitions live in the companion implementation unit
// (jac313.jtext.v002.importstd.impl.cpp). This replaces the textual partition
// modules (core/reader/writer + aggregator) for the import-std build.
//
// Same public API as the textual header; only differs in how std is supplied
// (`import std;` here vs textual #includes in jText.h).
export module jac313.jtext.v002;

import std;

export namespace jac313::jText::v002 {
#include "jText.api.inc"
}
