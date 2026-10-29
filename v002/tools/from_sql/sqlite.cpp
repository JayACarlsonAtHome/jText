// File: tools/from_sql/sqlite.cpp
//
// jtext_from_sql's SQLite, through Qlite (Qlite::v002::Sqlite), opened read-only.

#include "from_sql/source.hpp"

#include <Qlite/v002/Sqlite.hpp>

#include <format>
#include <memory>

namespace from_sql {

namespace {

class Sqlite final : public Source {
public:
    std::string dialect() const override { return "SQLite"; }

    std::optional<std::vector<Column>> columns(const std::string& table) override
    {
        auto r = rows_of("SELECT name, type, \"notnull\", pk FROM pragma_table_info(?)", table);
        if (!r) return std::nullopt;
        std::vector<Column> out;
        // A primary key holds a value in every row, as PostgreSQL reports it (not null).
        for (const auto& row : *r) out.push_back(column_of(row[0], row[1], 0, row[2] == "1" || row[3] != "0"));
        return out;
    }

    std::optional<std::vector<Row>> rows(const std::string& table) override
    {
        return rows_of(std::format("SELECT * FROM \"{}\" ORDER BY 1", table));
    }

    std::optional<bool> has_table(const std::string& table) override
    {
        auto r = rows_of("SELECT 1 FROM sqlite_schema WHERE type = 'table' AND name = ?", table);
        if (!r) return std::nullopt;
        return !r->empty();
    }

    std::optional<std::vector<Row>> comment_rows(const std::string& table) override
    {
        return rows_of(std::format("SELECT {} FROM \"{}\" ORDER BY 1, 3, 4, 5", kCommentColumns, table));
    }

    std::optional<std::vector<Row>> declaration_rows(const std::string& table) override
    {
        return rows_of(std::format("SELECT {} FROM \"{}\" ORDER BY 1, 2", kDeclarationColumns, table));
    }

    std::optional<Qlite::v002::Sqlite> db;

private:
    template<typename... Args>
    std::optional<std::vector<Row>> rows_of(const std::string& sql, const Args&... args)
    {
        return from_sql::rows_of(*db, error, sql, args...);
    }
};

}  // namespace

std::unique_ptr<Source> open_sqlite(const std::string& path, std::string& error)
{
    auto lite = std::make_unique<Sqlite>();
    try {
        lite->db.emplace(path, SQLITE_OPEN_READONLY);
    } catch (const Qlite::v002::Error& e) {
        error = e.what();
        return nullptr;
    }
    return lite;
}

}  // namespace from_sql
