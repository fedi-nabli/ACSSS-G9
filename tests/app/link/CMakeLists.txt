################################
## tests: app/link            ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 20 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

mts_add_test(app link
  SOURCES link_tests.c
  DEPS mts::utils::mailbox
)

mts_add_test(app link VARIANT sample_queue
  SOURCES sample_queue_tests.cc
)

