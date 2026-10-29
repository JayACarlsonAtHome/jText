// jText::v001 — PUBLIC API DECLARATIONS (single source of truth).
//
// Include-fragment, NOT a standalone header: no include guard, includes nothing
// itself, and must be included INSIDE the jText::v001 namespace by jText.h, which
// first makes std visible with textual std #includes.
// std names are spelled std:: (incl. std::size_t). Definitions live in jText.impl.inc.

// Package version, "major.minor": major = the v001 API line, minor = the revision. Bump on
// each small code change (edit the minor); v001 stays until a real (breaking) change, then
// this becomes "v001.001". Enforced by `jac313_test_cli version-check` (pre-push).
[[nodiscard]] constexpr std::string_view version() noexcept { return "v001.018"; }

enum class CaseMode { Insensitive, Sensitive };

// Compare two values under a file's case mode, the way a database collation does:
// Insensitive ignores ASCII letter case, Sensitive compares exactly. Values themselves are
// always stored and returned as written; use this for matching, filtering and duplicates.
[[nodiscard]] bool text_equal(std::string_view a, std::string_view b, CaseMode mode) noexcept;

// jText file profiles (SPEC §2.0):
//   Light — // wrapper + # metadata + "-- Section --" + "# Fields:" includes.
//           Default for human-facing matrix/summary files (e.g. ts_store run_manifest).
//   Full  — "=== jText File ===" + "=== Section:" + templates + "=== Fields/Data ===".
//           Default for DB round-trips and high-throughput event logs.
enum class JTextProfile { Light, Full };

// What the writer does with a row it cannot write safely, such as a field holding the
// row's separator (it would read back as two fields, and no reader can tell where).
//   Terminate - print the reason to stderr, write out the rows before it, and end the
//               process with std::terminate(). Nothing is silently lost. The default.
//   Skip      - print the reason to stderr, leave the row out, count it, carry on.
enum class BadRowPolicy { Terminate, Skip };

struct JTextEntry {
    std::size_t number = 0;
    char32_t delimiter = 0;       // the comment character; 0 = the writer picks (U+258C unless the data holds it)
    char32_t level_sep = 0;       // the separator; 0 = the writer picks (U+25AE unless the data holds it), otherwise
                                  // (any Unicode punctuation or symbol, e.g. U'|' or U'\u25AE')
    // The values, as text. A file holds no nulls (SPEC 2.6.1): a field given no value
    // (nullopt) is written as {NULL}, and the reader returns {NULL} as that text.
    std::vector<std::optional<std::string>> fields;
    std::string comment;
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
    // The jText version the file read declares ("" when none); writing always declares
    // the library's own (jtext::kFormatVersion), so a save upgrades an older file.
    std::string jtext_version;
    // What the last read noticed but read past, such as a missing, older or newer
    // jText version. Each is also printed to stderr as "Warning: ...".
    std::vector<std::string> warnings;

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

    // Reads the file with the same parser the validator and tools use, adding its
    // sections (only `only_section` when given) to `sections`.
    auto read_file(std::string_view filepath, std::string_view only_section)
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
    // The file's name, for header line 1 (full profile) and error messages. A writer
    // opened on a path knows it; one attached to a stream needs this. Name only: a
    // path is cut to its file name.
    void set_filename(std::string_view name);
    void set_date(std::string_view d);
    void set_purpose(std::string_view p);
    void set_case_mode(CaseMode mode);
    void set_sql_dialect(std::string_view dialect);
    void set_table_name(std::string_view name);
    void set_auto_id(bool enabled);

    // Rows that cannot be written safely (see BadRowPolicy). Terminate is the default.
    void set_bad_row_policy(BadRowPolicy policy) noexcept { bad_row_policy_ = policy; }
    [[nodiscard]] BadRowPolicy bad_row_policy() const noexcept { return bad_row_policy_; }
    // Rows left out under BadRowPolicy::Skip.
    [[nodiscard]] std::size_t skipped_rows() const noexcept { return skipped_rows_; }

