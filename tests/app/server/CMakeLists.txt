################################
## tests: app/server          ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 22 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

mts_add_test(app server
  SOURCES server_tests.cc
  DEPS mts::tests::common mts::utils::time mts::utils::logger
)

