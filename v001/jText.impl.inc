// jText::v001 — PUBLIC API DEFINITIONS (single source of truth).
//
// Include-fragment (no guard, includes nothing). Included INSIDE the
// jText::v001 namespace by jText.cpp (compiled into jtext_core), which first makes std
// visible with textual std #includes and provides the file-scope `fs` alias + std::literals.
// std names are spelled std:: (incl. std::size_t). Declarations live in jText.api.inc.

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

namespace {

// One parsed data line as a JTextEntry. A multiline value is one field holding the
// value's lines joined with '\n'; written back, it is a multiline value again.
auto entry_from_line(jtext::parsed_line pl) -> JTextEntry
{
    JTextEntry e;
    e.number = pl.number;
    e.comment = std::move(pl.comment);
    if (pl.kind == jtext::line_kind::multiline_opener) {
        e.fields.emplace_back(pl.hierarchy.empty() ? std::string{} : std::move(pl.hierarchy[0]));
        return e;
    }
    e.delimiter = pl.fmt.bookend;
    e.level_sep = pl.fmt.raw_data() ? 0 : pl.fmt.separator;

    auto& values = pl.hierarchy;
    if (values.size() == 1 && (values[0] == "NULL" || values[0] == "null" || values[0] == "Null")
        && !pl.fmt.raw_data()) {
        e.is_null = true;
        e.fields.emplace_back("NULL");
    } else if (!(values.size() == 1 && values[0].empty())) {
        for (auto& v : values) {
            // U+001F is the null marker of a compact row (SPEC 2.6.3); the writer emits it.
            if (v == "\x1F") e.fields.emplace_back(std::nullopt);
            else e.fields.emplace_back(std::move(v));
        }
    }

    // Values are kept exactly as written, like a database: case_mode is a comparison
    // setting (see text_equal), never a reason to rewrite the text a later save stores.
    return e;
}

} // namespace

auto JTextFile::read_file(std::string_view filepath, std::string_view only_section)
    -> std::expected<void, std::string>
{
    std::ifstream in(std::string{filepath});
    if (!in) return std::unexpected{"Cannot open file: " + std::string(filepath)};
    auto parsed = jtext::parse_file_structure(in);
    if (!parsed) {
        return std::unexpected{std::format("{}: line {}: {}", filepath, parsed.error().line_no,
                                           parsed.error().message)};
    }

    // Header: the light profile's "#" metadata, or the full profile's numbered
    // header lines by position (SPEC 2.3).
    for (const auto& m : parsed->metadata) parse_header(m);
    for (const auto& hl : parsed->header_lines) {
        if (hl.hierarchy.size() != 1) continue;
        const auto& value = hl.hierarchy[0];
        switch (hl.number) {
        case 2: date = value; break;
        case 3: purpose = value; break;
        case 5: parse_header("Case: " + value); break;
        case 6: sql_dialect = value; break;
        case 7: table_name = value; break;
        case 8: parse_header("AutoID: " + value); break;
        default: break;
        }
    }

    for (auto& ps : parsed->sections) {
        if (!only_section.empty() && ps.name != only_section) continue;
        JTextSection sec;
        sec.name = ps.name;
        for (auto& pl : ps.data_lines) {
            if (pl.number == 0) continue;   // record boundary
            sec.entries.push_back(entry_from_line(std::move(pl)));
        }
        sections.push_back(std::move(sec));
    }
    return {};
}

// ===================================================================
// Public API
// ===================================================================

auto JTextFile::read_full(std::string_view filepath) -> std::expected<void, std::string> {
    this->filename = std::string{filepath};   // ← store it

    return read_file(filepath, {});
}

auto JTextFile::read_section(std::string_view filepath, std::string_view section_name)
    -> std::expected<JTextSection, std::string>
{
    sections.clear();  // reuse internal storage

    auto res = read_file(filepath, section_name);
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
    writer.set_filename(target);
    writer.set_date(date);
    writer.set_purpose(purpose);
    writer.set_case_mode(case_mode);
    writer.set_sql_dialect(sql_dialect);
    writer.set_table_name(table_name);
    writer.set_auto_id(auto_id);

    writer.write_header();

    for (const auto& sec : sections) {
        writer.begin_section(sec.name);
        writer.set_line_number_pad_width(jtext_line_pad_width_for_count(sec.entries.size()));
        for (const auto& e : sec.entries) {
            writer.append_entry(e);
        }
    }

    writer.finalize();
    return {};
}

