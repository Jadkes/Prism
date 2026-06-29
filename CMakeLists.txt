# prism - C/C++ Error Detection Tool
#
# Build:  cmake -B build && cmake --build build
# Test:   ctest --test-dir build
# Install: cmake --install build

cmake_minimum_required(VERSION 3.16)
project(prism
    VERSION 1.4
    DESCRIPTION "C/C++ Error Detection Tool"
    LANGUAGES C
)

# ── C standard ──────────────────────────────────────────────
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)          # gnu11: strdup, strcasecmp, etc.

# ── Build type ──────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

# ── Sources ─────────────────────────────────────────────────
set(SOURCES
    prism.c
    ast_backend.c
    annotation.c
    check_engine.c
    sym_exec.c
    detect_libs.c
)

# ── Find libclang (for ast_backend) ────────────────────────
find_path(LIBCLANG_INCLUDE_DIR
    NAMES clang-c/Index.h
    HINTS
        /usr/include
        /usr/local/include
        /usr/lib/llvm-*/include
        /usr/lib64/llvm-*/include
)

find_library(LIBCLANG_LIBRARY
    NAMES clang libclang
    HINTS
        /usr/lib
        /usr/lib64
        /usr/local/lib
        /usr/lib/llvm-*/lib
        /usr/lib64/llvm-*/lib
)

if(NOT LIBCLANG_INCLUDE_DIR)
    message(FATAL_ERROR
        "libclang headers not found (clang-c/Index.h).\n"
        "Install clang-devel or llvm-devel:\n"
        "  Fedora: sudo dnf install clang-devel\n"
        "  Debian: sudo apt install libclang-dev\n"
        "  Arch:   sudo pacman -S clang")
endif()

if(NOT LIBCLANG_LIBRARY)
    message(FATAL_ERROR
        "libclang library not found.\n"
        "Install clang-devel or llvm-devel (same packages as above).")
endif()

message(STATUS "Found libclang: ${LIBCLANG_LIBRARY}")
message(STATUS "Found libclang includes: ${LIBCLANG_INCLUDE_DIR}")

# ── Build target ────────────────────────────────────────────
add_executable(prism ${SOURCES})

target_include_directories(prism PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBCLANG_INCLUDE_DIR}
)

target_link_libraries(prism PRIVATE
    ${LIBCLANG_LIBRARY}
)

# Base warning flags
target_compile_options(prism PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wno-format-truncation       # snprintf into size-1 buffer (ultra mode pipe protocol)
)

# Per-build-type optimization / debug
target_compile_options(prism PRIVATE
    $<$<CONFIG:Debug>:-O0;-g;-DDEBUG>
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:RelWithDebInfo>:-O2;-g>
    $<$<CONFIG:MinSizeRel>:-Os>
)

# ast_backend.c: strncpy(..., sizeof-1) from libclang API — intentional
set_property(SOURCE ast_backend.c APPEND PROPERTY COMPILE_OPTIONS
    -Wno-stringop-truncation
)

# ── Install ─────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS prism
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES
    hooks/pre-commit
    DESTINATION ${CMAKE_INSTALL_DATADIR}/prism/hooks
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                GROUP_READ GROUP_EXECUTE
                WORLD_READ WORLD_EXECUTE
)

install(PROGRAMS
    install_deps.sh
    DESTINATION ${CMAKE_INSTALL_DATADIR}/prism
)

# ── Git hooks install ───────────────────────────────────────
if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
    add_custom_target(install-hooks
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/hooks/pre-commit"
            "${CMAKE_SOURCE_DIR}/.git/hooks/pre-commit"
        COMMAND ${CMAKE_COMMAND} -E chmod +x
            "${CMAKE_SOURCE_DIR}/.git/hooks/pre-commit"
        COMMENT "Installing git pre-commit hook"
    )
endif()

# ── Smoke tests (CTest) ─────────────────────────────────────
if(BUILD_TESTING)
    enable_testing()

    file(WRITE ${CMAKE_BINARY_DIR}/_trivial.c "int main(void){return 0;}\n")

    function(add_c_test name source flag pattern)
        if(flag)
            add_test(NAME "smoke/${name}"
                COMMAND prism ${flag} "${source}"
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            )
        else()
            add_test(NAME "smoke/${name}"
                COMMAND prism "${source}"
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            )
        endif()
        set_tests_properties("smoke/${name}" PROPERTIES
            PASS_REGULAR_EXPRESSION "${pattern}"
            TIMEOUT 30
        )
    endfunction()

    add_c_test(clean  "${CMAKE_BINARY_DIR}/_trivial.c"   ""  "No errors detected")
    add_c_test(hell   hell_code.c                         ""  "detected|error")
endif()

# ── Convenience ─────────────────────────────────────────────
add_custom_target(run
    COMMAND prism --help
    DEPENDS prism
)

add_custom_target(deps
    COMMAND bash "${CMAKE_SOURCE_DIR}/install_deps.sh"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
