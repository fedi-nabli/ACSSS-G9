################################
## core/wflc                  ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 22 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

# Building block: weighted-frequency Fourier linear combiner, normalized,
# plain struct. m: libm (sin, cos, sqrt)
mts_add_module(core wflc
  SOURCES wflc.c
  PRIVATE_DEPS mts::utils::logger m
)
