# jText command-line tools (standalone repo).
#
# These live at the repo-root `tools/` here; in the jac313 umbrella the same
# sources live at `v002/tools/`. Both build against the identical jText library.
#
#   jtext_xtox_sql : jText -> SQL. Parses + validates a .jtFull/.jtext file and
#                    emits SQL by {N} template substitution. Links jtext_core
#                    (via jText::jText) for the parser + validator.
#   jtext_close    : adds the closers a file is missing (cut off mid-write, or
#                    written before closers were required), so it reads again.
#   jtext_to_xml   : the canonical XML export (XML_EXPORT.md), the compare standard
#                    for every round trip.
#   jtext_from_sql : DB -> jText. Reads a PostgreSQL (libpq) or SQLite table
#                    and generates the four jText component files, every row and
#                    comment included.

option(JTEXT_BUILD_TOOLS "Build the jText command-line tools" ON)

if(JTEXT_BUILD_TOOLS)
    add_executable(jtext_xtox_sql jtext_xtox_sql.cpp)
    target_link_libraries(jtext_xtox_sql PRIVATE jText::jText)
    target_compile_features(jtext_xtox_sql PRIVATE cxx_std_26)
    set_target_properties(jtext_xtox_sql PROPERTIES CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

    add_executable(jtext_close jtext_close.cpp)
    target_link_libraries(jtext_close PRIVATE jText::jText)
    target_compile_features(jtext_close PRIVATE cxx_std_26)
    set_target_properties(jtext_close PROPERTIES CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

    add_executable(jtext_to_xml jtext_to_xml.cpp)
    target_link_libraries(jtext_to_xml PRIVATE jText::jText)
    target_compile_features(jtext_to_xml PRIVATE cxx_std_26)
    set_target_properties(jtext_to_xml PROPERTIES CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

    # jtext_from_sql reads through the client libraries (never a text dump), so it is
    # built only where both are installed (libpq-devel, sqlite-devel).
    find_package(PostgreSQL QUIET)
    find_package(SQLite3 QUIET)
    if(TARGET PostgreSQL::PostgreSQL AND TARGET SQLite::SQLite3)
        add_executable(jtext_from_sql jtext_from_sql.cpp)
        target_link_libraries(jtext_from_sql PRIVATE jText::jText PostgreSQL::PostgreSQL SQLite::SQLite3)
        target_compile_features(jtext_from_sql PRIVATE cxx_std_26)
        set_target_properties(jtext_from_sql PROPERTIES CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    else()
        message(WARNING "jtext_from_sql not built: it needs libpq and SQLite3 (libpq-devel, sqlite-devel)")
    endif()

    # Independent tests for the tools (registered only when tests are enabled).
    # - xtox_sql: a real functional run against the bundled workshop_tools fixture.
    # - from_sql: arg-validation always; a live DB round-trip only when psql + a
    #   target database are present (skipped otherwise, never a hard failure).
    if(JTEXT_BUILD_TESTS)
        add_test(NAME tool_jtext_xtox_sql
            COMMAND ${CMAKE_COMMAND}
                -DTOOL=$<TARGET_FILE:jtext_xtox_sql>
                -DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures
                -DOUTDIR=${CMAKE_CURRENT_BINARY_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xtox_sql.cmake)

        add_test(NAME tool_jtext_close
            COMMAND ${CMAKE_COMMAND}
                -DTOOL=$<TARGET_FILE:jtext_close>
                -DOUTDIR=${CMAKE_CURRENT_BINARY_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_close.cmake)

        if(TARGET jtext_from_sql)
            add_test(NAME tool_jtext_from_sql_args
                COMMAND ${CMAKE_COMMAND}
                    -DTOOL=$<TARGET_FILE:jtext_from_sql>
                    -DOUTDIR=${CMAKE_CURRENT_BINARY_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_from_sql_args.cmake)
        endif()

        # Hardening regression tests.
        add_test(NAME tool_jtext_xtox_sql_escaping
            COMMAND ${CMAKE_COMMAND}
                -DTOOL=$<TARGET_FILE:jtext_xtox_sql>
                -DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures
                -DOUTDIR=${CMAKE_CURRENT_BINARY_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xtox_escape.cmake)

        # Round trips through SQLite and PostgreSQL, compared through the canonical XML
        # export. PostgreSQL is reported as skipped (exit 77) where no server answers.
        find_program(JTEXT_SQLITE3 sqlite3)
        find_program(JTEXT_PSQL psql)
        if(TARGET jtext_from_sql AND JTEXT_SQLITE3)
            add_test(NAME tool_round_trip_sqlite
                COMMAND ${CMAKE_COMMAND} -DDB=sqlite
                    -DXTOX=$<TARGET_FILE:jtext_xtox_sql> -DFROMSQL=$<TARGET_FILE:jtext_from_sql>
                    -DTOXML=$<TARGET_FILE:jtext_to_xml> -DSQLITE3=${JTEXT_SQLITE3}
                    -DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures
                    -DOUTDIR=${CMAKE_CURRENT_BINARY_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_round_trip.cmake)
        endif()
        if(TARGET jtext_from_sql AND JTEXT_PSQL)
            add_test(NAME tool_round_trip_postgres
                COMMAND ${CMAKE_COMMAND} -DDB=postgres
                    -DXTOX=$<TARGET_FILE:jtext_xtox_sql> -DFROMSQL=$<TARGET_FILE:jtext_from_sql>
                    -DTOXML=$<TARGET_FILE:jtext_to_xml> -DPSQL=${JTEXT_PSQL}
                    -DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures
                    -DOUTDIR=${CMAKE_CURRENT_BINARY_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_round_trip.cmake)
            set_tests_properties(tool_round_trip_postgres PROPERTIES SKIP_RETURN_CODE 77)
        endif()

        if(TARGET jtext_from_sql)
            add_test(NAME tool_jtext_from_sql_injection
                COMMAND ${CMAKE_COMMAND}
                    -DTOOL=$<TARGET_FILE:jtext_from_sql>
                    -DOUTDIR=${CMAKE_CURRENT_BINARY_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_from_sql_injection.cmake)
        endif()
    endif()
endif()
