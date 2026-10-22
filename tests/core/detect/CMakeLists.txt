################################
## tests: core/detect         ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 22 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

mts_add_test(core detect
  SOURCES detect_tests.c
  DEPS mts::utils::logger m
)
