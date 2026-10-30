// Qlite::v002 — MariaDB (and MySQL) IMPLEMENTATION BODY, through MariaDB Connector/C.
//
// An include-fragment like Sqlite.body.hpp: included INSIDE `Qlite::v002` by
// MariaDB.hpp, after Database.body.hpp. The same API as Sqlite (Database concept):
// placeholders are `?`; every value is a bound parameter of a real prepared statement
// (mysql_stmt_*). The connection is utf8mb4, and an UPDATE's changes() counts the rows it
// matched (CLIENT_FOUND_ROWS), whether or not their values changed.

class MariaDBError : public Error {
public:
    MariaDBError(const std::string& msg, unsigned errcode = 0)
        : Error(errcode ? msg + " (code " + std::to_string(errcode) + ")" : msg), code_(errcode) {}
    // The server's error number, e.g. 1062 for a duplicate key; 0 when there is none.
    unsigned code() const { return code_; }
private:
    unsigned code_;
};

class MariaDB {
public:
    struct Options {
        std::string host;       // empty: the local server, over its socket
        std::string user;       // empty: the current login
        std::string password;   // empty: none (e.g. unix_socket authentication)
        std::string database;   // empty: none selected
        unsigned port = 0;      // 0: the default
        std::string socket;     // empty: the default
    };

    // Reads the [client] group of the option files first, as the mariadb client does.
    explicit MariaDB(const Options& o) : conn_(mysql_init(nullptr)) {
        if (!conn_) throw MariaDBError("Failed to connect: out of memory");
        mysql_optionsv(conn_, MYSQL_READ_DEFAULT_GROUP, "client");
        mysql_optionsv(conn_, MYSQL_SET_CHARSET_NAME, "utf8mb4");
        const auto opt = [](const std::string& s) { return s.empty() ? nullptr : s.c_str(); };
        if (!mysql_real_connect(conn_, opt(o.host), opt(o.user), opt(o.password), opt(o.database), o.port,
                                opt(o.socket), CLIENT_FOUND_ROWS | CLIENT_MULTI_STATEMENTS)) {
            std::string msg = mysql_error(conn_);
            unsigned code = mysql_errno(conn_);
            mysql_close(conn_);
            conn_ = nullptr;
            throw MariaDBError("Failed to connect: " + msg, code);
        }
    }

    // The local server, as the current login, with `database` selected.
    explicit MariaDB(const std::string& database) : MariaDB(Options{{}, {}, {}, database, 0, {}}) {}

    ~MariaDB() {
        cache_.clear();           // close cached statements before the connection
        if (conn_) mysql_close(conn_);
    }

    MariaDB(const MariaDB&) = delete;
    MariaDB& operator=(const MariaDB&) = delete;
    MariaDB(MariaDB&& o) noexcept : conn_(o.conn_), changes_(o.changes_), cache_(std::move(o.cache_)) {
        o.conn_ = nullptr;
        adopt_cache();
    }
    MariaDB& operator=(MariaDB&& o) noexcept {
        if (this != &o) {
            cache_.clear();
            if (conn_) mysql_close(conn_);
            conn_ = o.conn_;
            changes_ = o.changes_;
            cache_ = std::move(o.cache_);
            o.conn_ = nullptr;
            adopt_cache();
        }
        return *this;
    }

    // One or more statements, no parameters.
    void exec(const std::string& sql) {
        if (mysql_real_query(conn_, sql.data(), sql.size()) != 0) fail("exec failed: " + sql);
        do {
            if (MYSQL_RES* res = mysql_store_result(conn_)) mysql_free_result(res);
            else if (mysql_field_count(conn_) == 0) changes_ = static_cast<std::int64_t>(mysql_affected_rows(conn_));
            else fail("exec failed: " + sql);
        } while (next_result(sql));
    }

    void begin() { exec("START TRANSACTION"); }
    void commit() { exec("COMMIT"); }
    void rollback() { exec("ROLLBACK"); }

    // Rows the last INSERT, UPDATE or DELETE changed (an UPDATE: the rows it matched).
    std::int64_t changes() const { return changes_; }

    class Statement {
    public:
        friend class MariaDB;   // the connection re-points db_ when it moves

