################################
## Root CMake File            ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 10 Oct 2026          ##
## Last Modified: 15 Oct 2026 ##
################################

cmake_minimum_required(VERSION 3.20)
project(
  mts
  VERSION 0.1.0
  LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MTS_BUILD_TESTS "Build unit tests" ON)
option(VERBOSE_OUTPUT "Enable verbose output" OFF)

add_compile_definitions($<$<COMPILE_LANGUAGE:C>:_POSIX_C_SOURCE=200809L>)

find_package(Threads REQUIRED)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(mts_module)

if (MTS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tools/test)
endif()

add_subdirectory(utils)
add_subdirectory(app)

