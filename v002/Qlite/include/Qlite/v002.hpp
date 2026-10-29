#pragma once

/**
 * Qlite::v002 — the C++26 SQLite wrapper. Public entry point:
 *
 *   #include <Qlite/v002.hpp>
 *   Qlite::v002::Sqlite db("app.db");
 *
 * Public symbols: Sqlite (Statement, Transaction, prepare/prepare_cached, get_one_*),
 * SqliteError, bind_value, column, version.
 */

#include "Qlite/v002/Sqlite.hpp"
