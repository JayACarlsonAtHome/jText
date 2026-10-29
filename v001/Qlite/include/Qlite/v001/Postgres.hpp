#pragma once

/**
 * Qlite::v001::Postgres — the Qlite API over PostgreSQL, through libpq (link libpq).
 * Same members as Qlite::v001::Sqlite (the Database concept in Database.hpp);
 * placeholders are PostgreSQL's own: $1, $2, ... The implementation lives in
 * Postgres.body.hpp (an include-free fragment).
 */

#include "Qlite/v001/Database.hpp"   // the concept, Error, Transaction, get_one_*

#include <libpq-fe.h>

#include <cstdlib>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>
#if __has_include(<expected>)
#  include <expected>
#endif

namespace Qlite::v001 {

#include "Qlite/v001/Postgres.body.hpp"

} // namespace Qlite::v001
