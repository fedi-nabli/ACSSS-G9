################################
## tests: app/hat             ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 20 Oct 2026          ##
## Last Modified: 22 Oct 2026 ##
################################

mts_add_test(app hat
  SOURCES hat_tests.cc
)

# hat_open + Hat_Sim, through the public interface
mts_add_test(app hat VARIANT sim
  SOURCES hat_sim_tests.cc
  DEPS mts::utils::time mts::utils::logger
)

# The "real without RTIMULib is refused" test only makes sense in such a build
if (NOT MTS_WITH_RTIMU)
  target_compile_definitions(mts_app_hat_sim_tests PRIVATE MTS_TEST_NO_RTIMU=1)
endif()

# Real-HAT hardware integration test is opt-in
option(MTS_ENABLE_HAT_REAL_TESTS
  "Enable tests requiring a physical Sense HAT"
  OFF
)

if (MTS_WITH_RTIMU AND MTS_ENABLE_HAT_REAL_TESTS)
  mts_add_test(app hat VARIANT real
    SOURCES hat_real_tests.cc
    DEPS mts::utils::time mts::utils::logger
  )
endif()
