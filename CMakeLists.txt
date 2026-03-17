cmake_minimum_required(VERSION 3.27)

project(wsh_terminal
  VERSION 0.1.0
  DESCRIPTION "Windows-first terminal emulator and hybrid shell"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WSH_BUILD_TESTS "Build tests" ON)
option(WSH_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

include(cmake/standard-project-settings.cmake)
include(cmake/warnings.cmake)

add_subdirectory(src)

if(WSH_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
