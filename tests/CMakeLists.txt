################################
## Tests CMake File           ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 17 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

# Only added when MTS_BUILD_TESTS is ON

# Helpers shared by several tests (tests/common/), included as <common/...>

add_library(mts_tests_common INTERFACE)
add_library(mts::tests::common ALIAS mts_tests_common)
target_include_directories(mts_tests_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mts_tests_common INTERFACE mts::app::ws mts::utils::time)

# utils
add_subdirectory(utils/time)
add_subdirectory(utils/logger)
add_subdirectory(utils/ring_buffer)
add_subdirectory(utils/mailbox)

# core
add_subdirectory(core/types)


# app
add_subdirectory(app/link)
add_subdirectory(app/hat)
add_subdirectory(app/ws)
add_subdirectory(app/protocol)
add_subdirectory(app/server)
add_subdirectory(app/processing)
add_subdirectory(app/orchestrator)

