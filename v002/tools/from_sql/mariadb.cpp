// File: tools/from_sql/mariadb.cpp
//
// jtext_from_sql's MariaDB (and MySQL), through Qlite (Qlite::v002::MariaDB, Connector/C):
// the local server, as the current login, with the named database.

#include "from_sql/source.hpp"

#include <Qlite/v002/MariaDB.hpp>

#include <format>
#include <memory>

namespace from_sql {

namespace {

class Mariadb final : public Source {
public:
    std::string dialect() const override { return "MariaDB"; }

    std::optional<std::vector<Column>> columns(const std::string& table) override
    {
        auto r = rows_of("SELECT column_name, data_type, COALESCE(character_maximum_length, 0), is_nullable "
                         "FROM information_schema.columns WHERE table_schema = DATABASE() AND table_name = ? "
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
        return rows_of(std::format("SELECT * FROM `{}` ORDER BY 1", table));
    }

    std::optional<bool> has_table(const std::string& table) override
    {
        auto r = rows_of("SELECT 1 FROM information_schema.tables "
                         "WHERE table_schema = DATABASE() AND table_name = ?", table);
        if (!r) return std::nullopt;
        return !r->empty();
    }

    std::optional<std::vector<Row>> comment_rows(const std::string& table) override
    {
        return rows_of(std::format("SELECT {} FROM `{}` ORDER BY 1, 3, 4, 5", kCommentColumns, table));
    }

    std::optional<std::vector<Row>> declaration_rows(const std::string& table) override
    {
        return rows_of(std::format("SELECT {} FROM `{}` ORDER BY 1, 2", kDeclarationColumns, table));
    }

    std::optional<Qlite::v002::MariaDB> db;

private:
    template<typename... Args>
    std::optional<std::vector<Row>> rows_of(const std::string& sql, const Args&... args)
    {
        return from_sql::rows_of(*db, error, sql, args...);
    }
};

}  // namespace

std::unique_ptr<Source> open_mariadb(const std::string& database, std::string& error)
{
    auto maria = std::make_unique<Mariadb>();
    try {
        maria->db.emplace(database);
    } catch (const Qlite::v002::Error& e) {
        error = e.what();
        return nullptr;
    }
    return maria;
}

}  // namespace from_sql
