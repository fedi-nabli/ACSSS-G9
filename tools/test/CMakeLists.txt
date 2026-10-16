################################
## Tests CMake File           ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 15 Oct 2026          ##
## Last Modified: 15 Oct 2026 ##
################################

add_library(mts_test STATIC mts_test.c)
target_include_directories(mts_test PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(mts_test PRIVATE ${MTS_WARNING_FLAGS})
add_library(mts::test ALIAS mts_test)

# Self tests
add_executable(mts_test_self_pass tests/self_pass.c)
target_link_libraries(mts_test_self_pass PRIVATE mts::test)
target_compile_options(mts_test_self_pass PRIVATE ${MTS_WARNING_FLAGS})
add_test(NAME tools.test.pass COMMAND mts_test_self_pass)

add_executable(mts_test_self_pass_cpp tests/self_pass_cpp.cc)
target_link_libraries(mts_test_self_pass_cpp PRIVATE mts::test)
target_compile_options(mts_test_self_pass_cpp PRIVATE ${MTS_WARNING_FLAGS})
add_test(NAME tools.test.pass_cpp COMMAND mts_test_self_pass_cpp)

# Self fail test, should have 9 failed
add_executable(mts_test_self_fail tests/self_fail.c)
target_link_libraries(mts_test_self_fail PRIVATE mts::test)
target_compile_options(mts_test_self_fail PRIVATE ${MTS_WARNING_FLAGS})
add_test(NAME tools.test.fail COMMAND mts_test_self_fail)
set_tests_properties(tools.test.fail PROPERTIES PASS_REGULAR_EXPRESSION "Results: 0 passed, 9 failed")

