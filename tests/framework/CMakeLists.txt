# ────────────────────────────────────────────────────────────────
#  jText test framework
# ────────────────────────────────────────────────────────────────
#
#  Session 2: now a STATIC library because runner.cpp and reporter.cpp
#  are compiled sources. The headers (concepts.hpp, type_name.hpp,
#  format_value.hpp, assertion.hpp, runner.hpp, reporter.hpp) are
#  exposed as PUBLIC includes.
#
#  Test executables link against jtext_test_framework via PRIVATE.
#
# ────────────────────────────────────────────────────────────────

add_library(jtext_test_framework STATIC
        runner.cpp
        reporter.cpp
)

target_include_directories(jtext_test_framework
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..    # so consumers write #include "framework/foo.hpp"
)

target_compile_features(jtext_test_framework
        PUBLIC
        cxx_std_23
)

set_target_properties(jtext_test_framework PROPERTIES
        CXX_STANDARD_REQUIRED   ON
        CXX_EXTENSIONS          OFF
)
