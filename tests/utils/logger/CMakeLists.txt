################################
## Logger tests CMake File    ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 19 Oct 2026          ##
## Last Modified: 19 Oct 2026 ##
################################

mts_add_test(utils logger
  SOURCES logger_tests.c
  DEPS mts::utils::time Threads::Threads
)

