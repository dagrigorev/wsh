# ── wsh_platform ──────────────────────────────────────────────────────────────
# Single Responsibility: Win32-specific platform services.
# Config loading, PTY management, keyboard translation.
add_library(wsh_platform STATIC
    config.c
    pty.c
    input.c
)

target_include_directories(wsh_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(wsh_platform PUBLIC wsh_core)
