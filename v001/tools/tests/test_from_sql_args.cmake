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

message(STATUS "jtext_from_sql arg-validation OK (exit ${rc}, Usage shown)")
