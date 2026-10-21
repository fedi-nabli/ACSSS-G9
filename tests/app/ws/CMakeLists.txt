################################
## tests: app/ws              ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 20 Oct 2026          ##
## Last Modified: 20 Oct 2026 ##
################################

# Codec: pure functions against the standards' reference values
mts_add_test(app ws
  SOURCES ws_tests.cc
)

# Server: real loopback sockets, single-threaded
mts_add_test(app ws VARIANT server
  SOURCES ws_server_tests.cc
  DEPS mts::utils::time mts::utils::logger
)

