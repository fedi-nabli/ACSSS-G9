/* ===================================================================================================
 * tests/core/wflc/wflc_tests.c
 *
 * The WFLC with its default steps on tremor-like signals: locking onto a
 * sine from a wrong start, following a frequency step, noise, harmonics,
 * forecasting. Then the design decisions one by one: normalization (the
 * same trajectory at any amplitude), the band clamp, phase wrapping over
 * hours, and the plain-data contracts. Noise is deterministic.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <core/wflc/wflc.h>
#include <utils/logger/logger.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static const double TWO_PI = 6.283185307179586;
static const double RATE = 100.0;
static const double T = 0.01;
static const double AMP = 0.35e-3; // 0.35 mm: Hat_Sim's tremor, as displacement

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

// Default config starting at f0_hz
static bool make(Wflc* w, double f0_hz)
{
  Wflc_Config c = wflc_config_default(RATE);
  c.f0_hz = f0_hz;
  return wflc_init(w, &c);
}

/* ---------------------------------------------------------------------------------------------------
 * Setup
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(init_rejects_bad_configs)
{
  Wflc w;
  const Wflc_Config good = wflc_config_default(RATE);
  MTS_ASSERT(wflc_init(&w, &good));

  Wflc_Config c;
  int refused = 0;
  quiet();
  c = good, c.rate_hz = 0.0, refused += !wflc_init(&w, &c);
  c = good, c.n_harmonics = 0, refused += !wflc_init(&w, &c);
  c = good, c.n_harmonics = WFLC_HARMONICS_MAX + 1, refused += !wflc_init(&w, &c);
  c = good, c.mu_freq = 0.0, refused += !wflc_init(&w, &c);
  c = good, c.mu_amp = -0.1, refused += !wflc_init(&w, &c);
  c = good, c.f_min_hz = 0.0, refused += !wflc_init(&w, &c);
  c = good, c.f0_hz = 2.0, refused += !wflc_init(&w, &c); // below f_min
  c = good, c.f0_hz = 15.0, refused += !wflc_init(&w, &c); // above f_max
  c = good, c.rate_hz = 50.0, c.n_harmonics = 2, refused += !wflc_init(&w, &c); // 2 x 14 Hz >= 25 Hz
  c = good, c.power_tau_s = 0.0, refused += !wflc_init(&w, &c);
  c = good, c.power_floor = -1.0, refused += !wflc_init(&w, &c);
  c = good, c.mu_freq = NAN, refused += !wflc_init(&w, &c);
  loud();
  MTS_ASSERT_EQ(refused, 12);
}

MTS_TEST(first_sample_is_normalized_to_unit_size)
{
  // Bias-corrected power: the first sample divides by itself, not by sqrt(alpha) times itself
  Wflc w;
  MTS_ASSERT(make(&w, 6.0));
  wflc_update(&w, 5e-4);
  MTS_ASSERT_NEAR(w.scale, 5e-4, 1e-12);
}

MTS_TEST(not_locked_before_it_has_learned)
{
  // The weights start at zero: the first samples are all error. The ratio
  // must say so, not mistake its own short averages for a lock
  Wflc w;
  MTS_ASSERT(make(&w, 6.0));
  for (int i = 0; i < 5; i++)
  {
    wflc_update(&w, AMP * cos(TWO_PI * 6.0 * i * T + 0.3));
  }
  MTS_ASSERT(wflc_error_ratio(&w) > 0.5);
}

MTS_TEST(frequency_step_follows_the_gradient)
{
  // One update from a known state, against the formula by hand. Each
  // harmonic r pulls the frequency with weight r: harmonic 2 turns twice as
  // fast as the fundamental when the frequency changes
  Wflc_Config c = wflc_config_default(RATE);
  c.n_harmonics = 2;
  Wflc w;
  MTS_ASSERT(wflc_init(&w, &c));
  wflc_update(&w, 1e-3); // power settled on 1e-3: sn = 1 for the next sample
  w.w[0] = 0.3; // sin 1
  w.w[1] = -0.2; // sin 2
  w.w[2] = 0.5; // cos 1
  w.w[3] = 0.1; // cos 2
  const double omega = w.omega;
  const double phi = w.phase + omega;
  const double fit = 0.3 * sin(phi) - 0.2 * sin(2 * phi) + 0.5 * cos(phi) + 0.1 * cos(2 * phi);
  const double grad = 1.0 * (0.3 * cos(phi) - 0.5 * sin(phi)) + 2.0 * (-0.2 * cos(2 * phi) - 0.1 * sin(2 * phi));

  wflc_update(&w, 1e-3);
  const double sn = 1e-3 / w.scale;
  const double e = sn - fit;
  MTS_ASSERT_NEAR(w.omega - omega, 2.0 * c.mu_freq * e * grad, 1e-15);
}

/* ---------------------------------------------------------------------------------------------------
 * On tremor-like signals
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(locks_onto_a_sine_from_a_wrong_start)
{
  Wflc w;
  MTS_ASSERT(make(&w, 8.0)); // the tremor is at 6 Hz
  double locked_at = -1.0;
  double worst_after = 0.0;
  for (int i = 0; i < 600; i++)
  {
    wflc_update(&w, AMP * cos(TWO_PI * 6.0 * i * T + 0.3));
    const double dev = fabs(wflc_frequency_hz(&w) - 6.0);
    if (locked_at < 0.0 && dev < 0.1)
    {
      locked_at = i * T;
    }
    if (i >= 300 && dev > worst_after)
    {
      worst_after = dev;
    }
  }
  MTS_ASSERT(locked_at >= 0.0 && locked_at < 2.0);
  MTS_ASSERT(worst_after < 0.02); // and stays there
  MTS_ASSERT(wflc_error_ratio(&w) < 0.05); // the fit holds the signal
  MTS_ASSERT_EQ(wflc_updates(&w), 600);
}

MTS_TEST(follows_a_frequency_step)
{
  Wflc w;
  MTS_ASSERT(make(&w, 6.0));
  double phase = 0.0;
  for (int i = 0; i < 1000; i++)
  {
    phase += TWO_PI * (i < 600 ? 6.0 : 8.0) * T; // continuous phase, 6 -> 8 Hz at 6 s
    wflc_update(&w, AMP * cos(phase));
  }
  MTS_ASSERT_NEAR(wflc_frequency_hz(&w), 8.0, 0.05);
}

MTS_TEST(holds_the_frequency_in_noise)
{
  // 10 % noise: the estimate wanders, but not far
  Wflc w;
  MTS_ASSERT(make(&w, 8.0));
  double worst = 0.0;
  for (int i = 0; i < 1000; i++)
  {
    wflc_update(&w, AMP * cos(TWO_PI * 6.0 * i * T) + 0.1 * AMP * gaussian());
    if (i >= 300)
    {
      const double dev = fabs(wflc_frequency_hz(&w) - 6.0);
      worst = dev > worst ? dev : worst;
    }
  }
  MTS_ASSERT(worst < 0.2);
}

MTS_TEST(second_harmonic_needs_two)
{
  // A distorted tremor: the fundamental plus 40 % of its second harmonic
  Wflc_Config c = wflc_config_default(RATE);
  Wflc one;
  MTS_ASSERT(wflc_init(&one, &c));
  c.n_harmonics = 2;
  Wflc two;
  MTS_ASSERT(wflc_init(&two, &c));
  for (int i = 0; i < 1000; i++)
  {
    const double th = TWO_PI * 6.0 * i * T;
    const double s = AMP * (cos(th) + 0.4 * cos(2.0 * th + 1.0));
    wflc_update(&one, s);
    wflc_update(&two, s);
  }
  // One harmonic cannot fit the second: about 0.08 / 0.58 of the power is left
  MTS_ASSERT(wflc_error_ratio(&one) > 0.08);
  MTS_ASSERT(wflc_error_ratio(&two) < 0.02);
  MTS_ASSERT_NEAR(wflc_frequency_hz(&two), 6.0, 0.05);
}

MTS_TEST(forecasts_30_ms_ahead)
{
  // 10 % noise. Score each 3-step forecast against the clean sine it was for
  Wflc w;
  MTS_ASSERT(make(&w, 8.0));
  double made[4][3];
  double err2 = 0.0, hold2 = 0.0;
  int scored = 0;
  for (int i = 0; i < 1000; i++)
  {
    const double truth = AMP * cos(TWO_PI * 6.0 * i * T + 0.3);
    if (i >= 200 + 3)
    {
      const double e = made[(i - 3) % 4][2] - truth;
      const double h = AMP * cos(TWO_PI * 6.0 * (i - 3) * T + 0.3) - truth; // holding the value
      err2 += e * e;
      hold2 += h * h;
      scored++;
    }
    wflc_update(&w, truth + 0.1 * AMP * gaussian());
    wflc_project(&w, 3, made[i % 4]);
  }
  // Measured: about 3 % of the amplitude (below the noise), against 76 % for holding
  const double rms = sqrt(err2 / scored);
  const double hold = sqrt(hold2 / scored);
  MTS_ASSERT(rms < 0.06 * AMP);
  MTS_ASSERT(rms < hold / 10.0);
}

/* ---------------------------------------------------------------------------------------------------
 * The design decisions
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(same_trajectory_at_any_amplitude)
{
  // Normalization: 0.1 mm or 1 (any unit), the same signal shape gives the
  // same frequency path. Only the error scales, by the amplitude ratio
  Wflc small;
  Wflc big;
  MTS_ASSERT(make(&small, 8.0));
  MTS_ASSERT(make(&big, 8.0));
  double worst = 0.0;
  for (int i = 0; i < 1000; i++)
  {
    const double shape = cos(TWO_PI * 6.0 * i * T) + 0.1 * gaussian();
    const double e_small = wflc_update(&small, 1e-4 * shape);
    const double e_big = wflc_update(&big, 1.0 * shape);
    const double d = fabs(wflc_frequency_hz(&small) - wflc_frequency_hz(&big));
    worst = d > worst ? d : worst;
    MTS_ASSERT_NEAR(e_small * 1e4, e_big, 1e-6 + 1e-6 * fabs(e_big));
  }
  MTS_ASSERT(worst < 1e-6);
}

MTS_TEST(frequency_stays_in_its_band)
{
  // A 1 Hz drift and a 20 Hz buzz: neither drags the estimate out of 3-14 Hz
  Wflc low;
  Wflc high;
  MTS_ASSERT(make(&low, 6.0));
  MTS_ASSERT(make(&high, 6.0));
  for (int i = 0; i < 2000; i++)
  {
    wflc_update(&low, AMP * cos(TWO_PI * 1.0 * i * T));
    wflc_update(&high, AMP * cos(TWO_PI * 20.0 * i * T));
    MTS_ASSERT(wflc_frequency_hz(&low) >= 3.0 - 1e-9);
    MTS_ASSERT(wflc_frequency_hz(&high) <= 14.0 + 1e-9);
  }
  MTS_ASSERT_NEAR(wflc_frequency_hz(&low), 3.0, 1e-9); // pinned at the edge it was pulled to
}

MTS_TEST(silence_is_harmless)
{
  // All zeros: the floor keeps the division finite, nothing moves
  Wflc w;
  MTS_ASSERT(make(&w, 6.0));
  double out[3];
  for (int i = 0; i < 500; i++)
  {
    MTS_ASSERT_NEAR(wflc_update(&w, 0.0), 0.0, 0.0);
  }
  wflc_project(&w, 3, out);
  MTS_ASSERT_NEAR(wflc_frequency_hz(&w), 6.0, 1e-12);
  MTS_ASSERT_NEAR(out[2], 0.0, 0.0);
  MTS_ASSERT_NEAR(wflc_error_ratio(&w), 1.0, 0.0); // no signal: not locked
}

MTS_TEST(still_locked_after_hours)
{
  // 10^6 samples (2.8 h): the wrapped phase keeps sin/cos exact, the fit as good as early on
  Wflc w;
  MTS_ASSERT(make(&w, 8.0));
  double early = 0.0;
  for (int i = 0; i < 1000000; i++)
  {
    wflc_update(&w, AMP * cos(TWO_PI * 6.0 * i * T + 0.3));
    MTS_ASSERT(w.phase >= 0.0 && w.phase < TWO_PI);
    if (i == 999)
    {
      early = wflc_error_ratio(&w);
    }
  }
  MTS_ASSERT_NEAR(wflc_frequency_hz(&w), 6.0, 0.01);
  MTS_ASSERT(wflc_error_ratio(&w) <= early * 1.1 + 1e-6);
}

/* ---------------------------------------------------------------------------------------------------
 * Contracts
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(bad_samples_are_ignored)
{
  Wflc w;
  MTS_ASSERT(make(&w, 6.0));
  for (int i = 0; i < 100; i++)
  {
    wflc_update(&w, AMP * cos(TWO_PI * 6.0 * i * T));
  }
  const Wflc before = w;
  MTS_ASSERT_NEAR(wflc_update(&w, NAN), 0.0, 0.0);
  MTS_ASSERT_NEAR(wflc_update(&w, -INFINITY), 0.0, 0.0);
  MTS_ASSERT_MEM_EQ(&before, &w, sizeof(w));
}

MTS_TEST(project_changes_nothing)
{
  Wflc w;
  MTS_ASSERT(make(&w, 6.0));
  for (int i = 0; i < 100; i++)
  {
    wflc_update(&w, AMP * cos(TWO_PI * 6.0 * i * T));
  }
  const Wflc before = w;
  double out[8];
  wflc_project(&w, 8, out);
  wflc_project(&w, 0, out);
  MTS_ASSERT_MEM_EQ(&before, &w, sizeof(w));
}

MTS_TEST(a_copy_is_an_independent_wflc)
{
  Wflc a;
  MTS_ASSERT(make(&a, 6.0));
  for (int i = 0; i < 100; i++)
  {
    wflc_update(&a, AMP * cos(TWO_PI * 6.0 * i * T));
  }
  Wflc b;
  memcpy(&b, &a, sizeof(a));
  wflc_update(&a, 1e-4);
  wflc_update(&b, 1e-4);
  MTS_ASSERT_MEM_EQ(&a, &b, sizeof(a));
  wflc_update(&b, 2e-4);
  MTS_ASSERT(memcmp(&a, &b, sizeof(a)) != 0);
}

MTS_TEST(reset_restores_the_start)
{
  Wflc w;
  MTS_ASSERT(make(&w, 6.0));
  const Wflc fresh = w;
  for (int i = 0; i < 300; i++)
  {
    wflc_update(&w, AMP * cos(TWO_PI * 7.0 * i * T));
  }
  wflc_reset(&w);
  MTS_ASSERT_MEM_EQ(&fresh, &w, sizeof(w));
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(init_rejects_bad_configs),
  MTS_TEST_ENTRY(first_sample_is_normalized_to_unit_size),
  MTS_TEST_ENTRY(not_locked_before_it_has_learned),
  MTS_TEST_ENTRY(frequency_step_follows_the_gradient),
  MTS_TEST_ENTRY(locks_onto_a_sine_from_a_wrong_start),
  MTS_TEST_ENTRY(follows_a_frequency_step),
  MTS_TEST_ENTRY(holds_the_frequency_in_noise),
  MTS_TEST_ENTRY(second_harmonic_needs_two),
  MTS_TEST_ENTRY(forecasts_30_ms_ahead),
  MTS_TEST_ENTRY(same_trajectory_at_any_amplitude),
  MTS_TEST_ENTRY(frequency_stays_in_its_band),
  MTS_TEST_ENTRY(silence_is_harmless),
  MTS_TEST_ENTRY(still_locked_after_hours),
  MTS_TEST_ENTRY(bad_samples_are_ignored),
  MTS_TEST_ENTRY(project_changes_nothing),
  MTS_TEST_ENTRY(a_copy_is_an_independent_wflc),
  MTS_TEST_ENTRY(reset_restores_the_start),
};

MTS_TEST_MAIN(tests)
