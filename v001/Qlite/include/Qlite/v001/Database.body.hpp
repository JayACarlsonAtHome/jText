// Qlite::v001 — what every database shares (single source of truth).
//
// An include-fragment like Sqlite.body.hpp: no include guard, includes nothing, and is
// included INSIDE `Qlite::v001` by Database.hpp (textual) or Qlite.v001.cppm (module),
// before any database's body.
//
// Qlite ("SQL lite") is one small API over several databases: Sqlite (Sqlite.hpp),
// Postgres (Postgres.hpp, libpq) and MariaDB (MariaDB.hpp, Connector/C, also MySQL).
// Each has the same members, so code written against the `Database` concept below works
// with any of them. A project includes only the databases it uses. Values are always
// bound parameters, never pasted into the SQL text; each database keeps its own
// placeholders (`?` for SQLite and MariaDB, `$1` for PostgreSQL), so no SQL is rewritten.

// Package version, "major.minor": major = the v001 API line, minor = the revision.
// Bump the minor on each small code change; a real (breaking) change moves the major.
[[nodiscard]] constexpr std::string_view version() noexcept { return "v001.013"; }

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
// A boolean as text, the way the databases send it: PostgreSQL "t" / "f", MariaDB and SQLite
// "1" / "0", or a word. True: "t", "true", "y", "yes", "on" (any case) or a number other than 0.
inline bool text_is_true(std::string_view t) {
    while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.remove_prefix(1);
    while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.remove_suffix(1);
    std::string w;
    for (char c : t) w += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    if (w == "t" || w == "true" || w == "y" || w == "yes" || w == "on") return true;
    double v = 0.0;
    const auto r = std::from_chars(w.data(), w.data() + w.size(), v);
    return r.ec == std::errc{} && r.ptr == w.data() + w.size() && v != 0.0;
}
template<> struct from_text_t<bool> {
    static bool read(const std::string* t) { return t && text_is_true(*t); }   // NULL: false
};
template<typename T> struct from_text_t<std::optional<T>> {
    static std::optional<T> read(const std::string* t) {
        return t ? std::optional<T>(from_text_t<T>::read(t)) : std::nullopt;
    }
};

// ---------------------------------------------------------------------------
// Top-1 query helpers, for any database: prepare, bind, step once, and read column 0 of the
// TOP row: the first of however many the query returns, in its ORDER BY (so "the newest run" is
// get_top_one_long(db, "SELECT run_id FROM run ORDER BY ts_utc DESC")).
//
//   get_top_one<T>(db, sql, args...)               the first row's first column as T; no row: T{}
//   get_top_one<T>(db, sql, debug, args...)        the same, and with debug=true the SQL and the
//                                              result go to stderr ("[SQL] ...")
//   get_top_one_or<T>(db, sql, deflt, args...)     no row: deflt (also with debug, after sql)
//   get_top_one_debug<T> / get_top_one_or_debug<T>     debug always on
//
// T is any type a column reads as: std::int64_t, int, double, bool, std::string, and
// std::optional<T> of each, which is nullopt for no row and for NULL (so an id lookup cannot
// mistake "none" for an id: get_top_one<std::optional<std::int64_t>>(db, "SELECT id ...", ...)).
// A bool reads 1/0, t/f and true/false/yes/no (text_is_true); NULL is false. Values bind in
// order. A genuine SQL / prepare error PROPAGATES (a malformed query must not read as a clean
// "no row"). A bool right after sql is always `debug`: to bind a bool first, pass debug
// before it (get_top_one_long(db, sql, false, true)). The named forms are the common types: get_top_one_long / _string / _double / _bool
// (and _or, _debug, _or_debug), e.g. get_top_one_bool(db, "SELECT observe FROM contract_modes
// WHERE os = ? AND toolchain = ?", false, "fedora", "gcc16").
// ---------------------------------------------------------------------------

// How a result shows in the debug line.
template<typename T>
std::string debug_text(const T& v) {
    if constexpr (std::is_same_v<T, bool>) return v ? "true" : "false";
    else if constexpr (std::is_same_v<T, std::string>) return "'" + v + "'";
    else if constexpr (requires { v.has_value(); }) return v ? debug_text(*v) : std::string("NULL");
    else return *to_text(v);
}

template<typename T, Database D, class... B>
std::optional<T> top_one_row(D& db, const char* sql, bool debug, B&&... b) {
    if (debug) ::fprintf(stderr, "[SQL] %s\n", sql);
    auto st = db.prepare(sql);
    if constexpr (sizeof...(B) > 0) st.bind(std::forward<B>(b)...);
    if (!st.step()) {
        if (debug) ::fprintf(stderr, "[SQL] -> (no row)\n");
        return std::nullopt;
    }
    T v{};
    st.get(v);
    if (debug) ::fprintf(stderr, "[SQL] -> %s\n", debug_text(v).c_str());
    return v;
}

