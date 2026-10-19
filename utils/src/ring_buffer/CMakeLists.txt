################################
## Utils RB CMake File        ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 19 Oct 2026          ##
## Last Modified: 19 Oct 2026 ##
################################

mts_add_module(utils ring_buffer
  SOURCES ring_buffer.c
  PRIVATE_DEPS mts::utils::logger
)

