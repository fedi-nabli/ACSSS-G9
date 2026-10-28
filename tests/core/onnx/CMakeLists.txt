################################
## tests: core/onnx           ##
##                            ##
## Authors: Chad Davis        ##
## Date: 28 Oct 2026          ##
## Last Modified: 28 Oct 2026 ##
################################


mts_add_test(core onnx
  SOURCES onnx_predictor_tests.cc
  
)

target_compile_definitions(mts_core_onnx_tests PRIVATE
  MTS_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)