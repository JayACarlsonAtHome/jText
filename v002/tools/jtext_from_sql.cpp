// File: tools/jtext_from_sql.cpp
//
// jtext_from_sql - The reverse tool (PostgreSQL assumed).
// Takes data from an existing PostgreSQL table/query and generates
// the four jText component files:
//
//   <base_name>.jschma   - Literal CREATE TABLE IF NOT EXISTS (PostgreSQL)
//   <base_name>.jtFlds   - Field declarations for jText
//   <base_name>.jtinsrt  - Insert template (vertical style)
//   <base_name>.jtext    - Starter data file (or assembly file)
//
// Invocation (first cut):
//   jtext_from_sql <data_path> <base_name> <descriptor>
//
// Connection: uses standard PostgreSQL environment variables
// (PGDATABASE, PGHOST, PGUSER, PGPASSWORD, PGPORT, etc.).
// The descriptor file currently contains a single SELECT query
// (the tool assumes it targets one table).
//
// This is an early prototype. Keep each tool file small.
// A real version would have better error handling, type mapping,
// and support for a richer descriptor format.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <format>

#include <unistd.h>      // pipe, fork, dup2, read, execvp, _exit
#include <sys/wait.h>    // waitpid

namespace fs = std::filesystem;

// Accept only a safe SQL identifier (table name): leading letter/underscore,
// then letters/digits/underscores, <= 63 chars (PostgreSQL's NAMEDATALEN-1).
// Rejecting anything else is what stops SQL/where-clause injection through the
// table name extracted from the user-supplied descriptor.
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

// --- Simple result row holder (tab-separated from psql) ---
struct ColumnMeta {
    std::string name;
    std::string pg_type;
    bool not_null = false;
    bool is_primary = false;
};

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

// Run a psql command and capture stdout+stderr. Returns empty on failure.
//
// Hardened: the SQL is passed as a single argv element to execvp() — there is
// NO shell, so nothing in `sql` (table names, query text) can be interpreted as
// a shell metacharacter or command. This closes the command-injection path that
// the previous popen("psql … -c \"" + sql + "\"") construction opened.
static std::string run_psql(const std::string& sql, bool tuples_only = true) {
    int fds[2];
    if (::pipe(fds) != 0) return {};

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(fds[0]);
        ::close(fds[1]);
        return {};
    }

    if (pid == 0) {
        // Child: send stdout+stderr to the pipe, then exec psql directly.
        ::dup2(fds[1], STDOUT_FILENO);
        ::dup2(fds[1], STDERR_FILENO);
        ::close(fds[0]);
        ::close(fds[1]);
        // Field separator is a literal tab; -A unaligned, -t tuples-only, -X no rc.
        std::vector<char*> argv;
        auto push = [&](const char* s) { argv.push_back(const_cast<char*>(s)); };
        push("psql"); push("-X"); push("-A"); push("-F"); push("\t");
        if (tuples_only) push("-t");
        push("-c"); push(sql.c_str());
        argv.push_back(nullptr);
        ::execvp("psql", argv.data());
        ::_exit(127);  // exec failed (psql not found, etc.)
    }

    // Parent: read everything from the pipe, then reap the child.
    ::close(fds[1]);
    std::string result;
    char buffer[4096];
    ssize_t n;
    while ((n = ::read(fds[0], buffer, sizeof(buffer))) > 0) {
        result.append(buffer, static_cast<std::size_t>(n));
    }
    ::close(fds[0]);
    int status = 0;
    ::waitpid(pid, &status, 0);
    return result;
}

// Very basic PostgreSQL -> jText type mapping for the prototype
static std::string map_pg_type_to_jtext(const std::string& pg) {
    std::string t = pg;
    for (auto& c : t) c = static_cast<char>(tolower(c));

    if (t.find("int") != std::string::npos ||
        t.find("serial") != std::string::npos ||
        t.find("bigint") != std::string::npos) return "Number";
    if (t.find("date") != std::string::npos ||
        t.find("time") != std::string::npos) return "Date";
    return "String";
}

