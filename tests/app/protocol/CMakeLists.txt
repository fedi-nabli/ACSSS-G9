################################
## tests: app/protocol        ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 22 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

# Encoder and decoder against the contract
mts_add_test(app protocol
  SOURCES protocol_tests.cc
)

# The strict JSON parser on its own
mts_add_test(app protocol VARIANT json
  SOURCES json_tests.cc
)

