################################
## tests: core/separation     ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 22 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

mts_add_test(core separation
  SOURCES separation_tests.c
  DEPS mts::utils::logger m
)
