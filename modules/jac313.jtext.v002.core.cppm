module;

#include <jText.h>

export module jac313.jtext.v002.core;

// Re-export under the namespace, not bare `export using` (which injects the names
// into the GLOBAL namespace and diverges from the header's jac313::jText::v002::
// spelling). Importers then see the symbols qualified, matching the header + Qlite.
export namespace jac313::jText::v002 {
    using jac313::jText::v002::version;
    using jac313::jText::v002::CaseMode;
    using jac313::jText::v002::JTextEntry;
    using jac313::jText::v002::JTextSection;
}