// Qlite::v002 — what every database shares (single source of truth).
//
// An include-fragment like Sqlite.body.hpp: no include guard, includes nothing, and is
// included INSIDE `Qlite::v002` by Database.hpp (textual) or Qlite.v002.cppm (module),
// before any database's body.
//
// Qlite ("SQL lite") is one small API over several databases: Sqlite (Sqlite.hpp),
// Postgres (Postgres.hpp, libpq) and MariaDB (MariaDB.hpp, Connector/C, also MySQL).
// Each has the same members, so code written against the `Database` concept below works
// with any of them. A project includes only the databases it uses. Values are always
// bound parameters, never pasted into the SQL text; each database keeps its own
// placeholders (`?` for SQLite and MariaDB, `$1` for PostgreSQL), so no SQL is rewritten.

// Package version, "major.minor": major = the v002 API line, minor = the revision.
// Bump the minor on each small code change; a real (breaking) change moves the major.
[[nodiscard]] constexpr std::string_view version() noexcept { return "v002.012"; }

// ---------------------------------------------------------------------------
// Errors: each database throws its own type, all derived from Error.
// ---------------------------------------------------------------------------
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// ---------------------------------------------------------------------------
// The common surface. A statement from prepare() binds its values positionally
// (bind(a, b, ...)), steps through its rows (step()) and reads the current row's
// columns in order (get(x, y, ...)), or one by its index (get_at<T>(i)); a std::optional<T>
// reads or binds NULL.
// ---------------------------------------------------------------------------
template<class D>
concept Database = requires(D& db, const std::string& sql) {
    db.exec(sql);
    db.begin();
    db.commit();
    db.rollback();
    { db.changes() } -> std::convertible_to<std::int64_t>;
    requires requires(decltype(db.prepare(sql)) st, std::int64_t& i, double& d, std::string& s,
                      std::optional<std::string>& o) {
        st.bind();
        st.bind(i, d, s, o);
        { st.step() } -> std::same_as<bool>;
        st.reset();
        st.get(i, d, s, o);
        { st.template get_at<std::optional<std::string>>(0) } -> std::same_as<std::optional<std::string>>;
        { st.column_count() } -> std::convertible_to<int>;
    };
};

// ---------------------------------------------------------------------------
// RAII transaction guard for any database: BEGIN on construction; auto-ROLLBACK on
// scope exit UNLESS commit() (or an explicit rollback()) was called.
// ---------------------------------------------------------------------------
template<class D>
class BasicTransaction {
public:
    explicit BasicTransaction(D& db) : db_(&db) { db_->begin(); }
    ~BasicTransaction() {
        if (db_) { try { db_->rollback(); } catch (...) { /* dtors don't throw */ } }
    }

    BasicTransaction(const BasicTransaction&) = delete;
    BasicTransaction& operator=(const BasicTransaction&) = delete;

    BasicTransaction(BasicTransaction&& o) noexcept : db_(o.db_) { o.db_ = nullptr; }
    BasicTransaction& operator=(BasicTransaction&& o) noexcept {
        if (this != &o) {
            if (db_) { try { db_->rollback(); } catch (...) {} }
            db_ = o.db_;
            o.db_ = nullptr;
        }
        return *this;
    }

    void commit()   { if (db_) { db_->commit();   db_ = nullptr; } }
    void rollback() { if (db_) { db_->rollback(); db_ = nullptr; } }
    bool active() const noexcept { return db_ != nullptr; }

private:
    D* db_;
};

// ---------------------------------------------------------------------------
// Text <-> values, for the databases that send values as text (Postgres, MariaDB):
// a bound value becomes its text (nullopt: NULL), a column's text becomes T.
// ---------------------------------------------------------------------------
inline std::optional<std::string> to_text(std::int64_t v) { return std::to_string(v); }
inline std::optional<std::string> to_text(int v) { return std::to_string(v); }
inline std::optional<std::string> to_text(double v) {
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, v);   // shortest text that reads back as v
    return std::string(buf, r.ptr);
}
inline std::optional<std::string> to_text(const std::string& v) { return v; }
inline std::optional<std::string> to_text(std::string_view v) { return std::string(v); }
inline std::optional<std::string> to_text(const char* v) {
    return v ? std::optional<std::string>(v) : std::nullopt;
}
inline std::optional<std::string> to_text(std::nullopt_t) { return std::nullopt; }
template<typename T>
inline std::optional<std::string> to_text(const std::optional<T>& v) {
    return v ? to_text(*v) : std::nullopt;
}

