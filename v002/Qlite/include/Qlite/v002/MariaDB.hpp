#pragma once

/**
 * Qlite::v002::MariaDB — the Qlite API over MariaDB (and MySQL), through MariaDB
 * Connector/C (link libmariadb). Same members as Qlite::v002::Sqlite (the Database
 * concept in Database.hpp); placeholders are `?`. The implementation lives in
 * MariaDB.body.hpp (an include-free fragment).
 */

#include "Qlite/v002/Database.hpp"   // the concept, Error, Transaction, get_one_*

#include <mysql.h>

#include <format>
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

#include "Qlite/v002/MariaDB.body.hpp"

} // namespace Qlite::v002
