# ── Unit tests ────────────────────────────────────────────────────────────────
# Each test is a standalone console executable that returns 0 on pass.

function(add_wisp_test name src)
    add_executable(${name} ${src} test_helpers.c)
    # Force C++ for both source and helpers so the auto-registration struct trick works.
    # set_source_files_properties + LANGUAGE CXX is the correct way for VS generator;
    # target_compile_options(/TP) is silently dropped by the VS project generator.
    set_source_files_properties(${src} test_helpers.c PROPERTIES LANGUAGE CXX)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/platform
        ${CMAKE_SOURCE_DIR}/src/terminal
    )
    target_link_libraries(${name} PRIVATE wisp_core wisp_platform wisp_terminal)
    if(MSVC)
        # Console subsystem — tests use main(), not WinMain
        set_target_properties(${name} PROPERTIES
            LINK_FLAGS "/SUBSYSTEM:CONSOLE /MANIFEST:NO")
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_wisp_test(test_arena      test_arena.c)
add_wisp_test(test_str_util   test_str_util.c)
add_wisp_test(test_config     test_config.c)
add_wisp_test(test_screen     test_screen.c)
add_wisp_test(test_vt_parser  test_vt_parser.c)
add_wisp_test(test_layout     test_layout.c)
add_wisp_test(test_size      test_size.cpp)
