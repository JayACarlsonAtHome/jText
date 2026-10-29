#pragma once

/**
 * Qlite::v001 — the C++23 SQLite wrapper. Public entry point:
 *
 *   #include <Qlite/v001.hpp>
 *   Qlite::v001::Sqlite db("app.db");
 *
 * Public symbols: Sqlite (Statement, Transaction, prepare/prepare_cached, get_one_*),
 * SqliteError, bind_value, column, version.
 */

#include "Qlite/v001/Sqlite.hpp"
