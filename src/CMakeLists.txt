# ── Wsh executable ────────────────────────────────────────────────────────────
add_executable(Wsh WIN32
    main.cpp
    window.cpp
    renderer.cpp
    window.cpp
    repl.c
    ${CMAKE_SOURCE_DIR}/res/Wsh.rc
)

target_include_directories(Wsh PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
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
    kernel32 user32 gdi32 shell32
    advapi32 shlwapi ole32 oleaut32
)

# Embed version in the preprocessor
target_compile_definitions(Wsh PRIVATE
    WSH_VERSION="${CMAKE_PROJECT_VERSION}"
)

# Post-build: copy default configs next to the exe so install.bat can find them
add_custom_command(TARGET Wsh POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/config
            $<TARGET_FILE_DIR:Wsh>/config
    COMMENT "Copying default config files"
)
