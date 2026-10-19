################################
## RB tests CMake File        ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 19 Oct 2026          ##
## Last Modified: 19 Oct 2026 ##
################################

mts_add_test(utils ring_buffer
  SOURCES ring_buffer_tests.c
  DEPS mts::utils::logger Threads::Threads
)
