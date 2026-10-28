
#include "core/onnx/onnx_predictor.hh"

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mts::core::onnx {
namespace {

using Json = nlohmann::json;

constexpr const char* kInputName = "sensor_window";
constexpr const char* kForecastOutputName = "future_predictions";

std::array<float, kSensorChannels> load_array(
    const Json& json,
    const char* key)
{
    if (!json.contains(key) ||
        !json.at(key).is_array() ||
        json.at(key).size() != kSensorChannels) {
        throw std::runtime_error(
            std::string("Invalid normalization metadata: expected six values for '") +
            key + "'");
    }

    std::array<float, kSensorChannels> result{};

    for (std::size_t i = 0; i < kSensorChannels; ++i) {
        result[i] = json.at(key).at(i).get<float>();

        if (!std::isfinite(result[i])) {
            throw std::runtime_error(
                std::string("Non-finite normalization value in '") + key + "'");
        }
    }

    return result;
}

void validate_tensor_shape(
    const std::vector<int64_t>& shape,
    const std::vector<int64_t>& expected,
    const char* tensor_name)
{
    if (shape.size() != expected.size()) {
        throw std::runtime_error(
            std::string("Unexpected rank for ONNX tensor '") + tensor_name + "'");
    }

    for (std::size_t i = 0; i < shape.size(); ++i) {
        // A negative dimension indicates a dynamic dimension.
        if (shape[i] >= 0 && shape[i] != expected[i]) {
            throw std::runtime_error(
                std::string("Unexpected shape for ONNX tensor '") +
                tensor_name + "'");
        }
    }
}

} // namespace

OnnxPredictor::OnnxPredictor(const char* model_path)
    : env_(ORT_LOGGING_LEVEL_WARNING, "mts_onnx"),
      options_(),
      session_(nullptr)
{
    if (model_path == nullptr || model_path[0] == '\0') {
        throw std::invalid_argument("ONNX model path must not be empty");
    }

    options_.SetIntraOpNumThreads(1);
    options_.SetInterOpNumThreads(1);
    options_.SetGraphOptimizationLevel(
        GraphOptimizationLevel::ORT_ENABLE_ALL);

    session_ = Ort::Session(env_, model_path, options_);

    // The normalization file sits alongside the model:
    // models/tremor_lstm.onnx
    // models/lstm_normalization.json
    const std::filesystem::path normalization_path =
        std::filesystem::path(model_path).parent_path() /
        "lstm_normalization.json";

    std::ifstream file(normalization_path);
    if (!file) {
        throw std::runtime_error(
            "Unable to open normalization file: " +
            normalization_path.string());
    }

    const Json metadata = Json::parse(file);

    mean_ = load_array(metadata, "mean");
    std_ = load_array(metadata, "std");

    for (std::size_t i = 0; i < kSensorChannels; ++i) {
        if (std_[i] <= 0.0F) {
            throw std::runtime_error(
                "Normalization standard deviations must be positive");
        }
    }

    if (metadata.value("window", 0) !=
            static_cast<int>(kWindowSize) ||
        metadata.value("future", 0) !=
            static_cast<int>(kForecastHorizon)) {
        throw std::runtime_error(
            "Normalization metadata does not match the expected "
            "window size (30) and forecast horizon (5)");
    }

    // Verify the model's primary input and forecast output.
    Ort::AllocatorWithDefaultOptions allocator;

    if (session_.GetInputCount() < 1 ||
        session_.GetOutputCount() < 1) {
        throw std::runtime_error(
            "ONNX model must have at least one input and one output");
    }

    auto input_name =
        session_.GetInputNameAllocated(0, allocator);
    auto output_name =
        session_.GetOutputNameAllocated(0, allocator);

    if (std::string(input_name.get()) != kInputName) {
        throw std::runtime_error(
            "Unexpected ONNX input name: " +
            std::string(input_name.get()));
    }

    // The exported model returns forecast_predictions first,
    // followed by hidden_state and cell_state.
    if (std::string(output_name.get()) != kForecastOutputName) {
        throw std::runtime_error(
            "Unexpected first ONNX output name: " +
            std::string(output_name.get()));
    }

    const auto input_info = session_.GetInputTypeInfo(0);
    const auto input_shape =
        input_info.GetTensorTypeAndShapeInfo().GetShape();

    validate_tensor_shape(
        input_shape, {1, 30, 6}, kInputName);

    const auto output_info = session_.GetOutputTypeInfo(0);
    const auto output_shape =
        output_info.GetTensorTypeAndShapeInfo().GetShape();

    validate_tensor_shape(
        output_shape, {1, 5, 6}, kForecastOutputName);
}

TremorForecast OnnxPredictor::predict(const SensorWindow& window)
{
    // Flatten the [30, 6] window and apply training-set normalization.
    std::array<float, kWindowSize * kSensorChannels> input_data{};

    for (std::size_t t = 0; t < kWindowSize; ++t) {
        for (std::size_t c = 0; c < kSensorChannels; ++c) {
            const float value = window[t][c];

            if (!std::isfinite(value)) {
                throw std::invalid_argument(
                    "Sensor window contains a non-finite value");
            }

            input_data[t * kSensorChannels + c] =
                (value - mean_[c]) / std_[c];
        }
    }

    const std::array<int64_t, 3> input_shape = {
        1,
        static_cast<int64_t>(kWindowSize),
        static_cast<int64_t>(kSensorChannels)
    };

    auto memory_info = Ort::MemoryInfo::CreateCpu(
        OrtArenaAllocator, OrtMemTypeDefault);

    auto input_tensor = Ort::Value::CreateTensor<float>(
        memory_info,
        input_data.data(),
        input_data.size(),
        input_shape.data(),
        input_shape.size());

    const char* input_names[] = {kInputName};
    const char* output_names[] = {kForecastOutputName};

    auto outputs = session_.Run(
        Ort::RunOptions{nullptr},
        input_names,
        &input_tensor,
        1,
        output_names,
        1);

    if (outputs.size() != 1 ||
        !outputs[0].IsTensor()) {
        throw std::runtime_error(
            "ONNX inference did not return a forecast tensor");
    }

    const auto output_tensor_info =
        outputs[0].GetTensorTypeAndShapeInfo();

    const auto output_shape = output_tensor_info.GetShape();

    validate_tensor_shape(
        output_shape, {1, 5, 6}, kForecastOutputName);

    if (output_tensor_info.GetElementType() !=
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        throw std::runtime_error(
            "ONNX forecast output must contain float values");
    }

    if (output_tensor_info.GetElementCount() !=
        kForecastHorizon * kSensorChannels) {
        throw std::runtime_error(
            "ONNX forecast output has an unexpected element count");
    }

    const float* output_data = outputs[0].GetTensorData<float>();

    TremorForecast forecast{};

    // The network predicts normalized values. Convert predictions back
    // to the original sensor units using the training-set statistics.
    for (std::size_t t = 0; t < kForecastHorizon; ++t) {
        for (std::size_t c = 0; c < kSensorChannels; ++c) {
            const float normalized =
                output_data[t * kSensorChannels + c];

            const float value = normalized * std_[c] + mean_[c];

            if (!std::isfinite(value)) {
                throw std::runtime_error(
                    "ONNX forecast contains a non-finite value");
            }

            forecast[t][c] = value;
        }
    }

    return forecast;
}

} // namespace mts::core::onnx
