################################
## tests: core/predict        ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 22 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

# The registry and each predictor through its Predictor_Ops
mts_add_test(core predict
  SOURCES predict_tests.c
  DEPS mts::utils::logger m
)
