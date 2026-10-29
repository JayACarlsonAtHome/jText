# jText command-line tools (standalone repo).
#
# These live at the repo-root `tools/` here; in the jac313 umbrella the same
# sources live at `v001/tools/`. Both build against the identical jText library.
#
#   jtext_xtox_sql : jText -> SQL. Parses + validates a .jtFull/.jtext file and
#                    emits SQL by {N} template substitution. Links jtext_core
#                    (via jText::jText) for the parser + validator.
#   jtext_close    : adds the closers a file is missing (cut off mid-write, or
#                    written before closers were required), so it reads again.
#   jtext_to_xml   : the canonical XML export (XML_EXPORT.md), the compare standard
#   jtext_from_xml : that XML back into jText (either scope)
#                    for every round trip.
#   jtext_from_sql : DB -> jText. Reads a PostgreSQL (libpq) or SQLite table
#                    and generates the four jText component files, every row and
#                    comment included.

option(JTEXT_BUILD_TOOLS "Build the jText command-line tools" ON)

if(JTEXT_BUILD_TOOLS)
    add_executable(jtext_xtox_sql jtext_xtox_sql.cpp)
    target_link_libraries(jtext_xtox_sql PRIVATE jText::jText)
    target_compile_features(jtext_xtox_sql PRIVATE cxx_std_23)
    set_target_properties(jtext_xtox_sql PROPERTIES CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

    add_executable(jtext_close jtext_close.cpp)
    target_link_libraries(jtext_close PRIVATE jText::jText)
    target_compile_features(jtext_close PRIVATE cxx_std_23)
    set_target_properties(jtext_close PROPERTIES CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

    add_executable(jtext_to_xml jtext_to_xml.cpp)
    target_link_libraries(jtext_to_xml PRIVATE jText::jText)
    target_compile_features(jtext_to_xml PRIVATE cxx_std_23)
    set_target_properties(jtext_to_xml PROPERTIES CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

    # jtext_from_sql reads each database through its client library (never a text dump).
    # A backend whose library is installed is built from from_sql/<db>.cpp; one whose
    # library is missing from from_sql/<db>_none.cpp, which says so when asked.
    add_executable(jtext_from_sql jtext_from_sql.cpp)
    target_include_directories(jtext_from_sql PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(jtext_from_sql PRIVATE jText::jText Qlite::Qlite)
    target_compile_features(jtext_from_sql PRIVATE cxx_std_23)
    set_target_properties(jtext_from_sql PROPERTIES CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    find_package(PostgreSQL QUIET)
    find_package(SQLite3 QUIET)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(JTEXT_MARIADB QUIET IMPORTED_TARGET libmariadb)
    endif()
    set(JTEXT_FROM_SQL_BACKENDS "")
    foreach(db postgres sqlite mariadb)
        if(db STREQUAL "postgres")
            set(lib PostgreSQL::PostgreSQL)
        elseif(db STREQUAL "sqlite")
            set(lib SQLite3::SQLite3)
        else()
            set(lib PkgConfig::JTEXT_MARIADB)
        endif()
        if(TARGET ${lib})
            target_sources(jtext_from_sql PRIVATE from_sql/${db}.cpp)
            target_link_libraries(jtext_from_sql PRIVATE ${lib})
            list(APPEND JTEXT_FROM_SQL_BACKENDS ${db})
        else()
            target_sources(jtext_from_sql PRIVATE from_sql/${db}_none.cpp)
        endif()
    endforeach()
    message(STATUS "jtext_from_sql backends: ${JTEXT_FROM_SQL_BACKENDS}")

    # jtext_to_ods / jtext_from_ods (ODS.md): the archive through libzip, content.xml
    # through libxml2; built where both are installed (libzip-devel, libxml2-devel).
    find_package(LibXml2 QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(JTEXT_LIBZIP QUIET IMPORTED_TARGET libzip)
    endif()
    if(TARGET LibXml2::LibXml2 AND TARGET PkgConfig::JTEXT_LIBZIP)
        add_library(jtext_ods STATIC ods/ods.cpp)
        target_include_directories(jtext_ods PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(jtext_ods PUBLIC jText::jText LibXml2::LibXml2 PkgConfig::JTEXT_LIBZIP)
        target_compile_features(jtext_ods PUBLIC cxx_std_23)
        foreach(tool jtext_to_ods jtext_from_ods)
            add_executable(${tool} ${tool}.cpp)
            target_link_libraries(${tool} PRIVATE jtext_ods)
            set_target_properties(${tool} PROPERTIES CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
        endforeach()
    else()
        message(STATUS "jtext_to_ods / jtext_from_ods not built: they need libzip and libxml2")
    endif()

    # jtext_from_xml (XML_EXPORT.md): the XML export back into jText, through libxml2.
    if(TARGET LibXml2::LibXml2)
        add_executable(jtext_from_xml jtext_from_xml.cpp)
        target_include_directories(jtext_from_xml PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(jtext_from_xml PRIVATE jText::jText LibXml2::LibXml2)
        target_compile_features(jtext_from_xml PRIVATE cxx_std_23)
        set_target_properties(jtext_from_xml PROPERTIES CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    else()
        message(STATUS "jtext_from_xml not built: it needs libxml2")
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

        add_test(NAME tool_jtext_from_sql_args
            COMMAND ${CMAKE_COMMAND}
                -DTOOL=$<TARGET_FILE:jtext_from_sql>
                -DOUTDIR=${CMAKE_CURRENT_BINARY_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_from_sql_args.cmake)

        # Hardening regression tests.
        add_test(NAME tool_jtext_xtox_sql_escaping
            COMMAND ${CMAKE_COMMAND}
                -DTOOL=$<TARGET_FILE:jtext_xtox_sql>
                -DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures
                -DOUTDIR=${CMAKE_CURRENT_BINARY_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xtox_escape.cmake)

        # Round trips through each database, compared through the canonical XML export.
        # A server that does not answer is reported as skipped (exit 77), never passed.
        find_program(JTEXT_SQLITE3 sqlite3)
        find_program(JTEXT_PSQL psql)
        find_program(JTEXT_MARIADB_CLIENT mariadb)
        foreach(db sqlite postgres mariadb)
            if(db STREQUAL "sqlite")
                set(client ${JTEXT_SQLITE3})
            elseif(db STREQUAL "postgres")
                set(client ${JTEXT_PSQL})
            else()
                set(client ${JTEXT_MARIADB_CLIENT})
            endif()
            if(db IN_LIST JTEXT_FROM_SQL_BACKENDS AND client)
                add_test(NAME tool_round_trip_${db}
                    COMMAND ${CMAKE_COMMAND} -DDB=${db} -DCLIENT=${client}
                        -DXTOX=$<TARGET_FILE:jtext_xtox_sql> -DFROMSQL=$<TARGET_FILE:jtext_from_sql>
                        -DTOXML=$<TARGET_FILE:jtext_to_xml>
                        -DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures
                        -DOUTDIR=${CMAKE_CURRENT_BINARY_DIR}
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_round_trip.cmake)
                set_tests_properties(tool_round_trip_${db} PROPERTIES SKIP_RETURN_CODE 77)
            endif()
        endforeach()

        # Round trips through an OpenDocument spreadsheet, directly and through LibreOffice.
        # XML import round trips: jText -> XML -> jText -> XML, byte-identical, both scopes.
        if(TARGET jtext_from_xml)
            foreach(fixture roundtrip/roundtrip.jtFull ods_declarations/ods_declarations.jtFull
                            sample/sample.jtFull escape/escape.jtFull)
                get_filename_component(fname ${fixture} NAME_WE)
                foreach(scope full data)
                    add_test(NAME tool_xml_round_trip_${fname}_${scope}
                        COMMAND ${CMAKE_COMMAND} -DTOXML=$<TARGET_FILE:jtext_to_xml>
                            -DFROMXML=$<TARGET_FILE:jtext_from_xml> -DSCOPE=${scope}
                            -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/${fixture}
                            -DOUTDIR=${CMAKE_CURRENT_BINARY_DIR}
                            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xml_round_trip.cmake)
                endforeach()
            endforeach()
        endif()

        if(TARGET jtext_to_ods)
            find_program(JTEXT_SOFFICE soffice)
            set(ods_args -DTOODS=$<TARGET_FILE:jtext_to_ods> -DFROMODS=$<TARGET_FILE:jtext_from_ods>
                         -DTOXML=$<TARGET_FILE:jtext_to_xml> -DOUTDIR=${CMAKE_CURRENT_BINARY_DIR})
            add_test(NAME tool_ods_round_trip
                COMMAND ${CMAKE_COMMAND} ${ods_args} -DNAME=ods_round_trip
                    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/roundtrip/roundtrip.jtFull
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ods_round_trip.cmake)
            add_test(NAME tool_ods_declarations
                COMMAND ${CMAKE_COMMAND} ${ods_args} -DNAME=ods_declarations
                    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ods_declarations/ods_declarations.jtFull
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ods_round_trip.cmake)
            if(JTEXT_SOFFICE)
                add_test(NAME tool_ods_through_calc
                    COMMAND ${CMAKE_COMMAND} ${ods_args} -DNAME=ods_through_calc -DSOFFICE=${JTEXT_SOFFICE}
                        -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/roundtrip/roundtrip.jtFull
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ods_round_trip.cmake)
            endif()
        endif()

        add_test(NAME tool_jtext_from_sql_injection
            COMMAND ${CMAKE_COMMAND}
                -DTOOL=$<TARGET_FILE:jtext_from_sql>
                -DOUTDIR=${CMAKE_CURRENT_BINARY_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_from_sql_injection.cmake)
    endif()
endif()
