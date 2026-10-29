# Qlite::v002, brought in from the Qlite repository (see VENDORED.jtext): one SQL API over
# SQLite (Sqlite.hpp), PostgreSQL (Postgres.hpp) and MariaDB (MariaDB.hpp). Header-only:
# a target that includes a database's header links that database's client library
# (SQLite3, libpq, libmariadb). This file and VENDORED.jtext belong to jText; change the
# headers in Qlite, then vendor-sync them here.
if(NOT TARGET Qlite::Qlite)
    add_library(Qlite_v002 INTERFACE)
    add_library(Qlite::Qlite ALIAS Qlite_v002)
    target_include_directories(Qlite_v002 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(Qlite_v002 INTERFACE cxx_std_26)
endif()