        Statement(MariaDB& db, const std::string& sql) : db_(&db), stmt_(mysql_stmt_init(db.conn_)) {
            if (!stmt_) db.fail("Failed to prepare: " + sql);
            if (mysql_stmt_prepare(stmt_, sql.data(), sql.size()) != 0) {
                MariaDBError e("Failed to prepare: " + sql + " - " + mysql_stmt_error(stmt_), mysql_stmt_errno(stmt_));
                mysql_stmt_close(stmt_);
                stmt_ = nullptr;
                throw e;
            }
            if (MYSQL_RES* meta = mysql_stmt_result_metadata(stmt_)) {
                const unsigned n = mysql_num_fields(meta);
                const MYSQL_FIELD* f = mysql_fetch_fields(meta);
                for (unsigned i = 0; i < n; ++i) {
                    names_.emplace_back(f[i].name ? f[i].name : "");
                    types_.push_back(f[i].type);
                    unsigned_.push_back((f[i].flags & UNSIGNED_FLAG) != 0);
                }
                mysql_free_result(meta);
            }
        }

        ~Statement() {
            if (stmt_) mysql_stmt_close(stmt_);
        }

        Statement(const Statement&) = delete;
        Statement& operator=(const Statement&) = delete;
        Statement(Statement&& o) noexcept
            : db_(o.db_), stmt_(o.stmt_), names_(std::move(o.names_)), types_(std::move(o.types_)),
              unsigned_(std::move(o.unsigned_)), params_(std::move(o.params_)), executed_(o.executed_),
              on_row_(o.on_row_), out_(std::move(o.out_)), out_len_(std::move(o.out_len_)),
              out_null_(std::move(o.out_null_)), out_text_(std::move(o.out_text_)),
              out_int_(std::move(o.out_int_)), out_double_(std::move(o.out_double_)),
              cells_(std::move(o.cells_)) {
            o.stmt_ = nullptr;
        }
        Statement& operator=(Statement&& o) noexcept {
            if (this != &o) {
                if (stmt_) mysql_stmt_close(stmt_);
                db_ = o.db_;
                stmt_ = o.stmt_;
                names_ = std::move(o.names_);
                types_ = std::move(o.types_);
                unsigned_ = std::move(o.unsigned_);
                params_ = std::move(o.params_);
                executed_ = o.executed_;
                on_row_ = o.on_row_;
                out_ = std::move(o.out_);
                out_len_ = std::move(o.out_len_);
                out_null_ = std::move(o.out_null_);
                out_text_ = std::move(o.out_text_);
                out_int_ = std::move(o.out_int_);
                out_double_ = std::move(o.out_double_);
                cells_ = std::move(o.cells_);
                o.stmt_ = nullptr;
            }
            return *this;
        }

        // Variadic positional bind (?, ?, ...): resets, then binds.
        template<typename... Args>
        void bind(Args&&... args) {
            reset();
            params_.clear();
            (params_.push_back(param(std::forward<Args>(args))), ...);
        }

        // Runs the statement on the first call, then walks its rows.
        bool step() {
            on_row_ = false;
            if (!executed_) execute();
            if (names_.empty()) return false;
            const int rc = mysql_stmt_fetch(stmt_);
            if (rc == MYSQL_NO_DATA) return false;
            if (rc != 0 && rc != MYSQL_DATA_TRUNCATED) throw_stmt("step failed");
            read_row();
            on_row_ = true;
            return true;
        }

        // Ready to run again with the same values.
        void reset() {
            if (executed_) {
                mysql_stmt_free_result(stmt_);
                mysql_stmt_reset(stmt_);
            }
            executed_ = false;
            on_row_ = false;
        }
        void clear_bindings() { params_.clear(); }

        // Runtime indexed bind helpers (1-based, the n-th ?).
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
            throw MariaDBError("no such column: " + std::string(name));
        }
        template<typename T>
        T get_by_name(std::string_view name) { return column_value<T>(column_index(name)); }
        // Column `col` (0-based) of the current row, for a width known only at run time.
        template<typename T>
        T get_at(int col) { return column_value<T>(col); }

#if defined(__cpp_lib_expected)
        // Non-throwing step: errors come back as std::unexpected(MariaDBError).
        std::expected<bool, MariaDBError> try_step() {
            try { return step(); }
            catch (const MariaDBError& e) { return std::unexpected(e); }
        }
#endif