auto JTextFile::validate() const -> std::expected<void, std::string> {
    std::vector<std::string> checks;
    std::size_t field_count = 0;
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
            std::size_t last_num = 0;

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
                                   std::string_view path,
                                   std::string_view purpose,
                                   std::string_view related) {
    auto now = std::chrono::system_clock::now();
    auto today = std::chrono::floor<std::chrono::days>(now);
    std::string date_str = std::format("{:%Y-%m-%d}", today);

    // The file's name only: a path would go stale when the file or its project moves.
    os << "//File:    " << std::filesystem::path(std::string(path)).filename().string() << "\n";
    os << "//Date:    " << date_str << "\n";
    os << "//Purpose: " << purpose << "\n";
    if (!related.empty()) {
        os << "//Related: " << related << "\n";
    }
    os << "//\n";
    // End-of-text-header marker for consumers (e.g. binary log readers doing
    // safe prefix scan). Binary readers must *not* rely on getline after this.
}

bool text_equal(std::string_view a, std::string_view b, CaseMode mode) noexcept {
    if (mode == CaseMode::Sensitive) return a == b;
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto x = static_cast<unsigned char>(a[i]);
        const auto y = static_cast<unsigned char>(b[i]);
        if (std::toupper(x) != std::toupper(y)) return false;
    }
    return true;
}

std::size_t jtext_line_pad_width_for_count(std::size_t item_count) {
    // Two columns for field positions 1-99 (SPEC 3.7), wider only past that.
    return std::max<std::size_t>(2, std::to_string(item_count).size());
}

void write_jtext_line_number(std::ostream& out, std::size_t number, std::size_t pad_width) {
    std::string s = std::to_string(number);
    if (pad_width > s.size()) {
        out << std::string(pad_width - s.size(), ' ');
    }
    out << s << ". ";
}

void write_jtext_hash_header(std::ostream& out,
                               std::string_view created_utc,
                               std::string_view purpose,
                               CaseMode case_mode,
                               std::string_view table_name,
                               std::string_view sql_dialect,
                               bool auto_id) {
    out << std::format("# JText File - created {}\n", created_utc);
    out << std::format("# Purpose - {}\n", purpose);
    out << "# jText Version: 0.8\n";   // the file may use non-ASCII formatters (SPEC v0.8, 2.3)
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
}

void write_light_section_banner(std::ostream& out, std::string_view section_name) {
    out << "-- " << section_name << " --\n\n";
}

void write_fields_include_line(std::ostream& out, std::string_view jtflds_path) {
    out << "# Fields: " << jtflds_path << "\n\n";
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
    write_jtext_hash_header(out, date, purpose, case_mode, table_name, sql_dialect, auto_id);
    out << "\n";
}

// Bookends the writer may choose, most readable first. All are punctuation the
// SPEC allows in a formatter (printable ASCII, not alphanumeric, not whitespace);
// '[' and ']' are left out because "[]" is the raw-data marker.
static constexpr std::string_view kBookendChoices = "#^%~!@$&*+=:;?\"'(),./<>\\_`{}|-";

// True when the parser would open a comment inside `field`: the bookend with a
// space on each side, or a trailing " b" on the last field when a comment follows.
static bool field_has_comment_marker(std::string_view field, char b, bool last_before_comment)
{
    for (std::size_t i = 1; i < field.size(); ++i) {
        if (field[i] != b || field[i - 1] != ' ') continue;
        if (i + 1 < field.size() ? field[i + 1] == ' ' : last_before_comment) return true;
    }
    return false;
}

static std::string_view field_text(const std::optional<std::string>& f)
{
    return f ? std::string_view(*f) : std::string_view();
}

static std::string_view field_text(const std::string& f) { return f; }

// The default formatter: U+25AE splits fields, U+258C starts the comment (SPEC v0.8 Part A).
static constexpr char32_t kDefaultSplice  = U'\u25AE';
static constexpr char32_t kDefaultComment = U'\u258C';

