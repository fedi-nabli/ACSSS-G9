################################
## Utils CMake File           ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 10 Oct 2026          ##
## Last Modified: 20 Oct 2026 ##
################################

add_subdirectory(src/time)
add_subdirectory(src/logger)
add_subdirectory(src/ring_buffer)
add_subdirectory(src/mailbox)

mts_add_umbrella(utils)

