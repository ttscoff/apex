cmake_minimum_required(VERSION 3.10)
# Set policy version minimum for compatibility with newer CMake versions
# This is especially important for cmark-gfm subdirectory
if(POLICY CMP0000)
    cmake_policy(SET CMP0000 NEW)
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum CMake policy version" FORCE)

project(apex VERSION 0.1.36 LANGUAGES C)

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

# Find libyaml for YAML parsing (arrays, nested structures)
#
# We try a few common pkg-config names in order:
#   - yaml-0.1  (Homebrew / many distros)
#   - yaml      (some distros)
#   - libyaml   (fallback)
#
# If that fails, we fall back to find_package(yaml).
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(YAML yaml-0.1 QUIET)
    if(NOT YAML_FOUND)
        pkg_check_modules(YAML yaml QUIET)
    endif()
    if(NOT YAML_FOUND)
        pkg_check_modules(YAML libyaml QUIET)
    endif()
endif()

if(NOT YAML_FOUND)
    find_package(yaml QUIET)
    if(yaml_FOUND)
        set(YAML_LIBRARIES yaml)
        set(YAML_INCLUDE_DIRS ${yaml_INCLUDE_DIRS})
        set(YAML_FOUND TRUE)
    endif()
endif()

if(NOT YAML_FOUND)
    message(WARNING "libyaml not found. YAML arrays and nested structures will not be supported.")
    message(STATUS "  Install libyaml-dev (Debian/Ubuntu) or libyaml-devel (RHEL/Fedora) or libyaml (Homebrew)")
    set(YAML_FOUND FALSE)
else()
    message(STATUS "libyaml: Found")
    message(STATUS "  Libraries: ${YAML_LIBRARIES}")
    message(STATUS "  Include dirs: ${YAML_INCLUDE_DIRS}")
endif()

# Build cmark-gfm as a subdirectory
# Disable cmark-gfm tests and programs
set(CMARK_TESTS OFF CACHE BOOL "Build cmark tests" FORCE)
set(CMARK_STATIC ON CACHE BOOL "Build static library" FORCE)
set(CMARK_SHARED ON CACHE BOOL "Build shared library" FORCE)
add_subdirectory(vendor/cmark-gfm)

# Silence noisy warnings from vendored cmark-gfm extensions
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    if(TARGET libcmark-gfm-extensions)
        target_compile_options(libcmark-gfm-extensions PRIVATE -Wno-unused-parameter)
    endif()
    if(TARGET libcmark-gfm-extensions_static)
        target_compile_options(libcmark-gfm-extensions_static PRIVATE -Wno-unused-parameter)
    endif()
endif()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/vendor/cmark-gfm/src
    ${CMAKE_SOURCE_DIR}/vendor/cmark-gfm/extensions
    ${CMAKE_BINARY_DIR}/vendor/cmark-gfm/src  # For generated headers
)

if(YAML_FOUND)
    include_directories(${YAML_INCLUDE_DIRS})
    link_directories(${YAML_LIBRARY_DIRS})
    add_compile_definitions(APEX_HAVE_LIBYAML=1)
endif()


# Library source files
set(APEX_LIB_SOURCES
    src/apex.c
    src/plugins_env.c
    src/plugins.c
    src/plugins_remote.c
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
    src/extensions/sup_sub.c
    src/extensions/header_ids.c
    src/extensions/relaxed_tables.c
    src/extensions/citations.c
    src/extensions/index.c
    src/pretty_html.c
)

# Build shared library
add_library(apex SHARED ${APEX_LIB_SOURCES})
set_target_properties(apex PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER include/apex/apex.h
)
if(YAML_FOUND)
    target_link_libraries(apex libcmark-gfm-extensions libcmark-gfm ${YAML_LIBRARIES})
else()
    target_link_libraries(apex libcmark-gfm-extensions libcmark-gfm)
endif()

# Build static library
add_library(apex_static STATIC ${APEX_LIB_SOURCES})
set_target_properties(apex_static PROPERTIES
    OUTPUT_NAME apex
    PUBLIC_HEADER include/apex/apex.h
)
if(YAML_FOUND)
    target_link_libraries(apex_static libcmark-gfm-extensions_static libcmark-gfm_static ${YAML_LIBRARIES})
else()
    target_link_libraries(apex_static libcmark-gfm-extensions_static libcmark-gfm_static)
endif()

# CLI executable
add_executable(apex_cli cli/main.c)
set_target_properties(apex_cli PROPERTIES OUTPUT_NAME apex)
if(YAML_FOUND)
    target_link_libraries(apex_cli apex_static libcmark-gfm-extensions_static libcmark-gfm_static ${YAML_LIBRARIES})
else()
    target_link_libraries(apex_cli apex_static libcmark-gfm-extensions_static libcmark-gfm_static)
