################################
## tests: app/link            ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 20 Oct 2026          ##
## Last Modified: 20 Oct 2026 ##
################################

mts_add_test(app link
  SOURCES link_tests.c
  DEPS mts::utils::mailbox
)