        // Escape hatch for advanced mysql_stmt calls.
        MYSQL_STMT* handle() const noexcept { return stmt_; }

    private:
        struct Param {
            std::optional<std::string> value;   // nullopt: NULL
            bool binary = false;                // a blob
        };

        template<typename T>
        static Param param(T&& v) { return Param{to_text(std::forward<T>(v)), false}; }
        static Param param(std::span<const std::byte> v) {
            return Param{std::string(reinterpret_cast<const char*>(v.data()), v.size()), true};
        }
        static Param param(const std::vector<std::byte>& v) { return param(std::span<const std::byte>(v)); }

        Param& at(int idx) {
            if (idx < 1) throw MariaDBError("bind index starts at 1");
            if (params_.size() < static_cast<std::size_t>(idx)) params_.resize(static_cast<std::size_t>(idx));
            return params_[static_cast<std::size_t>(idx - 1)];
        }

        [[noreturn]] void throw_stmt(const std::string& what) {
            throw MariaDBError(what + " - " + mysql_stmt_error(stmt_), mysql_stmt_errno(stmt_));
        }

        // Whole numbers and doubles are fetched as themselves (so their text is exact);
        // everything else as its text.
        static bool integer_type(enum_field_types t) {
            return t == MYSQL_TYPE_TINY || t == MYSQL_TYPE_SHORT || t == MYSQL_TYPE_INT24 ||
                   t == MYSQL_TYPE_LONG || t == MYSQL_TYPE_LONGLONG || t == MYSQL_TYPE_YEAR;
        }
        static bool double_type(enum_field_types t) { return t == MYSQL_TYPE_FLOAT || t == MYSQL_TYPE_DOUBLE; }

        void execute() {
            const auto n = mysql_stmt_param_count(stmt_);
            if (n != params_.size())
                throw MariaDBError(std::format("the statement takes {} value(s); {} bound", n, params_.size()));
            std::vector<MYSQL_BIND> in(params_.size());
            std::vector<unsigned long> in_len(params_.size());
            std::vector<my_bool> in_null(params_.size());
            for (std::size_t i = 0; i < params_.size(); ++i) {
                const auto& p = params_[i];
                in_null[i] = !p.value;
                in_len[i] = p.value ? p.value->size() : 0;
                in[i].buffer_type = p.binary ? MYSQL_TYPE_BLOB : MYSQL_TYPE_STRING;
                in[i].buffer = p.value ? const_cast<char*>(p.value->data()) : nullptr;
                in[i].buffer_length = in_len[i];
                in[i].length = &in_len[i];
                in[i].is_null = &in_null[i];
            }
            if (!in.empty() && mysql_stmt_bind_param(stmt_, in.data())) throw_stmt("bind failed");
            if (mysql_stmt_execute(stmt_)) throw_stmt("step failed");
            executed_ = true;
            if (names_.empty()) {
                db_->changes_ = static_cast<std::int64_t>(mysql_stmt_affected_rows(stmt_));
                return;
            }
            const my_bool update_max = 1;
            mysql_stmt_attr_set(stmt_, STMT_ATTR_UPDATE_MAX_LENGTH, &update_max);
            if (mysql_stmt_store_result(stmt_)) throw_stmt("step failed");
            db_->changes_ = static_cast<std::int64_t>(mysql_stmt_num_rows(stmt_));

            MYSQL_RES* meta = mysql_stmt_result_metadata(stmt_);
            const MYSQL_FIELD* f = meta ? mysql_fetch_fields(meta) : nullptr;
            const std::size_t width = names_.size();
            out_.assign(width, MYSQL_BIND{});
            out_len_.assign(width, 0);
            out_null_.assign(width, 0);
            out_text_.assign(width, std::string{});
            out_int_.assign(width, 0);
            out_double_.assign(width, 0.0);
            for (std::size_t i = 0; i < width; ++i) {
                auto& b = out_[i];
                b.length = &out_len_[i];
                b.is_null = &out_null_[i];
                if (integer_type(types_[i])) {
                    b.buffer_type = MYSQL_TYPE_LONGLONG;
                    b.buffer = &out_int_[i];
                    b.is_unsigned = unsigned_[i];
                } else if (double_type(types_[i])) {
                    b.buffer_type = MYSQL_TYPE_DOUBLE;
                    b.buffer = &out_double_[i];
                } else {
                    out_text_[i].resize((f ? f[i].max_length : 0) + 1);
                    b.buffer_type = MYSQL_TYPE_STRING;
                    b.buffer = out_text_[i].data();
                    b.buffer_length = out_text_[i].size();
                }
            }
            if (meta) mysql_free_result(meta);
            if (mysql_stmt_bind_result(stmt_, out_.data())) throw_stmt("bind failed");
        }

