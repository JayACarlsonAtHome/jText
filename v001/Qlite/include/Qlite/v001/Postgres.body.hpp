// Qlite::v001 — PostgreSQL IMPLEMENTATION BODY, through libpq.
//
// An include-fragment like Sqlite.body.hpp: included INSIDE `Qlite::v001` by
// Postgres.hpp, after Database.body.hpp. The same API as Sqlite (Database concept):
// placeholders are PostgreSQL's own ($1, $2, ...); every value is a bound parameter
// (PQprepare + PQexecPrepared), sent as text, a blob in binary. A parameter's type comes
// from where it is used (a column, a comparison); where it cannot, cast it ($1::int).

class PostgresError : public Error {
public:
    PostgresError(const std::string& msg, std::string sqlstate = {})
        : Error(sqlstate.empty() ? msg : msg + " (SQLSTATE " + sqlstate + ")"), sqlstate_(std::move(sqlstate)) {}
    // The SQLSTATE code, e.g. "23505" for a unique violation; empty when there is none.
    const std::string& sqlstate() const { return sqlstate_; }
private:
    std::string sqlstate_;
};

class Postgres {
    struct Clear { void operator()(PGresult* r) const { PQclear(r); } };
    using Result = std::unique_ptr<PGresult, Clear>;

public:
    // `conninfo` is libpq's connection string ("host=/var/run/postgresql dbname=app"), or
    // empty for the standard environment (PGHOST, PGDATABASE, PGUSER, ...), as psql uses.
    explicit Postgres(const std::string& conninfo = {}) : conn_(PQconnectdb(conninfo.c_str())) {
        if (PQstatus(conn_) != CONNECTION_OK) {
            std::string msg = conn_ ? PQerrorMessage(conn_) : "out of memory";
            if (conn_) PQfinish(conn_);
            conn_ = nullptr;
            throw PostgresError("Failed to connect: " + msg);
        }
    }

    ~Postgres() {
        cache_.clear();           // deallocate cached statements before closing
        if (conn_) PQfinish(conn_);
    }

    Postgres(const Postgres&) = delete;
    Postgres& operator=(const Postgres&) = delete;
    Postgres(Postgres&& o) noexcept
        : conn_(o.conn_), changes_(o.changes_), next_statement_(o.next_statement_), cache_(std::move(o.cache_)) {
        o.conn_ = nullptr;
    }
    Postgres& operator=(Postgres&& o) noexcept {
        if (this != &o) {
            cache_.clear();
            if (conn_) PQfinish(conn_);
            conn_ = o.conn_;
            changes_ = o.changes_;
            next_statement_ = o.next_statement_;
            cache_ = std::move(o.cache_);
            o.conn_ = nullptr;
        }
        return *this;
    }

    // One or more statements, no parameters.
    void exec(const std::string& sql) {
        Result r(PQexec(conn_, sql.c_str()));
        check(r.get(), "exec failed: " + sql);
    }

    void begin() { exec("BEGIN"); }
    void commit() { exec("COMMIT"); }
    void rollback() { exec("ROLLBACK"); }

    // Rows the last INSERT, UPDATE or DELETE changed (or the last SELECT returned).
    std::int64_t changes() const { return changes_; }

    class Statement {
    public:
        Statement(Postgres& db, const std::string& sql)
            : db_(&db), name_("qlite_" + std::to_string(++db.next_statement_)) {
            Result r(PQprepare(db.conn_, name_.c_str(), sql.c_str(), 0, nullptr));
            db.check(r.get(), "Failed to prepare: " + sql);
            Result d(PQdescribePrepared(db.conn_, name_.c_str()));
            db.check(d.get(), "Failed to describe: " + sql);
            for (int i = 0; i < PQnfields(d.get()); ++i) names_.emplace_back(PQfname(d.get(), i));
        }

        ~Statement() {
            if (db_ && db_->conn_) {
                Result r(PQexec(db_->conn_, ("DEALLOCATE " + name_).c_str()));   // errors ignored
            }
        }

