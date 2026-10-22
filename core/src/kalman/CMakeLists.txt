################################
## core/kalman                ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 22 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

# Building block: linear Kalman filter, scalar measurement, plain struct.
# libm (isfinite is a macro, but keep the dependency explicit for the
# predictors that link through us)
mts_add_module(core kalman
  SOURCES kalman.c
  PRIVATE_DEPS mts::utils::logger m
)
