// File: tools/from_sql/mariadb_none.cpp
//
// Built in place of mariadb.cpp where MariaDB Connector/C (mariadb-connector-c-devel) is not installed.

#include "from_sql/source.hpp"

namespace from_sql {

std::unique_ptr<Source> open_mariadb(const std::string&, std::string& error)
{
    error = "this jtext_from_sql was built without MariaDB Connector/C (mariadb-connector-c-devel)";
    return nullptr;
}

}  // namespace from_sql
