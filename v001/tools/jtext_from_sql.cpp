// File: tools/jtext_from_sql.cpp
//
// jtext_from_sql - a database table back into jText: PostgreSQL (libpq) or SQLite.
// Generates the four jText component files in <data_path>/<base_name>/:
//
//   <base_name>.jschma   - CREATE TABLE IF NOT EXISTS, from the columns (SQL, -- header)
//   <base_name>.jtFlds   - the field declarations
//   <base_name>.jtinsrt  - the SQL Insert template
//   <base_name>.jtext    - the table: header, fields, every row, every comment
//
//   jtext_from_sql [--sqlite <database>] <data_path> <base_name> <descriptor>
//
// The descriptor holds a SELECT; the table is the name after its FROM. Without
// --sqlite the tool connects to PostgreSQL through the standard environment
// (PGHOST, PGDATABASE, PGUSER, PGPASSWORD, PGOPTIONS, ...), as psql does.
//
// Values come through the client library, each with its NULL flag: never through a
// text dump to be split again (no CSV, no delimiters). A NULL is written as {NULL}, a
// NULL date as the null date (SPEC 2.6.1). The comments come back from the table's
// linked <table>_comments table (SPEC 4.3), each onto its own line.
//
// A database keeps no Not Empty, NullDate(...) or auto designation: those declarations
// do not come back.

#include <jText/v001.hpp>          // JTextWriter, the // and -- header blocks
#include "export/xml_export.hpp"   // jtext::located_comment
#include "parser/section.hpp"      // jtext::kNullValue, kDefaultNullDate

#include <libpq-fe.h>
#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Accept only a safe SQL identifier (table name): leading letter/underscore,
// then letters/digits/underscores, <= 63 chars (PostgreSQL's NAMEDATALEN-1).
// Rejecting anything else is what stops SQL injection through the table name
// extracted from the user-supplied descriptor.
static bool is_safe_identifier(const std::string& s) {
    if (s.empty() || s.size() > 63) return false;
    const unsigned char c0 = static_cast<unsigned char>(s[0]);
    if (!(std::isalpha(c0) || c0 == '_')) return false;
    for (char ch : s) {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (!(std::isalnum(u) || u == '_')) return false;
    }
    return true;
}

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

static std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ── The table, as either database describes it ────────────────

struct Column {
    std::string name;
    std::string type;          // the jText type: String, Integer, Number, Date, DateTime, Bool
    std::size_t length = 0;    // a String's declared length; 0: none
    bool not_null = false;
};

// A database column type as the jText type it holds (SPEC 2.4.1), and a length when
// the type carries one ("VARCHAR(80)"; PostgreSQL reports it separately).
static Column column_of(std::string name, const std::string& sql_type, std::size_t length, bool not_null)
{
    const std::string t = lower(sql_type);
    Column c{std::move(name), "String", length, not_null};
    if (t.find("int") != std::string::npos || t.find("serial") != std::string::npos) c.type = "Integer";
    else if (t.starts_with("timestamp") || t.find("datetime") != std::string::npos) c.type = "DateTime";
    else if (t == "date") c.type = "Date";
    else if (t.find("bool") != std::string::npos) c.type = "Bool";
    else if (t.find("real") != std::string::npos || t.find("floa") != std::string::npos ||
             t.find("doub") != std::string::npos || t.find("numeric") != std::string::npos ||
             t.find("decimal") != std::string::npos) c.type = "Number";
    if (c.type == "String" && c.length == 0) {
        if (const auto open = t.find('('); open != std::string::npos)
            std::from_chars(t.data() + open + 1, t.data() + t.size(), c.length);
    }
    if (c.type != "String") c.length = 0;
    return c;
}

using Row = std::vector<std::string>;

// One database, read through its client library.
struct Source {
    virtual ~Source() = default;
    virtual std::string dialect() const = 0;
    virtual std::optional<std::vector<Column>> columns(const std::string& table) = 0;
    virtual std::optional<std::vector<Row>> rows(const std::string& table) = 0;
    virtual std::optional<bool> has_table(const std::string& table) = 0;
    virtual std::optional<std::vector<Row>> comment_rows(const std::string& table) = 0;
    std::string error;
};

// The comments table's columns, in the order comment_of() reads them (SPEC 4.3).
constexpr const char* kCommentColumns = "block, kind, record_id, position, seq, comment";

