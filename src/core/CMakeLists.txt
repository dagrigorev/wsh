# ── wsh_core ─────────────────────────────────────────────────────────────────
# Single Responsibility: memory, strings, logging, arena allocation.
# Other modules depend on this; it depends on nothing but the Windows API.
add_library(wsh_core STATIC
    arena.c
    str_util.c
    log.c
    path_util.c
)

target_include_directories(wsh_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(wsh_core PRIVATE WSH_CORE_BUILD)