        Statement(const Statement&) = delete;
        Statement& operator=(const Statement&) = delete;
        Statement(Statement&& o) noexcept
            : db_(o.db_), name_(std::move(o.name_)), names_(std::move(o.names_)),
              params_(std::move(o.params_)), result_(std::move(o.result_)), row_(o.row_) {
            o.db_ = nullptr;
        }
        Statement& operator=(Statement&& o) noexcept {
            if (this != &o) {
                if (db_ && db_->conn_) { Result r(PQexec(db_->conn_, ("DEALLOCATE " + name_).c_str())); }
                db_ = o.db_;
                name_ = std::move(o.name_);
                names_ = std::move(o.names_);
                params_ = std::move(o.params_);
                result_ = std::move(o.result_);
                row_ = o.row_;
                o.db_ = nullptr;
            }
            return *this;
        }

        // Variadic positional bind ($1, $2, ...): resets, then binds.
        template<typename... Args>
        void bind(Args&&... args) {
            reset();
            params_.clear();
            (params_.push_back(param(std::forward<Args>(args))), ...);
        }

        // Runs the statement on the first call, then walks its rows.
        bool step() {
            if (!result_) execute();
            return ++row_ < PQntuples(result_.get());
        }

        // Ready to run again with the same values.
        void reset() { result_.reset(); row_ = -1; }
        void clear_bindings() { params_.clear(); }

        // Runtime indexed bind helpers (1-based, like $1).
        void bind_int64(int idx, std::int64_t v)       { at(idx) = Param{to_text(v), false}; }
        void bind_double(int idx, double v)            { at(idx) = Param{to_text(v), false}; }
        void bind_text(int idx, std::string_view v)    { at(idx) = Param{std::string(v), false}; }
        void bind_blob(int idx, std::span<const std::byte> v) { at(idx) = param(v); }
        void bind_null(int idx)                        { at(idx) = Param{}; }

        // Variadic extraction from the current row.
        template<typename... Ts>
        void get(Ts&... values) {
            int col = 0;
            ((values = column_value<Ts>(col++)), ...);
        }

        template<typename... Ts>
        std::tuple<Ts...> get_row() {
            std::tuple<Ts...> row{};
            int col = 0;
            std::apply([&](auto&... v) { ((v = column_value<std::remove_cvref_t<decltype(v)>>(col++)), ...); }, row);
            return row;
        }

        template<typename... Ts>
        std::vector<std::tuple<Ts...>> get_all() {
            std::vector<std::tuple<Ts...>> out;
            while (step()) out.push_back(get_row<Ts...>());
            return out;
        }

        int column_count() const { return static_cast<int>(names_.size()); }
        std::string column_name(int i) const {
            return i >= 0 && i < column_count() ? names_[static_cast<std::size_t>(i)] : std::string{};
        }
        int column_index(std::string_view name) const {
            for (int i = 0; i < column_count(); ++i)
                if (names_[static_cast<std::size_t>(i)] == name) return i;
            throw PostgresError("no such column: " + std::string(name));
        }
        template<typename T>
        T get_by_name(std::string_view name) { return column_value<T>(column_index(name)); }

#if defined(__cpp_lib_expected)
        // Non-throwing step: errors come back as std::unexpected(PostgresError).
        std::expected<bool, PostgresError> try_step() {
            try { return step(); }
            catch (const PostgresError& e) { return std::unexpected(e); }
        }
#endif

        // Escape hatch: the current result (nullptr before the first step()).
        PGresult* handle() const noexcept { return result_.get(); }

    private:
        struct Param {
            std::optional<std::string> value;   // nullopt: NULL
            bool binary = false;                // a blob, sent as raw bytes
        };

        template<typename T>
        static Param param(T&& v) { return Param{to_text(std::forward<T>(v)), false}; }
        static Param param(std::span<const std::byte> v) {
            return Param{std::string(reinterpret_cast<const char*>(v.data()), v.size()), true};
        }
        static Param param(const std::vector<std::byte>& v) { return param(std::span<const std::byte>(v)); }

