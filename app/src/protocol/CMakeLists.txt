################################
## app/protocol               ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 22 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

mts_add_module(app protocol
  SOURCES json.cc protocol.cc
  DEPS mts::app::link
)

