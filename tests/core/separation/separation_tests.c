/* ===================================================================================================
 * tests/core/separation/separation_tests.c
 *
 * Separation v0 on synthetic linear acceleration: a tremor sine must come
 * out as the displacement it integrates to (A / w^2), voluntary motion and
 * buzz must not, and nothing may drift however long it runs.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <core/separation/separation.h>
#include <utils/logger/logger.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static const double PI = 3.14159265358979323846;
static const double RATE = 100.0;
static const double T = 0.01;

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

static Separation* make(void)
{
  const Separation_Config c = separation_config_default(RATE);
  return separation_create(&c);
}

/* Feeds a = amp sin(2 pi f t) on x for n samples; returns the peak of
 * tremor_disp.x over the last second (steady state) */
static double disp_peak(Separation* s, double f, double amp, int n)
{
  Separation_Output out;
  double peak = 0.0;
  for (int i = 0; i < n; i++)
  {
    const Vec3 a = { { amp * sin(2.0 * PI * f * i * T), 0.0, 0.0 } };
    separation_update(s, &a, &out);
    if (i >= n - 100)
    {
      const double d = fabs(out.tremor_disp.v[0]);
      peak = d > peak ? d : peak;
    }
  }
  return peak;
}

MTS_TEST(create_rejects_bad_configs)
{
  const Separation_Config good = separation_config_default(RATE);
  Separation_Config c;
  int refused = 0;
  quiet();
  c = good, c.band_lo_hz = 12.0, refused += separation_create(&c) == NULL; // lo == hi
  c = good, c.band_hi_hz = 50.0, refused += separation_create(&c) == NULL; // Nyquist
  c = good, c.band_lo_hz = 0.0, refused += separation_create(&c) == NULL;
  c = good, c.filter_order = 3, refused += separation_create(&c) == NULL;
  c = good, c.estimator = SEPARATION_WFLC, refused += separation_create(&c) == NULL; // not in v0
  c = good, c.rate_hz = NAN, refused += separation_create(&c) == NULL;
  loud();
  MTS_ASSERT_EQ(refused, 6);
  separation_destroy(NULL);
}

MTS_TEST(tremor_becomes_its_displacement)
{
  // a = A sin(wt) integrates to x = -A / w^2 sin(wt): exact at the band
  // centre (6.93 Hz), within a few % across the middle of the band
  const double amp = 0.5; // m/s^2, Hat_Sim's tremor
  const double freqs[] = { 6.0, sqrt(48.0), 8.0 };
  const double tol[] = { 0.03, 0.005, 0.03 };
  for (int k = 0; k < 3; k++)
  {
    Separation* s = make();
    MTS_ASSERT(s != NULL);
    const double w = 2.0 * PI * freqs[k];
    const double peak = disp_peak(s, freqs[k], amp, 1000);
    MTS_ASSERT_NEAR(peak, amp / (w * w), tol[k] * amp / (w * w));
    separation_destroy(s);
  }
}

MTS_TEST(tremor_acceleration_passes_at_unit_gain)
{
  Separation* s = make();
  Separation_Output out;
  double peak = 0.0;
  const double f = sqrt(48.0);
  for (int i = 0; i < 1000; i++)
  {
    const Vec3 a = { { 0.0, 0.5 * sin(2.0 * PI * f * i * T), 0.0 } };
    separation_update(s, &a, &out);
    if (i >= 900)
    {
      peak = fabs(out.tremor_acc.v[1]) > peak ? fabs(out.tremor_acc.v[1]) : peak;
    }
  }
  MTS_ASSERT_NEAR(peak, 0.5, 0.005);
  separation_destroy(s);
}

