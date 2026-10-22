################################
## core/fusion                ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 20 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

# v0: Mahony complementary filter, 9 DOF (fusion.c). Opaque: replaceable
# behind fusion.h. m: libm
mts_add_module(core fusion
  SOURCES fusion.c
  DEPS mts::core::types
  PRIVATE_DEPS mts::utils::logger m
)