        Param& at(int idx) {
            if (idx < 1) throw PostgresError("bind index starts at 1");
            if (params_.size() < static_cast<std::size_t>(idx)) params_.resize(static_cast<std::size_t>(idx));
            return params_[static_cast<std::size_t>(idx - 1)];
        }

        void execute() {
            std::vector<const char*> values;
            std::vector<int> lengths, formats;
            for (const auto& p : params_) {
                values.push_back(p.value ? p.value->data() : nullptr);
                lengths.push_back(p.value ? static_cast<int>(p.value->size()) : 0);
                formats.push_back(p.binary ? 1 : 0);
            }
            result_.reset(PQexecPrepared(db_->conn_, name_.c_str(), static_cast<int>(values.size()),
                                         values.data(), lengths.data(), formats.data(), 0));
            db_->check(result_.get(), "step failed: " + name_);
            row_ = -1;
        }

        template<typename T>
        T column_value(int col) {
            const PGresult* r = result_.get();
            const bool null = PQgetisnull(r, row_, col);
            if constexpr (std::is_same_v<T, std::vector<std::byte>>) {
                return null ? T{} : bytea(PQgetvalue(r, row_, col));
            } else if constexpr (std::is_same_v<T, std::optional<std::vector<std::byte>>>) {
                return null ? T{} : T(bytea(PQgetvalue(r, row_, col)));
            } else {
                if (null) return from_text_t<T>::read(nullptr);
                const std::string text(PQgetvalue(r, row_, col), static_cast<std::size_t>(PQgetlength(r, row_, col)));
                return from_text_t<T>::read(&text);
            }
        }

        // A bytea in PostgreSQL's text form, "\x" then hex digits.
        static std::vector<std::byte> bytea(const char* t) {
            std::vector<std::byte> out;
            if (!t || t[0] != '\\' || t[1] != 'x') return out;
            const auto hex = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
            for (const char* p = t + 2; p[0] && p[1]; p += 2)
                out.push_back(static_cast<std::byte>(hex(p[0]) * 16 + hex(p[1])));
            return out;
        }

        Postgres* db_;
        std::string name_;
        std::vector<std::string> names_;
        std::vector<Param> params_;
        Result result_;
        int row_ = -1;
    };

    // RAII transaction guard (Database.body.hpp): BEGIN now, ROLLBACK at scope exit
    // unless commit() was called.
    using Transaction = BasicTransaction<Postgres>;

    Statement prepare(const std::string& sql) { return Statement(*this, sql); }

    // Prepared-statement cache: prepare once per SQL string, reuse thereafter.
    Statement& prepare_cached(const std::string& sql) {
        auto it = cache_.find(sql);
        if (it == cache_.end()) it = cache_.try_emplace(sql, *this, sql).first;
        return it->second;
    }
    void clear_cache() { cache_.clear(); }

    Transaction transaction() { return Transaction(*this); }

    template<typename... Args>
    void exec(const std::string& sql, Args&&... args) {
        Statement st(*this, sql);
        st.bind(std::forward<Args>(args)...);
        while (st.step()) {}
    }

#if defined(__cpp_lib_expected)
    std::expected<void, PostgresError> try_exec(const std::string& sql) {
        try { exec(sql); return {}; }
        catch (const PostgresError& e) { return std::unexpected(e); }
    }
#endif

    // Escape hatch for advanced libpq calls.
    PGconn* handle() const noexcept { return conn_; }

private:
    // Throws unless `r` succeeded; notes the rows it changed.
    void check(const PGresult* r, const std::string& what) {
        const auto status = PQresultStatus(r);
        if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
            const char* state = r ? PQresultErrorField(r, PG_DIAG_SQLSTATE) : nullptr;
            throw PostgresError(what + " - " + PQerrorMessage(conn_), state ? state : "");
        }
        const char* n = PQcmdTuples(const_cast<PGresult*>(r));
        changes_ = n && *n ? std::strtoll(n, nullptr, 10) : 0;
    }

    PGconn* conn_ = nullptr;
    std::int64_t changes_ = 0;
    unsigned next_statement_ = 0;
    std::unordered_map<std::string, Statement> cache_;
};
