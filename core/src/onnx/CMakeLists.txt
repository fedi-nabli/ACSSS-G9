################################
## core/onnx                  ##
##                            ##
## Authors: Chad Davis        ##
## Date: 21 Oct 2026          ##
## Last Modified: 21 Oct 2026 ##
################################

mts_add_module(core onnx
  SOURCES onnx_predictor.cc
  DEPS mts::core::types
  PRIVATE_DEPS onnxruntime nlohmann_json::nlohmann_json
)

target_include_directories(mts_core_onnx
  PUBLIC
    "${onnxruntime_SOURCE_DIR}/include"
)
