/* ===================================================================================================
 * tests/core/types/seam_tests.c
 *
 * The core seam from C: every seam header compiles together, the shared
 * types behave as documented, and the predictor ABI can be implemented and
 * called through Predictor_Ops (a tiny "hold last value" predictor stands
 * in for the real ones). The stage 1 modules have no implementation yet,
 * so nothing calls their functions.
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
#include <utils/mailbox/mailbox.h>

#include <string.h>

MTS_TEST(axes_index_vec3)
{
  Vec3 a = { { 1.0, 2.0, 3.0 } };
  MTS_ASSERT_NEAR(a.v[AXIS_X], 1.0, 0.0);
  MTS_ASSERT_NEAR(a.v[AXIS_Y], 2.0, 0.0);
  MTS_ASSERT_NEAR(a.v[AXIS_Z], 3.0, 0.0);
  MTS_ASSERT_EQ(AXIS_COUNT, 3);
}

MTS_TEST(identity_quaternion_is_w_first)
{
  Quat q = { { 1.0, 0.0, 0.0, 0.0 } };
  MTS_ASSERT_NEAR(q.q[QUAT_W], 1.0, 0.0);
  MTS_ASSERT_NEAR(q.q[QUAT_X] + q.q[QUAT_Y] + q.q[QUAT_Z], 0.0, 0.0);
}

MTS_TEST(predictor_ids_are_dense_and_distinct)
{
  const Predictor_Id ids[] = {
    PREDICTOR_INVERSION, PREDICTOR_HARMONIC, PREDICTOR_FFT_OSCILLATOR,
    PREDICTOR_WFLC, PREDICTOR_WFLC_KALMAN,
  };
  const size_t n = sizeof(ids) / sizeof(ids[0]);
  MTS_ASSERT_EQ(n, PREDICTOR_COUNT);

  // Dense: together they are exactly 0 .. COUNT - 1
  bool seen[PREDICTOR_COUNT] = { false };
  for (size_t i = 0; i < n; i++)
  {
    MTS_ASSERT_LT(ids[i], PREDICTOR_COUNT);
    MTS_ASSERT(!seen[ids[i]]);
    seen[ids[i]] = true;
  }
}

MTS_TEST(flags_are_distinct_single_bits)
{
  const uint32_t tremor[] = { TREMOR_ON, TREMOR_GAP, TREMOR_RESET };
  for (size_t i = 0; i < 3; i++)
  {
    MTS_ASSERT_EQ(tremor[i] & (tremor[i] - 1), 0);
    for (size_t j = i + 1; j < 3; j++)
    {
      MTS_ASSERT_EQ(tremor[i] & tremor[j], 0);
    }
  }
  MTS_ASSERT_EQ(FUSION_CONVERGED & (FUSION_CONVERGED - 1), 0);
  MTS_ASSERT_NE(SEPARATION_NEUTRAL, SEPARATION_WFLC);
}

MTS_TEST(forecast_holds_k_max_steps_per_axis)
{
  Forecast f;
  MTS_ASSERT_EQ(sizeof(f.y) / sizeof(f.y[0][0]), 3 * K_MAX);
  MTS_ASSERT_EQ(sizeof(f.y[0]) / sizeof(f.y[0][0]), K_MAX);
}

MTS_TEST(forecast_survives_mailbox)
{
  Mailbox* mb = mb_create(sizeof(Forecast));
  MTS_ASSERT(mb != NULL);

  Forecast in;
  memset(&in, 0, sizeof(in));
  in.origin_seq = 1000;
  in.step_ns = 10000000;
  in.k_valid = K_MAX;
  in.predictor_id = PREDICTOR_WFLC;
  in.cfg_gen = 2;
  for (int a = 0; a < 3; a++)
  {
    for (int k = 0; k < K_MAX; k++)
    {
      in.y[a][k] = 0.001 * (a + 1) * (k + 1);
    }
  }

  Forecast out;
  memset(&out, 0, sizeof(out));
  mb_write(mb, &in);
  bool got = mb_read(mb, &out);
  mb_destroy(mb);

  MTS_ASSERT(got);
  MTS_ASSERT_MEM_EQ(&in, &out, sizeof(Forecast));
}

MTS_TEST(calibration_is_plain_data)
{
  // Identity correction: offset 0, m = I
  Calibration cal;
  memset(&cal, 0, sizeof(cal));
  for (int i = 0; i < 3; i++)
  {
    cal.accel.m[i][i] = 1.0;
    cal.gyro.m[i][i] = 1.0;
    cal.mag.m[i][i] = 1.0;
  }
  MTS_ASSERT_NEAR(cal.gyro.m[2][2], 1.0, 0.0);
  MTS_ASSERT_NEAR(cal.mag.offset[0], 0.0, 0.0);
}

/* ---------------------------------------------------------------------------------------------------
 * A tiny predictor through the ABI: "hold" predicts that the last value
 * stays forever. Real predictors follow exactly this shape.
 * ------------------------------------------------------------------------------------------------- */

