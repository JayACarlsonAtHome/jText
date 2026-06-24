module;

#include <jText.h>

export module jText.v001.reader;

export import jText.v001.core;

// Re-export under the namespace (see core partition for why not bare `export using`).
export namespace jText::v001 {
    using jText::v001::JTextFile;
}