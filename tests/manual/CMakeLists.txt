# ────────────────────────────────────────────────────────────────
#  Manual tests
# ────────────────────────────────────────────────────────────────
#
#  Session 1: one executable (meta_test) that exercises the
#  framework against itself, proving the foundation works.
#
#  Session 3+ will add more executables (or test categories) here
#  as the parser/validator/emitter come online.
#
# ────────────────────────────────────────────────────────────────

add_executable(meta_test
        main.cpp
)

target_link_libraries(meta_test
        PRIVATE
        jtext_test_framework
)

set_target_properties(meta_test PROPERTIES
        CXX_STANDARD_REQUIRED   ON
        CXX_EXTENSIONS          OFF
)
