# ────────────────────────────────────────────────────────────────
#  jText test framework
# ────────────────────────────────────────────────────────────────
#
#  Session 1: header-only. Concepts, type names, value formatting,
#  basic assertion functions. INTERFACE library is the right target
#  type for header-only — consumers get the includes but no compiled
#  objects to link.
#
#  Session 2 will add runner.cpp and reporter.cpp, at which point
#  this becomes a STATIC library with compiled sources.
#
# ────────────────────────────────────────────────────────────────

add_library(jtext_test_framework INTERFACE)

target_include_directories(jtext_test_framework
        INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/..    # so consumers write #include "framework/foo.hpp"
)

target_compile_features(jtext_test_framework
        INTERFACE
        cxx_std_23
)