endif()

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
        if(YAML_FOUND)
            target_link_libraries(apex_framework libcmark-gfm libcmark-gfm-extensions ${YAML_LIBRARIES})
        else()
            target_link_libraries(apex_framework libcmark-gfm libcmark-gfm-extensions)
        endif()

        # Install framework
        # Use absolute path /Library/Frameworks (standard macOS framework location)
        # Users can override with CMAKE_INSTALL_PREFIX if needed
        if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT OR CMAKE_INSTALL_PREFIX STREQUAL "/usr/local")
            set(FRAMEWORK_INSTALL_DIR "/Library/Frameworks")
        else()
            set(FRAMEWORK_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/Library/Frameworks")
        endif()

        install(TARGETS apex_framework
            FRAMEWORK DESTINATION ${FRAMEWORK_INSTALL_DIR}
            PUBLIC_HEADER DESTINATION ${FRAMEWORK_INSTALL_DIR}/Apex.framework/Headers
        )

        # Explicitly install the header file into the framework Headers directory
        # (PUBLIC_HEADER doesn't automatically install into frameworks)
        install(FILES include/apex/apex.h
            DESTINATION ${FRAMEWORK_INSTALL_DIR}/Apex.framework/Headers
        )

        # Post-install script to copy dylibs into framework and fix install names
        # This runs after the framework is installed
        install(CODE "
            # Determine framework location (same logic as above)
            if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT OR CMAKE_INSTALL_PREFIX STREQUAL \"/usr/local\")
                set(FRAMEWORK_INSTALL_DIR \"/Library/Frameworks\")
            else()
                set(FRAMEWORK_INSTALL_DIR \"\${CMAKE_INSTALL_PREFIX}/Library/Frameworks\")
            endif()
            set(FRAMEWORK_DIR \"\${FRAMEWORK_INSTALL_DIR}/Apex.framework\")
            set(FRAMEWORK_FRAMEWORKS_DIR \"\${FRAMEWORK_DIR}/Frameworks\")
            set(FRAMEWORK_BINARY \"\${FRAMEWORK_DIR}/Apex\")
            set(BUILD_DIR \"${CMAKE_BINARY_DIR}\")

            # Create Frameworks directory inside the framework
            file(MAKE_DIRECTORY \"\${FRAMEWORK_FRAMEWORKS_DIR}\")

            # Find dylibs in build directory
            file(GLOB CMARK_GFM_DYLIB_BUILD \"\${BUILD_DIR}/vendor/cmark-gfm/src/libcmark-gfm*.dylib\")
            file(GLOB CMARK_GFM_EXTENSIONS_DYLIB_BUILD \"\${BUILD_DIR}/vendor/cmark-gfm/extensions/libcmark-gfm-extensions*.dylib\")

            # Copy and fix cmark-gfm dylib
            if(CMARK_GFM_DYLIB_BUILD)
                # Get first file from glob result (in case multiple files found)
                list(GET CMARK_GFM_DYLIB_BUILD 0 CMARK_GFM_DYLIB_SINGLE)
                get_filename_component(CMARK_GFM_NAME \"\${CMARK_GFM_DYLIB_SINGLE}\" NAME)
                set(CMARK_GFM_DEST \"\${FRAMEWORK_FRAMEWORKS_DIR}/\${CMARK_GFM_NAME}\")
                file(COPY \"\${CMARK_GFM_DYLIB_SINGLE}\" DESTINATION \"\${FRAMEWORK_FRAMEWORKS_DIR}\")
                # Fix install name
                execute_process(
                    COMMAND install_name_tool -id \"@rpath/Apex.framework/Frameworks/\${CMARK_GFM_NAME}\" \"\${CMARK_GFM_DEST}\"
                    RESULT_VARIABLE RESULT
                    ERROR_QUIET
                )
                # Fix framework binary reference
                execute_process(
                    COMMAND install_name_tool -change \"@rpath/\${CMARK_GFM_NAME}\" \"@rpath/Apex.framework/Frameworks/\${CMARK_GFM_NAME}\" \"\${FRAMEWORK_BINARY}\"
                    RESULT_VARIABLE RESULT
                    ERROR_QUIET
                )
            endif()

            # Copy and fix cmark-gfm-extensions dylib
            if(CMARK_GFM_EXTENSIONS_DYLIB_BUILD)
                # Get first file from glob result (in case multiple files found)
                list(GET CMARK_GFM_EXTENSIONS_DYLIB_BUILD 0 CMARK_GFM_EXTENSIONS_DYLIB_SINGLE)
                get_filename_component(CMARK_GFM_EXTENSIONS_NAME \"\${CMARK_GFM_EXTENSIONS_DYLIB_SINGLE}\" NAME)
                set(CMARK_GFM_EXTENSIONS_DEST \"\${FRAMEWORK_FRAMEWORKS_DIR}/\${CMARK_GFM_EXTENSIONS_NAME}\")
                file(COPY \"\${CMARK_GFM_EXTENSIONS_DYLIB_SINGLE}\" DESTINATION \"\${FRAMEWORK_FRAMEWORKS_DIR}\")
                # Fix install name
                execute_process(
                    COMMAND install_name_tool -id \"@rpath/Apex.framework/Frameworks/\${CMARK_GFM_EXTENSIONS_NAME}\" \"\${CMARK_GFM_EXTENSIONS_DEST}\"
                    RESULT_VARIABLE RESULT
                    ERROR_QUIET
                )
                # Fix framework binary reference
                execute_process(
                    COMMAND install_name_tool -change \"@rpath/\${CMARK_GFM_EXTENSIONS_NAME}\" \"@rpath/Apex.framework/Frameworks/\${CMARK_GFM_EXTENSIONS_NAME}\" \"\${FRAMEWORK_BINARY}\"
                    RESULT_VARIABLE RESULT
                    ERROR_QUIET
                )
                # Fix cmark-gfm-extensions to reference cmark-gfm from within framework
                if(CMARK_GFM_DYLIB_BUILD)
                    # Get first file from glob result (in case multiple files found)
                    list(GET CMARK_GFM_DYLIB_BUILD 0 CMARK_GFM_DYLIB_SINGLE)
                    get_filename_component(CMARK_GFM_NAME \"\${CMARK_GFM_DYLIB_SINGLE}\" NAME)
                    execute_process(
                        COMMAND install_name_tool -change \"@rpath/\${CMARK_GFM_NAME}\" \"@rpath/Apex.framework/Frameworks/\${CMARK_GFM_NAME}\" \"\${CMARK_GFM_EXTENSIONS_DEST}\"
                        RESULT_VARIABLE RESULT
                        ERROR_QUIET
                    )
                endif()
            endif()
        ")
    endif()
endif()

# Testing
enable_testing()

# Test runner executable
add_executable(apex_test_runner tests/test_runner.c)
target_link_libraries(apex_test_runner apex_static)
target_compile_definitions(apex_test_runner PRIVATE TEST_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/includes")

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

# Man page handling
# Prefer pre-generated man page from repository, generate only if missing
set(MAN_SOURCE ${CMAKE_SOURCE_DIR}/man/apex.1.md)
set(MAN_PREGENERATED ${CMAKE_SOURCE_DIR}/man/apex.1)
set(MAN_OUTPUT ${CMAKE_BINARY_DIR}/apex.1)

# Check if pre-generated man page exists
if(EXISTS ${MAN_PREGENERATED})
    # Use pre-generated man page (copy to build directory for installation)
    add_custom_command(
        OUTPUT ${MAN_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E copy ${MAN_PREGENERATED} ${MAN_OUTPUT}
        DEPENDS ${MAN_PREGENERATED}
        COMMENT "Using pre-generated man page"
    )
    add_custom_target(man_page_copy ALL DEPENDS ${MAN_OUTPUT})
    message(STATUS "Man page: Using pre-generated man/apex.1")
    set(MAN_AVAILABLE TRUE)
else()
    # Try to generate man page from Markdown source
    find_program(PANDOC_EXECUTABLE pandoc)
    find_program(GO_MD2MAN_EXECUTABLE go-md2man)

    if(PANDOC_EXECUTABLE)
        add_custom_command(
            OUTPUT ${MAN_OUTPUT}
            COMMAND ${PANDOC_EXECUTABLE} -s -t man -o ${MAN_OUTPUT} ${MAN_SOURCE}
            DEPENDS ${MAN_SOURCE}
            COMMENT "Generating man page from Markdown using pandoc"
        )
        add_custom_target(man_page ALL DEPENDS ${MAN_OUTPUT})
        message(STATUS "Man page: Will be generated using pandoc")
        set(MAN_AVAILABLE TRUE)
    elseif(GO_MD2MAN_EXECUTABLE)
        add_custom_command(
            OUTPUT ${MAN_OUTPUT}
            COMMAND ${GO_MD2MAN_EXECUTABLE} -in=${MAN_SOURCE} -out=${MAN_OUTPUT}
            DEPENDS ${MAN_SOURCE}
            COMMENT "Generating man page from Markdown using go-md2man"
        )
        add_custom_target(man_page ALL DEPENDS ${MAN_OUTPUT})
        message(STATUS "Man page: Will be generated using go-md2man")
        set(MAN_AVAILABLE TRUE)
    else()
        message(WARNING "Pre-generated man page not found and neither pandoc nor go-md2man available.")
        message(STATUS "  Man page will not be installed. Generate it with 'make man' before building.")
        set(MAN_AVAILABLE FALSE)
    endif()
endif()

# Install man page
if(MAN_AVAILABLE)
    # Determine man page section (1 for user commands)
    set(MAN_SECTION 1)
    install(FILES ${MAN_OUTPUT}
        DESTINATION share/man/man${MAN_SECTION}
        RENAME apex.${MAN_SECTION}
    )
endif()

# Print configuration
message(STATUS "Apex ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
if(APPLE AND BUILD_FRAMEWORK)
    message(STATUS "macOS Framework: Enabled")
endif()
