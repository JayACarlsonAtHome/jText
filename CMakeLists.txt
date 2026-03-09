cmake_minimum_required(VERSION 3.26)
project(JText
        VERSION 0.1.0
        DESCRIPTION "Lightweight structured text format library & tool (JText)"
        LANGUAGES CXX
)

# ────────────────────────────────────────────────────────────────
#  C++ standard
# ────────────────────────────────────────────────────────────────

set(CMAKE_CXX_STANDARD          23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ────────────────────────────────────────────────────────────────
#  Compiler warnings
# ────────────────────────────────────────────────────────────────

if (MSVC)
    add_compile_options(
            /W4
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
    )
else()
    add_compile_options(
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Wdouble-promotion
            -Wshadow
            -Wformat=2
            -Wmissing-format-attribute
    )
endif()

# Better debug experience
add_compile_options(
        $<$<CONFIG:DEBUG>:-g3>
        $<$<CONFIG:DEBUG>:-O0>
)

# ────────────────────────────────────────────────────────────────
#  Library (core logic)
# ────────────────────────────────────────────────────────────────

add_library(jText_core STATIC
        jText.cpp
)

target_include_directories(jText_core
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(jText_core PROPERTIES
        CXX_STANDARD_REQUIRED     ON
        CXX_EXTENSIONS            OFF
)

# ────────────────────────────────────────────────────────────────
#  Main executable (demo / tool)
# ────────────────────────────────────────────────────────────────

add_executable(jText
        main.cpp
)

target_link_libraries(jText PRIVATE
        jText_core
)

set_target_properties(jText PROPERTIES
        CXX_STANDARD_REQUIRED     ON
        CXX_EXTENSIONS            OFF
        EXPORT_COMPILE_COMMANDS   ON
)

# ────────────────────────────────────────────────────────────────
#  Helpers / future expansion points
# ────────────────────────────────────────────────────────────────

add_custom_target(check-compile ALL DEPENDS jText)

# Uncomment when you add tests:
# enable_testing()
# add_executable(jtext_tests tests/test_parser.cpp tests/test_write.cpp ...)
# target_link_libraries(jtext_tests PRIVATE jtext_core)
# add_test(NAME JText.Parser COMMAND jtext_tests)

# ────────────────────────────────────────────────────────────────
#  Summary
# ────────────────────────────────────────────────────────────────

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

message(STATUS "Project:      ${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS "C++ standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Build type:   ${CMAKE_BUILD_TYPE}")