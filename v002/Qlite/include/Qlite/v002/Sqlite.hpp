#pragma once

/**
 * Qlite::v002 — a modern C++ SQLite wrapper emphasizing clean variadic
 * "peel first, recurse on ellipses" patterns for binding and querying.
 *
 * Canonical evolution of the legacy jacQlite / jac::qlite header-only wrapper.
 * v002 is the C++26 frontier (v001 is the C++23 baseline). Public symbols: Sqlite, SqliteError, bind_value,
 * column; and from Database.hpp the Database concept, Error, version and get_one_*.
 * Postgres.hpp and MariaDB.hpp give the same API over PostgreSQL and MariaDB.
 *
 * The implementation lives in Sqlite.body.hpp (an include-free fragment). This
 * textual header makes the std + <sqlite3.h> names visible via the textual
 * includes below, then pastes the shared body inside the namespace. The module
 * (Qlite.v002.cppm) includes the SAME body, differing only in how std/sqlite3
 * are made visible. Keep declarations in the body file.
 */

#include "Qlite/v002/Database.hpp"   // the concept, Error, Transaction, get_one_*

#include <sqlite3.h>

#include <string>
#include <string_view>
#include <stdexcept>
#include <utility>
#include <type_traits>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <tuple>
#include <vector>
#include <span>
#include <unordered_map>
#include <cstdio>     // fprintf in debug get_one_* helpers (also in Sqlite.body.hpp)
#if __has_include(<expected>)
#  include <expected>
#endif

namespace Qlite::v002 {

#include "Qlite/v002/Sqlite.body.hpp"

} // namespace Qlite::v002
