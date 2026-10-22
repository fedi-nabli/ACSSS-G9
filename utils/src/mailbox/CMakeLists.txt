################################
## Utils MB CMake File        ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 20 Oct 2026          ##
## Last Modified: 20 Oct 2026 ##
################################

mts_add_module(utils mailbox
  SOURCES mailbox.c
  PRIVATE_DEPS mts::utils::logger
)

