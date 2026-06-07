#include "jText.h"
#include <charconv>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include <stdexcept>
#include <chrono>
#include <format>

namespace fs = std::filesystem;
using namespace std::literals;

// ===================================================================
// Helpers
// ===================================================================

auto JTextFile::trim(std::string_view sv) -> std::string {
    if (sv.empty()) return {};

    auto is_space = [](char c) {
        return std::isspace(static_cast<unsigned char>(c));
    };

    auto first = std::find_if_not(sv.begin(), sv.end(), is_space);
    if (first == sv.end()) return {};

    auto last = std::find_if_not(sv.rbegin(), sv.rend(), is_space).base();
    return std::string(first, last);
}

void JTextFile::log_error(std::string_view msg, const std::source_location loc) {
    std::println(stderr, "Error at {}:{} : {}", loc.file_name(), loc.line(), msg);
}

// ===================================================================
// Header parsing
// ===================================================================

auto JTextFile::parse_header(std::string_view line) -> bool {
    if (line.starts_with("JText File - created "sv)) {
        date = trim(line.substr(21));
        return true;
    }
    if (line.starts_with("Purpose - "sv)) {
        purpose = trim(line.substr(10));
        return true;
    }
    if (line.starts_with("AutoID:"sv)) {
        auto value = trim(line.substr(7));
        if (value == "yes" || value == "YES" || value == "true") {
            auto_id = true;
        } else if (value == "no" || value == "NO" || value == "false") {
            auto_id = false;
        }
        return true;
    }
    if (line.starts_with("Case:"sv)) {
        auto value = trim(line.substr(5));
        if (value == "INSENSITIVE" || value == "insensitive") {
            case_mode = CaseMode::Insensitive;
        } else if (value == "Sensitive" || value == "sensitive") {
            case_mode = CaseMode::Sensitive;
        } else {
            log_error("Unknown Case value: " + std::string(value));
        }
        return true;
    }

    // More robust SQL metadata parsing
    if (line.starts_with("SQL Dialect:"sv)) {
        sql_dialect = trim(line.substr(line.find(':') + 1));
        return true;
    }
    if (line.starts_with("Table Name:"sv)) {
        table_name = trim(line.substr(line.find(':') + 1));
        return true;
    }

    return false;
}
// ===================================================================
// parse_entry - new marker format
// ===================================================================

auto JTextFile::parse_entry(std::string_view line) -> std::expected<JTextEntry, std::string> {
    if (line.size() < 4) {
        return std::unexpected{"Missing 'N. ' prefix"};
    }

    // Parse entry number (supports multi-digit: "10. ", "123. ", etc.)
    size_t num = 0;
    auto [ptr, ec] = std::from_chars(line.data(), line.data() + line.size(), num);
    if (ec != std::errc{} || ptr == line.data() || *ptr != '.') {
        return std::unexpected{"Invalid entry number"};
    }
    ++ptr;
    if (ptr >= line.data() + line.size() || *ptr != ' ') {
        return std::unexpected{"Missing space after entry number"};
    }
    ++ptr;

    std::string_view rest(ptr, static_cast<size_t>(line.data() + line.size() - ptr));

    JTextEntry e;
    e.number = num;
    e.delimiter = '#';

    // === New Format: #X# <data> # <comment> (writer: "#<sep># <fields>") ===
    if (rest.size() >= 3 && rest[0] == '#' && rest[2] == '#') {
        char level_sep = rest[1];
        bool is_flat = (level_sep == '-');
        e.level_sep = is_flat ? 0 : level_sep;

        std::string_view content = rest.substr(3);
        if (!content.empty() && content[0] == ' ') {
            content.remove_prefix(1);
        }
        size_t last_hash = content.rfind('#');
        if (last_hash == std::string_view::npos) {
            return std::unexpected{"Missing final '#' for comment"};
        }

        std::string data_part = trim(content.substr(0, last_hash));
        std::string comment_part = trim(content.substr(last_hash + 1));

        if (data_part == "NULL" || data_part == "null" || data_part == "Null") {
            e.is_null = true;
            e.fields.emplace_back("NULL");
        } else if (!data_part.empty()) {
            if (!is_flat && level_sep != 0) {
                std::stringstream ss(std::string{data_part});
                std::string token;
                while (std::getline(ss, token, level_sep)) {
                    e.fields.emplace_back(trim(token));
                }
            } else {
                e.fields.emplace_back(std::string{data_part});
            }
        }
        e.comment = comment_part;
    }
    // === Clean Field List format: "1. # Name # comment" ===
    else if (rest.starts_with("# ")) {
        std::string_view content = rest.substr(2);
        size_t last_hash = content.rfind('#');
        if (last_hash != std::string_view::npos) {
            e.fields.emplace_back(trim(content.substr(0, last_hash)));
            e.comment = trim(content.substr(last_hash + 1));
        } else {
            e.fields.emplace_back(trim(content));
        }
    }
    // Generic fallback
    else {
        size_t last_hash = rest.rfind('#');
        if (last_hash != std::string_view::npos) {
            e.fields.emplace_back(trim(rest.substr(0, last_hash)));
            e.comment = trim(rest.substr(last_hash + 1));
        } else {
            e.fields.emplace_back(trim(rest));
        }
    }

    // Case normalization
    if (case_mode == CaseMode::Insensitive && !e.is_null && !e.fields.empty()) {
        for (auto& f : e.fields) {
            if (f) {
                std::string& s = *f;
                std::transform(s.begin(), s.end(), s.begin(), ::toupper);
            }
        }
        if (!e.comment.empty()) {
            std::transform(e.comment.begin(), e.comment.end(), e.comment.begin(), ::toupper);
        }
    }

    return e;
}
// ===================================================================
// Special Block
// ===================================================================

