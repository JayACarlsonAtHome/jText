// File: tools/jtext_from_sql.cpp
//
// jtext_from_sql - a database table back into jText: PostgreSQL (libpq), SQLite, or
// MariaDB / MySQL (Connector/C).
// Generates the four jText component files in <data_path>/<base_name>/:
//
//   <base_name>.jschma   - CREATE TABLE IF NOT EXISTS, from the columns (SQL, -- header)
//   <base_name>.jtFlds   - the field declarations
//   <base_name>.jtinsrt  - the SQL Insert template
//   <base_name>.jtext    - the table: header, fields, every row, every comment
//
//   jtext_from_sql [--sqlite <file> | --mariadb <database>] <data_path> <base_name> <descriptor>
//
// The descriptor holds a SELECT; the table is the name after its FROM. Without an
// option the tool connects to PostgreSQL through the standard environment (PGHOST,
// PGDATABASE, PGUSER, PGPASSWORD, PGOPTIONS, ...), as psql does. --mariadb connects
// over the local socket as the current login, as the mariadb client does.
//
// Values come through Qlite (Qlite/, one API over the three databases' client
// libraries), each with its NULL flag: never through a text dump to be split again (no
// CSV, no delimiters). A NULL is written as {NULL}, a
// NULL date as the null date (SPEC 2.6.1). The comments come back from the table's
// linked <table>_comments table (SPEC 4.3), each onto its own line.
//
// The field declarations come back from the linked <table>_fields table (SPEC 4.3), as
// written, Not Empty, NullDate(...) and auto designations included; a table without one
// gets declarations from its columns (name, type, length, Not Null).

#include <jText/v002.hpp>          // JTextWriter, the // and -- header blocks
#include "export/xml_export.hpp"   // jtext::located_comment
#include "parser/section.hpp"      // jtext::kNullValue, kDefaultNullDate

#include "from_sql/source.hpp"   // the databases, one file each
#include "common/record_lines.hpp"   // the rows and their comments, back as jText lines

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

using from_sql::Column;
using from_sql::Row;

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

// ── jText out ─────────────────────────────────────────────────

// A declaration's levels from a column: name/Type[/length]/Not Null|Nullable.
static std::vector<std::string> declaration(const Column& c)
{
    std::vector<std::string> d{c.name, c.type};
    if (c.length > 0) d.push_back(std::to_string(c.length));
    d.emplace_back(c.not_null ? "Not Null" : "Nullable");
    return d;
}

