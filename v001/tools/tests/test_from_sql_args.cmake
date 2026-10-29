# Independent test for jtext_from_sql that needs NO database.
# The full DB->jText round-trip requires a live PostgreSQL (via psql), so here we
# verify the tool's argument handling: run with no args -> must print Usage and
# exit non-zero. A real round-trip is exercised separately only when psql + a
# target DB are available (see run-tests.sh / README), and is skipped otherwise.
#
# Invoked via: cmake -DTOOL=<exe> -P test_from_sql_args.cmake

execute_process(
    COMMAND "${TOOL}"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE so
    ERROR_VARIABLE  se)

if(rc EQUAL 0)
    message(FATAL_ERROR "jtext_from_sql with no args should exit non-zero, got 0")
endif()
if(NOT "${se}${so}" MATCHES "Usage")
    message(FATAL_ERROR "jtext_from_sql should print a Usage message; output was:\n${se}${so}")
endif()

# psql that cannot connect (no server at this socket directory): the tool must fail
# and write nothing, never put psql's error message into the generated files.
set(desc "${OUTDIR}/from_sql_fail.desc")
file(WRITE "${desc}" "SELECT * FROM workshop_tools\n")
file(REMOVE_RECURSE "${OUTDIR}/from_sql_fail")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env PGHOST=${OUTDIR}/no_such_server PGPORT=1 PGCONNECT_TIMEOUT=2
            "${TOOL}" "${OUTDIR}" from_sql_fail "${desc}"
    RESULT_VARIABLE rc2
    OUTPUT_VARIABLE so2
    ERROR_VARIABLE  se2)
if(rc2 EQUAL 0)
    message(FATAL_ERROR "jtext_from_sql should fail when psql fails; output was:\n${se2}${so2}")
endif()
if(EXISTS "${OUTDIR}/from_sql_fail")
    message(FATAL_ERROR "jtext_from_sql wrote files although psql failed")
endif()

message(STATUS "jtext_from_sql arg-validation OK (exit ${rc}, Usage shown); psql failure writes nothing (exit ${rc2})")
