################################
## tests: core/wflc           ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 22 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

# Tremor-like signals, then the design decisions one by one. m: libm
mts_add_test(core wflc
  SOURCES wflc_tests.c
  DEPS mts::utils::logger m
)
