################################
## Root CMake File            ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 10 Oct 2026          ##
## Last Modified: 20 Oct 2026 ##
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
# The single compile flag for the real hardware: decides whether Hat_Real is
# built at all (needs RTIMULib). The runtime hat.mode picks real or sim.
option(MTS_WITH_RTIMU "Build Hat_Real (needs RTIMULib)" OFF)

add_compile_definitions($<$<COMPILE_LANGUAGE:C>:_POSIX_C_SOURCE=200809L>)

find_package(Threads REQUIRED)

# Onnx runtime
include(FetchContent)

# Pin the runtime version for reproducible builds.
set(ONNXRUNTIME_VERSION "1.25.1")

# Select the prebuilt CPU runtime for this platform.
if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
  set(ONNXRUNTIME_ARCHIVE
      "onnxruntime-osx-arm64-${ONNXRUNTIME_VERSION}.tgz")
elseif(UNIX AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  set(ONNXRUNTIME_ARCHIVE
      "onnxruntime-linux-aarch64-${ONNXRUNTIME_VERSION}.tgz")
elseif(UNIX AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  set(ONNXRUNTIME_ARCHIVE
      "onnxruntime-linux-x64-${ONNXRUNTIME_VERSION}.tgz")
else()
  message(FATAL_ERROR
      "No configured ONNX Runtime package for this platform: "
      "${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
endif()

FetchContent_Declare(onnxruntime
  URL
    "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/${ONNXRUNTIME_ARCHIVE}"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_GetProperties(onnxruntime)
if(NOT onnxruntime_POPULATED)
  FetchContent_Populate(onnxruntime)
endif()

# Locate the extracted ONNX Runtime package.
if(EXISTS "${onnxruntime_SOURCE_DIR}/include/onnxruntime_cxx_api.h"
   AND EXISTS "${onnxruntime_SOURCE_DIR}/lib")
  # Archive extracted directly into the source directory.
  set(ONNXRUNTIME_ROOT "${onnxruntime_SOURCE_DIR}")
else()
  # Archive contains a nested top-level directory.
  file(GLOB ONNXRUNTIME_DIRS
    LIST_DIRECTORIES TRUE
    "${onnxruntime_SOURCE_DIR}/onnxruntime-*"
  )

  set(ONNXRUNTIME_ROOT "")

  foreach(dir IN LISTS ONNXRUNTIME_DIRS)
    if(EXISTS "${dir}/include/onnxruntime_cxx_api.h"
       AND EXISTS "${dir}/lib")
      set(ONNXRUNTIME_ROOT "${dir}")
      break()
    endif()
  endforeach()

  if(NOT ONNXRUNTIME_ROOT)
    message(FATAL_ERROR
      "Could not locate ONNX Runtime headers and library.\n"
      "Extraction directory: ${onnxruntime_SOURCE_DIR}\n"
      "Inspect the directory contents and check the download URL.")
  endif()
endif()

find_path(ONNXRUNTIME_INCLUDE_DIR
  NAMES onnxruntime_cxx_api.h
  PATHS "${ONNXRUNTIME_ROOT}/include"
  NO_DEFAULT_PATH
  REQUIRED
)

find_library(ONNXRUNTIME_LIBRARY
  NAMES onnxruntime
  PATHS "${ONNXRUNTIME_ROOT}/lib"
  NO_DEFAULT_PATH
  REQUIRED
)

add_library(onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(onnxruntime PROPERTIES
  IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
)

# Json interfacing
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)


# Sanitizers (build.sh --tsan / --asan, each in its own build dir).
# Global on purpose: TSan only works if every target is instrumented.
set(MTS_SANITIZE "" CACHE STRING "Sanitizers: thread, address, undefined, address,undefined (empty = none)")
if (MTS_SANITIZE)
  if (NOT MTS_SANITIZE MATCHES "^(thread|address|undefined|address,undefined)$")
    message(FATAL_ERROR
      "MTS_SANITIZE='${MTS_SANITIZE}': expected thread, address, undefined or address,undefined")
  endif()

  # -fno-sanitize-recover: UBSan otherwise prints and carries on with exit code 0,
  # so a test with undefined behaviour would still pass
  add_compile_options(-fsanitize=${MTS_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=${MTS_SANITIZE})
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(mts_module)

if (MTS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tools/test)
endif()

add_subdirectory(utils)
add_subdirectory(core)
add_subdirectory(app)

if (MTS_BUILD_TESTS)
  add_subdirectory(tests)
endif()

