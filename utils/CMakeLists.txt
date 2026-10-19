################################
## Utils CMake File           ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 10 Oct 2026          ##
## Last Modified: 19 Oct 2026 ##
################################

add_subdirectory(src/time)
add_subdirectory(src/logger)

mts_add_umbrella(utils)

