module;

#include <jText.h>

export module jac313.jtext.v002.reader;

export import jac313.jtext.v002.core;

// Re-export under the namespace (see core partition for why not bare `export using`).
export namespace jac313::jText::v002 {
    using jac313::jText::v002::JTextFile;
}