    // Record id (SPEC 2.6.4): field 1 of every record is a mandatory integer id.
    // When a field-1 entry ("1. ...") arrives with its value missing, null or
    // empty, the writer generates one: `seed` for the first record of a section,
    // then the previous id + `step`, continuing past the largest (smallest, for a
    // negative step) id already written in the section. Matches Identity(seed,step);
    // the default (1, 1) matches AutoNumber / AutoID / Serial. Resets per section.
    void set_record_id_start(std::int64_t seed, std::int64_t step = 1);

    // The id most recently written or generated in the current section, so a
    // caller can link child rows (e.g. _Ints/_Floats tables) to it.
    [[nodiscard]] std::optional<std::int64_t> last_record_id() const noexcept { return last_record_id_; }

    // The profile written (SPEC 2.0): Light (the default) or Full. Set it before the header.
    void set_profile(JTextProfile profile) noexcept { profile_ = profile; }

    // Light: the "#" metadata block. Full: "=== jText File ===" and the numbered header
    // lines (SPEC 2.3: filename, date, purpose, jtext_version, case_mode, sql_dialect,
    // table_name, auto_id).
    void write_header();

    // Sections. Light: "-- Name --" (closed by "-- End Name --"). Full: "=== Section: Name ===";
    // its intro, Fields and Data blocks follow, and the next section or finalize() closes it.
    void begin_section(std::string_view name);

    // Full profile, in a section before its Data block:
    //   write_include("Schema", path)  ->  === <#include#> Schema: path ===
    //   write_template(name, body)     ->  === Template: name ===, the body as a [* value
    //   begin_fields() ... end_fields() ->  === Fields === ... === End Fields ===
    //   begin_data()                    ->  === Data ===  (an open Fields block is closed first;
    //                                       a section without one gets an empty one, as the
    //                                       parser requires a Fields block before the data)
    // finalize() closes every block still open, so a file never ends unclosed.
    void write_include(std::string_view kind, std::string_view path);
    void write_template(std::string_view name, std::string_view body);
    void begin_fields();
    void end_fields();
    void begin_data();

    // Append a complete entry
    void append_entry(const JTextEntry& entry);

    // End the record just written. A record written as lines of its own (one per field,
    // or broken by a multiline value) ends with a blank line. A record that is one
    // multifield row from field 1 needs none: the next row's "1." starts the next
    // record, so rows follow one another directly (SPEC 2.6).
    void end_record();

    // Batch append — recommended for high-volume use (ts_store, double-buffering, etc.)
    void append_entries(std::span<const JTextEntry> entries);

    // Convenience overloads
    void append_entries(const std::vector<JTextEntry>& entries);

    void append_row(std::size_t number,
                    const std::vector<std::string>& fields,
                    std::string_view comment = "",
                    char32_t level_sep = 0);

    void finalize();

    // Flush the underlying stream (useful after a batch for durability)
    void flush();

    // === Auto-batching (internal buffering) ===
    // When enabled, append_entry/append_row will buffer entries and only write
    // them to the stream when the batch fills up, a section changes, or finalize/flush is called.
    //
    // Default = 0 (disabled) for predictable library behavior.
    void set_auto_batch_size(std::size_t num_entries);   // 0 = disable auto-batching

    [[nodiscard]] std::size_t auto_batch_size() const noexcept { return auto_batch_size_; }

    // Recommended batch size when maximizing throughput (used by ts_store by default).
    // Users of ts_store can override this if they want a different balance
    // between throughput, memory usage, and data-loss window on crash.
    static constexpr std::size_t kDefaultHighThroughputBatchSize = 10'000;

    // Convenience: enables auto-batching tuned for high throughput (10K lines).
    // This is what ts_store persistence will use by default.
    void enable_high_throughput_batching();