        // The fetched row as each column's text (nullopt: NULL).
        void read_row() {
            cells_.assign(names_.size(), std::nullopt);
            for (std::size_t i = 0; i < names_.size(); ++i) {
                if (out_null_[i]) continue;
                if (out_[i].buffer_type == MYSQL_TYPE_LONGLONG) {
                    cells_[i] = unsigned_[i] ? std::to_string(static_cast<std::uint64_t>(out_int_[i]))
                                             : std::to_string(out_int_[i]);
                } else if (out_[i].buffer_type == MYSQL_TYPE_DOUBLE) {
                    cells_[i] = to_text(out_double_[i]);
                } else {
                    cells_[i] = std::string(out_text_[i].data(), out_len_[i]);
                }
            }
        }

        template<typename T>
        T column_value(int col) {
            // A current row (step() returned true): after the last row, cells_ still holds it.
            QLITE_PRE(on_row_);
            QLITE_PRE(col >= 0 && col < column_count());
            const auto& cell = cells_.at(static_cast<std::size_t>(col));
            if constexpr (std::is_same_v<T, std::vector<std::byte>>) {
                return cell ? bytes(*cell) : T{};
            } else if constexpr (std::is_same_v<T, std::optional<std::vector<std::byte>>>) {
                return cell ? T(bytes(*cell)) : T{};
            } else {
                return from_text_t<T>::read(cell ? &*cell : nullptr);
            }
        }

        static std::vector<std::byte> bytes(const std::string& s) {
            const auto* b = reinterpret_cast<const std::byte*>(s.data());
            return std::vector<std::byte>(b, b + s.size());
        }

        MariaDB* db_;
        MYSQL_STMT* stmt_;
        std::vector<std::string> names_;
        std::vector<enum_field_types> types_;
        std::vector<bool> unsigned_;
        std::vector<Param> params_;
        bool executed_ = false;
        bool on_row_ = false;   // step() returned true and nothing has moved off that row
        std::vector<MYSQL_BIND> out_;
        std::vector<unsigned long> out_len_;
        std::vector<my_bool> out_null_;
        std::vector<std::string> out_text_;
        std::vector<long long> out_int_;
        std::vector<double> out_double_;
        std::vector<std::optional<std::string>> cells_;
    };

    // RAII transaction guard (Database.body.hpp): START TRANSACTION now, ROLLBACK at
    // scope exit unless commit() was called.
    using Transaction = BasicTransaction<MariaDB>;

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
    std::expected<void, MariaDBError> try_exec(const std::string& sql) {
        try { exec(sql); return {}; }
        catch (const MariaDBError& e) { return std::unexpected(e); }
    }
#endif

    // Escape hatch for advanced Connector/C calls.
    MYSQL* handle() const noexcept { return conn_; }

private:
    [[noreturn]] void fail(const std::string& what) {
        throw MariaDBError(what + " - " + mysql_error(conn_), mysql_errno(conn_));
    }

    // Moves to the next result of a multi-statement exec(); false when there is none.
    bool next_result(const std::string& sql) {
        const int rc = mysql_next_result(conn_);
        if (rc > 0) fail("exec failed: " + sql);
        return rc == 0;
    }

    MYSQL* conn_ = nullptr;
    std::int64_t changes_ = 0;
    // The cached statements point back at their connection. After a move they must point
    // here, not at the moved-from object (it writes changes_ into a dead one).
    void adopt_cache() noexcept {
        for (auto& [sql, st] : cache_) st.db_ = this;
    }

    std::unordered_map<std::string, Statement> cache_;
};
