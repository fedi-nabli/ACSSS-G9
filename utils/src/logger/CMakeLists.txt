################################
## Utils Logger CMake File    ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 19 Oct 2026          ##
## Last Modified: 19 Oct 2026 ##
################################

mts_add_module(utils logger
  SOURCES logger.c
  PRIVATE_DEPS mts::utils::time
)

