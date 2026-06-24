module;

#include <jText.h>

export module jText.v002.reader;

export import jText.v002.core;

// Re-export under the namespace (see core partition for why not bare `export using`).
export namespace jText::v002 {
    using jText::v002::JTextFile;
}