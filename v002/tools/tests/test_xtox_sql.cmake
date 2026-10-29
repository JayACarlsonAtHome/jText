# Independent functional test for jtext_xtox_sql.
# Runs the tool against the bundled minimal-valid fixture
# (tests/fixtures/sample/sample.jtFull) and asserts it succeeds and emits the
# expected INSERT statements via {N} template substitution.
#
# Invoked via: cmake -DTOOL=<exe> -DFIXTURES=<dir> -DOUTDIR=<dir> -P test_xtox_sql.cmake

set(out "${OUTDIR}/sample.sql")
file(REMOVE "${out}")

execute_process(
    COMMAND "${TOOL}" "${FIXTURES}" sample "${out}"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE so
    ERROR_VARIABLE  se)

if(NOT rc EQUAL 0)
    message(FATAL_ERROR "jtext_xtox_sql exited ${rc}\n--- stdout ---\n${so}\n--- stderr ---\n${se}")
endif()
if(NOT EXISTS "${out}")
    message(FATAL_ERROR "jtext_xtox_sql produced no output file at ${out}")
endif()

file(READ "${out}" sql)
# Two valid records -> two INSERTs with the substituted values. {NULL} is the one
# value that becomes SQL NULL; an empty value is '' (SPEC 2.6.1).
if(NOT sql MATCHES "INSERT INTO sample .* VALUES .1, 'Stanley Hammer 16oz', 'Hand Tools', '2019-04-12', NULL.")
    message(FATAL_ERROR "expected substituted INSERT for record 1 not found; got:\n${sql}")
endif()
if(NOT sql MATCHES "INSERT INTO sample .* VALUES .2, 'DeWalt Drill DCD777', 'Power Tools', '1900-01-01', ''.")
    message(FATAL_ERROR "expected substituted INSERT for record 2 not found; got:\n${sql}")
endif()

# The header block is SQL comments, so the file runs as written (SPEC 2.1).
if(NOT sql MATCHES "^-- File:    sample.sql\n-- Date:    [0-9-]+\n-- Purpose: SQL Data File\n")
    message(FATAL_ERROR "the .sql header should be -- comments; got:\n${sql}")
endif()
if(sql MATCHES "(^|\n)//")
    message(FATAL_ERROR "a .sql file must not contain // lines; got:\n${sql}")
endif()

message(STATUS "jtext_xtox_sql OK: both INSERTs, {NULL} as NULL and empty as '', -- header")