auto JTextFile::parse_special_block(std::ifstream& in, JTextSection& sec)
    -> std::expected<void, std::string>
{
    std::string data_line, comment_line, closer_line;

    if (!std::getline(in, data_line))
        return std::unexpected{"Missing data line after *** ^^^ ###"};

    if (!std::getline(in, comment_line))
        return std::unexpected{"Missing line after data in special block"};

    if (!std::getline(in, closer_line))
        return std::unexpected{"Missing closer ^^^"};

    JTextEntry e;
    e.is_special_block = true;
    e.fields.emplace_back(trim(data_line));

    if (comment_line.starts_with("###")) {
        e.comment = trim(comment_line.substr(3));
    }

    if (trim(closer_line) != "^^^") {
        log_error("Special block missing proper ^^^ closer");
    }

    sec.entries.push_back(std::move(e));
    return {};
}

// ===================================================================
// Main Parser
// ===================================================================

auto JTextFile::parse_stream(std::ifstream& in, bool full_read, std::string_view target_section)
    -> std::expected<void, std::string>
{
    std::string line;
    bool in_header = true;
    std::string cur_section;
    JTextSection* active = nullptr;

    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty()) continue;

        // Skip comment lines in header area (# or // for standardized file headers)
        if (in_header && line.starts_with('#')) {
            if (parse_header(trim(line.substr(1)))) {
                continue;
            }
            continue;
        }
        if (in_header && line.starts_with("//")) {
            continue;
        }

        if (in_header) {
            if (parse_header(line)) {
                continue;
            }
            // Exit header mode when we hit first real section
            if (line == "Field List" || line == "Data Section" ||
                (line.starts_with("-- ") && line.ends_with(" --"))) {
                in_header = false;
                }
        }
        if (line == "-- EOF --") break;

        // Section header
        if (line == "Field List" || line == "Data Section" ||
            (line.starts_with("-- ") && line.ends_with(" --"))) {

            cur_section = (line.starts_with("-- "))
                ? trim(line.substr(3, line.size() - 6))
                : trim(line);

            if (full_read || cur_section == target_section) {
                sections.emplace_back();
                active = &sections.back();
                active->name = trim(cur_section);
            }
            continue;
            }

        // Special block
        if (line.ends_with("*** ^^^ ###")) {
            if (!active) {
                sections.emplace_back();
                active = &sections.back();
                active->name = cur_section.empty() ? "Data Section" : cur_section;
            }
            if (auto res = parse_special_block(in, *active); !res) {
                return res;
            }
            continue;
        }

        // Normal entry
        if (auto res = parse_entry(line); res) {
            if (active) {
                active->entries.push_back(std::move(*res));
            }
        } else {
            log_error(std::format("Invalid entry: {}", line));
        }
    }

    return {};
}
// ===================================================================
// Public API
// ===================================================================

