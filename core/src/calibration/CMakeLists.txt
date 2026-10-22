################################
## core/calibration           ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 20 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

mts_add_module(core calibration
  SOURCES calibration.c
  DEPS mts::core::types
)
