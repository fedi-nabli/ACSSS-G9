################################
## core/detect                ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 20 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

# v0: WFLC frequency + running power + hysteresis (detect.c). Opaque:
# replaceable behind detect.h
mts_add_module(core detect
  SOURCES detect.c
  DEPS mts::core::separation
  PRIVATE_DEPS mts::core::wflc mts::utils::logger m
)
