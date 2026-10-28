
#include <mts_test.h>
#include <core/onnx/onnx_predictor.hh>

#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace {

std::filesystem::path model_path()
{
    return std::filesystem::path(MTS_SOURCE_DIR) /
           "models" / "tremor_lstm.onnx";
}

} // namespace

MTS_TEST(model_and_normalization_files_exist)
{
    const auto model = model_path();
    const auto normalization =
        model.parent_path() / "lstm_normalization.json";

    MTS_ASSERT(std::filesystem::is_regular_file(model));
    MTS_ASSERT(std::filesystem::is_regular_file(normalization));
}

MTS_TEST(predict_returns_finite_forecast)
{
    const auto model = model_path();
    mts::core::onnx::OnnxPredictor predictor(model.c_str());

    // A constant window in the original sensor units.
    // This tests the full normalization/inference/denormalization path.
    mts::core::onnx::SensorWindow window{};

    for (auto& sample : window) {
        sample = {0.0F, 0.0F, 9.81F, 0.0F, 0.0F, 0.0F};
    }

    const auto forecast = predictor.predict(window);

    MTS_ASSERT_EQ(forecast.size(),
                  mts::core::onnx::kForecastHorizon);

    for (const auto& sample : forecast) {
        MTS_ASSERT_EQ(sample.size(),
                      mts::core::onnx::kSensorChannels);

        for (const float value : sample) {
            MTS_ASSERT(std::isfinite(value));
        }
    }
}

MTS_TEST(invalid_model_path_is_rejected)
{
    bool threw = false;

    try {
        mts::core::onnx::OnnxPredictor predictor(
            "/path/that/does/not/exist/tremor_lstm.onnx");
    } catch (const std::exception&) {
        threw = true;
    }

    MTS_ASSERT(threw);
}

static const Mts_Test tests[] = {
    MTS_TEST_ENTRY(model_and_normalization_files_exist),
    MTS_TEST_ENTRY(predict_returns_finite_forecast),
    MTS_TEST_ENTRY(invalid_model_path_is_rejected),
};

MTS_TEST_MAIN(tests)
