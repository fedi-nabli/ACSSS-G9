/* ===================================================================================================
 * tests/core/kalman/kalman_tests.c
 *
 * The Kalman filter against results known in closed form (the weighted
 * mean, the Riccati steady state, a straight line), then on what it is
 * for here: tracking a noisy 6 Hz sine as a 2-state oscillator and
 * forecasting it 30 ms ahead. Noise is deterministic (xorshift + Irwin-Hall),
 * so every run sees the same numbers.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <core/kalman/kalman.h>
#include <utils/logger/logger.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static const double TWO_PI = 6.283185307179586;
static const double T = 0.01; // 100 Hz

/* ---------------------------------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------------------------------- */

static uint64_t g_rng = 0x9e3779b97f4a7c15ull;

static double uniform(void)
{
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 7;
  g_rng ^= g_rng << 17;
  return (double)(g_rng >> 11) * (1.0 / 9007199254740992.0);
}

// Standard normal, approximately: Irwin-Hall, the sum of 12 uniforms minus 6
static double gaussian(void)
{
  double s = 0.0;
  for (int i = 0; i < 12; i++)
  {
    s += uniform();
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

// x = [A cos(theta), A sin(theta)], measured: the first. One step turns theta by w T
static void rotation(double w, double F[2][2])
{
  const double c = cos(w * T);
  const double s = sin(w * T);
  F[0][0] = c;
  F[0][1] = -s;
  F[1][0] = s;
  F[1][1] = c;
}

static bool oscillator(Kalman* k, double hz, double q, double r)
{
  double F[2][2];
  rotation(TWO_PI * hz, F);
  const double H[2] = { 1.0, 0.0 };
  const double Q[2][2] = { { q, 0.0 }, { 0.0, q } };
  const double x0[2] = { 0.0, 0.0 };
  const double P0[2][2] = { { 1e-6, 0.0 }, { 0.0, 1e-6 } };
  return kalman_init(k, 2, &F[0][0], H, &Q[0][0], r, x0, &P0[0][0]);
}

static void step(Kalman* k, double z)
{
  kalman_predict(k);
  kalman_update(k, z);
}

/* ---------------------------------------------------------------------------------------------------
 * Setup
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(init_rejects_bad_input)
{
  Kalman k;
  const double one[KALMAN_N_MAX * KALMAN_N_MAX] = { 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
                                                    0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0 };
  const double bad[1] = { NAN };

  quiet();
  const bool zero = kalman_init(&k, 0, one, one, one, 1.0, one, one);
  const bool five = kalman_init(&k, KALMAN_N_MAX + 1, one, one, one, 1.0, one, one);
  const bool negative_r = kalman_init(&k, 1, one, one, one, -1.0, one, one);
  const bool nan_r = kalman_init(&k, 1, one, one, one, NAN, one, one);
  const bool nan_f = kalman_init(&k, 1, bad, one, one, 1.0, one, one);
  const bool nan_p0 = kalman_init(&k, 1, one, one, one, 1.0, one, bad);
  loud();

  MTS_ASSERT(!zero);
  MTS_ASSERT(!five);
  MTS_ASSERT(!negative_r);
  MTS_ASSERT(!nan_r);
  MTS_ASSERT(!nan_f);
  MTS_ASSERT(!nan_p0);
  MTS_ASSERT(kalman_init(&k, KALMAN_N_MAX, one, one, one, 0.0, one, one)); // r = 0 is allowed
}

/* ---------------------------------------------------------------------------------------------------
 * Closed forms
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(constant_without_process_noise_is_the_weighted_mean)
{
  // Q = 0: the filter is exactly the precision-weighted mean of the prior and the measurements
  const double F = 1.0, H = 1.0, Q = 0.0, r = 4.0, x0 = 0.0, P0 = 100.0;
  Kalman k;
  MTS_ASSERT(kalman_init(&k, 1, &F, &H, &Q, r, &x0, &P0));

  double sum = 0.0;
  const int n = 50;
  for (int i = 0; i < n; i++)
  {
    const double z = 3.0 + 0.1 * sin(i); // any numbers
    sum += z;
    step(&k, z);
  }
  const double info = 1.0 / P0 + n / r;
  MTS_ASSERT_NEAR(k.x[0], (x0 / P0 + sum / r) / info, 1e-12);
  MTS_ASSERT_NEAR(k.P[0][0], 1.0 / info, 1e-15);
}

MTS_TEST(random_walk_reaches_the_riccati_steady_state)
{
  // x' = x + w, z = x + v: P- solves P^2 - q P - q r = 0
  const double q = 0.01, r = 1.0;
  const double F = 1.0, H = 1.0, x0 = 0.0, P0 = 1.0;
  Kalman k;
  MTS_ASSERT(kalman_init(&k, 1, &F, &H, &q, r, &x0, &P0));
  for (int i = 0; i < 2000; i++)
  {
    step(&k, gaussian());
  }
  const double prior = 0.5 * (q + sqrt(q * q + 4.0 * q * r));
  MTS_ASSERT_NEAR(k.innovation_var, prior + r, 1e-12);
  MTS_ASSERT_NEAR(k.P[0][0], prior * r / (prior + r), 1e-12);
}

MTS_TEST(constant_velocity_learns_a_line)
{
  // Position and velocity from positions alone: z = 2 + 0.5 t, noise-free
  const double F[2][2] = { { 1.0, T }, { 0.0, 1.0 } };
  const double H[2] = { 1.0, 0.0 };
  const double Q[2][2] = { { 0.0, 0.0 }, { 0.0, 0.0 } };
  const double x0[2] = { 0.0, 0.0 };
  const double P0[2][2] = { { 100.0, 0.0 }, { 0.0, 100.0 } };
  Kalman k;
  MTS_ASSERT(kalman_init(&k, 2, &F[0][0], H, &Q[0][0], 1e-6, x0, &P0[0][0]));

  int i = 0;
  for (; i < 500; i++)
  {
    step(&k, 2.0 + 0.5 * i * T);
  }
  MTS_ASSERT_NEAR(k.x[1], 0.5, 1e-6);

  // 10 steps ahead lies on the line
  double ahead[10];
  kalman_project(&k, 10, ahead);
  for (int j = 0; j < 10; j++)
  {
    MTS_ASSERT_NEAR(ahead[j], 2.0 + 0.5 * (i - 1 + j + 1) * T, 1e-6);
  }
}

/* ---------------------------------------------------------------------------------------------------
 * What it is for: an oscillator forecasting a tremor-like sine
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(oscillator_forecasts_a_noisy_sine_30_ms_ahead)
{
  // 6 Hz, 0.35 mm peak (what Hat_Sim's tremor integrates to), 5 % noise
  const double hz = 6.0, amp = 0.35e-3, sigma = 0.0175e-3;
  const int horizon = 3; // 30 ms at 100 Hz
  Kalman k;
  MTS_ASSERT(oscillator(&k, hz, 1e-12, sigma * sigma));

  double err2 = 0.0, hold2 = 0.0;
  int scored = 0;
  double made[4][3]; // the last horizon + 1 forecasts, by step % 4
  for (int i = 0; i < 3000; i++)
  {
    const double truth = amp * cos(TWO_PI * hz * i * T + 0.3);

    // Score the forecast made horizon steps ago for this instant, after a 1 s warm-up
    if (i >= 100 + horizon)
    {
      const double e = made[(i - horizon) % 4][horizon - 1] - truth;
      const double h = amp * cos(TWO_PI * hz * (i - horizon) * T + 0.3) - truth; // inversion: hold the value
      err2 += e * e;
      hold2 += h * h;
      scored++;
    }

    step(&k, truth + sigma * gaussian());
    kalman_project(&k, horizon, made[i % 4]);
  }
  // Measured: about 1 % of the amplitude, against 76 % for holding the value
  const double rms = sqrt(err2 / scored);
  const double hold = sqrt(hold2 / scored);
  MTS_ASSERT(rms < 0.05 * amp); // within the measurement noise
  MTS_ASSERT(rms < hold / 10.0); // and an order of magnitude better than not predicting
}

MTS_TEST(new_frequency_through_set_F)
{
  // The signal jumps from 6 to 8 Hz; the model follows when told (WFLC's job later)
  const double amp = 1.0, sigma = 0.02;
  Kalman k;
  MTS_ASSERT(oscillator(&k, 6.0, 1e-6, sigma * sigma));
  int i = 0;
  for (; i < 200; i++)
  {
    step(&k, amp * cos(TWO_PI * 6.0 * i * T) + sigma * gaussian());
  }

  double F[2][2];
  rotation(TWO_PI * 8.0, F);
  kalman_set_F(&k, &F[0][0]);
  double phase = TWO_PI * 6.0 * i * T; // continuous phase across the jump
  for (int j = 0; j < 200; j++, i++)
  {
    phase += TWO_PI * 8.0 * T;
    step(&k, amp * cos(phase) + sigma * gaussian());
  }

  // One step ahead: on the 8 Hz sine
  double ahead[1];
  kalman_project(&k, 1, ahead);
  MTS_ASSERT_NEAR(ahead[0], amp * cos(phase + TWO_PI * 8.0 * T), 0.1 * amp);
}

/* ---------------------------------------------------------------------------------------------------
 * Numerics and contracts
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(covariance_stays_symmetric_positive_for_hours)
{
  // 10^6 steps: 2.8 hours at 100 Hz
  Kalman k;
  MTS_ASSERT(oscillator(&k, 6.0, 1e-12, 1e-10));
  for (int i = 0; i < 1000000; i++)
  {
    step(&k, 1e-4 * cos(TWO_PI * 6.0 * i * T) + 1e-5 * gaussian());
  }
  MTS_ASSERT(isfinite(k.P[0][0]) && isfinite(k.P[1][1]) && isfinite(k.P[0][1]));
  MTS_ASSERT(k.P[0][1] == k.P[1][0]); // exactly: re-symmetrized every step
  MTS_ASSERT(k.P[0][0] > 0.0);
  MTS_ASSERT(k.P[1][1] > 0.0);
  MTS_ASSERT(k.P[0][0] * k.P[1][1] - k.P[0][1] * k.P[1][0] > 0.0);
}

MTS_TEST(covariance_is_exactly_symmetric_after_every_step)
{
  // A 4-state filter with untidy numbers: rounding would make P[i][j] and
  // P[j][i] differ in the last bits without the re-symmetrizing
  const double F[4][4] = { { 0.9, 0.11, -0.07, 0.013 },
                           { -0.05, 0.97, 0.21, 0.0 },
                           { 0.031, -0.19, 0.93, 0.17 },
                           { 0.0, 0.047, -0.15, 0.99 } };
  const double H[4] = { 0.7, -0.3, 0.2, 0.11 };
  const double Q[4][4] = { { 1e-3, 1e-4, 0.0, 0.0 },
                           { 1e-4, 2e-3, 3e-4, 0.0 },
                           { 0.0, 3e-4, 1.5e-3, 1e-4 },
                           { 0.0, 0.0, 1e-4, 1e-3 } };
  const double x0[4] = { 0.1, -0.2, 0.3, 0.05 };
  const double P0[4][4] = { { 1.0, 0.1, 0.0, 0.0 }, { 0.1, 1.3, 0.2, 0.0 }, { 0.0, 0.2, 0.7, 0.1 }, { 0.0, 0.0, 0.1, 0.9 } };
  Kalman k;
  MTS_ASSERT(kalman_init(&k, 4, &F[0][0], H, &Q[0][0], 0.37, x0, &P0[0][0]));

  int asymmetric = 0;
  for (int n = 0; n < 1000; n++)
  {
    kalman_predict(&k);
    kalman_update(&k, gaussian());
    for (int i = 0; i < 4; i++)
    {
      for (int j = i + 1; j < 4; j++)
      {
        asymmetric += k.P[i][j] != k.P[j][i];
      }
    }
  }
  MTS_ASSERT_EQ(asymmetric, 0);
}

MTS_TEST(update_refuses_what_it_cannot_use)
{
  // Nothing observable (H = 0) and no measurement noise: S = 0
  const double F = 1.0, H = 0.0, Q = 0.0, x0 = 1.0, P0 = 0.0;
  Kalman k;
  MTS_ASSERT(kalman_init(&k, 1, &F, &H, &Q, 0.0, &x0, &P0));
  Kalman before = k;
  MTS_ASSERT(!kalman_update(&k, 5.0));
  MTS_ASSERT_MEM_EQ(&before, &k, sizeof(k));

  // A bad sample never reaches the state
  MTS_ASSERT(oscillator(&k, 6.0, 1e-6, 1e-4));
  step(&k, 0.5);
  before = k;
  MTS_ASSERT(!kalman_update(&k, NAN));
  MTS_ASSERT(!kalman_update(&k, INFINITY));
  MTS_ASSERT_MEM_EQ(&before, &k, sizeof(k));
}

MTS_TEST(project_leaves_the_filter_alone)
{
  Kalman k;
  MTS_ASSERT(oscillator(&k, 6.0, 1e-6, 1e-4));
  for (int i = 0; i < 50; i++)
  {
    step(&k, cos(TWO_PI * 6.0 * i * T));
  }
  const Kalman before = k;
  double ahead[8];
  kalman_project(&k, 8, ahead);
  kalman_project(&k, 0, ahead); // nothing asked: nothing written, nothing read
  MTS_ASSERT_MEM_EQ(&before, &k, sizeof(k));
}

MTS_TEST(a_copy_is_an_independent_filter)
{
  // Plain data: copying the struct copies the filter (predictors embed it by value)
  Kalman a;
  MTS_ASSERT(oscillator(&a, 6.0, 1e-6, 1e-4));
  for (int i = 0; i < 100; i++)
  {
    step(&a, cos(TWO_PI * 6.0 * i * T));
  }
  Kalman b;
  memcpy(&b, &a, sizeof(a));

  step(&a, 0.25);
  step(&b, 0.25);
  MTS_ASSERT_MEM_EQ(&a, &b, sizeof(a)); // same input, same filter

  step(&b, 0.75);
  MTS_ASSERT(memcmp(&a, &b, sizeof(a)) != 0); // b moved on, a did not
}

MTS_TEST(reset_restores_the_start)
{
  Kalman k;
  MTS_ASSERT(oscillator(&k, 6.0, 1e-6, 1e-4));
  const Kalman fresh = k;
  for (int i = 0; i < 100; i++)
  {
    step(&k, cos(TWO_PI * 6.0 * i * T));
  }
  const double x0[2] = { 0.0, 0.0 };
  const double P0[2][2] = { { 1e-6, 0.0 }, { 0.0, 1e-6 } };
  kalman_reset(&k, x0, &P0[0][0]);
  MTS_ASSERT_MEM_EQ(&fresh, &k, sizeof(k));
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(init_rejects_bad_input),
  MTS_TEST_ENTRY(constant_without_process_noise_is_the_weighted_mean),
  MTS_TEST_ENTRY(random_walk_reaches_the_riccati_steady_state),
  MTS_TEST_ENTRY(constant_velocity_learns_a_line),
  MTS_TEST_ENTRY(oscillator_forecasts_a_noisy_sine_30_ms_ahead),
  MTS_TEST_ENTRY(new_frequency_through_set_F),
  MTS_TEST_ENTRY(covariance_stays_symmetric_positive_for_hours),
  MTS_TEST_ENTRY(covariance_is_exactly_symmetric_after_every_step),
  MTS_TEST_ENTRY(update_refuses_what_it_cannot_use),
  MTS_TEST_ENTRY(project_leaves_the_filter_alone),
  MTS_TEST_ENTRY(a_copy_is_an_independent_filter),
  MTS_TEST_ENTRY(reset_restores_the_start),
};

MTS_TEST_MAIN(tests)
