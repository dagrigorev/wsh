# ── Unit tests ────────────────────────────────────────────────────────────────
# Each test is a standalone console executable that returns 0 on pass.
# CTest runs them via `ctest --output-on-failure`.

function(add_wsh_test name src)
    add_executable(${name} ${src} test_helpers.c)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/shell
        ${CMAKE_SOURCE_DIR}/src/platform
    )
    target_link_libraries(${name} PRIVATE wsh_core wsh_shell wsh_platform)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_wsh_test(test_arena        test_arena.c)
add_wsh_test(test_str_util     test_str_util.c)
add_wsh_test(test_lexer        test_lexer.c)
add_wsh_test(test_parser       test_parser.c)
add_wsh_test(test_expand       test_expand.c)
add_wsh_test(test_builtins     test_builtins.c)
add_wsh_test(test_history      test_history.c)