template<typename T>
struct from_text_t;   // specialize to extend

template<> struct from_text_t<std::string> {
    static std::string read(const std::string* t) { return t ? *t : std::string{}; }
};
template<> struct from_text_t<std::int64_t> {
    static std::int64_t read(const std::string* t) {
        std::int64_t v = 0;
        if (t) std::from_chars(t->data(), t->data() + t->size(), v);
        return v;
    }
};
template<> struct from_text_t<int> {
    static int read(const std::string* t) { return static_cast<int>(from_text_t<std::int64_t>::read(t)); }
};
template<> struct from_text_t<double> {
    static double read(const std::string* t) {
        double v = 0.0;
        if (t) std::from_chars(t->data(), t->data() + t->size(), v);
        return v;
    }
};
template<typename T> struct from_text_t<std::optional<T>> {
    static std::optional<T> read(const std::string* t) {
        return t ? std::optional<T>(from_text_t<T>::read(t)) : std::nullopt;
    }
};

// ---------------------------------------------------------------------------
// Scalar "top-1" query helpers, for any database: wrap prepare/bind/step/get for a
// single value, e.g. get_one_long(db, "SELECT n FROM t WHERE id=? LIMIT 1", id).
// Variadic binds. No row -> the type default (0 / "" / 0.0). Genuine SQL/prepare
// errors PROPAGATE (a malformed query must not silently read as a clean 0). The *_or
// variants take an explicit default for the no-row case.
// ---------------------------------------------------------------------------
template<Database D, class... B>
std::int64_t get_one_long(D& db, const char* sql, B&&... b) {
    std::int64_t v = 0;
    auto st = db.prepare(sql);
    if constexpr (sizeof...(B) > 0) st.bind(std::forward<B>(b)...);
    if (st.step()) st.get(v);
    return v;
}

template<Database D, class... B>
std::string get_one_string(D& db, const char* sql, B&&... b) {
    std::string v;
    auto st = db.prepare(sql);
    if constexpr (sizeof...(B) > 0) st.bind(std::forward<B>(b)...);
    if (st.step()) st.get(v);
    return v;
}

template<Database D, class... B>
double get_one_double(D& db, const char* sql, B&&... b) {
    double v = 0.0;
    auto st = db.prepare(sql);
    if constexpr (sizeof...(B) > 0) st.bind(std::forward<B>(b)...);
    if (st.step()) st.get(v);
    return v;
}

template<Database D, class... B>
std::int64_t get_one_long_or(D& db, const char* sql, std::int64_t deflt, B&&... b) {
    auto st = db.prepare(sql);
    if constexpr (sizeof...(B) > 0) st.bind(std::forward<B>(b)...);
    if (st.step()) { std::int64_t v = 0; st.get(v); return v; }
    return deflt;
}

template<Database D, class... B>
std::string get_one_string_or(D& db, const char* sql, std::string deflt, B&&... b) {
    auto st = db.prepare(sql);
    if constexpr (sizeof...(B) > 0) st.bind(std::forward<B>(b)...);
    if (st.step()) { std::string v; st.get(v); return v; }
    return deflt;
}

