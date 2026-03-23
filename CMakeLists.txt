cmake_minimum_required(VERSION 3.20)

project(Wsh
    VERSION 1.0.0
    DESCRIPTION "Wsh – ZSH-compatible Windows Terminal Emulator"
    LANGUAGES C CXX
)

# ── C standard ─────────────────────────────────────────────────────────────
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Build types ─────────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ── Compiler / platform guards ──────────────────────────────────────────────
if(NOT WIN32)
    message(FATAL_ERROR "Wsh targets Windows only (ConPTY, Direct2D, DirectWrite).")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "Wsh requires MSVC (cl.exe). Use the VS Developer Command Prompt.")
endif()

# ── Global MSVC flags ────────────────────────────────────────────────────────
add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    # Expose version to source code
    WSH_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    WSH_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    WSH_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

# Use the static MSVC runtime to avoid external VC++ CRT side-by-side
# dependencies in NMake/standalone builds.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_options(
    /W4         # Warning level 4
    /WX-        # Warnings not errors (we audit manually)
    /utf-8      # Source as UTF-8
    /wd4005     # Macro redefinition (WIN32_LEAN_AND_MEAN set by both CMake and headers)
    /wd4100     # Unreferenced formal parameter (common in stubs)
    /wd4996     # Deprecated POSIX names (we use _snprintf deliberately)
    /wd4200     # Nonstandard extension: zero-sized array
    $<$<CONFIG:Release>:/O2>
    $<$<CONFIG:Release>:/GL>    # Whole-program optimisation
    $<$<CONFIG:Debug>:/Od>
    $<$<CONFIG:Debug>:/Zi>
)

add_link_options(
    $<$<CONFIG:Release>:/LTCG>
)

# ── Sub-libraries (each a static lib — SOLID: one reason to change) ──────────
add_subdirectory(src/core)
add_subdirectory(src/shell)
add_subdirectory(src/terminal)
add_subdirectory(src/platform)

# ── Main executable ──────────────────────────────────────────────────────────
add_subdirectory(src)

# ── Tests ────────────────────────────────────────────────────────────────────
option(WSH_BUILD_TESTS "Build unit tests" ON)
if(WSH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Install rules ────────────────────────────────────────────────────────────
install(TARGETS Wsh RUNTIME DESTINATION bin)
install(DIRECTORY config/ DESTINATION share/Wsh)

# ── CPack (ZIP distribution) ─────────────────────────────────────────────────
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "Wsh")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
include(CPack)