auto JTextFile::read_full(std::string_view filepath) -> std::expected<void, std::string> {
    std::ifstream in(std::string{filepath});
    if (!in) return std::unexpected{"Cannot open file: " + std::string(filepath)};

    this->filename = std::string{filepath};   // ← store it

    return parse_stream(in, true);
}

auto JTextFile::read_section(std::string_view filepath, std::string_view section_name)
    -> std::expected<JTextSection, std::string>
{
    std::ifstream in(std::string{filepath});
    if (!in) {
        return std::unexpected{"Cannot open file: " + std::string(filepath)};
    }

    sections.clear();  // reuse internal storage

    auto res = parse_stream(in, false, section_name);
    if (!res) {
        return std::unexpected{res.error()};
    }

    if (sections.empty() || sections[0].name != section_name) {
        return std::unexpected{std::format("Section '{}' not found", section_name)};
    }

    JTextSection result = std::move(sections[0]);
    sections.clear();
    return result;
}

auto JTextFile::write(std::string_view filepath) const -> std::expected<void, std::string> {
    std::string target = std::string{filepath};
    if (target.empty() && !filename.empty()) {
        target = filename;
    }
    if (target.empty()) {
        return std::unexpected{"No filename provided"};
    }

    std::ofstream file(target);
    if (!file) return std::unexpected{"Cannot open file for writing: " + target};

    // Emit leading // comments (for stream-backed writer we do it here to get File Name)
    std::string purp = purpose.empty() ? "jText Data File" : purpose;
    write_file_comment_header(file, target, purp);  // related info can be passed by callers using the 4-arg form when known

    JTextWriter writer(file);

    // Copy header metadata
    writer.set_date(date);
    writer.set_purpose(purpose);
    writer.set_case_mode(case_mode);
    writer.set_sql_dialect(sql_dialect);
    writer.set_table_name(table_name);
    writer.set_auto_id(auto_id);

    writer.write_header();

    for (const auto& sec : sections) {
        writer.begin_section(sec.name);
        for (const auto& e : sec.entries) {
            writer.append_entry(e);
        }
    }

    writer.finalize();
    return {};
}

auto JTextFile::validate() const -> std::expected<void, std::string> {
    std::vector<std::string> checks;
    size_t field_count = 0;
    bool has_field_list = false;
    bool has_data_section = false;
    bool field_list_before_data = true;

    std::println("=== JText Validation Check ===");

    // 1. Field List Exists?
    for (const auto& sec : sections) {
        if (sec.name == "Field List") {
            has_field_list = true;
            field_count = sec.entries.size();
            break;
        }
    }
    checks.push_back(has_field_list ? "✓ 1. Field List exists" : "✗ 1. No Field List found");

    if (!has_field_list) {
        for (const auto& s : checks) std::println("{}", s);
        return std::unexpected{"Validation failed: No Field List section."};
    }

    // 2. Field List has unique names and numbers
    std::unordered_set<std::string> name_set;
    std::vector<bool> number_set(field_count + 1, false);
    bool field_list_valid = true;

    for (const auto& sec : sections) {
        if (sec.name != "Field List") continue;

        for (const auto& e : sec.entries) {
            std::string name = trim(e.fields.empty() || !e.fields[0] ? "" : *e.fields[0]);

            if (name.empty()) {
                checks.push_back("✗ 2. Empty field name in Field List");
                field_list_valid = false;
            }
            if (name_set.contains(name)) {
                checks.push_back("✗ 2. Duplicate field name: " + name);
                field_list_valid = false;
            }
            name_set.insert(name);

            if (e.number == 0 || e.number > field_count) {
                checks.push_back(std::format("✗ 2. Invalid field number {} in Field List", e.number));
                field_list_valid = false;
            } else if (number_set[e.number]) {
                checks.push_back(std::format("✗ 2. Duplicate field number {}", e.number));
                field_list_valid = false;
            }
            number_set[e.number] = true;
        }
    }
    checks.push_back(field_list_valid ? "✓ 2. Field List has unique names and numbers"
                                      : "✗ 2. Field List has errors");

    if (!field_list_valid) {
        for (const auto& s : checks) std::println("{}", s);
        return std::unexpected{"Validation failed in Field List."};
    }

    // 3. Field List appears before any Data Section
    for (const auto& sec : sections) {
        if (sec.name == "Field List") break;
        if (sec.name == "Data Section") {
            field_list_before_data = false;
            break;
        }
    }
    checks.push_back(field_list_before_data ? "✓ 3. Field List appears before Data Section"
                                             : "✗ 3. Field List must appear before Data Section");

    // 4. Data Section (optional)
    for (const auto& sec : sections) {
        if (sec.name == "Data Section") {
            has_data_section = true;
            size_t last_num = 0;

            for (const auto& e : sec.entries) {
                if (e.number == 0) {
                    checks.push_back("✗ 4. Data entry with number 0");
                }
                if (e.number <= last_num) {
                    checks.push_back(std::format("✗ 4. Data numbers not strictly increasing ({} after {})",
                                                 e.number, last_num));
                }
                if (e.number > last_num + 1) {
                    std::println(stderr, "Warning: Skipped field number(s) in Data Section ({} after {})",
                                 e.number, last_num);
                }
                if (e.fields.size() > field_count) {
                    checks.push_back(std::format("✗ 4. Entry {} has too many fields ({})",
                                                 e.number, e.fields.size()));
                }
                last_num = e.number;
            }
        }
    }

    checks.push_back(has_data_section ? "✓ 4. Data Section present"
                                      : "✓ 4. No Data Section (optional - OK)");

    // Print all checks
    for (const auto& s : checks) {
        std::println("{}", s);
    }

    std::println("✅ Validation passed.");
    return {};
}

