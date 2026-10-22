/* ===================================================================================================
 * tests/core/types/seam_tests_cpp.cc
 *
 * The core seam from C++, the side that will call it (Processing,
 * Prediction). Every seam header compiles from C++, every type that
 * crosses a thread is trivially copyable and standard layout (checked at
 * compile time), and the transported ones fit the typed queue / mailbox.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>

#include <core/calibration/calibration.h>
#include <core/detect/detect.h>
#include <core/fusion/fusion.h>
#include <core/predict/predict.h>
#include <core/separation/separation.h>
#include <core/types/forecast.h>
#include <core/types/predictor_id.h>
#include <core/types/raw_sample.h>
#include <core/types/tremor_sample.h>
#include <core/types/vec.h>
#include <utils/mailbox/mailbox.hpp>
#include <utils/ring_buffer/queue.hpp>

#include <cstring>
#include <type_traits>

template <typename T>
constexpr bool is_plain_data_v = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>;

static_assert(is_plain_data_v<Vec3>, "Vec3 must be plain data");
static_assert(is_plain_data_v<Quat>, "Quat must be plain data");
static_assert(is_plain_data_v<Tremor_Sample>, "Tremor_Sample must be plain data");
static_assert(is_plain_data_v<Forecast>, "Forecast must be plain data");
static_assert(is_plain_data_v<Calibration>, "Calibration must be plain data");
static_assert(is_plain_data_v<Fusion_Output>, "Fusion_Output must be plain data");
static_assert(is_plain_data_v<Separation_Output>, "Separation_Output must be plain data");
static_assert(is_plain_data_v<Detect_Output>, "Detect_Output must be plain data");
static_assert(is_plain_data_v<Predictor_Config>, "Predictor_Config must be plain data");

// Instantiating the wrappers runs their compile-time checks
// (Mailbox<T> refuses a size that is not a multiple of 8)
template class mts::Mailbox<Forecast>;
template class mts::Queue<Tremor_Sample>;

MTS_TEST(forecast_through_typed_mailbox)
{
  mts::Mailbox<Forecast> mb;
  MTS_ASSERT(mb.valid());

  Forecast in{};
  in.origin_seq = 7;
  in.k_valid = 3;
  in.predictor_id = PREDICTOR_HARMONIC;
  in.y[2][2] = -0.0015;
  mb.write(in);

  Forecast out{};
  MTS_ASSERT(mb.read(out));
  MTS_ASSERT(std::memcmp(&in, &out, sizeof(Forecast)) == 0);
}

MTS_TEST(tremor_sample_through_typed_queue)
{
  mts::Queue<Tremor_Sample> q(8);
  MTS_ASSERT(q.valid());

  Tremor_Sample in{};
  in.seq = 42;
  in.flags = TREMOR_ON;
  in.tremor_disp.v[AXIS_X] = 0.0008;
  in.orientation.q[QUAT_W] = 1.0;
  in.tremor_hz = 6.0;
  MTS_ASSERT(q.try_push(in));

  Tremor_Sample out{};
  MTS_ASSERT(q.pop(out));
  MTS_ASSERT(std::memcmp(&in, &out, sizeof(Tremor_Sample)) == 0);
}

MTS_TEST(predictor_config_from_cpp)
{
  Predictor_Config cfg{};
  cfg.rate_hz = 100.0;
  cfg.horizon_steps = 10;
  cfg.params.harmonic.n_harmonics = 3;
  MTS_ASSERT_EQ(cfg.params.harmonic.n_harmonics, 3);
  MTS_ASSERT_LE(cfg.horizon_steps, K_MAX);
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(forecast_through_typed_mailbox),
  MTS_TEST_ENTRY(tremor_sample_through_typed_queue),
  MTS_TEST_ENTRY(predictor_config_from_cpp),
};

MTS_TEST_MAIN(tests)

