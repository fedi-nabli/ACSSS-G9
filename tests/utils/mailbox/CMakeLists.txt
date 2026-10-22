################################
## MB tests CMake File        ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 20 Oct 2026          ##
## Last Modified: 20 Oct 2026 ##
################################

mts_add_test(utils mailbox
  SOURCES mailbox_tests.c
  DEPS mts::utils::logger Threads::Threads
)

mts_add_test(utils mailbox VARIANT cpp
  SOURCES mailbox_tests_cpp.cc
  DEPS Threads::Threads
)