// ===================================================================
// JTextWriter implementation
// ===================================================================

void write_file_comment_header(std::ostream& os,
                                   std::string_view full_path,
                                   std::string_view purpose,
                                   std::string_view related) {
    auto now = std::chrono::system_clock::now();
    auto today = std::chrono::floor<std::chrono::days>(now);
    std::string date_str = std::format("{:%Y-%m-%d}", today);

    os << "//File:    " << full_path << "\n";
    os << "//Date:    " << date_str << "\n";
    os << "//Purpose: " << purpose << "\n";
    if (!related.empty()) {
        os << "//Related: " << related << "\n";
    }
    os << "//\n";
}

namespace {

void write_header_block(std::ostream& out,
                        std::string_view date,
                        std::string_view purpose,
                        CaseMode case_mode,
                        std::string_view sql_dialect,
                        std::string_view table_name,
                        bool auto_id)
{
    out << std::format("# JText File - created {}\n", date);
    out << std::format("# Purpose - {}\n", purpose);
    out << std::format("# Case: {}\n",
        case_mode == CaseMode::Insensitive ? "INSENSITIVE" : "Sensitive");

    if (!sql_dialect.empty()) {
        out << std::format("# SQL Dialect: {}\n", sql_dialect);
    }
    if (!table_name.empty()) {
        out << std::format("# Table Name: {}\n", table_name);
    }
    if (!sql_dialect.empty()) {
        out << std::format("# AutoID: {}\n", auto_id ? "yes" : "no");
        out << "# Note: Field types are user-defined and not validated. "
               "The SQL generator will use them as provided.\n";
    }
    out << "\n";
}

void write_entry_to_stream(std::ostream& out, const JTextEntry& e)
{
    if (e.is_special_block) {
        out << "*** ^^^ ###\n";
        if (!e.fields.empty() && e.fields[0]) out << *e.fields[0] << '\n';
        if (!e.comment.empty()) out << "### " << e.comment << '\n';
        out << "^^^" << '\n';
        return;
    }

    char numbuf[32];
    auto [p, ec] = std::to_chars(numbuf, numbuf + sizeof(numbuf), e.number);
    out.write(numbuf, p - numbuf);
    out << ". ";

    char sep = (e.level_sep == 0) ? '-' : e.level_sep;
    out << "#" << sep << "# ";

    for (size_t i = 0; i < e.fields.size(); ++i) {
        if (i > 0) out << e.level_sep;
        const auto& f = e.fields[i];
        if (!f.has_value()) {
            out << '\x1F';  // official null marker for compact (chosen ASCII US)
        } else {
            out << *f;
        }
    }

    if (!e.comment.empty()) {
        out << " # " << e.comment;
    }
    out << '\n';
}

// Helper for auto-batching: format once into a string (cheaper to store than full JTextEntry)
static std::string format_entry(const JTextEntry& e)
{
    if (e.is_special_block) {
        std::string s;
        s.reserve(128);
        s += "*** ^^^ ###\n";
        if (!e.fields.empty() && e.fields[0]) { s += *e.fields[0]; s += '\n'; }
        if (!e.comment.empty()) { s += "### "; s += e.comment; s += '\n'; }
        s += "^^^\n";
        return s;
    }

    std::string s;
    s.reserve(256);

    // Faster than std::format for the common case
    char numbuf[32];
    auto [p, ec] = std::to_chars(numbuf, numbuf + sizeof(numbuf), e.number);
    s.append(numbuf, p);
    s += ". ";

    char sep = (e.level_sep == 0) ? '-' : e.level_sep;
    s += '#';
    s += sep;
    s += "# ";

    for (size_t i = 0; i < e.fields.size(); ++i) {
        if (i > 0) s += e.level_sep;
        const auto& f = e.fields[i];
        if (!f.has_value()) {
            s += '\x1F';
        } else {
            s += *f;
        }
    }

    if (!e.comment.empty()) {
        s += " # ";
        s += e.comment;
    }
    s += '\n';
    return s;
}

// Even faster path for the common append_row case when auto-batching
static std::string format_row(size_t number,
                              const std::vector<std::string>& fields,
                              std::string_view comment,
                              char level_sep)
{
    std::string s;
    s.reserve(256);

    char numbuf[32];
    auto [p, ec] = std::to_chars(numbuf, numbuf + sizeof(numbuf), number);
    s.append(numbuf, p);
    s += ". ";

    char sep = (level_sep == 0) ? '-' : level_sep;
    s += '#';
    s += sep;
    s += "# ";

    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) s += level_sep;
        s += fields[i];
    }

    if (!comment.empty()) {
        s += " # ";
        s += comment;
    }
    s += '\n';
    return s;
}

