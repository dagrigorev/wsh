# ── wsh_terminal ──────────────────────────────────────────────────────────────
# Single Responsibility: VT/ANSI emulation, screen buffer, rendering.
# Knows nothing about shell semantics or ConPTY.
add_library(wsh_terminal STATIC
    screen.c
    vt_parser.c
    renderer.cpp
    font.cpp
    guid_init.cpp
)

target_include_directories(wsh_terminal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(wsh_terminal PUBLIC wsh_core d2d1 dwrite)