// A row the caller gave no comment character (0) is written with U+258C, or, when its
// data holds U+258C, with an ASCII choice that is not in its data, so the row reads
// back as written (SPEC 3.2). A comment character the caller set is kept as given.
// `has_default_comment` comes from scan_row, so a row without U+258C costs nothing more.
template <class Fields>
static char32_t choose_bookend(char32_t requested, char32_t sep, const Fields& fields, bool has_comment,
                               bool has_default_comment)
{
    if (requested != 0) return requested;
    if (!has_default_comment && sep != kDefaultComment) return kDefaultComment;
    const auto appears = [&](char b) {
        for (const auto& f : fields)
            if (field_text(f).find(b) != std::string_view::npos) return true;
        return false;
    };
    for (const char b : kBookendChoices)
        if (b != sep && !appears(b)) return b;
    // Every choice is somewhere in the data: take the first the parser cannot
    // mistake for a comment marker.
    for (const char b : kBookendChoices) {
        if (b == sep) continue;
        bool clash = false;
        for (std::size_t i = 0; i < fields.size() && !clash; ++i)
            clash = field_has_comment_marker(field_text(fields[i]), b, has_comment && i + 1 == fields.size());
        if (!clash) return b;
    }
    return '#';
}

// What one pass over a row's data finds. sep_field is the 1-based number of the first
// field holding the row's separator, or 0: such a field reads back as two fields and
// the reader cannot tell where the split was, so the caller must give that row a
// separator that is not in its data.
struct RowScan {
    std::size_t sep_field = 0;
    bool has_default_comment = false;   // U+258C is in the data
    bool has_newline = false;           // a field is a multiline value
};

// One pass over a row's data: is U+258C anywhere, and which field (1-based) first holds
// `sep`. A plain byte loop over each character's first byte: it measured faster than an
// eight-byte (SWAR) scan here, and the compiler vectorizes it for long fields. This runs
// for every row written; a field whose first-byte test hits is then searched for the
// whole character.
template <class Fields>
static RowScan scan_row(const Fields& fields, char32_t sep)
{
    // A field holding a newline is written as a multiline value, whose lines are
    // verbatim: its comment character and separator do not matter.
    static const std::string comment = jtext::encode_utf8(kDefaultComment);
    RowScan scan;
    const std::string needle = jtext::encode_utf8(sep);
    const char sep_lead = needle.empty() ? '\0' : needle[0];
    const char comment_lead = comment[0];
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const std::string_view text = field_text(fields[i]);
        unsigned char lead = 0, found = 0, newline = 0;
        for (const char c : text) {
            lead |= static_cast<unsigned char>(c == comment_lead);
            found |= static_cast<unsigned char>(c == sep_lead);
            newline |= static_cast<unsigned char>(c == '\n');
        }
        if (newline) {
            scan.has_newline = true;
            continue;
        }
        if (found && needle.size() > 1) found = text.find(needle) != std::string_view::npos;
        if (lead) scan.has_default_comment |= text.find(comment) != std::string_view::npos;
        if (found && sep != 0 && scan.sep_field == 0) scan.sep_field = i + 1;
    }
    return scan;
}

// A row the caller gave no separator (level_sep 0) whose data holds U+25AE is written
// with the first ASCII choice that is not in its data. 0 when every choice is.
template <class Fields>
static char32_t choose_splice(char32_t requested_bookend, const Fields& fields)
{
    for (const char b : kBookendChoices) {
        if (b == '#' || b == requested_bookend) continue;
        bool in_data = false;
        for (const auto& f : fields)
            if (field_text(f).find(b) != std::string_view::npos) { in_data = true; break; }
        if (!in_data) return b;
    }
    return 0;
}

// How one row is written: its separator and bookend, or the field that makes it a bad
// row (the caller's separator is in that field's data).
struct RowFormat {
    char32_t sep = kDefaultSplice;
    char32_t bookend = kDefaultComment;
    std::size_t bad_field = 0;
    bool multiline = false;     // a field holds a newline: format_multiline_record
};

template <class Fields>
static RowFormat row_format(char32_t requested_bookend, char32_t level_sep, const Fields& fields, bool has_comment)
{
    const char32_t sep = level_sep != 0 ? level_sep : kDefaultSplice;
    const RowScan scan = scan_row(fields, sep);
    RowFormat f;
    f.sep = sep;
    if (scan.sep_field) {
        if (level_sep != 0) {
            f.bad_field = scan.sep_field;
            return f;
        }
        f.sep = choose_splice(requested_bookend, fields);
        if (f.sep == 0) {
            f.sep = kDefaultSplice;
            f.bad_field = scan.sep_field;
            return f;
        }
    }
    f.bookend = choose_bookend(requested_bookend, f.sep, fields, has_comment, scan.has_default_comment);
    f.multiline = scan.has_newline;
    return f;
}

