// File: tools/from_sql/sqlite_none.cpp
//
// Built in place of sqlite.cpp where the SQLite library (sqlite-devel) is not installed.

#include "from_sql/source.hpp"

namespace from_sql {

std::unique_ptr<Source> open_sqlite(const std::string&, std::string& error)
{
    error = "this jtext_from_sql was built without the SQLite library (sqlite-devel)";
    return nullptr;
}

}  // namespace from_sql
