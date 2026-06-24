module;

#include <jText.h>

export module jText.v002.writer;

export import jText.v002.core;

// Re-export under the namespace (see core partition for why not bare `export using`).
export namespace jText::v002 {
    using jText::v002::JTextProfile;
    using jText::v002::JTextWriter;
    using jText::v002::jtext_line_pad_width_for_count;
    using jText::v002::write_fields_include_line;
    using jText::v002::write_file_comment_header;
    using jText::v002::write_jtext_hash_header;
    using jText::v002::write_jtext_line_number;
    using jText::v002::write_light_section_banner;
}