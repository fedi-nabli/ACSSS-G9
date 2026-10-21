################################
## Tests CMake File           ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 17 Oct 2026          ##
## Last Modified: 17 Oct 2026 ##
################################

# Only added when MTS_BUILD_TESTS is ON

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