typedef struct Hold_State
{
  double last;
  uint32_t horizon;
  bool has_sample;
} Hold_State;

static bool hold_init(void* state, const Predictor_Config* cfg)
{
  Hold_State* s = (Hold_State*)state;
  if (cfg->horizon_steps == 0 || cfg->horizon_steps > K_MAX)
  {
    return false;
  }
  s->last = 0.0;
  s->horizon = cfg->horizon_steps;
  s->has_sample = false;
  return true;
}

static void hold_reset(void* state)
{
  Hold_State* s = (Hold_State*)state;
  s->last = 0.0;
  s->has_sample = false;
}

static void hold_update(void* state, double x)
{
  Hold_State* s = (Hold_State*)state;
  s->last = x;
  s->has_sample = true;
}

static size_t hold_predict(void* state, double* out, size_t k_max)
{
  Hold_State* s = (Hold_State*)state;
  if (!s->has_sample)
  {
    return 0;
  }
  size_t k = s->horizon < k_max ? s->horizon : k_max;
  for (size_t i = 0; i < k; i++)
  {
    out[i] = s->last;
  }
  return k;
}

static int64_t hold_delay_ns(const void* state)
{
  (void)state;
  return 0;
}

static const Predictor_Ops HOLD_OPS = {
  .id = PREDICTOR_INVERSION,
  .name = "hold",
  .state_size = sizeof(Hold_State),
  .init = hold_init,
  .reset = hold_reset,
  .update = hold_update,
  .predict = hold_predict,
  .delay_ns = hold_delay_ns,
};

MTS_TEST(predictor_abi_through_ops)
{
  const Predictor_Ops* ops = &HOLD_OPS;

  // The caller owns the state block, sized by the descriptor
  unsigned char storage[64];
  MTS_ASSERT_LE(ops->state_size, sizeof(storage));
  void* state = storage;

  Predictor_Config cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.rate_hz = 100.0;
  cfg.horizon_steps = 4;
  MTS_ASSERT(ops->init(state, &cfg));

  double out[K_MAX];
  MTS_ASSERT_EQ(ops->predict(state, out, K_MAX), 0); // not ready before any sample

  ops->update(state, 0.002);
  MTS_ASSERT_EQ(ops->predict(state, out, K_MAX), 4);
  MTS_ASSERT_NEAR(out[3], 0.002, 0.0);

  // k_max smaller than the horizon wins
  MTS_ASSERT_EQ(ops->predict(state, out, 2), 2);

  ops->reset(state);
  MTS_ASSERT_EQ(ops->predict(state, out, K_MAX), 0);
  MTS_ASSERT_EQ(ops->delay_ns(state), 0);

  // A bad horizon is refused
  cfg.horizon_steps = K_MAX + 1;
  MTS_ASSERT(!ops->init(state, &cfg));
}

MTS_TEST(predictor_params_union)
{
  Predictor_Config cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.params.wflc.n_harmonics = 2;
  cfg.params.wflc.mu_freq = 0.001;
  cfg.params.wflc.mu_amp = 0.01;

  MTS_ASSERT_EQ(cfg.params.wflc.n_harmonics, 2);
  MTS_ASSERT_NEAR(cfg.params.wflc.mu_amp, 0.01, 0.0);
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(axes_index_vec3),
  MTS_TEST_ENTRY(identity_quaternion_is_w_first),
  MTS_TEST_ENTRY(predictor_ids_are_dense_and_distinct),
  MTS_TEST_ENTRY(flags_are_distinct_single_bits),
  MTS_TEST_ENTRY(forecast_holds_k_max_steps_per_axis),
  MTS_TEST_ENTRY(forecast_survives_mailbox),
  MTS_TEST_ENTRY(calibration_is_plain_data),
  MTS_TEST_ENTRY(predictor_abi_through_ops),
  MTS_TEST_ENTRY(predictor_params_union),
};

MTS_TEST_MAIN(tests)