void write_section_header(std::ostream& out, std::string_view name)
{
    if (name == "Field List" || name == "Data Section") {
        out << name << '\n';
    } else {
        out << std::format("-- {} --\n", name);
    }
}

} // anonymous namespace

JTextWriter::JTextWriter(std::ostream& stream)
    : out_(&stream), owns_stream_(false)
{
}

JTextWriter::JTextWriter(std::string_view filepath)
{
    auto* file = new std::ofstream(std::string{filepath});
    if (!file->is_open()) {
        delete file;
        throw std::runtime_error("JTextWriter: failed to open file: " + std::string(filepath));
    }
    out_ = file;
    owns_stream_ = true;
    source_path_ = std::string{filepath};
}

JTextWriter::~JTextWriter()
{
    if (out_ && !finalized_) {
        try {
            finalize();
        } catch (...) {
            // best effort in destructor
        }
    }
    if (owns_stream_) {
        delete out_;
    }
}

JTextWriter::JTextWriter(JTextWriter&& other) noexcept
    : out_(other.out_),
      owns_stream_(other.owns_stream_),
      header_written_(other.header_written_),
      finalized_(other.finalized_),
      date_(std::move(other.date_)),
      purpose_(std::move(other.purpose_)),
      case_mode_(other.case_mode_),
      sql_dialect_(std::move(other.sql_dialect_)),
      table_name_(std::move(other.table_name_)),
      auto_id_(other.auto_id_),
      source_path_(std::move(other.source_path_))
{
    other.out_ = nullptr;
    other.owns_stream_ = false;
}

JTextWriter& JTextWriter::operator=(JTextWriter&& other) noexcept
{
    if (this != &other) {
        if (owns_stream_ && out_) delete out_;

        out_ = other.out_;
        owns_stream_ = other.owns_stream_;
        header_written_ = other.header_written_;
        finalized_ = other.finalized_;
        date_ = std::move(other.date_);
        purpose_ = std::move(other.purpose_);
        case_mode_ = other.case_mode_;
        sql_dialect_ = std::move(other.sql_dialect_);
        table_name_ = std::move(other.table_name_);
        auto_id_ = other.auto_id_;
        source_path_ = std::move(other.source_path_);

        other.out_ = nullptr;
        other.owns_stream_ = false;
    }
    return *this;
}

