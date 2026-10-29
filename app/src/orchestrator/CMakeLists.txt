################################
## app/orchestrator           ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 22 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

# The main thread's logic: the state machine and Pipeline,
# which owns the queues and threads and runs it.

mts_add_module(app orchestrator
  SOURCES orchestrator.cc pipeline.cc
  DEPS mts::app::link mts::app::hat mts::app::server mts::utils::time
  PRIVATE_DEPS mts::app::processing mts::utils::logger
)

