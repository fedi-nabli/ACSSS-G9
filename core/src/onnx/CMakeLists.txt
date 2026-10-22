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
  PRIVATE_DEPS onnxruntime
)

