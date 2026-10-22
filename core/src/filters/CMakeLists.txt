################################
## core/filters               ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 22 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

# Building block: biquads and Butterworth chains, plain structs. m: libm
mts_add_module(core filters
  SOURCES filters.c
  PRIVATE_DEPS mts::utils::logger m
)
