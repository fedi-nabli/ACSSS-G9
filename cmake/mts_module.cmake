################################
## CMake lib helper functions ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 15 Oct 2026          ##
## Last Modified: 15 Oct 2026 ##
################################

include_guard(GLOBAL)

set(MTS_WARNING_FLAGS -Wall -Wextra -Wpedantic)

# mts_add_module(<lib> <name>
#   [SOURCES        file...] no sources means header only (INTERFACE) library
#   [DEPS           target...] public: anyone linking this module get them too
#   [PRIVATE_DEPS   target...] used by the implementation only
#   [TESTS          file...]) sources of one test executable, registered with CTest
#
# Creates:
#   mts_<lib>_<name>        static library or interface is header only
#   mts::<lib>::<name>      alias, the name everyone else links against
#   mts_<lib>_<name>_tests  test executable, CTest name "<lib>.<name>" (only when MTS_BUILD_TESTS is on and tests are given)
#
# Must be called from <lib>/src/<name>/CMakeLists.txt and
# <lib>/include/<lib>/<name>/ must exist, both are checked at configure time
function(mts_add_module lib name)
  cmake_parse_arguments(PARSE_ARGV 2 arg "" "" "SOURCES;DEPS;PRIVATE_DEPS;TESTS")
  if (arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "mts_add_module(${lib} ${name}): unknown arguments: ${arg_UNPARSED_ARGUMENTS}")
  endif()

  set(lib_root "${PROJECT_SOURCE_DIR}/${lib}")
  set(include_dir "${lib_root}/include")
  set(target "mts_${lib}_${name}")

  # Layout check
  if (NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL "${lib_root}/src/${name}")
    message(FATAL_ERROR
      "mts_add_module(${lib} ${name}) called from ${CMAKE_CURRENT_SOURCE_DIR}, "
      "expected ${lib_root}/src/${name}")
  endif()
  if (NOT IS_DIRECTORY "${include_dir}/${lib}/${name}")
    message(FATAL_ERROR
      "mts_add_module(${lib} ${name}): missing header folder ${include_dir}/${lib}/${name}/")
  endif()

  if (arg_SOURCES)
    add_library(${target} STATIC ${arg_SOURCES})
    target_include_directories(${target} PUBLIC "${include_dir}")
    target_link_libraries(${target} PUBLIC ${arg_DEPS} PRIVATE ${arg_PRIVATE_DEPS})
    target_compile_options(${target} PRIVATE ${MTS_WARNING_FLAGS})
  else()
    # Header only, nothing is compiled so nothing can be private
    if (arg_PRIVATE_DEPS)
      message(FATAL_ERROR
        "mts_add_module(${lib} ${name}): header only module cannot have PRIVATE_DEPS")
    endif()
    add_library(${target} INTERFACE)
    target_include_directories(${target} INTERFACE "${include_dir}")
    target_link_libraries(${target} INTERFACE ${arg_DEPS})
  endif()
  add_library(mts::${lib}::${name} ALIAS ${target})

  # Remembered for mts_add_umbrella
  set_property(GLOBAL APPEND PROPERTY MTS_${lib}_MODULES ${target})

  if (MTS_BUILD_TESTS AND arg_TESTS)
    set(test_target "${target}_tests")
    add_executable(${test_target} ${arg_TESTS})
    target_link_libraries(${test_target} PRIVATE ${target} mts::test)
    target_compile_options(${test_target} PRIVATE ${MTS_WARNING_FLAGS})
    add_test(NAME "${lib}.${name}" COMMAND ${test_target})
  endif()
endfunction()

# mts_add_umbrella(<lib>)
#
# Creates mts_<lib> / mts::<lib>, an INTERFACE target linking every module
# registered so far for <lib>. Call it at the end of <lib>/CMakeLists.txt
# after all add_subdirectory(src/<module>) lines
function(mts_add_umbrella lib)
  get_property(modules GLOBAL PROPERTY MTS_${lib}_MODULES)
  
  if (NOT modules)
    message(FATAL_ERROR
      "mts_add_umbrella(${lib}): no modules registered for ${lib}")
  endif()

  add_library(mts_${lib} INTERFACE)
  target_link_libraries(mts_${lib} INTERFACE ${modules})
  add_library(mts::${lib} ALIAS mts_${lib})
endfunction()

