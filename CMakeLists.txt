cmake_minimum_required(VERSION 3.10)
project(apex VERSION 0.1.4 LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE MATCHES Debug)
        add_compile_options(-g -O0)
    else()
        add_compile_options(-O3)
    endif()
endif()

# Build cmark-gfm as a subdirectory
# Disable cmark-gfm tests and programs
set(CMARK_TESTS OFF CACHE BOOL "Build cmark tests" FORCE)
set(CMARK_STATIC ON CACHE BOOL "Build static library" FORCE)
set(CMARK_SHARED ON CACHE BOOL "Build shared library" FORCE)
add_subdirectory(vendor/cmark-gfm)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/vendor/cmark-gfm/src
    ${CMAKE_SOURCE_DIR}/vendor/cmark-gfm/extensions
    ${CMAKE_BINARY_DIR}/vendor/cmark-gfm/src  # For generated headers
)

# Library source files
set(APEX_LIB_SOURCES
    src/apex.c
    src/html_renderer.c
    src/extensions/metadata.c
    src/extensions/wiki_links.c
    src/extensions/math.c
    src/extensions/critic.c
    src/extensions/callouts.c
    src/extensions/includes.c
    src/extensions/toc.c
    src/extensions/abbreviations.c
    src/extensions/emoji.c
    src/extensions/special_markers.c
    src/extensions/ial.c
    src/extensions/definition_list.c
    src/extensions/advanced_footnotes.c
    src/extensions/advanced_tables.c
    src/extensions/html_markdown.c
    src/extensions/table_html_postprocess.c
    src/extensions/inline_footnotes.c
    src/extensions/highlight.c
    src/extensions/header_ids.c
    src/extensions/relaxed_tables.c
    src/pretty_html.c
)

# Build shared library
add_library(apex SHARED ${APEX_LIB_SOURCES})
set_target_properties(apex PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER include/apex/apex.h
)
target_link_libraries(apex libcmark-gfm libcmark-gfm-extensions)

# Build static library
add_library(apex_static STATIC ${APEX_LIB_SOURCES})
set_target_properties(apex_static PROPERTIES
    OUTPUT_NAME apex
    PUBLIC_HEADER include/apex/apex.h
)
target_link_libraries(apex_static libcmark-gfm_static libcmark-gfm-extensions_static)

# CLI executable
add_executable(apex_cli cli/main.c)
set_target_properties(apex_cli PROPERTIES OUTPUT_NAME apex)
target_link_libraries(apex_cli apex_static)

# Installation
install(TARGETS apex apex_static apex_cli
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include/apex
)

# macOS Framework (optional, when building on macOS)
if(APPLE)
    option(BUILD_FRAMEWORK "Build macOS Framework" ON)
    if(BUILD_FRAMEWORK)
        add_library(apex_framework SHARED ${APEX_LIB_SOURCES})
        set_target_properties(apex_framework PROPERTIES
            FRAMEWORK TRUE
            FRAMEWORK_VERSION A
            MACOSX_FRAMEWORK_IDENTIFIER com.brettterpstra.apex
            MACOSX_FRAMEWORK_INFO_PLIST ${CMAKE_SOURCE_DIR}/Info.plist.in
            VERSION ${PROJECT_VERSION}
            SOVERSION ${PROJECT_VERSION_MAJOR}
            PUBLIC_HEADER include/apex/apex.h
            OUTPUT_NAME Apex
        )
        target_link_libraries(apex_framework libcmark-gfm libcmark-gfm-extensions)
        install(TARGETS apex_framework
            FRAMEWORK DESTINATION /Library/Frameworks
        )
    endif()
endif()

# Testing
enable_testing()

# Test runner executable
add_executable(apex_test_runner tests/test_runner.c)
target_link_libraries(apex_test_runner apex_static)

# Add test
add_test(NAME apex_tests COMMAND apex_test_runner)

# Documentation
option(BUILD_DOCS "Build documentation" OFF)
if(BUILD_DOCS)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        add_custom_target(docs
            COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_SOURCE_DIR}/Doxyfile
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Generating API documentation with Doxygen"
        )
    endif()
endif()

# Print configuration
message(STATUS "Apex ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
if(APPLE AND BUILD_FRAMEWORK)
    message(STATUS "macOS Framework: Enabled")
endif()
