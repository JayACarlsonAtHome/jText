#pragma once

/**
 * Qlite::v002::Postgres — the Qlite API over PostgreSQL, through libpq (link libpq).
 * Same members as Qlite::v002::Sqlite (the Database concept in Database.hpp);
 * placeholders are PostgreSQL's own: $1, $2, ... The implementation lives in
 * Postgres.body.hpp (an include-free fragment).
 */

#include "Qlite/v002/Database.hpp"   // the concept, Error, Transaction, get_top_one_*

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

namespace Qlite::v002 {

#include "Qlite/v002/Postgres.body.hpp"

} // namespace Qlite::v002
