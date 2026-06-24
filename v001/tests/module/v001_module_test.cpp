#include <cstdlib>
#include <cstddef>

import jText.v001;

// Reference the exported symbols by their FULLY-QUALIFIED names — exactly what
// bare `export using` broke (it injected them into the global namespace instead
// of jText::v001::). Covers all three partitions: core (CaseMode,
// JTextEntry, JTextSection), reader (JTextFile), writer (JTextProfile,
// JTextWriter, and a free function). The old bare-import smoke referenced no
// symbol, so it could not catch this.
int main() {
    using namespace_ok = jText::v001::JTextFile;        // reader partition
    (void)sizeof(namespace_ok);
    (void)sizeof(jText::v001::JTextEntry);              // core
    (void)sizeof(jText::v001::JTextSection);            // core
    (void)sizeof(jText::v001::JTextWriter);             // writer

    const jText::v001::CaseMode cm = jText::v001::CaseMode::Sensitive;
    const jText::v001::JTextProfile pf = jText::v001::JTextProfile::Full;
    const std::size_t w = jText::v001::jtext_line_pad_width_for_count(42);

    return (cm == jText::v001::CaseMode::Sensitive
            && pf == jText::v001::JTextProfile::Full
            && w >= 1)
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
