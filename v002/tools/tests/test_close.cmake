# Functional test for jtext_close: a file written before closers were required
# (and cut off) is reported by --check, closed in place, and then reports clean.
#
# Invoked via: cmake -DTOOL=<exe> -DOUTDIR=<dir> -P test_close.cmake

set(f "${OUTDIR}/close_test.jtext")
file(WRITE "${f}" "=== jText File ===\n 1. ?# close test\n\n=== Section: s ===\n=== Fields ===\n 1. /# id/Identity(1,1)\n=== Data ===\n1. |# 1\n")

execute_process(COMMAND "${TOOL}" --check "${f}" RESULT_VARIABLE rc OUTPUT_VARIABLE so)
if(NOT rc EQUAL 1)
    message(FATAL_ERROR "--check on an open file should exit 1, got ${rc}\n${so}")
endif()
execute_process(COMMAND "${TOOL}" "${f}" RESULT_VARIABLE rc OUTPUT_VARIABLE so)
if(NOT rc EQUAL 0 OR NOT so MATCHES "closed")
    message(FATAL_ERROR "closing failed (${rc})\n${so}")
endif()
execute_process(COMMAND "${TOOL}" --check "${f}" RESULT_VARIABLE rc OUTPUT_VARIABLE so)
if(NOT rc EQUAL 0 OR NOT so MATCHES "already closed")
    message(FATAL_ERROR "the closed file should check clean, got ${rc}\n${so}")
endif()
file(READ "${f}" text)
if(NOT text MATCHES "=== End Fields ===\n=== Data ===" OR NOT text MATCHES "=== End File ===\n$")
    message(FATAL_ERROR "closers not where expected:\n${text}")
endif()
