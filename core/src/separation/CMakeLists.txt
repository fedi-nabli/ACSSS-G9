################################
## core/separation            ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 20 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

# v0: band-pass + drift-free double integration (separation.c). Opaque:
# replaceable behind separation.h
mts_add_module(core separation
  SOURCES separation.c
  DEPS mts::core::types
  PRIVATE_DEPS mts::core::filters mts::utils::logger m
)