void JTextWriter::set_date(std::string_view d) { date_ = d; }
void JTextWriter::set_purpose(std::string_view p) { purpose_ = p; }
void JTextWriter::set_case_mode(CaseMode mode) { case_mode_ = mode; }
void JTextWriter::set_sql_dialect(std::string_view dialect) { sql_dialect_ = dialect; }
void JTextWriter::set_table_name(std::string_view name) { table_name_ = name; }
void JTextWriter::set_auto_id(bool enabled) { auto_id_ = enabled; }

void JTextWriter::write_header()
{
    ensure_not_finalized();
    if (header_written_) return;

    if (!out_) return;

    // Emit the standardized top-of-file // comment header if we know the source path
    // (i.e. when constructed with filename, not raw stream). For stream case the
    // caller (e.g. JTextSplitEventLog or JTextFile::write) may emit it first.
    if (!source_path_.empty()) {
        std::string purp = purpose_.empty() ? "jText Data File" : purpose_;
        write_file_comment_header(*out_, source_path_, purp);
    }

    write_header_block(*out_, date_, purpose_, case_mode_,
                       sql_dialect_, table_name_, auto_id_);
    header_written_ = true;
}

void JTextWriter::begin_section(std::string_view name)
{
    ensure_header();
    ensure_not_finalized();
    if (!out_) return;

    flush_pending_batch();   // never buffer across section boundaries
    write_section_header(*out_, name);
    out_->put('\n');
}

void JTextWriter::append_entry(const JTextEntry& entry)
{
    ensure_header();
    ensure_not_finalized();
    if (!out_) return;

    if (auto_batch_size_ > 0) {
        pending_batch_.push_back(format_entry(entry));
        if (pending_batch_.size() >= auto_batch_size_) {
            flush_pending_batch();
        }
    } else {
        write_entry_to_stream(*out_, entry);
    }
}

void JTextWriter::append_entries(std::span<const JTextEntry> entries)
{
    ensure_header();
    ensure_not_finalized();
    if (!out_) return;

    if (auto_batch_size_ > 0) {
        for (const auto& e : entries) {
            pending_batch_.push_back(format_entry(e));
            if (pending_batch_.size() >= auto_batch_size_) {
                flush_pending_batch();
            }
        }
    } else {
        for (const auto& e : entries) {
            write_entry_to_stream(*out_, e);
        }
    }
}

void JTextWriter::append_entries(const std::vector<JTextEntry>& entries)
{
    append_entries(std::span<const JTextEntry>(entries));
}

void JTextWriter::append_row(size_t number,
                             const std::vector<std::string>& fields,
                             std::string_view comment,
                             char level_sep)
{
    ensure_header();
    ensure_not_finalized();
    if (!out_) return;

    if (auto_batch_size_ > 0) {
        // Fast path: format directly into the pending batch without ever creating a JTextEntry
        pending_batch_.push_back(format_row(number, fields, comment, level_sep));
        if (pending_batch_.size() >= auto_batch_size_) {
            flush_pending_batch();
        }
    } else {
        JTextEntry e;
        e.number = number;
        for (const auto& f : fields) e.fields.emplace_back(f);
        e.comment = std::string(comment);
        e.level_sep = level_sep;
        write_entry_to_stream(*out_, e);
    }
}

void JTextWriter::finalize()
{
    if (finalized_ || !out_) return;

    flush_pending_batch();
    ensure_header();
    *out_ << "-- EOF --\n";
    out_->flush();
    finalized_ = true;
}

void JTextWriter::flush()
{
    if (out_ && !finalized_) {
        flush_pending_batch();
        out_->flush();
    }
}

void JTextWriter::set_auto_batch_size(size_t num_entries)
{
    if (finalized_) return;

    // Flush whatever is currently buffered before changing policy
    flush_pending_batch();

    auto_batch_size_ = num_entries;
}

void JTextWriter::enable_high_throughput_batching()
{
    set_auto_batch_size(kDefaultHighThroughputBatchSize);
}

void JTextWriter::flush_pending_batch()
{
    if (!out_ || pending_batch_.empty()) return;

    for (const auto& line : pending_batch_) {
        *out_ << line;           // already formatted — very cheap
    }
    pending_batch_.clear();
}

void JTextWriter::ensure_header()
{
    if (!header_written_) {
        write_header();
    }
}

void JTextWriter::ensure_not_finalized()
{
    if (finalized_) {
        throw std::logic_error("JTextWriter: cannot write after finalize()");
    }
}
