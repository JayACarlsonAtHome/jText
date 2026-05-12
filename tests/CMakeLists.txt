# ────────────────────────────────────────────────────────────────
#  Tests
# ────────────────────────────────────────────────────────────────
#
#  The framework lives in framework/ and builds as a header-only
#  interface library. Test executables (in manual/, conformance/)
#  link against it.
#
#  Session 1 establishes:
#    - tests/framework/        the framework itself
#    - tests/manual/           with one meta_test executable
#
#  Session 2 adds:
#    - tests/framework/        runner.cpp, reporter.cpp
#
#  Session 3+ adds:
#    - tests/manual/           parser, validator, emitter tests
#    - tests/conformance/      file-driven conformance tests
#
# ────────────────────────────────────────────────────────────────

add_subdirectory(framework)
add_subdirectory(manual)
