# Round trip through a database, compared through the canonical XML export
# (XML_EXPORT.md): the fixture goes to SQL (jtext_xtox_sql), into the database, back
# to jText (jtext_from_sql, through the client library), and both the original and the
# copy are exported in data scope. The two XML files must be byte-identical: every
# value, type, length, constraint and comment survived.
#
# Invoked via: cmake -DDB=sqlite|postgres|mariadb -DCLIENT=<sqlite3|psql|mariadb>
#                    -DXTOX=<exe> -DFROMSQL=<exe> -DTOXML=<exe>
#                    -DFIXTURES=<dir> -DOUTDIR=<dir> -P test_round_trip.cmake
# PostgreSQL and MariaDB: exit 77 (reported as skipped, never passed) when no server
# answers. Each works in a throwaway schema / database, dropped afterwards; no other
# table is touched. MariaDB loads the SQL with NO_BACKSLASH_ESCAPES, so a backslash in a
# string literal is a backslash, as in standard SQL, PostgreSQL and SQLite (SPEC 4.3).

set(work "${OUTDIR}/round_trip_${DB}")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")
set(fixture "${FIXTURES}/roundtrip/roundtrip.jtFull")
set(desc "${work}/roundtrip.desc")
file(WRITE "${desc}" "SELECT * FROM roundtrip_items\n")

function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE so ERROR_VARIABLE se)
    if(NOT rc EQUAL 0)
        set(failure "${ARGN}\nexited ${rc}\n${so}${se}" PARENT_SCOPE)
    endif()
endfunction()

# run(), with `file` as the command's standard input.
function(run_input file)
    execute_process(COMMAND ${ARGN} INPUT_FILE "${file}" RESULT_VARIABLE rc OUTPUT_VARIABLE so ERROR_VARIABLE se)
    if(NOT rc EQUAL 0)
        set(failure "${ARGN} < ${file}\nexited ${rc}\n${so}${se}" PARENT_SCOPE)
    endif()
endfunction()

string(RANDOM LENGTH 12 ALPHABET "abcdefghijklmnopqrstuvwxyz" suffix)
set(throwaway "jtext_round_trip_${suffix}")

run("${XTOX}" "${FIXTURES}" roundtrip "${work}/roundtrip.sql")
if(failure)
    message(FATAL_ERROR "jtext_xtox_sql failed: ${failure}")
endif()
run("${TOXML}" --data "${fixture}" "${work}/original.xml")
if(failure)
    message(FATAL_ERROR "jtext_to_xml (original) failed: ${failure}")
endif()

if(DB STREQUAL "sqlite")
    set(db "${work}/roundtrip.db")
    run("${CLIENT}" -bail "${db}" ".read ${work}/roundtrip.sql")
    if(NOT failure)
        run("${FROMSQL}" --sqlite "${db}" "${work}" copy "${desc}")
    endif()
elseif(DB STREQUAL "mariadb")
    set(mariadb "${CLIENT}" --default-character-set=utf8mb4)
    run(${mariadb} -e "SELECT 1")
    if(failure)
        message(STATUS "MariaDB: no server answers; SKIPPED\n${failure}")
        file(REMOVE_RECURSE "${work}")
        cmake_language(EXIT 77)
    endif()
    run(${mariadb} -e "CREATE DATABASE ${throwaway}")
    if(failure)
        message(FATAL_ERROR "cannot create the throwaway database (the login needs: GRANT ALL ON "
                            "`jtext\\_round\\_trip\\_%`.* TO it): ${failure}")
    endif()
    run_input("${work}/roundtrip.sql" ${mariadb}
              "--init-command=SET SESSION sql_mode = CONCAT(@@sql_mode, ',NO_BACKSLASH_ESCAPES')" ${throwaway})
    if(NOT failure)
        run("${FROMSQL}" --mariadb ${throwaway} "${work}" copy "${desc}")
    endif()
    set(load_failure "${failure}")
    unset(failure)
    run(${mariadb} -e "DROP DATABASE ${throwaway}")
    if(failure)
        message(WARNING "could not drop the throwaway database ${throwaway}: ${failure}")
    endif()
    set(failure "${load_failure}")
else()
    run("${CLIENT}" -X -q -c "SELECT 1")
    if(failure)
        message(STATUS "PostgreSQL: no server answers; SKIPPED\n${failure}")
        file(REMOVE_RECURSE "${work}")
        cmake_language(EXIT 77)
    endif()
    set(schema "${throwaway}")
    run("${CLIENT}" -X -q -v ON_ERROR_STOP=1 -c "CREATE SCHEMA ${schema}")
    if(failure)
        message(FATAL_ERROR "cannot create the throwaway schema: ${failure}")
    endif()
    set(ENV{PGOPTIONS} "-c search_path=${schema}")
    run("${CLIENT}" -X -q -v ON_ERROR_STOP=1 -f "${work}/roundtrip.sql")
    if(NOT failure)
        run("${FROMSQL}" "${work}" copy "${desc}")
    endif()
    set(load_failure "${failure}")
    unset(failure)
    unset(ENV{PGOPTIONS})
    run("${CLIENT}" -X -q -v ON_ERROR_STOP=1 -c "DROP SCHEMA ${schema} CASCADE")
    if(failure)
        message(WARNING "could not drop the throwaway schema ${schema}: ${failure}")
    endif()
    set(failure "${load_failure}")
endif()
if(failure)
    message(FATAL_ERROR "loading into ${DB} or reading back failed: ${failure}")
endif()

run("${TOXML}" --data "${work}/copy/copy.jtext" "${work}/copy.xml")
if(failure)
    message(FATAL_ERROR "jtext_to_xml (copy) failed: ${failure}")
endif()

file(READ "${work}/original.xml" original)
file(READ "${work}/copy.xml" copy)
if(NOT original STREQUAL copy)
    execute_process(COMMAND diff -u "${work}/original.xml" "${work}/copy.xml" OUTPUT_VARIABLE d)
    message(FATAL_ERROR "the ${DB} round trip changed the data:\n${d}")
endif()
message(STATUS "${DB} round trip OK: the data-scope XML of the original and the copy are identical")
