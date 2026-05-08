# ── Unit tests ────────────────────────────────────────────────────────────────
# Each test is a standalone console executable that returns 0 on pass.

function(add_wsh_test name src)
    add_executable(${name} ${src} test_helpers.c)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/shell
        ${CMAKE_SOURCE_DIR}/src/platform
        ${CMAKE_SOURCE_DIR}/src/terminal
    )
    target_link_libraries(${name} PRIVATE wsh_core wsh_shell wsh_platform wsh_terminal)
    if(MSVC)
        # Compile as C++ so the auto-registration struct trick in test_helpers.h works
        target_compile_options(${name} PRIVATE /TP)
        # Console subsystem — tests use main(), not WinMain
        set_target_properties(${name} PROPERTIES
            LINK_FLAGS "/SUBSYSTEM:CONSOLE /MANIFEST:NO")
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_wsh_test(test_arena        test_arena.c)
add_wsh_test(test_str_util     test_str_util.c)
add_wsh_test(test_lexer        test_lexer.c)
add_wsh_test(test_parser       test_parser.c)
add_wsh_test(test_expand       test_expand.c)
add_wsh_test(test_builtins     test_builtins.c)
add_wsh_test(test_history      test_history.c)

add_wsh_test(test_prompt       test_prompt.c)

add_wsh_test(test_config       test_config.c)
add_wsh_test(test_completion   test_completion.c)
add_wsh_test(test_executor     test_executor.c)
add_wsh_test(test_screen       test_screen.c)
add_wsh_test(test_vt_parser    test_vt_parser.c)
add_wsh_test(test_layout       test_layout.c)
