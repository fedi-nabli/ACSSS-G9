################################
## app/processing             ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 22 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

mts_add_module(app processing
  SOURCES processing.cc
  DEPS mts::app::link mts::core::calibration mts::core::predict mts::utils::mailbox mts::utils::time Threads::Threads
  PRIVATE_DEPS mts::core::fusion mts::core::separation mts::core::detect mts::utils::logger
)

