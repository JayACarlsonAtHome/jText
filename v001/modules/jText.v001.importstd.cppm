// Module-native jText interface unit — `import std;` front-end (gcc pilot, opt-in
// via JTEXT_IMPORT_STD). A single interface unit exports the full public API
// under jText::v001 from the shared declaration fragment (jText.api.inc);
// the out-of-line definitions live in the companion implementation unit
// (jText.v001.importstd.impl.cpp). This replaces the textual partition
// modules (core/reader/writer + aggregator) for the import-std build.
//
// Same public API as the textual header; only differs in how std is supplied
// (`import std;` here vs textual #includes in jText.h).
export module jText.v001;

import std;

export namespace jText::v001 {
#include "jText.api.inc"
}
