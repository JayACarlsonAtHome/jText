#include <cstdlib>
#include <cstddef>

import jText.v002;

// Reference the exported symbols by their FULLY-QUALIFIED names — exactly what
// bare `export using` broke (it injected them into the global namespace instead
// of jText::v002::). Covers all three partitions: core (CaseMode,
// JTextEntry, JTextSection), reader (JTextFile), writer (JTextProfile,
// JTextWriter, and a free function). The old bare-import smoke referenced no
// symbol, so it could not catch this.
int main() {
    using namespace_ok = jText::v002::JTextFile;        // reader partition
    (void)sizeof(namespace_ok);
    (void)sizeof(jText::v002::JTextEntry);              // core
    (void)sizeof(jText::v002::JTextSection);            // core
    (void)sizeof(jText::v002::JTextWriter);             // writer

    const jText::v002::CaseMode cm = jText::v002::CaseMode::Sensitive;
    const jText::v002::JTextProfile pf = jText::v002::JTextProfile::Full;
    const std::size_t w = jText::v002::jtext_line_pad_width_for_count(42);

    return (cm == jText::v002::CaseMode::Sensitive
            && pf == jText::v002::JTextProfile::Full
            && w >= 1)
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