template<typename T, Database D, class... B>
T get_top_one(D& db, const char* sql, B&&... b) {
    return top_one_row<T>(db, sql, false, std::forward<B>(b)...).value_or(T{});
}
// `debug` takes only a real bool (std::same_as), so a text default such as "none" is never
// taken for it (a const char* would otherwise convert to bool before it converts to std::string).
template<typename T, Database D, std::same_as<bool> Debug, class... B>
T get_top_one(D& db, const char* sql, Debug debug, B&&... b) {
    return top_one_row<T>(db, sql, debug, std::forward<B>(b)...).value_or(T{});
}
template<typename T, Database D, class... B>
T get_top_one_or(D& db, const char* sql, T deflt, B&&... b) {
    return top_one_row<T>(db, sql, false, std::forward<B>(b)...).value_or(std::move(deflt));
}
template<typename T, Database D, std::same_as<bool> Debug, class... B>
T get_top_one_or(D& db, const char* sql, Debug debug, T deflt, B&&... b) {
    return top_one_row<T>(db, sql, debug, std::forward<B>(b)...).value_or(std::move(deflt));
}
template<typename T, Database D, class... B>
T get_top_one_debug(D& db, const char* sql, B&&... b) {
    return get_top_one<T>(db, sql, true, std::forward<B>(b)...);
}
template<typename T, Database D, class... B>
T get_top_one_or_debug(D& db, const char* sql, T deflt, B&&... b) {
    return get_top_one_or<T>(db, sql, true, std::move(deflt), std::forward<B>(b)...);
}

// The common types by name.
template<Database D, class... B> std::int64_t get_top_one_long(D& db, const char* sql, B&&... b) {
    return get_top_one<std::int64_t>(db, sql, std::forward<B>(b)...);
}
template<Database D, class... B> std::string get_top_one_string(D& db, const char* sql, B&&... b) {
    return get_top_one<std::string>(db, sql, std::forward<B>(b)...);
}
template<Database D, class... B> double get_top_one_double(D& db, const char* sql, B&&... b) {
    return get_top_one<double>(db, sql, std::forward<B>(b)...);
}
template<Database D, class... B> bool get_top_one_bool(D& db, const char* sql, B&&... b) {
    return get_top_one<bool>(db, sql, std::forward<B>(b)...);
}
template<Database D, class... B> std::int64_t get_top_one_long_or(D& db, const char* sql, B&&... b) {
    return get_top_one_or<std::int64_t>(db, sql, std::forward<B>(b)...);
}
template<Database D, class... B> std::string get_top_one_string_or(D& db, const char* sql, B&&... b) {
    return get_top_one_or<std::string>(db, sql, std::forward<B>(b)...);
}
template<Database D, class... B> bool get_top_one_bool_or(D& db, const char* sql, B&&... b) {
    return get_top_one_or<bool>(db, sql, std::forward<B>(b)...);
}
template<Database D, class... B> std::int64_t get_top_one_long_debug(D& db, const char* sql, B&&... b) {
    return get_top_one_debug<std::int64_t>(db, sql, std::forward<B>(b)...);
}
template<Database D, class... B> std::string get_top_one_string_debug(D& db, const char* sql, B&&... b) {
    return get_top_one_debug<std::string>(db, sql, std::forward<B>(b)...);
}
template<Database D, class... B> double get_top_one_double_debug(D& db, const char* sql, B&&... b) {
    return get_top_one_debug<double>(db, sql, std::forward<B>(b)...);
}
template<Database D, class... B> bool get_top_one_bool_debug(D& db, const char* sql, B&&... b) {
    return get_top_one_debug<bool>(db, sql, std::forward<B>(b)...);
}
template<Database D, class... B> std::int64_t get_top_one_long_or_debug(D& db, const char* sql, B&&... b) {
    return get_top_one_or_debug<std::int64_t>(db, sql, std::forward<B>(b)...);
}
template<Database D, class... B> std::string get_top_one_string_or_debug(D& db, const char* sql, B&&... b) {
    return get_top_one_or_debug<std::string>(db, sql, std::forward<B>(b)...);
}
template<Database D, class... B> bool get_top_one_bool_or_debug(D& db, const char* sql, B&&... b) {
    return get_top_one_or_debug<bool>(db, sql, std::forward<B>(b)...);
}
