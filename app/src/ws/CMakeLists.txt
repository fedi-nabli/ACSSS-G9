################################
## app/ws                     ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 20 Oct 2026          ##
## Last Modified: 20 Oct 2026 ##
################################

# WebSocket transport (RFC 6455) on POSIX sockets + poll(). Knows nothing
# about our messages: protocol and server sit on top
mts_add_module(app ws
  SOURCES ws_codec.cc ws_server.cc
  PRIVATE_DEPS mts::utils::time mts::utils::logger
)

