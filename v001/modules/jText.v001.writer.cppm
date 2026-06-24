module;

#include <jText.h>

export module jText.v001.writer;

export import jText.v001.core;

// Re-export under the namespace (see core partition for why not bare `export using`).
export namespace jText::v001 {
    using jText::v001::JTextProfile;
    using jText::v001::JTextWriter;
    using jText::v001::jtext_line_pad_width_for_count;
    using jText::v001::write_fields_include_line;
    using jText::v001::write_file_comment_header;
    using jText::v001::write_jtext_hash_header;
    using jText::v001::write_jtext_line_number;
    using jText::v001::write_light_section_banner;
}