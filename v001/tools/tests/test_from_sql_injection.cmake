# Security regression test: jtext_from_sql must reject a table name that isn't a
# plain SQL identifier, BEFORE building or running any psql query — so an injected
# descriptor like  SELECT * FROM evil'; DROP TABLE x  cannot reach the shell/DB.
# Needs no database: the rejection happens during argument/descriptor handling.
#
# Invoked via: cmake -DTOOL=<exe> -DOUTDIR=<dir> -P test_from_sql_injection.cmake

set(desc "${OUTDIR}/injection_descriptor.sql")
# A FROM clause whose table token carries SQL/shell metacharacters.
file(WRITE "${desc}" "SELECT * FROM evil'; DROP TABLE x; --\n")

execute_process(
    COMMAND "${TOOL}" "${OUTDIR}" injbase "${desc}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE so ERROR_VARIABLE se)

if(rc EQUAL 0)
    message(FATAL_ERROR "jtext_from_sql accepted an injected table name (exit 0); output:\n${so}${se}")
endif()
if(NOT "${se}${so}" MATCHES "unsafe/invalid table name")
    message(FATAL_ERROR "expected an unsafe-table-name rejection; output was:\n${se}${so}")
endif()

message(STATUS "jtext_from_sql injection rejection OK (exit ${rc})")
