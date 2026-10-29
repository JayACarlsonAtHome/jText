cmake_minimum_required(VERSION 3.26)

project(jText_v002 VERSION 0.1.0 LANGUAGES CXX)

# jText::jText — in-tree structured-text library.
# Migrated from the legacy sibling jText repo; no sibling checkout needed.
# The compiled core (jtext_core) lives here; the public API is the global
# namespace (::JTextWriter, ...), wrapped under jText::v002 by the umbrella.

option(JTEXT_BUILD_EXAMPLES "Build example programs" ON)
option(JTEXT_BUILD_TESTS "Build and register tests" ON)
option(JTEXT_BUILD_MODULES "Build jText.v002 C++23 modules" OFF)

# In-tree compiled core (keeps the target name jtext_core so existing consumers,
# e.g. store's jText persistence, link unchanged).
if(NOT TARGET jtext_core)
    add_library(jtext_core STATIC
        jText.cpp
        src/parser/line.cpp
        src/parser/section.cpp
        src/parser/close.cpp
        src/validator/validator.cpp
        src/export/xml_export.cpp
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

add_library(jText_v002 INTERFACE)
add_library(jText::jText ALIAS jText_v002)

target_include_directories(jText_v002 INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(jText_v002 INTERFACE cxx_std_26)
target_link_libraries(jText_v002 INTERFACE jtext_core)

if(JTEXT_BUILD_MODULES)
    # Enable module dependency scanning for this directory tree (incl. the module
    # test). The umbrella root sets this; setting it here too makes a STANDALONE
    # jText module build work.
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)

    add_library(jtext_v002_module OBJECT)
    add_library(jText::jText_module ALIAS jtext_v002_module)

    # Textual partition modules — include jText.h in the global
    # module fragment, re-export under the namespace, and link the compiled core.
    target_sources(jtext_v002_module
        PUBLIC
            FILE_SET cxx_modules TYPE CXX_MODULES
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
            FILES
                modules/jText.v002.core.cppm
                modules/jText.v002.reader.cppm
                modules/jText.v002.writer.cppm
                modules/jText.v002.cppm
    )
    target_link_libraries(jtext_v002_module PUBLIC jText_v002)

    target_compile_features(jtext_v002_module PUBLIC cxx_std_26)
endif()

if(JTEXT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(JTEXT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Standalone-repo CLI tools (jtext_xtox_sql / jtext_from_sql). In the jac313
# umbrella these live at v002/tools/ instead; here they ship with the library.
# enable_testing() above also covers the tool tests registered in tools/.
add_subdirectory(tools)

if(JTEXT_BUILD_MODULES AND JTEXT_BUILD_TESTS)
    add_subdirectory(tests/module)
endif()

message(STATUS "jText::jText configured (in-tree)")