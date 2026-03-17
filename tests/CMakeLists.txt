add_executable(wsh_tests
  common/strings_tests.cpp
  common/unicode_tests.cpp
  parser/tokenizer_tests.cpp
  config/settings_tests.cpp
)

target_link_libraries(wsh_tests PRIVATE
  wsh_common
  wsh_parser
  wsh_config
)

add_test(NAME wsh_tests COMMAND wsh_tests)
