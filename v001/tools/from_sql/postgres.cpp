// File: tools/from_sql/postgres.cpp
//
// jtext_from_sql's PostgreSQL, through Qlite (Qlite::v001::Postgres, libpq).

#include "from_sql/source.hpp"

#include <Qlite/v001/Postgres.hpp>

#include <format>
#include <memory>

namespace from_sql {

namespace {

class Postgres final : public Source {
public:
    std::string dialect() const override { return "PostgreSQL"; }

    std::optional<std::vector<Column>> columns(const std::string& table) override
    {
        auto r = rows_of("SELECT column_name, data_type, COALESCE(character_maximum_length, 0), is_nullable "
                         "FROM information_schema.columns "
                         "WHERE table_schema = current_schema() AND table_name = $1 "
                         "ORDER BY ordinal_position", table);
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
        return rows_of(std::format("SELECT * FROM \"{}\" ORDER BY 1", table));
    }

    std::optional<bool> has_table(const std::string& table) override
    {
        auto r = rows_of("SELECT 1 FROM information_schema.tables "
                         "WHERE table_schema = current_schema() AND table_name = $1", table);
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

    std::optional<Qlite::v001::Postgres> db;

private:
    template<typename... Args>
    std::optional<std::vector<Row>> rows_of(const std::string& sql, const Args&... args)
    {
        return from_sql::rows_of(*db, error, sql, args...);
    }
};

}  // namespace

std::unique_ptr<Source> open_postgres(std::string& error)
{
    auto pg = std::make_unique<Postgres>();
    try {
        pg->db.emplace();   // the PG* environment, as psql
    } catch (const Qlite::v001::Error& e) {
        error = e.what();
        return nullptr;
    }
    return pg;
}

}  // namespace from_sql
