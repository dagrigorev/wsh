# ── wsh_shell ─────────────────────────────────────────────────────────────────
# Single Responsibility: shell language semantics only.
# No rendering, no Win32 GUI — those live in wsh_terminal / wsh_platform.
add_library(wsh_shell STATIC
    env.c
    lexer.c
    parser.c
    executor.c
    expand.c
    builtins.c
    history.c
    jobs.c
    completion.c
    shell_ctx.c
    scheduler.c
)

target_include_directories(wsh_shell PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(wsh_shell PUBLIC wsh_core)