static bool same_name(std::string_view a, std::string_view b)
{
    return std::ranges::equal(a, b, [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
}

// Each column's declaration: as stored in <table>_fields when that names the same field
// at its position (a database may fold a name's case), else from the column.
static std::vector<std::vector<std::string>> declarations(const std::vector<Column>& cols,
                                                          const std::map<unsigned, std::vector<std::string>>& stored)
{
    std::vector<std::vector<std::string>> out;
    for (std::size_t i = 0; i < cols.size(); ++i) {
        const auto it = stored.find(static_cast<unsigned>(i + 1));
        const bool use = it != stored.end() && !it->second.empty() && same_name(it->second[0], cols[i].name);
        out.push_back(use ? it->second : declaration(cols[i]));
    }
    return out;
}

// A Date/DateTime field's null date (SPEC 2.6.1): its NullDate(...), else the default.
static std::string null_date_of(const std::vector<std::string>& decl)
{
    for (const auto& level : decl)
        if (level.starts_with("NullDate(") && level.ends_with(")"))
            return level.substr(9, level.size() - 10);
    return std::string{jtext::kDefaultNullDate};
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

static void write_table(std::ostream& out, const std::string& base, const std::string& table,
                        const std::string& dialect, const std::string& related,
                        const std::vector<Column>& cols, const std::vector<std::vector<std::string>>& decls,
                        std::vector<Row> rows, const std::vector<jtext::located_comment>& comments)
{
    const auto grouped = jtools::group_comments(comments);

    jText::v002::write_file_comment_header(out, base + ".jtext", "jText Data File", related);
    jText::v002::JTextWriter w(out);
    w.set_profile(jText::v002::JTextProfile::Full);
    w.set_filename(base + ".jtext");
    w.set_purpose(std::format("Data retrieved from {} table {}", dialect, table));
    w.set_sql_dialect(dialect);
    w.set_table_name(table);
    w.write_header();
    w.begin_section(table);

    std::vector<jtools::Declaration> fields;
    for (const auto& d : decls) fields.push_back({d, {}});
    for (const auto& [p, text] : grouped.fields.line)
        if (p >= 1 && p <= fields.size()) fields[p - 1].comment = text;
    jtools::write_fields(w, fields, grouped.fields);

    w.begin_data();
    static const jtools::LineComments kNone;
    for (auto& row : rows) {
        // A date field always holds a date: its null date (SPEC 2.6.1).
        for (std::size_t i = 0; i < cols.size() && i < row.size(); ++i) {
            if ((cols[i].type == "Date" || cols[i].type == "DateTime") &&
                (row[i].empty() || row[i] == jtext::kNullValue)) {
                const std::string d = null_date_of(decls[i]);
                row[i] = cols[i].type == "Date" || d.size() > 10 ? d : std::format("{} 00:00:00", d);
            }
        }
        const auto it = grouped.records.find(jtools::record_id(row));
        jtools::write_record(w, row, it == grouped.records.end() ? kNone : it->second);
    }
    for (const auto& t : grouped.data_end) w.write_comment_line(t);
    w.finalize();
}

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    std::string backend = "postgres";
    std::string where;   // the SQLite file, or the MariaDB database
    if (args.size() >= 2 && (args[0] == "--sqlite" || args[0] == "--mariadb")) {
        backend = args[0].substr(2);
        where = args[1];
        args.erase(args.begin(), args.begin() + 2);
    }
    if (args.size() != 3) {
        std::cerr << "Usage: jtext_from_sql [--sqlite <file> | --mariadb <database>] "
                     "<data_path> <base_name> <descriptor_file>\n";
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
    std::string error;
    std::unique_ptr<from_sql::Source> db = backend == "sqlite"  ? from_sql::open_sqlite(where, error)
                                         : backend == "mariadb" ? from_sql::open_mariadb(where, error)
                                                                : from_sql::open_postgres(error);
    if (!db) {
        std::cerr << "ERROR: " << backend << (where.empty() ? "" : " " + where) << ": " << error
                  << (error.ends_with('\n') ? "" : "\n") << "nothing was written.\n";
        return 6;
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

    // The declarations, as written, from <table>_fields (SPEC 4.3), when the table has one.
    std::map<unsigned, std::vector<std::string>> stored;
    const std::string fields_table = table_name + "_fields";
    const auto has_fields = db->has_table(fields_table);
    if (!has_fields) return failed("looking for the declarations table");
    if (*has_fields) {
        const auto d = db->declaration_rows(fields_table);
        if (!d) return failed("reading the declarations");
        for (const auto& r : *d) {
            unsigned position = 0;
            std::from_chars(r[0].data(), r[0].data() + r[0].size(), position);
            stored[position].push_back(r[2]);   // rows come by position, then level
        }
    }
    const auto decls = declarations(*columns, stored);

    const std::string related = std::format("type={} table={}", db->dialect(), table_name);
    fs::path out_dir = data_dir / base;
    fs::create_directories(out_dir);

    // 4. .jschma: the table's CREATE TABLE, from the jText types.
    {
        fs::path f = out_dir / (base + ".jschma");
        std::ofstream out(f);
        jText::v002::write_sql_comment_header(out, base + ".jschma", "SQL Schema File", related);
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
        jText::v002::write_file_comment_header(out, base + ".jtFlds", "jText Field List File", related);
        out << "=== Fields ===\n";
        for (size_t i = 0; i < columns->size(); ++i) {
            const auto& d = decls[i];
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
        jText::v002::write_file_comment_header(out, base + ".jtinsrt", "jText Data File", related);
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
        write_table(out, base, table_name, db->dialect(), related, *columns, decls, *rows, comments);
        std::cout << "Wrote " << f << " (" << rows->size() << " rows, " << comments.size() << " comments)\n";
    }
    return 0;
}
