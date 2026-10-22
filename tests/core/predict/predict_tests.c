/* ===================================================================================================
 * tests/core/predict/predict_tests.c
 *
 * The registry, and every registered predictor through its Predictor_Ops
 * only, exactly as Processing uses them.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <core/predict/predict.h>
#include <utils/logger/logger.h>

#include <math.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const double PI = 3.14159265358979323846;

static uint64_t g_rng = 0x9e3779b97f4a7c15ull;

static double gaussian(void)
{
  double s = 0.0;
  for (int i = 0; i < 12; i++)
  {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    s += (double)(g_rng >> 11) * (1.0 / 9007199254740992.0);
  }
  return s - 6.0;
}

static void quiet(void)
{
  static FILE* sink = NULL;
  if (!sink)
  {
    sink = fopen("/dev/null", "w");
  }
  mts_log_set_stream(sink);
}

static void loud(void)
{
  mts_log_set_stream(NULL);
}

static Predictor_Config config(uint32_t horizon)
{
  Predictor_Config c;
  memset(&c, 0, sizeof(c));
  c.rate_hz = 100.0;
  c.horizon_steps = horizon;
  return c;
}

MTS_TEST(registry_is_consistent)
{
  // Every registered entry sits at its own id, under the wire name, within the state limit
  int registered = 0;
  for (Predictor_Id id = 0; id < PREDICTOR_COUNT; id++)
  {
    const Predictor_Ops* ops = predictor_by_id(id);
    if (!ops)
    {
      continue;
    }
    registered++;
    MTS_ASSERT_EQ(ops->id, id);
    MTS_ASSERT(strcmp(ops->name, predictor_name(id)) == 0);
    MTS_ASSERT(predictor_find(ops->name) == ops);
    MTS_ASSERT(ops->state_size > 0 && ops->state_size <= PREDICTOR_STATE_MAX);
    MTS_ASSERT(ops->init && ops->reset && ops->update && ops->predict && ops->delay_ns);
  }
  MTS_ASSERT_GE(registered, 1);
}

MTS_TEST(unknowns_are_null)
{
  MTS_ASSERT(predictor_by_id(PREDICTOR_COUNT) == NULL);
  MTS_ASSERT(predictor_find("nope") == NULL);
  MTS_ASSERT(predictor_find(NULL) == NULL);
}

MTS_TEST(inversion_holds_the_present)
{
  const Predictor_Ops* ops = predictor_find("inversion");
  MTS_ASSERT(ops != NULL);
  MTS_ASSERT_EQ(ops->id, PREDICTOR_INVERSION);

  alignas(max_align_t) unsigned char state[PREDICTOR_STATE_MAX];
  const Predictor_Config cfg = config(3);
  MTS_ASSERT(ops->init(state, &cfg));

  double out[K_MAX];
  MTS_ASSERT_EQ(ops->predict(state, out, K_MAX), 0); // nothing seen yet

  ops->update(state, 0.4e-3);
  ops->update(state, -0.2e-3);
  MTS_ASSERT_EQ(ops->predict(state, out, K_MAX), 3);
  for (int k = 0; k < 3; k++)
  {
    MTS_ASSERT_NEAR(out[k], -0.2e-3, 0.0); // every step: the latest value
  }
  MTS_ASSERT_EQ(ops->predict(state, out, 2), 2); // k_max wins over the horizon
  MTS_ASSERT_EQ(ops->delay_ns(state), 0);

  ops->reset(state);
  MTS_ASSERT_EQ(ops->predict(state, out, K_MAX), 0);
}

MTS_TEST(inversion_refuses_a_bad_horizon)
{
  const Predictor_Ops* ops = predictor_by_id(PREDICTOR_INVERSION);
  alignas(max_align_t) unsigned char state[PREDICTOR_STATE_MAX];
  Predictor_Config cfg = config(0);
  quiet();
  const bool zero = ops->init(state, &cfg);
  cfg.horizon_steps = K_MAX + 1;
  const bool too_far = ops->init(state, &cfg);
  loud();
  MTS_ASSERT(!zero);
  MTS_ASSERT(!too_far);
  cfg.horizon_steps = K_MAX;
  MTS_ASSERT(ops->init(state, &cfg));
}

MTS_TEST(defaults_fill_the_params)
{
  const Predictor_Config w = predictor_config_default(PREDICTOR_WFLC, 100.0, 3);
  MTS_ASSERT_NEAR(w.rate_hz, 100.0, 0.0);
  MTS_ASSERT_EQ(w.horizon_steps, 3);
  MTS_ASSERT_EQ(w.params.wflc.n_harmonics, 1);
  MTS_ASSERT(w.params.wflc.mu_freq > 0.0 && w.params.wflc.mu_amp > 0.0);

  const Predictor_Config i = predictor_config_default(PREDICTOR_INVERSION, 100.0, 3);
  MTS_ASSERT_EQ(i.params.wflc.n_harmonics, 0); // no params: zeros
}

MTS_TEST(wflc_is_silent_until_locked_then_forecasts)
{
  // 0.35 mm at 6 Hz, 5 % noise: null while it locks, then 30 ms ahead
  // within a few % of the amplitude (holding the value: 76 %)
  const Predictor_Ops* ops = predictor_find("wflc");
  MTS_ASSERT(ops != NULL);
  alignas(max_align_t) unsigned char state[PREDICTOR_STATE_MAX];
  const Predictor_Config cfg = predictor_config_default(PREDICTOR_WFLC, 100.0, 3);
  MTS_ASSERT(ops->init(state, &cfg));

  const double amp = 0.35e-3;
  double made[4][3];
  bool valid[4] = { false, false, false, false };
  int silent = 0;
  double err2 = 0.0;
  int scored = 0;
  for (int i = 0; i < 1000; i++)
  {
    const double truth = amp * cos(2.0 * PI * 6.0 * i * 0.01);
    if (i >= 200 && valid[(i - 3) % 4])
    {
      const double e = made[(i - 3) % 4][2] - truth;
      err2 += e * e;
      scored++;
    }
    ops->update(state, truth + 0.05 * amp * gaussian());
    const size_t k = ops->predict(state, made[i % 4], K_MAX);
    valid[i % 4] = k == 3;
    silent += k == 0 ? 1 : 0;
    if (i < 49) // 50 samples: half a second before any lock
    {
      MTS_ASSERT_EQ(k, 0);
    }
  }
  MTS_ASSERT(silent >= 49 && silent < 200); // not before the warm-up, locked within 2 s
  MTS_ASSERT_GT(scored, 700);
  MTS_ASSERT(sqrt(err2 / scored) < 0.05 * amp);
  MTS_ASSERT_EQ(ops->delay_ns(state), 0);

  ops->reset(state);
  double out[K_MAX];
  MTS_ASSERT_EQ(ops->predict(state, out, K_MAX), 0); // starts over
}

MTS_TEST(wflc_lets_go_when_the_tremor_turns_to_noise)
{
  const Predictor_Ops* ops = predictor_by_id(PREDICTOR_WFLC);
  alignas(max_align_t) unsigned char state[PREDICTOR_STATE_MAX];
  const Predictor_Config cfg = predictor_config_default(PREDICTOR_WFLC, 100.0, 3);
  MTS_ASSERT(ops->init(state, &cfg));
  double out[K_MAX];
  for (int i = 0; i < 500; i++)
  {
    ops->update(state, 0.35e-3 * cos(2.0 * PI * 6.0 * i * 0.01));
  }
  MTS_ASSERT_EQ(ops->predict(state, out, K_MAX), 3);
  for (int i = 0; i < 500; i++)
  {
    ops->update(state, 0.35e-3 * gaussian()); // broadband: nothing to extrapolate
  }
  MTS_ASSERT_EQ(ops->predict(state, out, K_MAX), 0);
}

MTS_TEST(wflc_refuses_bad_configs)
{
  const Predictor_Ops* ops = predictor_by_id(PREDICTOR_WFLC);
  alignas(max_align_t) unsigned char state[PREDICTOR_STATE_MAX];
  Predictor_Config cfg = predictor_config_default(PREDICTOR_WFLC, 100.0, 0);
  quiet();
  const bool zero = ops->init(state, &cfg);
  cfg = predictor_config_default(PREDICTOR_INVERSION, 100.0, 3); // wflc params all zero
  const bool no_params = ops->init(state, &cfg);
  loud();
  MTS_ASSERT(!zero);
  MTS_ASSERT(!no_params);
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(registry_is_consistent),
  MTS_TEST_ENTRY(unknowns_are_null),
  MTS_TEST_ENTRY(inversion_holds_the_present),
  MTS_TEST_ENTRY(inversion_refuses_a_bad_horizon),
  MTS_TEST_ENTRY(defaults_fill_the_params),
  MTS_TEST_ENTRY(wflc_is_silent_until_locked_then_forecasts),
  MTS_TEST_ENTRY(wflc_lets_go_when_the_tremor_turns_to_noise),
  MTS_TEST_ENTRY(wflc_refuses_bad_configs),
};

MTS_TEST_MAIN(tests)
