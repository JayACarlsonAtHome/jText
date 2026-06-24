#pragma once

/**
 * jText::v001 — the jText structured-text library.
 *
 * The implementation now lives in-tree and the public API is declared directly in
 * namespace jText::v001 (jText.h). This is the public entry point:
 *
 *   #include <jText/v001.hpp>
 *   jText::v001::JTextWriter writer("out.jtext");
 *
 * Public symbols: JTextWriter, JTextEntry, JTextSection, JTextFile, CaseMode,
 * JTextProfile, and the write_* / jtext_* free helpers.
 */

#include <jText.h>
