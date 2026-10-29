// File: tools/from_sql/postgres.cpp
//
// jtext_from_sql's PostgreSQL, through libpq.

#include "from_sql/source.hpp"

#include <libpq-fe.h>

#include <format>
#include <memory>

namespace from_sql {

namespace {

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

}  // namespace

std::unique_ptr<Source> open_postgres(std::string& error)
{
    auto pg = std::make_unique<Postgres>();
    if (!pg->ok()) {
        error = pg->error;
        return nullptr;
    }
    return pg;
}

}  // namespace from_sql
