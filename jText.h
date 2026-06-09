#pragma once

#include <expected>
#include <string>
#include <vector>
#include <optional>
#include <fstream>
#include <print>
#include <format>
#include <source_location>

enum class CaseMode { Insensitive, Sensitive };

// jText file profiles (SPEC §2.0):
//   Light — // wrapper + # metadata + "-- Section --" + "# Fields:" includes.
//           Default for human-facing matrix/summary files (e.g. ts_store run_manifest).
//   Full  — "=== jText File ===" + "=== Section:" + templates + "=== Fields/Data ===".
//           Default for DB round-trips and high-throughput event logs.
enum class JTextProfile { Light, Full };

struct JTextEntry {
    size_t number = 0;
    char delimiter = '#';
    char level_sep = 0;           // 0 = flat, otherwise separator char
    std::vector<std::optional<std::string>> fields;  // nullopt = null value; for compact ts_store, null is encoded as \x1F when writing
    std::string comment;
    bool is_null = false;  // legacy per-entry
    bool is_special_block = false;
};

struct JTextSection {
    std::string name;
    std::vector<JTextEntry> entries;
};

class JTextFile {
public:
    std::string date;
    std::string purpose;
    CaseMode case_mode = CaseMode::Insensitive;
    std::string sql_dialect;
    std::string table_name;
    std::string filename;
    bool auto_id = true;

    std::vector<JTextSection> sections;

    auto write(std::string_view filepath) const -> std::expected<void, std::string>;
    auto read_full(std::string_view filepath) -> std::expected<void, std::string>;
    auto read_section(std::string_view filepath, std::string_view section_name)
        -> std::expected<JTextSection, std::string>;

    auto validate() const -> std::expected<void, std::string>;

    static auto trim(std::string_view sv) -> std::string;

private:
    static void log_error(std::string_view msg, const std::source_location loc = std::source_location::current());

    auto parse_header(std::string_view line) -> bool;
    auto parse_entry(std::string_view line) -> std::expected<JTextEntry, std::string>;
    auto parse_special_block(std::ifstream& in, JTextSection& sec) -> std::expected<void, std::string>;

    auto parse_stream(std::ifstream& in, bool full_read, std::string_view target_section = {})
        -> std::expected<void, std::string>;
};

// ===================================================================
// JTextWriter — Incremental / streaming writer
// Supports the "write headers + open files early, append rows later" pattern
// (required for high-throughput buffers with future double-buffering needs).
// ===================================================================

class JTextWriter {
public:
    // Attach to an already-open stream (caller owns the stream lifetime)
    explicit JTextWriter(std::ostream& stream);

    // Opens (truncates) the file itself
    explicit JTextWriter(std::string_view filepath);

    ~JTextWriter();

    JTextWriter(const JTextWriter&) = delete;
    JTextWriter& operator=(const JTextWriter&) = delete;
    JTextWriter(JTextWriter&&) noexcept;
    JTextWriter& operator=(JTextWriter&&) noexcept;

    // Header metadata
    void set_date(std::string_view d);
    void set_purpose(std::string_view p);
    void set_case_mode(CaseMode mode);
    void set_sql_dialect(std::string_view dialect);
    void set_table_name(std::string_view name);
    void set_auto_id(bool enabled);

    void write_header();

    // Sections
    void begin_section(std::string_view name);

    // Append a complete entry (supports is_special_block)
    void append_entry(const JTextEntry& entry);

    // Batch append — recommended for high-volume use (ts_store, double-buffering, etc.)
    void append_entries(std::span<const JTextEntry> entries);

    // Convenience overloads
    void append_entries(const std::vector<JTextEntry>& entries);

    void append_row(size_t number,
                    const std::vector<std::string>& fields,
                    std::string_view comment = "",
                    char level_sep = 0);

    void finalize();

    // Flush the underlying stream (useful after a batch for durability)
    void flush();

