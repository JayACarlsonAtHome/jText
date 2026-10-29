#pragma once

/**
 * Qlite::v001 — what every database shares: the Database concept, Error, the
 * transaction guard, the text <-> value conversions and the get_one_* helpers.
 * Included by Sqlite.hpp, Postgres.hpp and MariaDB.hpp; the body is
 * Database.body.hpp (an include-free fragment the module shares).
 */

#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>     // fprintf in the debug get_one_* helpers
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace Qlite::v001 {

#include "Qlite/v001/Database.body.hpp"

} // namespace Qlite::v001