void write_entry_to_stream(std::ostream& out, const JTextEntry& e, std::size_t pad_width,
                           char32_t sep_cp, char32_t bookend_cp)
{
    write_jtext_line_number(out, e.number, pad_width);

    // The two-character formatter <splice><comment> (SPEC 3.1), e.g. |# or |^. The
    // comment character can differ per row so values may contain " # "; the comment
    // marker is that character with a space on each side.
    const std::string sep = jtext::encode_utf8(sep_cp);
    const std::string bookend = jtext::encode_utf8(bookend_cp);
    out << sep << bookend << ' ';

    for (std::size_t i = 0; i < e.fields.size(); ++i) {
        if (i > 0) out << sep;
        const auto& f = e.fields[i];
        if (!f.has_value()) {
            out << '\x1F';  // official null marker for compact (chosen ASCII US)
        } else {
            out << *f;
        }
    }

    if (!e.comment.empty()) {
        out << ' ' << bookend << ' ' << e.comment;
    }
    out << '\n';
}

// Helper for auto-batching: format once into a string (cheaper to store than full JTextEntry)
static std::string format_entry(const JTextEntry& e, std::size_t pad_width, char32_t sep_cp, char32_t bookend_cp)
{
    std::string s;
    s.reserve(256);

    {
        std::ostringstream os;
        write_jtext_line_number(os, e.number, pad_width);
        s += os.str();
    }

    const std::string sep = jtext::encode_utf8(sep_cp);
    const std::string bookend = jtext::encode_utf8(bookend_cp);
    s += sep;        // <splice><comment> (SPEC 3.1)
    s += bookend;
    s += ' ';

    for (std::size_t i = 0; i < e.fields.size(); ++i) {
        if (i > 0) s += sep;
        const auto& f = e.fields[i];
        if (!f.has_value()) {
            s += '\x1F';
        } else {
            s += *f;
        }
    }

    if (!e.comment.empty()) {
        s += ' ';
        s += bookend;
        s += ' ';
        s += e.comment;
    }
    s += '\n';
    return s;
}

// Even faster path for the common append_row case when auto-batching
static std::string format_row(std::size_t number,
                              const std::vector<std::string>& fields,
                              std::string_view comment,
                              std::size_t pad_width,
                              char32_t sep_cp,
                              char32_t bookend_cp)
{
    std::string s;
    s.reserve(256);

    {
        std::ostringstream os;
        write_jtext_line_number(os, number, pad_width);
        s += os.str();
    }

    const std::string sep = jtext::encode_utf8(sep_cp);
    const std::string bookend = jtext::encode_utf8(bookend_cp);
    s += sep;        // <splice><comment> (SPEC 3.1)
    s += bookend;
    s += ' ';

    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) s += sep;
        s += fields[i];
    }

    if (!comment.empty()) {
        s += ' ';
        s += bookend;
        s += ' ';
        s += comment;
    }
    s += '\n';
    return s;
}

// A field as written: its text, or the compact-row null marker U+001F (SPEC 2.6.3).
static std::string_view field_or_null(const std::optional<std::string>& f)
{
    return f ? std::string_view(*f) : std::string_view("\x1F", 1);
}

static std::string_view field_or_null(const std::string& f) { return f; }