    // When a numbered list has more than 9 entries, set pad width so 1–99
    // line numbers right-align and '.' shares a column (SPEC §3.7).
    // 0 = no extra padding (single-digit entries omit the leading space).
    void set_line_number_pad_width(std::size_t width);
    [[nodiscard]] std::size_t line_number_pad_width() const noexcept { return line_pad_width_; }

    [[nodiscard]] bool is_finalized() const noexcept { return finalized_; }

private:
    void ensure_header();
    void ensure_not_finalized();
    void flush_pending_batch();   // internal helper
    // Write already-formatted lines (a multiline record), batched like any row.
    void write_lines(std::string text);
    // Report a bad row, then leave it out (Skip) or end the process (Terminate).
    void reject_row(std::size_t field, char32_t sep, std::string_view value);
    // Field-1 entry with no id: fill one in `scratch` and return it; otherwise
    // note the id it carries and return `entry` unchanged.
    const JTextEntry& resolve_record_id(const JTextEntry& entry, JTextEntry& scratch);
    std::int64_t take_record_id();
    void note_record_id(std::string_view value);

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
    std::size_t auto_batch_size_ = 0;
    std::vector<std::string> pending_batch_;   // stores pre-formatted lines for cheaper storage & faster flush

    std::size_t line_pad_width_ = 2;   // line numbers right-aligned in two columns (SPEC 3.7)

    BadRowPolicy bad_row_policy_ = BadRowPolicy::Terminate;
    std::size_t skipped_rows_ = 0;
    // The record being written (see end_record): its lines so far, and whether its one
    // line is a whole multifield row from field 1.
    std::size_t record_lines_ = 0;
    bool record_is_row_ = false;
    void count_record_line(std::size_t number, std::size_t field_count, bool multiline);
    std::string section_;   // current section, for error messages
    bool section_open_ = false;   // a section waits for its closer
    JTextProfile profile_ = JTextProfile::Light;
    enum class FullBlock { none, fields, data };
    FullBlock full_block_ = FullBlock::none;   // Full profile: the block open in the section
    bool fields_written_ = false;              // Full profile: this section has its Fields block
    void close_full_section();               // Full profile: closes the open block and section
    void write_full_header_line(std::size_t number, std::string_view value);

    // Record id generation (per section)
    std::int64_t record_id_seed_ = 1;
    std::int64_t record_id_step_ = 1;
    std::optional<std::int64_t> last_record_id_;

    // For emitting leading standardized // comment header (File Name etc) on path-based construction
    std::string source_path_;
    std::string filename_;   // the file's name only (header line 1)
};

// Line-number padding (SPEC §3.7): when item_count > 9, returns column width
// for right-aligned numbers ( 1. …  9. … 10. … up to 99.; wider when > 99).
[[nodiscard]] std::size_t jtext_line_pad_width_for_count(std::size_t item_count);

// Emit "<padded>N. " — pad_width from jtext_line_pad_width_for_count().
void write_jtext_line_number(std::ostream& out, std::size_t number, std::size_t pad_width);

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
// self-describing files (jText data, jText field lists, binary data, etc). A file of
// SQL (.sql, .jschma) takes write_sql_comment_header instead, the same block as "-- "
// comments. Call this before writing any data or jText's own # headers.
// The produced block is (keys chosen for brevity + colons for consistency; values aligned):
//
//   //File:    <the file's name: never a path, which goes stale when the file moves>
//   //Date:    YYYY-MM-DD
//   //Purpose: jText Data File | jText Field List File | ...
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
                               std::string_view path,
                               std::string_view purpose,
                               std::string_view related = {});

// The same block for a file of SQL (.sql, .jschma), as SQL comments, so psql, sqlite3
// and mysql can run the file as written (MySQL and MariaDB need the space after "--"):
//   -- File:    <the file's name>
//   -- Date:    YYYY-MM-DD
//   -- Purpose: <purpose>
//   -- Related: <related>        (only when given)
//   --
void write_sql_comment_header(std::ostream& os,
                              std::string_view path,
                              std::string_view purpose,
                              std::string_view related = {});
