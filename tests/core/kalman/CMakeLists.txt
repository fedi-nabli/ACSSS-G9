################################
## tests: core/kalman         ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 22 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

# Closed forms, then the oscillator use case. m: libm for sin/cos/sqrt
mts_add_test(core kalman
  SOURCES kalman_tests.c
  DEPS mts::utils::logger m
)