MTS_TEST(voluntary_motion_and_buzz_are_rejected)
{
  // 2 m/s^2 at 0.5 Hz is 200 mm of displacement; 0.5 m/s^2 at 30 Hz is 14 um
  Separation* slow = make();
  Separation* fast = make();
  const double out_slow = disp_peak(slow, 0.5, 2.0, 3000);
  const double out_fast = disp_peak(fast, 30.0, 0.5, 1000);
  // What is left of the 200 mm: the band-pass's high-pass passes (0.5/4)^2,
  // each drift high-pass (0.5/2)^2: about 0.014 mm, under detect's 0.05 mm floor
  MTS_ASSERT(out_slow < 0.02e-3);
  const double w30 = 2.0 * PI * 30.0;
  MTS_ASSERT(out_fast < 0.1 * 0.5 / (w30 * w30)); // less than 10 % of what 30 Hz would integrate to
  separation_destroy(slow);
  separation_destroy(fast);
}

MTS_TEST(nothing_drifts_over_hours)
{
  // A gravity residue (0.3 m/s^2 constant) alone: a plain double integrator
  // would be 1.5 km away after 10^6 samples. Here it settles to nothing
  Separation* s = make();
  Separation_Output out;
  for (int i = 0; i < 1000000; i++)
  {
    const Vec3 a = { { 0.3, -0.1, 0.0 } };
    separation_update(s, &a, &out);
  }
  MTS_ASSERT(fabs(out.tremor_disp.v[0]) < 1e-12);
  MTS_ASSERT(fabs(out.tremor_disp.v[1]) < 1e-12);
  separation_destroy(s);

  // With noise on top: the displacement noise is the same in hour 3 as in
  // minute 1 (stationary, not growing), and never anywhere near tremor size
  s = make();
  double early = 0.0, late = 0.0, worst = 0.0;
  int n_early = 0, n_late = 0;
  for (int i = 0; i < 1000000; i++)
  {
    const Vec3 a = { { 0.3 + 0.02 * gaussian(), -0.1, 0.05 * gaussian() } };
    separation_update(s, &a, &out);
    const double d = out.tremor_disp.v[2];
    if (i > 1000 && i <= 101000)
    {
      early += d * d;
      n_early++;
    }
    if (i >= 900000)
    {
      late += d * d;
      n_late++;
    }
    for (int k = 0; k < 3 && i > 1000; k++)
    {
      worst = fabs(out.tremor_disp.v[k]) > worst ? fabs(out.tremor_disp.v[k]) : worst;
    }
  }
  const double rms_early = sqrt(early / n_early), rms_late = sqrt(late / n_late);
  MTS_ASSERT_NEAR(rms_late / rms_early, 1.0, 0.1); // measured: 0.017 mm both
  MTS_ASSERT(worst < 0.15e-3); // measured: 0.08 mm, the noise's peak over 2.8 h
  separation_destroy(s);
}

MTS_TEST(reports_its_delay)
{
  // 2nd-order band-pass and two drift high-passes: tens of ms at the centre
  Separation* s = make();
  Separation_Output out;
  const Vec3 zero = { { 0.0, 0.0, 0.0 } };
  separation_update(s, &zero, &out);
  MTS_ASSERT_GT(out.delay_ns, 5 * 1000000LL);
  MTS_ASSERT_LT(out.delay_ns, 80 * 1000000LL);
  separation_destroy(s);
}

MTS_TEST(reset_forgets_the_past)
{
  Separation* a = make();
  Separation* b = make();
  disp_peak(a, 6.0, 0.5, 500); // a has history, b none
  separation_reset(a);
  Separation_Output oa, ob;
  for (int i = 0; i < 50; i++)
  {
    const Vec3 x = { { sin(i * 0.4), cos(i * 0.3), 0.1 } };
    separation_update(a, &x, &oa);
    separation_update(b, &x, &ob);
  }
  MTS_ASSERT_MEM_EQ(&oa, &ob, sizeof(oa));
  separation_destroy(a);
  separation_destroy(b);
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(create_rejects_bad_configs),
  MTS_TEST_ENTRY(tremor_becomes_its_displacement),
  MTS_TEST_ENTRY(tremor_acceleration_passes_at_unit_gain),
  MTS_TEST_ENTRY(voluntary_motion_and_buzz_are_rejected),
  MTS_TEST_ENTRY(nothing_drifts_over_hours),
  MTS_TEST_ENTRY(reports_its_delay),
  MTS_TEST_ENTRY(reset_forgets_the_past),
};

MTS_TEST_MAIN(tests)
