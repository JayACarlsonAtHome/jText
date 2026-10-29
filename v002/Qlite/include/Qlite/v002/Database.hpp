#pragma once

/**
 * Qlite::v002 — what every database shares: the Database concept, Error, the
 * transaction guard, the text <-> value conversions and the get_top_one_* helpers.
 * Included by Sqlite.hpp, Postgres.hpp and MariaDB.hpp; the body is
 * Database.body.hpp (an include-free fragment the module shares).
 */

#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>     // fprintf in the debug get_top_one_* helpers
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "Qlite/v002/contracts.hpp"   // QLITE_PRE/POST/ASSERT, used by the bodies

namespace Qlite::v002 {

#include "Qlite/v002/Database.body.hpp"

} // namespace Qlite::v002