static jtext::located_comment comment_of(const Row& r)
{
    jtext::located_comment c;
    c.block = r[0];
    c.kind = r[1];
    std::from_chars(r[2].data(), r[2].data() + r[2].size(), c.record_id);
    std::from_chars(r[3].data(), r[3].data() + r[3].size(), c.position);
    std::from_chars(r[4].data(), r[4].data() + r[4].size(), c.seq);
    c.text = r[5];
    return c;
}

class Postgres final : public Source {
public:
    Postgres() : conn_(PQconnectdb(""), PQfinish) {}
    bool ok()
    {
        if (PQstatus(conn_.get()) == CONNECTION_OK) return true;
        error = PQerrorMessage(conn_.get());
        return false;
    }
    std::string dialect() const override { return "PostgreSQL"; }

    std::optional<std::vector<Column>> columns(const std::string& table) override
    {
        auto r = query("SELECT column_name, data_type, COALESCE(character_maximum_length, 0), is_nullable "
                       "FROM information_schema.columns "
                       "WHERE table_schema = current_schema() AND table_name = $1 "
                       "ORDER BY ordinal_position", {table});
        if (!r) return std::nullopt;
        std::vector<Column> out;
        for (const auto& row : *r) {
            std::size_t len = 0;
            std::from_chars(row[2].data(), row[2].data() + row[2].size(), len);
            out.push_back(column_of(row[0], row[1], len, row[3] == "NO"));
        }
        return out;
    }

    std::optional<std::vector<Row>> rows(const std::string& table) override
    {
        return query(std::format("SELECT * FROM {} ORDER BY 1", ident(table)), {});
    }

    std::optional<bool> has_table(const std::string& table) override
    {
        auto r = query("SELECT 1 FROM information_schema.tables "
                       "WHERE table_schema = current_schema() AND table_name = $1", {table});
        if (!r) return std::nullopt;
        return !r->empty();
    }

    std::optional<std::vector<Row>> comment_rows(const std::string& table) override
    {
        return query(std::format("SELECT {} FROM {} ORDER BY 1, 3, 4, 5", kCommentColumns, ident(table)), {});
    }

private:
    std::unique_ptr<PGconn, decltype(&PQfinish)> conn_;

    std::string ident(const std::string& name)
    {
        std::unique_ptr<char, decltype(&PQfreemem)> q(
            PQescapeIdentifier(conn_.get(), name.c_str(), name.size()), PQfreemem);
        return q ? std::string(q.get()) : std::string{};
    }

    // Each value as the server sends it in text form; a NULL as {NULL}.
    std::optional<std::vector<Row>> query(const std::string& sql, const std::vector<std::string>& params)
    {
        std::vector<const char*> p;
        for (const auto& s : params) p.push_back(s.c_str());
        std::unique_ptr<PGresult, decltype(&PQclear)> res(
            PQexecParams(conn_.get(), sql.c_str(), static_cast<int>(p.size()), nullptr, p.data(),
                         nullptr, nullptr, 0),
            PQclear);
        if (PQresultStatus(res.get()) != PGRES_TUPLES_OK) {
            error = PQerrorMessage(conn_.get());
            return std::nullopt;
        }
        std::vector<Row> out;
        for (int r = 0; r < PQntuples(res.get()); ++r) {
            Row row;
            for (int c = 0; c < PQnfields(res.get()); ++c)
                row.push_back(PQgetisnull(res.get(), r, c)
                                  ? std::string{jtext::kNullValue}
                                  : std::string(PQgetvalue(res.get(), r, c),
                                                static_cast<std::size_t>(PQgetlength(res.get(), r, c))));
            out.push_back(std::move(row));
        }
        return out;
    }
};

class Sqlite final : public Source {
public:
    explicit Sqlite(const std::string& path) : db_(nullptr, sqlite3_close)
    {
        sqlite3* db = nullptr;
        rc_ = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
        db_.reset(db);
        if (rc_ != SQLITE_OK) error = db ? sqlite3_errmsg(db) : "cannot open the database";
    }
    bool ok() const { return rc_ == SQLITE_OK; }
    std::string dialect() const override { return "SQLite"; }

    std::optional<std::vector<Column>> columns(const std::string& table) override
    {
        auto r = query("SELECT name, type, \"notnull\", pk FROM pragma_table_info(?1)", {table});
        if (!r) return std::nullopt;
        std::vector<Column> out;
        // A primary key holds a value in every row, as PostgreSQL reports it (not null).
        for (const auto& row : *r) out.push_back(column_of(row[0], row[1], 0, row[2] == "1" || row[3] != "0"));
        return out;
    }

