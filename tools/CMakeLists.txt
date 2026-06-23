# jText command-line tools (standalone repo).
#
# These live at the repo-root `tools/` here; in the jac313 umbrella the same
# sources live at `v002/tools/`. Both build against the identical jText library.
#
#   jtext_xtox_sql : jText -> SQL. Parses + validates a .jtFull/.jtext file and
#                    emits SQL by {N} template substitution. Links jtext_core
#                    (via jac313::jText) for the parser + validator.
#   jtext_from_sql : DB -> jText. Reads a PostgreSQL table/query (through psql)
#                    and generates the four jText component files. std-only.

option(JTEXT_BUILD_TOOLS "Build the jText command-line tools" ON)

if(JTEXT_BUILD_TOOLS)
    add_executable(jtext_xtox_sql jtext_xtox_sql.cpp)
    target_link_libraries(jtext_xtox_sql PRIVATE jac313::jText)
    target_compile_features(jtext_xtox_sql PRIVATE cxx_std_26)
    set_target_properties(jtext_xtox_sql PROPERTIES CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

    add_executable(jtext_from_sql jtext_from_sql.cpp)
    target_compile_features(jtext_from_sql PRIVATE cxx_std_26)
    set_target_properties(jtext_from_sql PROPERTIES CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

    # Independent tests for the tools (registered only when tests are enabled).
    # - xtox_sql: a real functional run against the bundled workshop_tools fixture.
    # - from_sql: arg-validation always; a live DB round-trip only when psql + a
    #   target database are present (skipped otherwise, never a hard failure).
    if(JAC313_JTEXT_BUILD_TESTS)
        add_test(NAME tool_jtext_xtox_sql
            COMMAND ${CMAKE_COMMAND}
                -DTOOL=$<TARGET_FILE:jtext_xtox_sql>
                -DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures
                -DOUTDIR=${CMAKE_CURRENT_BINARY_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xtox_sql.cmake)

        add_test(NAME tool_jtext_from_sql_args
            COMMAND ${CMAKE_COMMAND}
                -DTOOL=$<TARGET_FILE:jtext_from_sql>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_from_sql_args.cmake)

        # Hardening regression tests.
        add_test(NAME tool_jtext_xtox_sql_escaping
            COMMAND ${CMAKE_COMMAND}
                -DTOOL=$<TARGET_FILE:jtext_xtox_sql>
                -DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures
                -DOUTDIR=${CMAKE_CURRENT_BINARY_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xtox_escape.cmake)

        add_test(NAME tool_jtext_from_sql_injection
            COMMAND ${CMAKE_COMMAND}
                -DTOOL=$<TARGET_FILE:jtext_from_sql>
                -DOUTDIR=${CMAKE_CURRENT_BINARY_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_from_sql_injection.cmake)
    endif()
endif()
