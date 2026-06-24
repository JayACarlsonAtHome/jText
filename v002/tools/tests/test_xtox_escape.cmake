# Hardening regression test: jtext_xtox_sql must escape single quotes in
# emitted SQL string literals (doubling them), so a value like
#   O'Reilly's "Pro" Set
# becomes the safe literal
#   'O''Reilly''s "Pro" Set'
#
# Invoked via: cmake -DTOOL=<exe> -DFIXTURES=<dir> -DOUTDIR=<dir> -P test_xtox_escape.cmake

set(out "${OUTDIR}/escape.sql")
file(REMOVE "${out}")

execute_process(
    COMMAND "${TOOL}" "${FIXTURES}" escape "${out}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE so ERROR_VARIABLE se)

if(NOT rc EQUAL 0)
    message(FATAL_ERROR "jtext_xtox_sql exited ${rc}\n${so}\n${se}")
endif()

file(READ "${out}" sql)
# Single quotes doubled; double quotes left verbatim (correct for SQL literals).
if(NOT sql MATCHES "'O''Reilly''s \"Pro\" Set'")
    message(FATAL_ERROR "single-quote escaping wrong; emitted SQL:\n${sql}")
endif()
# Guard against a regression that emits a lone (unescaped) single quote run.
if(sql MATCHES "'O'Reilly")
    message(FATAL_ERROR "found UNescaped single quote in output:\n${sql}")
endif()

message(STATUS "jtext_xtox_sql SQL-escaping OK")