    // === Auto-batching (internal buffering) ===
    // When enabled, append_entry/append_row will buffer entries and only write
    // them to the stream when the batch fills up, a section changes, or finalize/flush is called.
    //
    // Default = 0 (disabled) for predictable library behavior.
    void set_auto_batch_size(size_t num_entries);   // 0 = disable auto-batching

    [[nodiscard]] size_t auto_batch_size() const noexcept { return auto_batch_size_; }

    // Recommended batch size when maximizing throughput (used by ts_store by default).
    // Users of ts_store can override this if they want a different balance
    // between throughput, memory usage, and data-loss window on crash.
    static constexpr size_t kDefaultHighThroughputBatchSize = 10'000;

    // Convenience: enables auto-batching tuned for high throughput (10K lines).
    // This is what ts_store persistence will use by default.
    void enable_high_throughput_batching();

    // When a numbered list has more than 9 entries, set pad width so 1–99
    // line numbers right-align and '.' shares a column (SPEC §3.7).
    // 0 = no extra padding (single-digit entries omit the leading space).
    void set_line_number_pad_width(size_t width);
    [[nodiscard]] size_t line_number_pad_width() const noexcept { return line_pad_width_; }

    [[nodiscard]] bool is_finalized() const noexcept { return finalized_; }

private:
    void ensure_header();
    void ensure_not_finalized();
    void flush_pending_batch();   // internal helper

    std::ostream* out_ = nullptr;
    bool owns_stream_ = false;
    bool header_written_ = false;
    bool finalized_ = false;

    // Header state
    std::string date_;
    std::string purpose_;
    CaseMode case_mode_ = CaseMode::Insensitive;
    std::string sql_dialect_;
    std::string table_name_;
    bool auto_id_ = true;

    // Auto-batching state
    size_t auto_batch_size_ = 0;
    std::vector<std::string> pending_batch_;   // stores pre-formatted lines for cheaper storage & faster flush

    size_t line_pad_width_ = 0;

    // For emitting leading standardized // comment header (File Name etc) on path-based construction
    std::string source_path_;
};

// Line-number padding (SPEC §3.7): when item_count > 9, returns column width
// for right-aligned numbers ( 1. …  9. … 10. … up to 99.; wider when > 99).
[[nodiscard]] size_t jtext_line_pad_width_for_count(size_t item_count);

// Emit "<padded>N. " — pad_width from jtext_line_pad_width_for_count().
void write_jtext_line_number(std::ostream& out, size_t number, size_t pad_width);

// Light profile (SPEC §2.0.1): # metadata block immediately after the // wrapper.
void write_jtext_hash_header(std::ostream& out,
                               std::string_view created_utc,
                               std::string_view purpose,
                               CaseMode case_mode = CaseMode::Insensitive,
                               std::string_view table_name = {},
                               std::string_view sql_dialect = {},
                               bool auto_id = true);

// Light profile section banner: "-- SectionName --" plus trailing blank line.
void write_light_section_banner(std::ostream& out, std::string_view section_name);

// Light profile field-list include: "# Fields: path.jtFlds" plus trailing blank line.
void write_fields_include_line(std::ostream& out, std::string_view jtflds_path);

// Writes the required standardized leading // comment header for
// self-describing files (jText data, jText field lists, SQL schemas, binary data, etc).
// Always uses // comments. Call this before writing any data or jText's own # headers.
// The produced block is (keys chosen for brevity + colons for consistency; values aligned):
//
//   //File:    <full-or-relative-path>
//   //Date:    YYYY-MM-DD
//   //Purpose: jText Data File | jText Field List File | SQL Schema File | SQL Data File | ...
//   //Related: type=PostgreSQL table=workshop_tools   (optional, if origin known)
//
// The leading // lines are comments and are skipped by parsers when looking for
// the internal # metadata block (light profile) or "=== jText File ===" (full profile).
// Use of the optional Related line provides quick human provenance when
// inspecting raw files with head/cat, and makes jText a consistent,
// self-documenting interchange format across projects.
//
// Data...
void write_file_comment_header(std::ostream& os,
                               std::string_view full_path,
                               std::string_view purpose,
                               std::string_view related = {});
