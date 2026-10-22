################################
## tests: core/filters        ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 22 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

# Against the digital Butterworth closed form, then a running sine. m: libm
mts_add_test(core filters
  SOURCES filters_tests.c
  DEPS mts::utils::logger m
)
