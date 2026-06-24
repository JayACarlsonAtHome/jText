module;

#include <jText.h>

export module jText.v001.core;

// Re-export under the namespace, not bare `export using` (which injects the names
// into the GLOBAL namespace and diverges from the header's jText::v001::
// spelling). Importers then see the symbols qualified, matching the header + Qlite.
export namespace jText::v001 {
    using jText::v001::version;
    using jText::v001::CaseMode;
    using jText::v001::JTextEntry;
    using jText::v001::JTextSection;
}