// A row with a field that holds a newline (SPEC v0.8 Part C): the fields before it
// are a compact row, that field is a multiline value on its own numbered line, and
// the fields after it continue at the next position. Value lines and the closer are
// indented to the column of the "[*", so every marker lines up. The row's comment
// goes on its first line.
template <class Fields>
static std::string format_multiline_record(std::size_t number, const Fields& fields,
                                           std::string_view comment, char32_t sep_cp,
                                           char32_t bookend_cp, std::size_t pad_width)
{
    const std::string sep = jtext::encode_utf8(sep_cp);
    const std::string bookend = jtext::encode_utf8(bookend_cp);
    const auto multiline = [&](std::size_t i) {
        return field_text(fields[i]).find('\n') != std::string_view::npos;
    };
    std::string out;
    bool comment_written = comment.empty();
    std::size_t i = 0;
    while (i < fields.size()) {
        std::ostringstream os;
        write_jtext_line_number(os, number + i, pad_width);
        out += os.str();
        if (multiline(i)) {
            out += "[*";
            if (!comment_written) { out += " # "; out += comment; comment_written = true; }
            out += '\n';
            const std::string indent(std::max(pad_width, std::to_string(number + i).size()) + 2, ' ');
            std::string_view text = field_text(fields[i]);
            for (;;) {
                const auto nl = text.find('\n');
                const auto line = text.substr(0, nl);
                out += indent;
                out += "**";
                if (!line.empty()) { out += ' '; out += line; }
                out += '\n';
                if (nl == std::string_view::npos) break;
                text.remove_prefix(nl + 1);
            }
            out += indent;
            out += "*]\n";
            ++i;
            continue;
        }
        out += sep;
        out += bookend;
        out += ' ';
        const std::size_t first = i;
        for (; i < fields.size() && !multiline(i); ++i) {
            if (i > first) out += sep;
            out += field_or_null(fields[i]);
        }
        if (!comment_written) { out += ' '; out += bookend; out += ' '; out += comment; comment_written = true; }
        out += '\n';
    }
    return out;
}

// Light profile: a section opens with "-- Name --" and closes with "-- End Name --".
void write_section_header(std::ostream& out, std::string_view name)
{
    out << std::format("-- {} --\n", name);
}

void write_section_closer(std::ostream& out, std::string_view name)
{
    out << std::format("-- End {} --\n", name);
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
    filename_ = std::filesystem::path(source_path_).filename().string();
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
      auto_batch_size_(other.auto_batch_size_),
      pending_batch_(std::move(other.pending_batch_)),
      line_pad_width_(other.line_pad_width_),
      record_id_seed_(other.record_id_seed_),
      record_id_step_(other.record_id_step_),
      last_record_id_(other.last_record_id_),
      bad_row_policy_(other.bad_row_policy_),
      skipped_rows_(other.skipped_rows_),
      section_(std::move(other.section_)),
      section_open_(other.section_open_),
      profile_(other.profile_),
      full_block_(other.full_block_),
      fields_written_(other.fields_written_),
      source_path_(std::move(other.source_path_)),
      filename_(std::move(other.filename_))
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
        auto_batch_size_ = other.auto_batch_size_;
        pending_batch_ = std::move(other.pending_batch_);
        line_pad_width_ = other.line_pad_width_;
        record_id_seed_ = other.record_id_seed_;
        record_id_step_ = other.record_id_step_;
        last_record_id_ = other.last_record_id_;
        bad_row_policy_ = other.bad_row_policy_;
        skipped_rows_ = other.skipped_rows_;
        section_ = std::move(other.section_);
        section_open_ = other.section_open_;
        profile_ = other.profile_;
        full_block_ = other.full_block_;
        fields_written_ = other.fields_written_;
        source_path_ = std::move(other.source_path_);
        filename_ = std::move(other.filename_);

        other.out_ = nullptr;
        other.owns_stream_ = false;
    }
    return *this;
}

void JTextWriter::set_filename(std::string_view name)
{
    filename_ = std::filesystem::path(std::string(name)).filename().string();
}
void JTextWriter::set_date(std::string_view d) { date_ = d; }
void JTextWriter::set_purpose(std::string_view p) { purpose_ = p; }
void JTextWriter::set_case_mode(CaseMode mode) { case_mode_ = mode; }
void JTextWriter::set_sql_dialect(std::string_view dialect) { sql_dialect_ = dialect; }
void JTextWriter::set_table_name(std::string_view name) { table_name_ = name; }
void JTextWriter::set_auto_id(bool enabled) { auto_id_ = enabled; }

void JTextWriter::set_record_id_start(std::int64_t seed, std::int64_t step)
{
    record_id_seed_ = seed;
    record_id_step_ = (step == 0) ? 1 : step;
    last_record_id_.reset();
}

std::int64_t JTextWriter::take_record_id()
{
    const std::int64_t id = last_record_id_ ? *last_record_id_ + record_id_step_ : record_id_seed_;
    last_record_id_ = id;
    return id;
}

