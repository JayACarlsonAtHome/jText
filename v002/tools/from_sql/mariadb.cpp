// File: tools/from_sql/mariadb.cpp
//
// jtext_from_sql's MariaDB (and MySQL), through MariaDB Connector/C. It connects over
// the local socket as the current login, reading the [client] group of the option
// files, as the mariadb client does; the database is named on the command line.

#include "from_sql/source.hpp"

#include <mysql.h>

#include <format>
#include <memory>

namespace from_sql {

namespace {

class Mariadb final : public Source {
public:
    explicit Mariadb(const std::string& database) : conn_(mysql_init(nullptr), mysql_close)
    {
        if (!conn_) {
            error = "cannot start the MariaDB client";
            return;
        }
        mysql_optionsv(conn_.get(), MYSQL_READ_DEFAULT_GROUP, "client");
        mysql_optionsv(conn_.get(), MYSQL_SET_CHARSET_NAME, "utf8mb4");
        if (!mysql_real_connect(conn_.get(), nullptr, nullptr, nullptr, database.c_str(), 0, nullptr, 0))
            error = mysql_error(conn_.get());
        else
            ok_ = true;
    }
    bool ok() const { return ok_; }
    std::string dialect() const override { return "MariaDB"; }

    std::optional<std::vector<Column>> columns(const std::string& table) override
    {
        auto r = query(std::format(
            "SELECT column_name, data_type, COALESCE(character_maximum_length, 0), is_nullable "
            "FROM information_schema.columns WHERE table_schema = DATABASE() AND table_name = {} "
            "ORDER BY ordinal_position", literal(table)));
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
        return query(std::format("SELECT * FROM `{}` ORDER BY 1", table));
    }

    std::optional<bool> has_table(const std::string& table) override
    {
        auto r = query(std::format("SELECT 1 FROM information_schema.tables "
                                   "WHERE table_schema = DATABASE() AND table_name = {}", literal(table)));
        if (!r) return std::nullopt;
        return !r->empty();
    }

    std::optional<std::vector<Row>> comment_rows(const std::string& table) override
    {
        return query(std::format("SELECT {} FROM `{}` ORDER BY 1, 3, 4, 5", kCommentColumns, table));
    }

private:
    std::unique_ptr<MYSQL, decltype(&mysql_close)> conn_;
    bool ok_ = false;

    // A quoted string literal, escaped by the client library for this connection's
    // character set and SQL mode.
    std::string literal(const std::string& s)
    {
        std::string out(s.size() * 2 + 1, '\0');
        out.resize(mysql_real_escape_string(conn_.get(), out.data(), s.c_str(), s.size()));
        return "'" + out + "'";
    }

    // Each value as the server sends it in text form; a NULL as {NULL}.
    std::optional<std::vector<Row>> query(const std::string& sql)
    {
        if (mysql_real_query(conn_.get(), sql.c_str(), sql.size()) != 0) {
            error = mysql_error(conn_.get());
            return std::nullopt;
        }
        std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)> res(mysql_store_result(conn_.get()),
                                                                     mysql_free_result);
        if (!res) {
            error = mysql_error(conn_.get());
            return std::nullopt;
        }
        const unsigned width = mysql_num_fields(res.get());
        std::vector<Row> out;
        while (MYSQL_ROW r = mysql_fetch_row(res.get())) {
            const unsigned long* lengths = mysql_fetch_lengths(res.get());
            Row row;
            for (unsigned c = 0; c < width; ++c)
                row.push_back(r[c] ? std::string(r[c], lengths[c]) : std::string{jtext::kNullValue});
            out.push_back(std::move(row));
        }
        return out;
    }
};

}  // namespace

std::unique_ptr<Source> open_mariadb(const std::string& database, std::string& error)
{
    auto db = std::make_unique<Mariadb>(database);
    if (!db->ok()) {
        error = db->error;
        return nullptr;
    }
    return db;
}

}  // namespace from_sql
