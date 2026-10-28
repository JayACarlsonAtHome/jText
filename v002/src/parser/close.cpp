// File: src/parser/close.cpp
//
// See close.hpp.

#include "parser/close.hpp"
#include "parser/line.hpp"

#include <format>
#include <optional>

namespace jtext {

namespace {

auto trim(std::string_view sv) -> std::string_view
{
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r')) sv.remove_suffix(1);
    return sv;
}

// "=== X ===" → "X", else empty.
auto full_marker(std::string_view line) -> std::string_view
{
    const auto t = trim(line);
    if (t.size() < 8 || !t.starts_with("===") || !t.ends_with("===")) return {};
    return trim(t.substr(3, t.size() - 6));
}

// "-- X --" → "X", else empty.
auto light_banner(std::string_view line) -> std::string_view
{
    const auto t = trim(line);
    if (t.size() < 6 || !t.starts_with("-- ") || !t.ends_with(" --")) return {};
    return trim(t.substr(3, t.size() - 6));
}

enum class block { none, fields, data };

class closer {
public:
    explicit closer(std::string_view text) { split(text); }

    auto run() -> closed_file
    {
        closed_file out;
        out.last_line_unterminated = unterminated_;
        const bool light = is_light();
        for (std::size_t i = 0; i < in_.size(); ++i) {
            line_no_ = i + 1;
            const std::string& line = in_[i];
            if (sentinel_) {
                if (trim(line) == *sentinel_) sentinel_.reset();
                lines_.push_back(line);
                continue;
            }
            if (light) light_line(line);
            else full_line(line);
        }
        line_no_ = 0;
        if (sentinel_) add(*sentinel_);
        if (light) {
            close_light_section();
            if (file_open_) add("-- EOF --");
        } else {
            close_block();
            close_section();
            if (file_open_) add("=== End File ===");
        }
        for (const auto& l : lines_) { out.text += l; out.text += '\n'; }
        out.added = std::move(added_);
        return out;
    }

private:
    void split(std::string_view text)
    {
        unterminated_ = !text.empty() && text.back() != '\n';
        std::size_t start = 0;
        while (start < text.size()) {
            auto end = text.find('\n', start);
            if (end == std::string_view::npos) end = text.size();
            in_.emplace_back(text.substr(start, end - start));
            start = end + 1;
        }
    }

    auto is_light() const -> bool
    {
        for (const auto& l : in_) {
            const auto t = trim(l);
            if (t.empty() || t.starts_with("//")) continue;
            return t.starts_with("# JText File - created");
        }
        return false;
    }

    // Put `closer` after the last non-blank line so far, so it sits right under the
    // block it closes and the blank lines stay between blocks.
    void add(std::string closer)
    {
        auto at = lines_.size();
        while (at > 0 && trim(lines_[at - 1]).empty()) --at;
        lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(at), closer);
        added_.push_back(line_no_ ? std::format("line {}: added '{}'", line_no_, closer)
                                  : std::format("end of file: added '{}'", closer));
    }

    // A numbered line that opens a multiline block ("1. <<< SENTINEL").
    void note_multiline(std::string_view line)
    {
        if (const auto pl = parse_line(line); pl && pl->kind == line_kind::multiline_opener)
            sentinel_ = pl->sentinel;
    }

    void close_block()
    {
        if (block_ == block::fields) add("=== End Fields ===");
        if (block_ == block::data) add("=== End Data ===");
        block_ = block::none;
    }

    void close_section()
    {
        if (section_open_) add("=== End Section ===");
        section_open_ = false;
    }

    void full_line(const std::string& line)
    {
        const auto m = full_marker(line);
        if (m == "jText File") file_open_ = true;
        else if (m.starts_with("Section:")) { close_block(); close_section(); section_open_ = true; }
        else if (m == "Fields") block_ = block::fields;
        else if (m == "End Fields" || m == "End Data") block_ = block::none;
        else if (m == "Data") { if (block_ == block::fields) add("=== End Fields ==="); block_ = block::data; }
        else if (m == "End Section") { close_block(); section_open_ = false; }
        else if (m == "End File") { close_block(); close_section(); file_open_ = false; }
        else if (m.empty() && light_banner(line) == "EOF") {
            // An old light-profile trailer in a full-profile file ends it.
            close_block();
            close_section();
            if (file_open_) add("=== End File ===");
            file_open_ = false;
            added_.push_back(std::format("line {}: removed '-- EOF --' (not a full-profile marker)", line_no_));
            return;
        }
        else if (m.empty()) note_multiline(line);
        lines_.push_back(line);
    }

    void close_light_section()
    {
        if (light_section_) add(std::format("-- End {} --", *light_section_));
        light_section_.reset();
    }

    void light_line(const std::string& line)
    {
        const auto t = trim(line);
        if (t.starts_with("# JText File - created")) file_open_ = true;
        const auto b = light_banner(line);
        if (b == "EOF") { close_light_section(); file_open_ = false; }
        else if (b.starts_with("End ")) { if (light_section_ && trim(b.substr(4)) == *light_section_) light_section_.reset(); }
        else if (!b.empty()) { close_light_section(); light_section_ = std::string{b}; }
        else note_multiline(line);
        lines_.push_back(line);
    }

    std::vector<std::string> in_;
    std::vector<std::string> lines_;
    std::vector<std::string> added_;
    std::size_t line_no_ = 0;
    bool unterminated_ = false;
    bool file_open_ = false;
    bool section_open_ = false;
    block block_ = block::none;
    std::optional<std::string> light_section_;
    std::optional<std::string> sentinel_;
};

}  // anonymous namespace

auto add_missing_closers(std::string_view text) -> closed_file
{
    return closer{text}.run();
}

}  // namespace jtext
