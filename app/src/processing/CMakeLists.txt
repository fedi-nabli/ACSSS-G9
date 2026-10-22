################################
## app/processing             ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 22 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

# The Processing thread: Raw_Sample -> Output_Frame

mts_add_module(app processing
  SOURCES processing.cc
  DEPS mts::app::link mts::utils::mailbox mts::utils::time Threads::Threads
  PRIVATE_DEPS mts::app::hat mts::utils::logger
)

