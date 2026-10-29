// File: tools/from_sql/sqlite.cpp
//
// jtext_from_sql's SQLite, through its C library.

#include "from_sql/source.hpp"

#include <sqlite3.h>

#include <format>
#include <memory>

namespace from_sql {

namespace {

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

}  // namespace

std::unique_ptr<Source> open_sqlite(const std::string& path, std::string& error)
{
    auto lite = std::make_unique<Sqlite>(path);
    if (!lite->ok()) {
        error = lite->error;
        return nullptr;
    }
    return lite;
}

}  // namespace from_sql
