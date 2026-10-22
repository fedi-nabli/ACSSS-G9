################################
## tests: core/fusion         ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 22 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

# Synthetic motion: sensors computed from a chosen true attitude. m: libm
mts_add_test(core fusion
  SOURCES fusion_tests.c
  DEPS mts::utils::logger m
)
