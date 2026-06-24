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
# Two valid records -> two INSERTs with the substituted values.
if(NOT sql MATCHES "INSERT INTO sample .* VALUES .1, 'Stanley Hammer 16oz', 'Hand Tools'.")
    message(FATAL_ERROR "expected substituted INSERT for record 1 not found; got:\n${sql}")
endif()
if(NOT sql MATCHES "INSERT INTO sample .* VALUES .2, 'DeWalt Drill DCD777', 'Power Tools'.")
    message(FATAL_ERROR "expected substituted INSERT for record 2 not found; got:\n${sql}")
endif()

message(STATUS "jtext_xtox_sql OK: emitted both INSERTs with correct {N} substitution")
