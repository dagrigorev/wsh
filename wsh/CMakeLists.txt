# CMakeList.txt : CMake project for wsh, include source and define
# project specific logic here.
#

# Add source to this project's executable.
add_executable (wsh "wsh.cpp" "wsh.h" "zwinshell.h" "zwinshell.cpp" "utils.h" "utils.cpp" "history.h" "history.cpp" "completer.h" "completer.cpp" "alias.h" "alias.cpp" "config.h" "config.cpp" "virtual_env.h" "virtual_env.cpp")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET wsh PROPERTY CXX_STANDARD 20)
endif()