// Track the id an incoming record already carries, so generated ids continue
// past it (largest so far for a positive step, smallest for a negative one).
// A non-integer value is left for the validator to report.
void JTextWriter::note_record_id(std::string_view value)
{
    if (!value.empty() && value.front() == '+') value.remove_prefix(1);
    std::int64_t id = 0;
    auto [p, ec] = std::from_chars(value.data(), value.data() + value.size(), id);
    if (value.empty() || ec != std::errc{} || p != value.data() + value.size()) return;
    if (!last_record_id_ || (record_id_step_ > 0 ? id > *last_record_id_ : id < *last_record_id_)) {
        last_record_id_ = id;
    }
}

const JTextEntry& JTextWriter::resolve_record_id(const JTextEntry& entry, JTextEntry& scratch)
{
    // Only a field-1 line carries the record id (the number is a field position).
    if (entry.number != 1) return entry;
    const bool missing = entry.fields.empty() || !entry.fields[0] || entry.fields[0]->empty();
    if (!missing) {
        note_record_id(*entry.fields[0]);
        return entry;
    }
    scratch = entry;
    if (scratch.fields.empty()) scratch.fields.emplace_back();
    scratch.fields[0] = std::to_string(take_record_id());
    return scratch;
}

void JTextWriter::set_line_number_pad_width(std::size_t width) { line_pad_width_ = width; }

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

    header_written_ = true;
    if (profile_ == JTextProfile::Full) {
        *out_ << "=== jText File ===\n";
        std::string date = date_;
        if (date.empty()) {
            date = std::format("{:%Y-%m-%d}", std::chrono::floor<std::chrono::days>(
                                                  std::chrono::system_clock::now()));
        }
        if (!filename_.empty()) write_full_header_line(1, filename_);
        write_full_header_line(2, date);
        if (!purpose_.empty()) write_full_header_line(3, purpose_);
        write_full_header_line(4, "0.8");
        write_full_header_line(5, case_mode_ == CaseMode::Insensitive ? "Insensitive" : "Sensitive");
        if (!sql_dialect_.empty()) write_full_header_line(6, sql_dialect_);
        if (!table_name_.empty()) write_full_header_line(7, table_name_);
        write_full_header_line(8, auto_id_ ? "true" : "false");
        out_->put('\n');
        return;
    }
    write_header_block(*out_, date_, purpose_, case_mode_,
                       sql_dialect_, table_name_, auto_id_);
}

// One numbered header line: the value with a formatter that cannot clash with it.
void JTextWriter::write_full_header_line(std::size_t number, std::string_view value)
{
    const std::vector<std::string> fields{std::string(value)};
    const RowFormat fmt = row_format(0, 0, fields, false);
    *out_ << format_row(number, fields, "", 2, fmt.sep, fmt.bookend);
}

void JTextWriter::close_full_section()
{
    if (full_block_ == FullBlock::fields) *out_ << "=== End Fields ===\n";
    if (full_block_ == FullBlock::data) *out_ << "=== End Data ===\n";
    full_block_ = FullBlock::none;
    if (section_open_) *out_ << "=== End Section ===\n\n";
    section_open_ = false;
}

void JTextWriter::write_include(std::string_view kind, std::string_view path)
{
    ensure_header();
    ensure_not_finalized();
    if (!out_) return;
    *out_ << std::format("=== <#include#> {}: {} ===\n", kind, path);
}

void JTextWriter::write_template(std::string_view name, std::string_view body)
{
    ensure_header();
    ensure_not_finalized();
    if (!out_) return;
    if (!body.empty() && body.back() == '\n') body.remove_suffix(1);
    *out_ << std::format("=== Template: {} ===\n[*\n", name);
    for (;;) {
        const auto nl = body.find('\n');
        const auto line = body.substr(0, nl);
        *out_ << "**";
        if (!line.empty()) *out_ << ' ' << line;
        out_->put('\n');
        if (nl == std::string_view::npos) break;
        body.remove_prefix(nl + 1);
    }
    *out_ << "*]\n";
}

void JTextWriter::begin_fields()
{
    ensure_header();
    ensure_not_finalized();
    if (!out_) return;
    *out_ << "=== Fields ===\n";
    full_block_ = FullBlock::fields;
    fields_written_ = true;
}

void JTextWriter::end_fields()
{
    if (!out_ || full_block_ != FullBlock::fields) return;
    *out_ << "=== End Fields ===\n";
    full_block_ = FullBlock::none;
}

