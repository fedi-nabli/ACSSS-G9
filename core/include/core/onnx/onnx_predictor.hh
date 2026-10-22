// #pragma once

// #include <array>
// #include <memory>

// #include <onnxruntime_cxx_api.h>

// namespace mts::core::onnx {

// constexpr std::size_t kWindowSize = 30;
// constexpr std::size_t kForecastHorizon = 5;
// constexpr std::size_t kSensorChannels = 6;

// using SensorSample = std::array<float, kSensorChannels>;
// using SensorWindow = std::array<SensorSample, kWindowSize>;
// using TremorForecast = std::array<SensorSample, kForecastHorizon>;

// class OnnxPredictor {
// public:
//     explicit OnnxPredictor(const char* model_path);

//     TremorForecast predict(const SensorWindow& window);

// private:
//     Ort::Env env_;
//     Ort::SessionOptions options_;
//     Ort::Session session_{nullptr};

//     std::array<float, kSensorChannels> mean_{};
//     std::array<float, kSensorChannels> std_{};
// };

// } // namespace mts::core::onnx
