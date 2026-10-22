################################
## app/hat                    ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 20 Oct 2026          ##
## Last Modified: 20 Oct 2026 ##
################################

mts_add_module(app hat
  SOURCES hat.cc hat_sim.cc
  DEPS mts::core::types mts::app::link
  PRIVATE_DEPS mts::utils::time mts::utils::logger
)

# Real hardware: only with -DMTS_WITH_RTIMU=ON. Hat_Real adds here:
#   target_sources(mts_app_hat PRIVATE hat_real.cc) and RTIMULib as a private dependency
if (MTS_WITH_RTIMU)
  target_sources(mts_app_hat PRIVATE hat_real.cc)

  target_compile_definitions(mts_app_hat PRIVATE MTS_WITH_RTIMU=1)

  target_include_directories(mts_app_hat PRIVATE
    "$ENV{HOME}/RTIMULib/RTIMULib"
  )

  target_link_directories(mts_app_hat PRIVATE
    "$ENV{HOME}/RTIMULib/RTIMULib/build"
  )

  target_link_libraries(mts_app_hat PRIVATE /usr/lib/libRTIMULib.so)
endif()