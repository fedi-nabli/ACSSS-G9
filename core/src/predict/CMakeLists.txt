################################
## core/predict               ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 20 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

# The predictor ABI's registry and the predictors themselves. Next:
# predict_wflc_kalman.c (+ core/kalman)
mts_add_module(core predict
  SOURCES predict.c inversion.c predict_wflc.c
  DEPS mts::core::types
  PRIVATE_DEPS mts::core::wflc mts::utils::logger
)
