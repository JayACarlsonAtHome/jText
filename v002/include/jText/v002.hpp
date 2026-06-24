#pragma once

/**
 * jText::v002 — the jText structured-text library.
 *
 * The implementation now lives in-tree and the public API is declared directly in
 * namespace jText::v002 (jText.h). This is the public entry point:
 *
 *   #include <jText/v002.hpp>
 *   jText::v002::JTextWriter writer("out.jtext");
 *
 * Public symbols: JTextWriter, JTextEntry, JTextSection, JTextFile, CaseMode,
 * JTextProfile, and the write_* / jtext_* free helpers.
 */

#include <jText.h>