void JTextWriter::begin_data()
{
    ensure_header();
    ensure_not_finalized();
    if (!out_) return;
    end_fields();
    if (!fields_written_) *out_ << "=== Fields ===\n=== End Fields ===\n";
    fields_written_ = true;
    flush_pending_batch();
    *out_ << "=== Data ===\n";
    full_block_ = FullBlock::data;
}

void JTextWriter::begin_section(std::string_view name)
{
    ensure_header();
    ensure_not_finalized();
    if (!out_) return;

    flush_pending_batch();   // never buffer across section boundaries
    line_pad_width_ = 2;
    last_record_id_.reset(); // record ids are per section (per table)
    if (profile_ == JTextProfile::Full) {
        close_full_section();
        section_ = name;
        section_open_ = true;
        fields_written_ = false;
        *out_ << std::format("=== Section: {} ===\n", name);
        return;
    }
    if (section_open_) {
        write_section_closer(*out_, section_);
        out_->put('\n');
    }
    section_ = name;
    section_open_ = true;
    write_section_header(*out_, name);
    out_->put('\n');
}

void JTextWriter::append_entry(const JTextEntry& entry)
{
    ensure_header();
    ensure_not_finalized();
    if (!out_) return;
    const RowFormat fmt = row_format(entry.delimiter, entry.level_sep, entry.fields, !entry.comment.empty());
    if (fmt.bad_field) {
        reject_row(fmt.bad_field, fmt.sep, field_text(entry.fields[fmt.bad_field - 1]));
        return;
    }

    JTextEntry scratch;
    const JTextEntry& e = resolve_record_id(entry, scratch);

    if (fmt.multiline) {
        write_lines(format_multiline_record(e.number, e.fields, e.comment, fmt.sep, fmt.bookend,
                                            line_pad_width_));
        return;
    }
    if (auto_batch_size_ > 0) {
        pending_batch_.push_back(format_entry(e, line_pad_width_, fmt.sep, fmt.bookend));
        if (pending_batch_.size() >= auto_batch_size_) {
            flush_pending_batch();
        }
    } else {
        write_entry_to_stream(*out_, e, line_pad_width_, fmt.sep, fmt.bookend);
    }
}

void JTextWriter::append_entries(std::span<const JTextEntry> entries)
{
    ensure_header();
    ensure_not_finalized();
    if (!out_) return;
    // Scan the whole batch first. Under Terminate the process ends before any of it is
    // written; under Skip each bad row is reported and left out below.
    std::vector<RowFormat> formats;
    formats.reserve(entries.size());
    for (const auto& entry : entries) {
        formats.push_back(row_format(entry.delimiter, entry.level_sep, entry.fields, !entry.comment.empty()));
        const auto& f = formats.back();
        if (f.bad_field && bad_row_policy_ == BadRowPolicy::Terminate)
            reject_row(f.bad_field, f.sep, field_text(entry.fields[f.bad_field - 1]));
    }
    const auto keep = [&](std::size_t n) {
        const auto& f = formats[n];
        if (!f.bad_field) return true;
        reject_row(f.bad_field, f.sep, field_text(entries[n].fields[f.bad_field - 1]));
        return false;
    };

    JTextEntry scratch;
    if (auto_batch_size_ > 0) {
        for (std::size_t n = 0; n < entries.size(); ++n) {
            if (!keep(n)) continue;
            const JTextEntry& e = resolve_record_id(entries[n], scratch);
            if (formats[n].multiline) {
                write_lines(format_multiline_record(e.number, e.fields, e.comment, formats[n].sep,
                                                    formats[n].bookend, line_pad_width_));
                continue;
            }
            pending_batch_.push_back(format_entry(e, line_pad_width_, formats[n].sep, formats[n].bookend));
            if (pending_batch_.size() >= auto_batch_size_) {
                flush_pending_batch();
            }
        }
    } else {
        for (std::size_t n = 0; n < entries.size(); ++n) {
            if (!keep(n)) continue;
            const JTextEntry& e = resolve_record_id(entries[n], scratch);
            if (formats[n].multiline) {
                write_lines(format_multiline_record(e.number, e.fields, e.comment, formats[n].sep,
                                                    formats[n].bookend, line_pad_width_));
                continue;
            }
            write_entry_to_stream(*out_, e, line_pad_width_, formats[n].sep, formats[n].bookend);
        }
    }
}

