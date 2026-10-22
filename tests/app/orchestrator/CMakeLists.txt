################################
## tests: app/orchestrator    ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 22 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

# The state machine, with time as a number the test moves
mts_add_test(app orchestrator
  SOURCES orchestrator_tests.cc
)

# The whole program in one process, a UI session over real loopback
mts_add_test(app orchestrator VARIANT pipeline
  SOURCES pipeline_tests.cc
  DEPS mts::tests::common mts::utils::time mts::utils::logger
)

