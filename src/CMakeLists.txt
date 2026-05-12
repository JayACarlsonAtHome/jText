# ────────────────────────────────────────────────────────────────
#  jText core library (Session 3+)
# ────────────────────────────────────────────────────────────────
#
#  This directory will hold the jText core library implementation.
#  Session 1 establishes the test framework; Session 3 starts
#  populating this directory with the parser, validator, and
#  emitter targeting SPEC.md v0.4.
#
#  When source files are added, the library target will look like:
#
#    add_library(jtext_core STATIC
#        parser/line.cpp
#        parser/section.cpp
#        ...
#    )
#
#    target_include_directories(jtext_core
#        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
#    )
#
# For now, this CMakeLists.txt is intentionally empty.
