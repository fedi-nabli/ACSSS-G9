################################
## tests: app/processing      ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 22 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

mts_add_test(app processing
  SOURCES processing_tests.cc
  DEPS mts::app::hat mts::utils::time
)

