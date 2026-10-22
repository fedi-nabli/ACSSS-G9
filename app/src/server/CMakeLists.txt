################################
## app/server                 ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 22 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

# The server thread: queues <-> protocol <-> WebSocket. Its header shows
# Ws_Limits, Protocol_Encoder, Queue and MTS_TIME_NS_PER_MS: public deps
mts_add_module(app server
  SOURCES server.cc
  DEPS mts::app::link mts::app::ws mts::app::protocol mts::utils::ring_buffer mts::utils::time Threads::Threads
  PRIVATE_DEPS mts::utils::logger
)

