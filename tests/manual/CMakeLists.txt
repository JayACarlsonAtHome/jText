# ────────────────────────────────────────────────────────────────
#  Manual tests
# ────────────────────────────────────────────────────────────────
#
#  meta_test links against the test framework AND the jText core
#  library, so it can run framework self-tests, parser tests,
#  validator tests (future), etc., all in one executable.
#
# ────────────────────────────────────────────────────────────────

add_executable(meta_test
        main.cpp
)

target_link_libraries(meta_test
        PRIVATE
        jtext_test_framework
        jtext_core
)

set_target_properties(meta_test PROPERTIES
        CXX_STANDARD_REQUIRED   ON
        CXX_EXTENSIONS          OFF
)