    std::optional<std::vector<Row>> rows(const std::string& table) override
    {
        return query(std::format("SELECT * FROM \"{}\" ORDER BY 1", table), {});
    }

    std::optional<bool> has_table(const std::string& table) override
    {
        auto r = query("SELECT 1 FROM sqlite_schema WHERE type = 'table' AND name = ?1", {table});
        if (!r) return std::nullopt;
        return !r->empty();
    }

    std::optional<std::vector<Row>> comment_rows(const std::string& table) override
    {
        return query(std::format("SELECT {} FROM \"{}\" ORDER BY 1, 3, 4, 5", kCommentColumns, table), {});
    }

private:
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db_;
    int rc_ = SQLITE_OK;

    // Each value as SQLite's text for it; a NULL as {NULL}. Only safe identifiers go
    // into the SQL text; values are bound.
    std::optional<std::vector<Row>> query(const std::string& sql, const std::vector<std::string>& params)
    {
        sqlite3_stmt* raw = nullptr;
        if (sqlite3_prepare_v2(db_.get(), sql.c_str(), -1, &raw, nullptr) != SQLITE_OK) {
            error = sqlite3_errmsg(db_.get());
            return std::nullopt;
        }
        std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> st(raw, sqlite3_finalize);
        for (std::size_t i = 0; i < params.size(); ++i)
            sqlite3_bind_text(st.get(), static_cast<int>(i + 1), params[i].c_str(),
                              static_cast<int>(params[i].size()), SQLITE_TRANSIENT);
        std::vector<Row> out;
        int step = 0;
        while ((step = sqlite3_step(st.get())) == SQLITE_ROW) {
            Row row;
            for (int c = 0; c < sqlite3_column_count(st.get()); ++c) {
                if (sqlite3_column_type(st.get(), c) == SQLITE_NULL) {
                    row.emplace_back(jtext::kNullValue);
                    continue;
                }
                const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(st.get(), c));
                row.emplace_back(text, static_cast<std::size_t>(sqlite3_column_bytes(st.get(), c)));
            }
            out.push_back(std::move(row));
        }
        if (step != SQLITE_DONE) {
            error = sqlite3_errmsg(db_.get());
            return std::nullopt;
        }
        return out;
    }
};

// ── jText out ─────────────────────────────────────────────────

// A declaration's levels: name/Type[/length]/Not Null|Nullable.
static std::vector<std::string> declaration(const Column& c)
{
    std::vector<std::string> d{c.name, c.type};
    if (c.length > 0) d.push_back(std::to_string(c.length));
    d.emplace_back(c.not_null ? "Not Null" : "Nullable");
    return d;
}

static std::string sql_type(const Column& c)
{
    if (c.type == "Integer") return "INTEGER";
    if (c.type == "Number") return "NUMERIC";
    if (c.type == "Date") return "DATE";
    if (c.type == "DateTime") return "TIMESTAMP";
    if (c.type == "Bool") return "BOOLEAN";
    return c.length > 0 ? std::format("VARCHAR({})", c.length) : std::string{"TEXT"};
}

// The comments of one record (or of the Fields block), by the field their line starts at.
struct LineComments {
    std::map<unsigned, std::string> line;                  // the comment on the line
    std::map<unsigned, std::vector<std::string>> before;   // comment lines before it
    std::vector<std::string> end;                          // comment lines at the block's end
};

