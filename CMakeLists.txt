cmake_minimum_required(VERSION 3.26)

# Native `import std;` (JAC313_JTEXT_IMPORT_STD) needs CMake's experimental std-module
# feature ARMED before project() enables CXX. Mirrors Qlite; the umbrella root also
# sets this for in-tree builds. UUID is pinned to the CMake we standardize on (4.3.3).
if(JAC313_JTEXT_IMPORT_STD AND NOT CMAKE_EXPERIMENTAL_CXX_IMPORT_STD)
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
endif()

project(jac313_jText VERSION 0.1.0 LANGUAGES CXX)

# jac313::jText — in-tree structured-text library.
# Migrated from the legacy sibling jText repo; no sibling checkout needed.
# The compiled core (jtext_core) lives here; the public API is the global
# namespace (::JTextWriter, ...), wrapped under jac313::jText::v002 by the umbrella.

option(JAC313_JTEXT_BUILD_EXAMPLES "Build example programs" ON)
option(JAC313_JTEXT_BUILD_TESTS "Build and register tests" ON)
option(JAC313_JTEXT_BUILD_MODULES "Build jac313.jtext.v002 C++23 modules" OFF)
# import-std pilot (g++-15 only). When ON, the module is built MODULE-NATIVE against
# `import std;` (interface + implementation units over the shared jText.api.inc /
# jText.impl.inc fragments) instead of the textual partition modules. Uses CMake's
# native std-module support -> requires CMake >= 3.30. Clang keeps the textual
# partitions (leave OFF). The compiled jtext_core / textual jText.h are unchanged.
option(JAC313_JTEXT_IMPORT_STD "Build the jac313.jtext.v002 module against `import std;` (g++-15 pilot, native CMake std module)" OFF)

# In-tree compiled core (keeps the target name jtext_core so existing consumers,
# e.g. store's jText persistence, link unchanged).
if(NOT TARGET jtext_core)
    add_library(jtext_core STATIC
        jText.cpp
        src/parser/line.cpp
        src/parser/section.cpp
        src/validator/validator.cpp
    )
    target_include_directories(jtext_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}        # jText.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src    # parser/, validator/
    )
    target_compile_features(jtext_core PUBLIC cxx_std_26)
    set_target_properties(jtext_core PROPERTIES
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
endif()

add_library(jac313_jText INTERFACE)
add_library(jac313::jText ALIAS jac313_jText)

target_include_directories(jac313_jText INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(jac313_jText INTERFACE cxx_std_26)
target_link_libraries(jac313_jText INTERFACE jtext_core)

if(JAC313_JTEXT_BUILD_MODULES)
    # Enable module dependency scanning for this directory tree (incl. the module
    # test). The umbrella root sets this; setting it here too makes a STANDALONE
    # jText module build work.
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)

    add_library(jac313_jtext_module OBJECT)
    add_library(jac313::jText_module ALIAS jac313_jtext_module)

    if(JAC313_JTEXT_IMPORT_STD)
        # Module-native `import std;` front-end (gcc pilot). Native CXX_MODULE_STD
        # requires CMake >= 3.30; clang keeps the textual partitions below.
        if(CMAKE_VERSION VERSION_LESS 3.30)
            message(FATAL_ERROR
                "JAC313_JTEXT_IMPORT_STD needs CMake >= 3.30 for native `import std;` "
                "(have ${CMAKE_VERSION}). See the root README \"Newer CMake on Linux Mint\".")
        endif()
        if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            message(FATAL_ERROR
                "JAC313_JTEXT_IMPORT_STD is a g++-15 pilot; compiler is "
                "${CMAKE_CXX_COMPILER_ID}. Leave it OFF for clang (textual partitions).")
        endif()
        # Interface unit (BMI) + implementation unit, over the SAME shared fragments
        # as the textual jText.h/jText.cpp. Self-contained (public API uses no jtext::
        # internals), so this does NOT link jtext_core — only needs the include dir
        # for the fragments.
        target_sources(jac313_jtext_module
            PUBLIC
                FILE_SET cxx_modules TYPE CXX_MODULES
                BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
                FILES modules/jac313.jtext.v002.importstd.cppm
        )
        target_sources(jac313_jtext_module
            PRIVATE modules/jac313.jtext.v002.importstd.impl.cpp)
        target_include_directories(jac313_jtext_module PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        set_target_properties(jac313_jtext_module PROPERTIES CXX_MODULE_STD ON)
        message(STATUS "jac313::jText module: import std ENABLED (native CXX_MODULE_STD)")
    else()
        # Textual partition modules (clang / fallback) — include jText.h in the global
        # module fragment, re-export under the namespace, and link the compiled core.
        target_sources(jac313_jtext_module
            PUBLIC
                FILE_SET cxx_modules TYPE CXX_MODULES
                BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
                FILES
                    modules/jac313.jtext.v002.core.cppm
                    modules/jac313.jtext.v002.reader.cppm
                    modules/jac313.jtext.v002.writer.cppm
                    modules/jac313.jtext.v002.cppm
        )
        target_link_libraries(jac313_jtext_module PUBLIC jac313_jText)
    endif()

    target_compile_features(jac313_jtext_module PUBLIC cxx_std_26)
endif()

if(JAC313_JTEXT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(JAC313_JTEXT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Standalone-repo CLI tools (jtext_xtox_sql / jtext_from_sql). In the jac313
# umbrella these live at v002/tools/ instead; here they ship with the library.
# enable_testing() above also covers the tool tests registered in tools/.
add_subdirectory(tools)

if(JAC313_JTEXT_BUILD_MODULES AND JAC313_JTEXT_BUILD_TESTS)
    add_subdirectory(tests/module)
endif()

message(STATUS "jac313::jText configured (in-tree)")