void JTextWriter::append_entries(const std::vector<JTextEntry>& entries)
{
    append_entries(std::span<const JTextEntry>(entries));
}

void JTextWriter::append_row(std::size_t number,
                             const std::vector<std::string>& fields,
                             std::string_view comment,
                             char32_t level_sep)
{
    ensure_header();
    ensure_not_finalized();
    if (!out_) return;
    const RowFormat fmt = row_format(0, level_sep, fields, !comment.empty());
    if (fmt.bad_field) {
        reject_row(fmt.bad_field, fmt.sep, fields[fmt.bad_field - 1]);
        return;
    }

    // Field-1 row: generate the record id when it is missing, else note it.
    if (number == 1 && (fields.empty() || fields[0].empty())) {
        std::vector<std::string> with_id = fields;
        if (with_id.empty()) with_id.emplace_back();
        with_id[0] = std::to_string(take_record_id());
        append_row(number, with_id, comment, level_sep);
        return;
    }
    if (number == 1) note_record_id(fields[0]);

    if (fmt.multiline) {
        write_lines(format_multiline_record(number, fields, comment, fmt.sep, fmt.bookend, line_pad_width_));
        return;
    }
    if (auto_batch_size_ > 0) {
        // Fast path: format directly into the pending batch without ever creating a JTextEntry
        pending_batch_.push_back(format_row(number, fields, comment, line_pad_width_, fmt.sep, fmt.bookend));
        if (pending_batch_.size() >= auto_batch_size_) {
            flush_pending_batch();
        }
    } else {
        JTextEntry e;
        e.number = number;
        for (const auto& f : fields) e.fields.emplace_back(f);
        e.comment = std::string(comment);
        e.level_sep = level_sep;
        write_entry_to_stream(*out_, e, line_pad_width_, fmt.sep, fmt.bookend);
    }
}

void JTextWriter::end_record()
{
    ensure_not_finalized();
    if (!out_) return;
    write_lines("\n");
}

void JTextWriter::write_lines(std::string text)
{
    if (auto_batch_size_ > 0) {
        pending_batch_.push_back(std::move(text));
        if (pending_batch_.size() >= auto_batch_size_) flush_pending_batch();
    } else {
        *out_ << text;
    }
}

void JTextWriter::finalize()
{
    if (finalized_ || !out_) return;

    flush_pending_batch();
    ensure_header();
    // Every block closes, like XML: what is open in the section, the section, then the file.
    if (profile_ == JTextProfile::Full) {
        close_full_section();
        *out_ << "=== End File ===\n";
        out_->flush();
        finalized_ = true;
        return;
    }
    if (section_open_) {
        write_section_closer(*out_, section_);
        section_open_ = false;
    }
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

void JTextWriter::set_auto_batch_size(std::size_t num_entries)
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

void JTextWriter::reject_row(std::size_t field, char32_t sep, std::string_view value)
{
    constexpr std::size_t kShown = 80;
    const std::string shown = value.size() > kShown
        ? std::string(value.substr(0, kShown)) + "..." : std::string(value);
    const bool skip = bad_row_policy_ == BadRowPolicy::Skip;
    std::println(stderr, "jText {}: cannot write a row: field {} contains the separator '{}'",
                 skip ? "WARNING" : "ERROR", field, jtext::encode_utf8(sep));
    std::println(stderr, "  file:    {}", !source_path_.empty() ? source_path_
                                         : !filename_.empty() ? filename_ : std::string("(stream)"));
    std::println(stderr, "  section: {}", section_.empty() ? "(none)" : section_);
    std::println(stderr, "  value:   \"{}\"", shown);
    std::println(stderr, "  fix:     give this row a separator that is not in its data (level_sep)");
    if (skip) {
        ++skipped_rows_;
        std::println(stderr, "  policy:  Skip - row left out ({} skipped so far)", skipped_rows_);
        return;
    }
    std::println(stderr, "  policy:  Terminate - rows before this one are written; ending the process");
    std::fflush(stderr);
    flush_pending_batch();
    if (out_) out_->flush();
    std::terminate();
}

void JTextWriter::ensure_not_finalized()
{
    if (finalized_) {
        throw std::logic_error("JTextWriter: cannot write after finalize()");
    }
}

