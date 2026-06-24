module;

#include <jText.h>

export module jText.v002.core;

// Re-export under the namespace, not bare `export using` (which injects the names
// into the GLOBAL namespace and diverges from the header's jText::v002::
// spelling). Importers then see the symbols qualified, matching the header + Qlite.
export namespace jText::v002 {
    using jText::v002::version;
    using jText::v002::CaseMode;
    using jText::v002::JTextEntry;
    using jText::v002::JTextSection;
}