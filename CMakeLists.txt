cmake_minimum_required(VERSION 3.26)
project(jText
        VERSION 0.4.0
        DESCRIPTION "Lightweight structured text format library & tool (jText)"
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

# Default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# ────────────────────────────────────────────────────────────────
#  Subdirectories
# ────────────────────────────────────────────────────────────────

add_subdirectory(src)
add_subdirectory(tests)

# ────────────────────────────────────────────────────────────────
#  Early prototype tool (jtext_process)
#  Takes: <data_path> <base_name> <output_file>
# ────────────────────────────────────────────────────────────────

if(NOT TS_STORE_BUILDING_JTEXT_TOOLS)
add_executable(jtext_process
    src/tool/jtext_process.cpp
)

target_link_libraries(jtext_process PRIVATE jtext_core)

target_include_directories(jtext_process PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_features(jtext_process PRIVATE cxx_std_23)

add_executable(jtext_retrieve
    src/tool/jtext_retrieve.cpp
)

# No link to jtext_core needed yet (it drives psql externally for v1)
target_include_directories(jtext_retrieve PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_features(jtext_retrieve PRIVATE cxx_std_23)
endif()

# ────────────────────────────────────────────────────────────────
#  Summary
# ────────────────────────────────────────────────────────────────

message(STATUS "Project:      ${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS "C++ standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Build type:   ${CMAKE_BUILD_TYPE}")

add_executable(jText_test tests/test_smoke.cpp)

