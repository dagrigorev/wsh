cmake_minimum_required(VERSION 3.24)

project(WSHTerminal VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WSH_ENABLE_WARNINGS "Enable strict warnings" ON)

add_executable(wsh-terminal WIN32
    src/app/main.cpp
    src/app/application.cpp
    src/app/main_window.cpp
    src/core/logger.cpp
    src/core/utf.cpp
    src/platform/clipboard.cpp
    src/config/settings.cpp
    src/terminal/screen_buffer.cpp
    src/terminal/vt_parser.cpp
    src/terminal/input_translator.cpp
    src/conpty/conpty_session.cpp
    src/terminal/terminal_tab.cpp
    src/workspace/workspace.cpp
)

target_include_directories(wsh-terminal PRIVATE src)

target_compile_definitions(wsh-terminal PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(wsh-terminal PRIVATE /std:c++20 /permissive- /EHsc /utf-8)
    if(WSH_ENABLE_WARNINGS)
        target_compile_options(wsh-terminal PRIVATE /W4)
    endif()
else()
    target_compile_options(wsh-terminal PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(wsh-terminal PRIVATE
    d2d1
    dwrite
    dxguid
    user32
    gdi32
    shell32
    ole32
    advapi32
    dwmapi
)

set_target_properties(wsh-terminal PROPERTIES
    OUTPUT_NAME "wsh-terminal"
)

install(TARGETS wsh-terminal RUNTIME DESTINATION .)
install(FILES assets/wsh_profiles.toml DESTINATION .)