static void write_table(std::ostream& out, const std::string& base, const std::string& table,
                        const std::string& dialect, const std::string& related,
                        const std::vector<Column>& cols, std::vector<Row> rows,
                        const std::vector<jtext::located_comment>& comments)
{
    LineComments fields;
    std::map<std::int64_t, LineComments> records;
    std::vector<std::string> data_end;
    for (const auto& c : comments) {   // ordered by block, record, position, seq
        const bool in_fields = c.block == "fields";
        if (!in_fields && c.position == 0) { data_end.push_back(c.text); continue; }
        auto& lc = in_fields ? fields : records[c.record_id];
        if (c.position == 0) lc.end.push_back(c.text);
        else if (c.kind == "line") lc.line[c.position] = c.text;
        else lc.before[c.position].push_back(c.text);
    }

    jText::v001::write_file_comment_header(out, base + ".jtext", "jText Data File", related);
    jText::v001::JTextWriter w(out);
    w.set_profile(jText::v001::JTextProfile::Full);
    w.set_filename(base + ".jtext");
    w.set_purpose(std::format("Data retrieved from {} table {}", dialect, table));
    w.set_sql_dialect(dialect);
    w.set_table_name(table);
    w.write_header();
    w.begin_section(table);

    w.begin_fields();
    for (std::size_t i = 0; i < cols.size(); ++i) {
        const auto p = static_cast<unsigned>(i + 1);
        for (const auto& t : fields.before[p]) w.write_comment_line(t);
        w.append_row(p, declaration(cols[i]), fields.line.contains(p) ? fields.line[p] : "", U'/');
    }
    for (const auto& t : fields.end) w.write_comment_line(t);
    w.end_fields();

    w.begin_data();
    for (auto& row : rows) {
        // A date field always holds a date (SPEC 2.6.1).
        for (std::size_t i = 0; i < cols.size() && i < row.size(); ++i) {
            if ((cols[i].type == "Date" || cols[i].type == "DateTime") &&
                (row[i].empty() || row[i] == jtext::kNullValue))
                row[i] = cols[i].type == "Date" ? std::string{jtext::kDefaultNullDate}
                                                : std::format("{} 00:00:00", jtext::kDefaultNullDate);
        }
        std::int64_t id = 0;
        if (!row.empty()) std::from_chars(row[0].data(), row[0].data() + row[0].size(), id);
        auto& lc = records[id];

        // One line from field 1, and a new line wherever a comment's line starts.
        std::vector<unsigned> starts{1};
        for (const auto& [p, _] : lc.line) starts.push_back(p);
        for (const auto& [p, _] : lc.before) starts.push_back(p);
        std::ranges::sort(starts);
        starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
        for (std::size_t s = 0; s < starts.size() && starts[s] <= row.size(); ++s) {
            const unsigned from = starts[s];
            const unsigned to = s + 1 < starts.size() ? starts[s + 1] : static_cast<unsigned>(row.size() + 1);
            for (const auto& t : lc.before[from]) w.write_comment_line(t);
            jText::v001::JTextEntry e;
            e.number = from;
            for (unsigned p = from; p < to && p <= row.size(); ++p) e.fields.emplace_back(row[p - 1]);
            if (lc.line.contains(from)) e.comment = lc.line[from];
            w.append_entry(e);
        }
        w.end_record();
    }
    for (const auto& t : data_end) w.write_comment_line(t);
    w.finalize();
}

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    std::string sqlite_path;
    if (args.size() >= 2 && args[0] == "--sqlite") {
        sqlite_path = args[1];
        args.erase(args.begin(), args.begin() + 2);
    }
    if (args.size() != 3) {
        std::cerr << "Usage: jtext_from_sql [--sqlite <database>] <data_path> <base_name> <descriptor_file>\n";
        std::cerr << "Example: jtext_from_sql ./samples workshop_tools_from_db descriptor.sql\n";
        return 1;
    }

    fs::path data_dir = args[0];
    std::string base  = args[1];
    fs::path desc_path = args[2];

    // 1. Read descriptor (for v1: just the SELECT query)
    std::ifstream desc(desc_path);
    if (!desc) {
        std::cerr << "ERROR: Cannot read descriptor: " << desc_path << "\n";
        return 2;
    }
    std::string query;
    std::string line;
    while (std::getline(desc, line)) {
        line = trim(line);
        if (!line.empty() && line[0] != '#') {
            query += line + " ";
        }
    }
    query = trim(query);
    if (query.empty()) {
        std::cerr << "ERROR: Descriptor file contained no query.\n";
        return 3;
    }

    // 2. Get table name (very crude - take the first table-like token after FROM)
    // For a real tool we would parse properly or require the table name in the descriptor.
    std::string table_name = "unknown_table";
    size_t from_pos = query.find(" from ");
    if (from_pos == std::string::npos) from_pos = query.find(" FROM ");
    if (from_pos != std::string::npos) {
        std::istringstream iss(query.substr(from_pos + 6));
        iss >> table_name;
        // strip schema if present
        size_t dot = table_name.find('.');
        if (dot != std::string::npos) table_name = table_name.substr(dot + 1);
    }

    // Defense-in-depth: the table name goes into SQL text, so it must be a legal
    // identifier. Reject anything else (e.g. an injected "tools'; DROP TABLE x; --")
    // before connecting to anything.
    if (!is_safe_identifier(table_name)) {
        std::cerr << "ERROR: refusing unsafe/invalid table name '" << table_name
                  << "' (expected a plain SQL identifier from the descriptor's FROM clause)\n";
        return 4;
    }

    // 3. Connect, and read the table: columns, rows, comments. Nothing is written
    // unless all of it was read.
    std::unique_ptr<Source> db;
    if (sqlite_path.empty()) {
        auto pg = std::make_unique<Postgres>();
        if (!pg->ok()) {
            std::cerr << "ERROR: PostgreSQL: " << pg->error << "nothing was written.\n";
            return 6;
        }
        db = std::move(pg);
    } else {
        auto lite = std::make_unique<Sqlite>(sqlite_path);
        if (!lite->ok()) {
            std::cerr << "ERROR: SQLite: " << sqlite_path << ": " << lite->error
                      << "\nnothing was written.\n";
            return 6;
        }
        db = std::move(lite);
    }
    std::cout << "Retrieving table " << table_name << " from " << db->dialect() << "\n";

    const auto failed = [&](std::string_view what) {
        std::cerr << "ERROR: " << db->dialect() << ": " << what << ": " << db->error
                  << "\nnothing was written.\n";
        return 6;
    };
    const auto columns = db->columns(table_name);
    if (!columns) return failed("reading the columns");
    if (columns->empty()) {
        std::cerr << "ERROR: No columns found for table '" << table_name << "'.\n";
        return 5;
    }
    const auto rows = db->rows(table_name);
    if (!rows) return failed("reading the rows");
    const std::string comments_table = table_name + "_comments";
    const auto has_comments = db->has_table(comments_table);
    if (!has_comments) return failed("looking for the comments table");
    std::vector<jtext::located_comment> comments;
    if (*has_comments) {
        const auto c = db->comment_rows(comments_table);
        if (!c) return failed("reading the comments");
        for (const auto& r : *c) comments.push_back(comment_of(r));
    }

    const std::string related = std::format("type={} table={}", db->dialect(), table_name);
    fs::path out_dir = data_dir / base;
    fs::create_directories(out_dir);

    // 4. .jschma: the table's CREATE TABLE, from the jText types.
    {
        fs::path f = out_dir / (base + ".jschma");
        std::ofstream out(f);
        jText::v001::write_sql_comment_header(out, base + ".jschma", "SQL Schema File", related);
        out << "CREATE TABLE IF NOT EXISTS " << table_name << " (\n";
        for (size_t i = 0; i < columns->size(); ++i) {
            const auto& c = (*columns)[i];
            out << "    " << c.name << " " << sql_type(c) << (c.not_null ? " NOT NULL" : "")
                << (i + 1 < columns->size() ? "," : "") << "\n";
        }
        out << ");\n";
        std::cout << "Wrote " << f << "\n";
    }

    // 5. .jtFlds: the field declarations.
    {
        fs::path f = out_dir / (base + ".jtFlds");
        std::ofstream out(f);
        jText::v001::write_file_comment_header(out, base + ".jtFlds", "jText Field List File", related);
        out << "=== Fields ===\n";
        for (size_t i = 0; i < columns->size(); ++i) {
            const auto d = declaration((*columns)[i]);
            std::string levels;
            for (std::size_t k = 0; k < d.size(); ++k) levels += (k ? "/" : "") + d[k];
            out << std::format("{:>2}. /# {}\n", i + 1, levels);
        }
        out << "=== End Fields ===\n";
        std::cout << "Wrote " << f << "\n";
    }

    // 6. .jtinsrt: the SQL Insert template (SPEC 5: a bare "[*", "** " lines, "*]").
    {
        fs::path f = out_dir / (base + ".jtinsrt");
        std::ofstream out(f);
        jText::v001::write_file_comment_header(out, base + ".jtinsrt", "jText Data File", related);
        out << "=== Template: SQL Insert ===\n";
        out << "[*\n";
        out << "** INSERT INTO " << table_name << " (\n";
        for (size_t i = 0; i < columns->size(); ++i)
            out << "**     " << (*columns)[i].name << (i + 1 < columns->size() ? "," : "") << "\n";
        out << "** ) VALUES (\n";
        for (size_t i = 0; i < columns->size(); ++i)
            out << "**     {" << (i + 1) << "}" << (i + 1 < columns->size() ? "," : "") << "\n";
        out << "** );\n";
        out << "*]\n";
        std::cout << "Wrote " << f << "\n";
    }

    // 7. .jtext: the table, every row and every comment.
    {
        fs::path f = out_dir / (base + ".jtext");
        std::ofstream out(f);
        write_table(out, base, table_name, db->dialect(), related, *columns, *rows, comments);
        std::cout << "Wrote " << f << " (" << rows->size() << " rows, " << comments.size() << " comments)\n";
    }
    return 0;
}
