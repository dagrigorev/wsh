add_library(wsh_common
  common/src/logging.cpp
  common/src/strings.cpp
  common/src/unicode.cpp
  common/src/fs.cpp
)

target_include_directories(wsh_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common/include
)

target_compile_features(wsh_common PUBLIC cxx_std_20)
wsh_set_project_warnings(wsh_common)

add_library(wsh_platform_windows
  platform/windows/src/win_error.cpp
  platform/windows/src/handle.cpp
  platform/windows/src/pipe.cpp
  platform/windows/src/process.cpp
  platform/windows/src/environment.cpp
  platform/windows/src/clipboard.cpp
  platform/windows/src/keyboard.cpp
  platform/windows/src/paths.cpp
  platform/windows/src/conpty_api.cpp
)

target_include_directories(wsh_platform_windows PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/platform/windows/include
)

target_link_libraries(wsh_platform_windows PUBLIC
  wsh_common
)

target_compile_definitions(wsh_platform_windows PUBLIC
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)

wsh_set_project_warnings(wsh_platform_windows)

add_library(wsh_vt
  vt/src/ansi_parser.cpp
)

target_include_directories(wsh_vt PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/vt/include
)

target_link_libraries(wsh_vt PUBLIC
  wsh_common
  wsh_terminal
)

wsh_set_project_warnings(wsh_vt)

target_include_directories(wsh_vt PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/vt/include
)

target_link_libraries(wsh_vt PUBLIC
  wsh_common
)

wsh_set_project_warnings(wsh_vt)

add_library(wsh_terminal
  terminal/src/terminal_session.cpp
  terminal/src/screen_buffer.cpp
  terminal/src/scrollback_buffer.cpp
)

target_include_directories(wsh_terminal PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/terminal/include
)

target_link_libraries(wsh_terminal PUBLIC
  wsh_common
  wsh_vt
  wsh_conpty
)

wsh_set_project_warnings(wsh_terminal)

target_include_directories(wsh_terminal PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/terminal/include
)

target_link_libraries(wsh_terminal PUBLIC
  wsh_common
  wsh_vt
)

wsh_set_project_warnings(wsh_terminal)

add_library(wsh_conpty
  conpty/src/conpty_session.cpp
)

target_include_directories(wsh_conpty PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/conpty/include
)

target_link_libraries(wsh_conpty PUBLIC
  wsh_common
  wsh_platform_windows
)

wsh_set_project_warnings(wsh_conpty)

add_library(wsh_config
  config/src/settings_loader.cpp
)

target_include_directories(wsh_config PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/config/include
)

target_link_libraries(wsh_config PUBLIC
  wsh_common
)

wsh_set_project_warnings(wsh_config)

add_library(wsh_parser
  parser/src/tokenizer.cpp
  parser/src/parser.cpp
)

target_include_directories(wsh_parser PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/parser/include
)

target_link_libraries(wsh_parser PUBLIC
  wsh_common
)

wsh_set_project_warnings(wsh_parser)

add_library(wsh_builtins
  builtins/src/builtin_registry.cpp
)

target_include_directories(wsh_builtins PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/builtins/include
)

target_link_libraries(wsh_builtins PUBLIC
  wsh_common
  wsh_parser
)

wsh_set_project_warnings(wsh_builtins)

add_library(wsh_shell
  shell/src/shell_session.cpp
)

target_include_directories(wsh_shell PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/shell/include
)

target_link_libraries(wsh_shell PUBLIC
  wsh_common
  wsh_parser
  wsh_builtins
)

wsh_set_project_warnings(wsh_shell)

add_library(wsh_workspace INTERFACE)
target_include_directories(wsh_workspace INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/workspace/include
)

add_library(wsh_ui
  ui/src/app_window.cpp
  ui/src/text_renderer.cpp
  ui/src/selection_model.cpp
  ui/src/keymap.cpp
)

target_include_directories(wsh_ui PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/include
)

target_link_libraries(wsh_ui PUBLIC
  wsh_common
  wsh_terminal
)

target_link_libraries(wsh_ui PRIVATE
  dwrite
  d2d1
  user32
  gdi32
)

wsh_set_project_warnings(wsh_ui)

add_executable(wsh_app
  app/main.cpp
)

target_link_libraries(wsh_app PRIVATE
  wsh_common
  wsh_platform_windows
  wsh_conpty
  wsh_terminal
  wsh_vt
  wsh_ui
  wsh_config
  wsh_shell
  wsh_workspace
)

set_target_properties(wsh_app PROPERTIES
  OUTPUT_NAME "wsh"
)
