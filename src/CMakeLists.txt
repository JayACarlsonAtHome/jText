# ────────────────────────────────────────────────────────────────
#  jText core library
# ────────────────────────────────────────────────────────────────
#
#  Builds the jtext_core static library. Source files are added here
#  as each parser/validator/emitter component comes online.
#
#  Consumers (tests, future tools) link against jtext_core and write:
#    #include "parser/line.hpp"
#    #include "parser/section.hpp"
#    #include "validator/validator.hpp"
#    #include "emitter.hpp"        (future)
#
# ────────────────────────────────────────────────────────────────

add_library(jtext_core STATIC
        ../jText.cpp
        parser/line.cpp
        parser/section.cpp
        validator/validator.cpp
)

target_include_directories(jtext_core
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(jtext_core
        PUBLIC
        cxx_std_23
)

set_target_properties(jtext_core PROPERTIES
        CXX_STANDARD_REQUIRED   ON
        CXX_EXTENSIONS          OFF
)

