#pragma once

/**
 * jac313::jText::v002 — the jac313 structured-text library.
 *
 * The implementation now lives in-tree and the public API is declared directly in
 * namespace jac313::jText::v002 (jText.h). This is the public entry point:
 *
 *   #include <jac313/jText/v002.hpp>
 *   jac313::jText::v002::JTextWriter writer("out.jtext");
 *
 * Public symbols: JTextWriter, JTextEntry, JTextSection, JTextFile, CaseMode,
 * JTextProfile, and the write_* / jtext_* free helpers.
 */

#include <jText.h>
