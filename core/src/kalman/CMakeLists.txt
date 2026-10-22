################################
## core/kalman                ##
##                            ##
## Authors: Chad Davis        ##
## Date: 21 Oct 2026          ##
## Last Modified: 21 Oct 2026 ##
################################

mts_add_module(core kalman
  SOURCES kalman.c
  DEPS mts::core::types
)

add_executable(mts_core_kalman_replay
    kalman_replay_test.c
)

target_link_libraries(mts_core_kalman_replay
    PRIVATE
        mts::core::kalman
        m
)

target_compile_options(mts_core_kalman_replay
    PRIVATE ${MTS_WARNING_FLAGS}
)

add_executable(mts_core_kalman_synthetic_test
    kalman_synthetic_test.c
)

target_link_libraries(mts_core_kalman_synthetic_test
    PRIVATE
        mts::core::kalman
        m
)

target_compile_options(mts_core_kalman_synthetic_test
    PRIVATE ${MTS_WARNING_FLAGS}
)