module;

#include <jText.h>

export module jac313.jtext.v002.writer;

export import jac313.jtext.v002.core;

// Re-export under the namespace (see core partition for why not bare `export using`).
export namespace jac313::jText::v002 {
    using jac313::jText::v002::JTextProfile;
    using jac313::jText::v002::JTextWriter;
    using jac313::jText::v002::jtext_line_pad_width_for_count;
    using jac313::jText::v002::write_fields_include_line;
    using jac313::jText::v002::write_file_comment_header;
    using jac313::jText::v002::write_jtext_hash_header;
    using jac313::jText::v002::write_jtext_line_number;
    using jac313::jText::v002::write_light_section_banner;
}