// File: tools/from_sql/source.hpp
//
// One database, as jtext_from_sql reads it: through Qlite (one API over the client
// libraries), each value with its NULL flag, never through a text dump to be split again (no CSV, no
// delimiters). Each backend lives in its own file; where its library is missing, the
// build uses that backend's *_none.cpp, whose open function says so.

#pragma once

#include "parser/section.hpp"   // jtext::kNullValue

#include <Qlite/v001/Database.hpp>   // the Database concept and Error every backend shares

#include <cctype>
#include <charconv>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace from_sql {

struct Column {
    std::string name;
    std::string type;          // the jText type: String, Integer, Number, Date, DateTime, Bool
    std::size_t length = 0;    // a String's declared length; 0: none
    bool not_null = false;
};

// A database column type as the jText type it holds (SPEC 2.4.1), and a length when
// the type carries one ("VARCHAR(80)"; PostgreSQL and MariaDB report it separately).
inline Column column_of(std::string name, const std::string& sql_type, std::size_t length, bool not_null)
{
    std::string t = sql_type;
    for (auto& ch : t) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
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
    // TEXT and CLOB are unbounded, though MariaDB reports TEXT's 65535: no length.
    if (c.type != "String" || t.find("text") != std::string::npos || t.find("clob") != std::string::npos)
        c.length = 0;
    return c;
}

// One row: each value as the database's text for it; a NULL as {NULL}.
using Row = std::vector<std::string>;

// The comments table's columns (SPEC 4.3), in the order callers read them.
inline constexpr const char* kCommentColumns = "block, kind, record_id, position, seq, comment";

// The declarations table's columns (SPEC 4.3): each field's declaration, part by part.
inline constexpr const char* kDeclarationColumns = "position, level, text";

struct Source {
    virtual ~Source() = default;
    virtual std::string dialect() const = 0;
    // `table` is always a plain SQL identifier (checked by the caller).
    virtual std::optional<std::vector<Column>> columns(const std::string& table) = 0;
    virtual std::optional<std::vector<Row>> rows(const std::string& table) = 0;   // ordered by field 1
    virtual std::optional<bool> has_table(const std::string& table) = 0;
    virtual std::optional<std::vector<Row>> comment_rows(const std::string& table) = 0;
    virtual std::optional<std::vector<Row>> declaration_rows(const std::string& table) = 0;   // by position, level
    std::string error;   // why the last call returned nothing
};

// Every row of `sql` on `db`, each value as the database's text for it (a NULL as
// {NULL}); `args` bound to its placeholders. On a Qlite error: nothing, with `error` set.
template<Qlite::v001::Database Db, typename... Args>
std::optional<std::vector<Row>> rows_of(Db& db, std::string& error, const std::string& sql, const Args&... args)
{
    try {
        auto st = db.prepare(sql);
        st.bind(args...);
        std::vector<Row> out;
        while (st.step()) {
            Row row;
            for (int c = 0; c < st.column_count(); ++c) {
                auto v = st.template get_at<std::optional<std::string>>(c);
                row.push_back(v ? std::move(*v) : std::string{jtext::kNullValue});
            }
            out.push_back(std::move(row));
        }
        return out;
    } catch (const Qlite::v001::Error& e) {
        error = e.what();
        return std::nullopt;
    }
}

// Each returns the connected source, or nothing with `error` set (the database's own
// message, or that this build has no such backend).
std::unique_ptr<Source> open_postgres(std::string& error);   // the PG* environment, as psql
std::unique_ptr<Source> open_sqlite(const std::string& path, std::string& error);   // read-only
std::unique_ptr<Source> open_mariadb(const std::string& database, std::string& error);

}  // namespace from_sql
