# ── Wsh executable ────────────────────────────────────────────────────────────
add_executable(Wsh WIN32
    main.cpp
    window.cpp
    repl.c
    ${CMAKE_SOURCE_DIR}/res/Wsh.rc
)

# Wsh is a GUI app: Windows subsystem, no auto-manifest (RC file embeds one)
if(MSVC)
    set_target_properties(Wsh PROPERTIES
        LINK_FLAGS "/MANIFEST:NO")
endif()

target_include_directories(Wsh PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/shell
    ${CMAKE_SOURCE_DIR}/src/terminal
    ${CMAKE_SOURCE_DIR}/src/platform
)

target_link_libraries(Wsh PRIVATE
    wsh_core
    wsh_shell
    wsh_terminal
    wsh_platform
    wsh_ai
    kernel32 user32 gdi32 shell32
    advapi32 shlwapi ole32 oleaut32
    d2d1 dwrite
)

target_compile_definitions(Wsh PRIVATE
    WSH_VERSION="${CMAKE_PROJECT_VERSION}"
)

set_target_properties(Wsh PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WSH_DIST_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WSH_DIST_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WSH_DIST_DIR}"
)

add_custom_command(TARGET Wsh POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/config
            $<TARGET_FILE_DIR:Wsh>/config
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/themes
            $<TARGET_FILE_DIR:Wsh>/themes
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/man
            $<TARGET_FILE_DIR:Wsh>/man
    COMMENT "Copying default config, themes and man pages"
)