// ---------------------------------------------------------------------------
// Debug variants: pass debug=true to log the SQL and the returned value to stderr.
// The overloads let you write: get_one_string(db, sql, true, arg1, arg2)
// There are also _debug convenience names that always debug.
// ---------------------------------------------------------------------------
template<Database D, class... B>
std::int64_t get_one_long(D& db, const char* sql, bool debug, B&&... b) {
    if (debug) ::fprintf(stderr, "[SQL] %s\n", sql);
    std::int64_t v = 0;
    auto st = db.prepare(sql);
    if constexpr (sizeof...(B) > 0) st.bind(std::forward<B>(b)...);
    if (st.step()) {
        st.get(v);
        if (debug) ::fprintf(stderr, "[SQL] -> %lld\n", static_cast<long long>(v));
    } else if (debug) {
        ::fprintf(stderr, "[SQL] -> (no row)\n");
    }
    return v;
}

template<Database D, class... B>
std::string get_one_string(D& db, const char* sql, bool debug, B&&... b) {
    if (debug) ::fprintf(stderr, "[SQL] %s\n", sql);
    std::string v;
    auto st = db.prepare(sql);
    if constexpr (sizeof...(B) > 0) st.bind(std::forward<B>(b)...);
    if (st.step()) {
        st.get(v);
        if (debug) ::fprintf(stderr, "[SQL] -> '%s'\n", v.c_str());
    } else if (debug) {
        ::fprintf(stderr, "[SQL] -> (no row)\n");
    }
    return v;
}

template<Database D, class... B>
double get_one_double(D& db, const char* sql, bool debug, B&&... b) {
    if (debug) ::fprintf(stderr, "[SQL] %s\n", sql);
    double v = 0.0;
    auto st = db.prepare(sql);
    if constexpr (sizeof...(B) > 0) st.bind(std::forward<B>(b)...);
    if (st.step()) {
        st.get(v);
        if (debug) ::fprintf(stderr, "[SQL] -> %g\n", v);
    } else if (debug) {
        ::fprintf(stderr, "[SQL] -> (no row)\n");
    }
    return v;
}

template<Database D, class... B>
std::int64_t get_one_long_or(D& db, const char* sql, bool debug, std::int64_t deflt, B&&... b) {
    if (debug) ::fprintf(stderr, "[SQL] %s\n", sql);
    auto st = db.prepare(sql);
    if constexpr (sizeof...(B) > 0) st.bind(std::forward<B>(b)...);
    if (st.step()) {
        std::int64_t v = 0; st.get(v);
        if (debug) ::fprintf(stderr, "[SQL] -> %lld\n", static_cast<long long>(v));
        return v;
    }
    if (debug) ::fprintf(stderr, "[SQL] -> (no row, using default %lld)\n", static_cast<long long>(deflt));
    return deflt;
}

template<Database D, class... B>
std::string get_one_string_or(D& db, const char* sql, bool debug, std::string deflt, B&&... b) {
    if (debug) ::fprintf(stderr, "[SQL] %s\n", sql);
    auto st = db.prepare(sql);
    if constexpr (sizeof...(B) > 0) st.bind(std::forward<B>(b)...);
    if (st.step()) {
        std::string v; st.get(v);
        if (debug) ::fprintf(stderr, "[SQL] -> '%s'\n", v.c_str());
        return v;
    }
    if (debug) ::fprintf(stderr, "[SQL] -> (no row, using default)\n");
    return deflt;
}

// Convenience names that force debug=true
template<Database D, class... B> inline std::int64_t get_one_long_debug(D& db, const char* sql, B&&... b) {
    return get_one_long(db, sql, true, std::forward<B>(b)...);
}
template<Database D, class... B> inline std::string get_one_string_debug(D& db, const char* sql, B&&... b) {
    return get_one_string(db, sql, true, std::forward<B>(b)...);
}
template<Database D, class... B> inline double get_one_double_debug(D& db, const char* sql, B&&... b) {
    return get_one_double(db, sql, true, std::forward<B>(b)...);
}
template<Database D, class... B> inline std::int64_t get_one_long_or_debug(D& db, const char* sql, std::int64_t deflt, B&&... b) {
    return get_one_long_or(db, sql, true, deflt, std::forward<B>(b)...);
}
template<Database D, class... B> inline std::string get_one_string_or_debug(D& db, const char* sql, std::string deflt, B&&... b) {
    return get_one_string_or(db, sql, true, deflt, std::forward<B>(b)...);
}
