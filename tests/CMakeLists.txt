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

add_executable(test_repl test_repl.c test_helpers.c ${CMAKE_SOURCE_DIR}/src/repl.c)
target_include_directories(test_repl PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/shell
    ${CMAKE_SOURCE_DIR}/src/platform
    ${CMAKE_SOURCE_DIR}/src/terminal
)
target_link_libraries(test_repl PRIVATE wsh_core wsh_shell wsh_platform wsh_terminal)
if(MSVC)
    target_compile_options(test_repl PRIVATE /TP)
    set_target_properties(test_repl PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE /MANIFEST:NO")
endif()
add_test(NAME test_repl COMMAND test_repl)

# Companion utility smoke tests: every utility must expose a stable help surface.
function(add_wsh_tool_help_test tool)
    add_test(NAME test_tool_${tool}_help COMMAND ${tool} --help)
endfunction()
add_wsh_tool_help_test(ls)
add_wsh_tool_help_test(md)
add_wsh_tool_help_test(tree)
add_wsh_tool_help_test(wshinit)
add_wsh_tool_help_test(cat)
add_wsh_tool_help_test(pwd)
add_wsh_tool_help_test(cp)
add_wsh_tool_help_test(mv)
add_wsh_tool_help_test(rm)
add_wsh_tool_help_test(mkdir)
add_wsh_tool_help_test(rmdir)
add_wsh_tool_help_test(touch)
add_wsh_tool_help_test(head)
add_wsh_tool_help_test(tail)
add_wsh_tool_help_test(wc)
add_wsh_tool_help_test(grep)
add_wsh_tool_help_test(sort)
add_wsh_tool_help_test(uniq)
add_wsh_tool_help_test(basename)
add_wsh_tool_help_test(dirname)
add_wsh_tool_help_test(whoami)
add_wsh_tool_help_test(hostname)
add_wsh_tool_help_test(uname)
add_wsh_tool_help_test(date)
add_wsh_tool_help_test(clear)
add_wsh_tool_help_test(which)
add_wsh_tool_help_test(sleep)
add_wsh_tool_help_test(yes)
add_wsh_tool_help_test(true)
add_wsh_tool_help_test(false)
add_wsh_tool_help_test(echo)
add_wsh_tool_help_test(env)
add_wsh_tool_help_test(printenv)
add_wsh_tool_help_test(more)
add_wsh_tool_help_test(less)
add_wsh_tool_help_test(find)
add_wsh_tool_help_test(tee)
add_wsh_tool_help_test(cut)

add_test(NAME test_tool_pwd_runs COMMAND pwd)
add_test(NAME test_tool_true_runs COMMAND true)
add_test(NAME test_tool_false_runs COMMAND false)
set_tests_properties(test_tool_false_runs PROPERTIES WILL_FAIL TRUE)
