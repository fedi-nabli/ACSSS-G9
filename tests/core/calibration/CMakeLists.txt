################################
## tests: core/calibration    ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 22 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

mts_add_test(core calibration
  SOURCES calibration_tests.c
  DEPS mts::utils::logger m
)
