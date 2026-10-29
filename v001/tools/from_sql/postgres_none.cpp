// File: tools/from_sql/postgres_none.cpp
//
// Built in place of postgres.cpp where libpq (libpq-devel) is not installed.

#include "from_sql/source.hpp"

namespace from_sql {

std::unique_ptr<Source> open_postgres(std::string& error)
{
    error = "this jtext_from_sql was built without libpq (libpq-devel)";
    return nullptr;
}

}  // namespace from_sql