static std::string make_header(const std::string& filename,
                                  std::string_view purpose,
                                  std::string_view related = {}) {
    auto now = std::chrono::system_clock::now();
    auto today = std::chrono::floor<std::chrono::days>(now);
    std::string date_str = std::format("{:%Y-%m-%d}", today);

    std::string h = std::format(
        "//File:    {}\n"
        "//Date:    {}\n"
        "//Purpose: {}\n",
        filename, date_str, purpose);
    if (!related.empty()) {
        h += std::format("//Related: {}\n", related);
    }
    h += "//\n";
    return h;
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: jtext_from_sql <data_path> <base_name> <descriptor_file>\n";
        std::cerr << "Example: jtext_from_sql ./samples workshop_tools_from_db descriptor.sql\n";
        return 1;
    }

    fs::path data_dir = argv[1];
    std::string base  = argv[2];
    fs::path desc_path = argv[3];

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

    // Defense-in-depth: the table name is interpolated into SQL sent to psql, so
    // it must be a legal identifier. Reject anything else (e.g. an injected
    // "tools'; DROP TABLE x; --") before we build or run any query.
    if (!is_safe_identifier(table_name)) {
        std::cerr << "ERROR: refusing unsafe/invalid table name '" << table_name
                  << "' (expected a plain SQL identifier from the descriptor's FROM clause)\n";
        return 4;
    }

    std::cout << "Retrieving structure for table: " << table_name << "\n";

    // For the standardized top header (and internal content)
    // Compact single-line form per project convention:
    //   //Related: type=PostgreSQL table=workshop_tools
    // (db= can be added if PGDATABASE etc. is known)
    std::string related = std::format("type=PostgreSQL table={}", table_name);

    auto now0 = std::chrono::system_clock::now();
    auto today0 = std::chrono::floor<std::chrono::days>(now0);
    std::string date_str = std::format("{:%Y-%m-%d}", today0);

    // 3. Get column metadata - use a clean COPY to get one line per column
    std::string meta_sql =
        "COPY (SELECT column_name || E'\\t' || data_type || E'\\t' || is_nullable "
        "FROM information_schema.columns "
        "WHERE table_name = '" + table_name + "' "
        "ORDER BY ordinal_position) TO STDOUT;";

    std::string meta_raw = run_psql(meta_sql, false);

    std::string meta_line;
    std::vector<ColumnMeta> columns;
    std::istringstream row_stream(meta_raw);
    while (std::getline(row_stream, meta_line)) {
        meta_line = trim(meta_line);
        if (meta_line.empty()) continue;

        std::istringstream col(meta_line);
        ColumnMeta cm;
        std::string nullable;
        std::getline(col, cm.name, '\t');
        std::getline(col, cm.pg_type, '\t');
        std::getline(col, nullable, '\t');

        cm.not_null = (trim(nullable) == "NO");
        columns.push_back(cm);
    }

    if (columns.empty()) {
        std::cerr << "ERROR: No columns found for table '" << table_name << "'.\n";
        return 5;
    }

    fs::path out_dir = data_dir / base;
    fs::create_directories(out_dir);

    // 4. Generate .jschma (literal PostgreSQL DDL)
    {
        fs::path f = out_dir / (base + ".jschma");
        std::ofstream out(f);
        out << make_header(base + ".jschma", "SQL Schema File", related);
        out << "CREATE TABLE IF NOT EXISTS " << table_name << " (\n";
        for (size_t i = 0; i < columns.size(); ++i) {
            const auto& c = columns[i];
            out << "    " << c.name << " ";
            // crude type mapping for CREATE TABLE
            std::string t = c.pg_type;
            if (t == "integer") t = "INTEGER";
            else if (t == "bigint") t = "BIGINT";
            else if (t.find("character varying") != std::string::npos) t = "TEXT";
            else if (t == "text") t = "TEXT";
            else if (t.find("timestamp") != std::string::npos) t = "TIMESTAMPTZ";
            else if (t == "date") t = "DATE";
            else t = "TEXT";

            out << t;
            if (c.not_null) out << " NOT NULL";
            if (i + 1 < columns.size()) out << ",";
            out << "\n";
        }
        out << ");\n";
        std::cout << "Wrote " << f << "\n";
    }

    // 5. Generate .jtFlds
    {
        fs::path f = out_dir / (base + ".jtFlds");
        std::ofstream out(f);
        out << make_header(base + ".jtFlds", "jText Field List File", related);
        out << "=== Fields ===\n";
        for (size_t i = 0; i < columns.size(); ++i) {
            const auto& c = columns[i];
            out << " " << (i+1) << ". #/# " << c.name << "/"
                << map_pg_type_to_jtext(c.pg_type)
                << (c.not_null ? "/Not Null" : "/Nullable") << "\n";
        }
        std::cout << "Wrote " << f << "\n";
    }

    // 6. Generate .jtinsrt (basic vertical INSERT template)
    {
        fs::path f = out_dir / (base + ".jtinsrt");
        std::ofstream out(f);
        out << make_header(base + ".jtinsrt", "jText Data File", related);
        out << "=== Template: SQL Insert ===\n";
        out << " 1. <<< !!!INS!!!\n";
        out << "INSERT INTO " << table_name << " (\n";
        for (size_t i = 0; i < columns.size(); ++i) {
            out << "    " << columns[i].name;
            if (i + 1 < columns.size()) out << ",";
            out << "\n";
        }
        out << ") VALUES (\n";
        for (size_t i = 0; i < columns.size(); ++i) {
            out << "    {" << (i+1) << "}";
            if (i + 1 < columns.size()) out << ",";
            out << "\n";
        }
        out << ");\n";
        out << "!!!INS!!!\n";
        std::cout << "Wrote " << f << "\n";
    }

    // 7. Generate a starter .jtext (very minimal, with header only for now)
    {
        fs::path f = out_dir / (base + ".jtext");
        std::ofstream out(f);
        out << make_header(base + ".jtext", "jText Data File", related);
        out << "=== jText File ===\n";
        out << " 1. #?# " << base << ".jtext\n";
        out << " 2. #?# " << date_str << "\n";
        out << " 3. #?# Data retrieved from PostgreSQL table " << table_name << "\n";
        out << "\n";
        out << "=== Section: " << table_name << " ===\n";
        out << "=== Fields ===\n";
        // Reference the generated field list via comment for now
        out << "// (In real use you would use === <#include#> Fields: " << base << ".jtFlds ===)\n";
        for (size_t i = 0; i < columns.size(); ++i) {
            out << " " << (i+1) << ". #/# " << columns[i].name << "/"
                << map_pg_type_to_jtext(columns[i].pg_type) << "\n";
        }
        out << "=== Data ===\n";
        out << "// TODO: Run a data query and emit actual records here\n";
        out << "=== End Data ===\n";
        out << "=== End Section ===\n";
        out << "=== End File ===\n";
        std::cout << "Wrote " << f << "\n";
    }

    std::cout << "\nRetrieve complete. Files written to: " << out_dir << "\n";
    std::cout << "You will still need to fill in actual data rows (or run a second query).\n";
    return 0;
}
