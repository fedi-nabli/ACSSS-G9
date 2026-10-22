################################
## tests: core/types          ##
##                            ##
## Authors: Fedi Nabli        ##
## Date: 20 Oct 2026          ##
## Last Modified: 20 Oct 2026 ##
################################

mts_add_test(core types
  SOURCES types_tests.c
  DEPS mts::utils::ring_buffer
)

mts_add_test(core types VARIANT cpp
  SOURCES types_tests_cpp.cc
  DEPS mts::utils::ring_buffer
)

# The core seam: every seam header together, from C and from C++
mts_add_test(core types VARIANT seam
  SOURCES seam_tests.c
  DEPS mts::core mts::utils::mailbox
)

mts_add_test(core types VARIANT seam_cpp
  SOURCES seam_tests_cpp.cc
  DEPS mts::core mts::utils::mailbox mts::utils::ring_buffer
)

