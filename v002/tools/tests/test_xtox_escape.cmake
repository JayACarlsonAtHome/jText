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

# A value that looks like a placeholder stays text inside its literal: the old
# pass-per-placeholder substitution put value 3 (and its quotes) inside value 2.
if(NOT sql MATCHES "VALUES .2, 'a {3} b', 'back\\\\slash', 'NaN'.")
    message(FATAL_ERROR "a value holding {3} was substituted again; emitted SQL:\n${sql}")
endif()
# A backslash is written as itself; MariaDB alone runs the sql_mode line that makes it one.
if(NOT sql MATCHES "/\\*M! SET SESSION sql_mode = CONCAT\\(@@sql_mode, ',NO_BACKSLASH_ESCAPES'\\) \\*/;")
    message(FATAL_ERROR "the MariaDB NO_BACKSLASH_ESCAPES line is missing; emitted SQL:\n${sql}")
endif()

# NaN and the infinities in a Number are data: written as quoted words.
if(NOT sql MATCHES "VALUES .3, 'minus infinity', 'limits', '-Infinity'.")
    message(FATAL_ERROR "a -Infinity Number is not the quoted word '-Infinity'; emitted SQL:\n${sql}")
endif()

message(STATUS "jtext_xtox_sql SQL-escaping OK (quotes, {N} in a value, backslash, NaN/-Infinity)")
