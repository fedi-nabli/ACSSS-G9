/* ===================================================================================================
 * tests/core/filters/filters_tests.c
 *
 * Biquads and Butterworth chains against the closed form of a digital
 * Butterworth (bilinear, prewarped):
 *   |H(f)|^2 = 1 / (1 + (tan(pi f / fs) / tan(pi fc / fs))^(2N))   low-pass
 * then the response functions against what a sine actually comes out as.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <core/filters/filters.h>
#include <utils/logger/logger.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static const double PI = 3.14159265358979323846;
static const double FS = 100.0;

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

static double butterworth_gain(double f, double fc, uint32_t order, bool high)
{
  double r = tan(PI * f / FS) / tan(PI * fc / FS);
  if (high)
  {
    r = 1.0 / r;
  }
  return 1.0 / sqrt(1.0 + pow(r, 2.0 * order));
}

// Runs a sine through c, returns the steady-state output amplitude and phase (rad, in (-pi, pi])
static void measure(Filter_Chain* c, double f, double* gain, double* phase)
{
  filter_chain_reset(c);
  const int n = 20000;
  double si = 0.0, co = 0.0;
  int used = 0;
  for (int i = 0; i < n; i++)
  {
    const double th = 2.0 * PI * f * i / FS;
    const double y = filter_chain_process(c, sin(th));
    if (i >= n / 2) // transients gone: correlate with sin and cos (an exact number of cycles is not needed at this length)
    {
      si += y * sin(th);
      co += y * cos(th);
      used++;
    }
  }
  *gain = 2.0 * sqrt(si * si + co * co) / used;
  *phase = atan2(co, si);
}

static double wrap(double p)
{
  while (p > PI)
  {
    p -= 2.0 * PI;
  }
  while (p <= -PI)
  {
    p += 2.0 * PI;
  }
  return p;
}

MTS_TEST(designs_reject_bad_parameters)
{
  Biquad b;
  Filter_Chain c;
  filter_chain_init(&c);
  int refused = 0;
  quiet();
  refused += !biquad_lowpass(&b, 0.0, 0.7, FS);
  refused += !biquad_lowpass(&b, 50.0, 0.7, FS); // Nyquist
  refused += !biquad_highpass(&b, 10.0, 0.0, FS);
  refused += !biquad_highpass(&b, NAN, 0.7, FS);
  refused += !filter_chain_add_lowpass(&c, 3, 10.0, FS); // odd
  refused += !filter_chain_add_lowpass(&c, 10, 10.0, FS); // too high
  refused += !filter_chain_add_highpass(&c, 2, -1.0, FS);
  loud();
  MTS_ASSERT_EQ(refused, 7);
  MTS_ASSERT_EQ(c.n, 0); // nothing half-added
}

MTS_TEST(chain_capacity_is_enforced)
{
  Filter_Chain c;
  filter_chain_init(&c);
  MTS_ASSERT(filter_chain_add_highpass(&c, 8, 4.0, FS));
  MTS_ASSERT(filter_chain_add_lowpass(&c, 8, 12.0, FS));
  MTS_ASSERT_EQ(c.n, FILTER_SECTIONS_MAX);
  quiet();
  const bool more = filter_chain_add_lowpass(&c, 2, 20.0, FS);
  loud();
  MTS_ASSERT(!more);
  MTS_ASSERT_EQ(c.n, FILTER_SECTIONS_MAX);
}

MTS_TEST(lowpass_matches_the_butterworth_closed_form)
{
  const uint32_t orders[] = { 2, 4, 8 };
  const double freqs[] = { 0.5, 3.0, 6.0, 10.0, 12.0, 20.0, 35.0, 49.0 };
  for (size_t o = 0; o < 3; o++)
  {
    Filter_Chain c;
    filter_chain_init(&c);
    MTS_ASSERT(filter_chain_add_lowpass(&c, orders[o], 12.0, FS));
    for (size_t i = 0; i < sizeof(freqs) / sizeof(freqs[0]); i++)
    {
      double g, p;
      filter_chain_response(&c, freqs[i], FS, &g, &p);
      MTS_ASSERT_NEAR(g, butterworth_gain(freqs[i], 12.0, orders[o], false), 1e-9);
    }
  }
}

MTS_TEST(highpass_matches_the_butterworth_closed_form)
{
  const uint32_t orders[] = { 2, 4, 8 };
  const double freqs[] = { 0.5, 2.0, 4.0, 6.0, 12.0, 30.0 };
  for (size_t o = 0; o < 3; o++)
  {
    Filter_Chain c;
    filter_chain_init(&c);
    MTS_ASSERT(filter_chain_add_highpass(&c, orders[o], 4.0, FS));
    for (size_t i = 0; i < sizeof(freqs) / sizeof(freqs[0]); i++)
    {
      double g, p;
      filter_chain_response(&c, freqs[i], FS, &g, &p);
      MTS_ASSERT_NEAR(g, butterworth_gain(freqs[i], 4.0, orders[o], true), 1e-9);
    }
  }
}

MTS_TEST(response_is_what_a_sine_comes_out_as)
{
  // A 4-12 Hz band-pass: the formula must agree with the filter actually running
  Filter_Chain c;
  filter_chain_init(&c);
  MTS_ASSERT(filter_chain_add_highpass(&c, 2, 4.0, FS));
  MTS_ASSERT(filter_chain_add_lowpass(&c, 2, 12.0, FS));
  const double freqs[] = { 2.0, 6.0, 8.0, 15.0 };
  for (size_t i = 0; i < 4; i++)
  {
    double g_formula, p_formula, g_run, p_run;
    filter_chain_response(&c, freqs[i], FS, &g_formula, &p_formula);
    measure(&c, freqs[i], &g_run, &p_run);
    MTS_ASSERT_NEAR(g_run, g_formula, 2e-3);
    MTS_ASSERT_NEAR(wrap(p_run - p_formula), 0.0, 2e-3);
  }
}

MTS_TEST(group_delay_of_a_lowpass_at_low_frequency)
{
  // Analog 2nd-order Butterworth: group delay sqrt(2) / wc as f -> 0; the
  // digital one at fc << fs is within a fraction of a percent
  Filter_Chain c;
  filter_chain_init(&c);
  MTS_ASSERT(filter_chain_add_lowpass(&c, 2, 2.0, FS));
  const double expected = sqrt(2.0) / (2.0 * PI * 2.0);
  MTS_ASSERT_NEAR(filter_chain_group_delay_s(&c, 0.05, FS), expected, 0.01 * expected);
}

MTS_TEST(empty_chain_passes_through)
{
  Filter_Chain c;
  filter_chain_init(&c);
  MTS_ASSERT_NEAR(filter_chain_process(&c, 1.25), 1.25, 0.0);
  double g, p;
  filter_chain_response(&c, 5.0, FS, &g, &p);
  MTS_ASSERT_NEAR(g, 1.0, 0.0);
  MTS_ASSERT_NEAR(filter_chain_group_delay_s(&c, 5.0, FS), 0.0, 0.0);
}

MTS_TEST(reset_and_copies)
{
  Filter_Chain a;
  filter_chain_init(&a);
  MTS_ASSERT(filter_chain_add_lowpass(&a, 4, 10.0, FS));
  const Filter_Chain fresh = a;
  for (int i = 0; i < 50; i++)
  {
    filter_chain_process(&a, sin(i * 0.3));
  }
  Filter_Chain b = a; // plain data: an independent filter
  MTS_ASSERT_NEAR(filter_chain_process(&a, 0.5), filter_chain_process(&b, 0.5), 0.0);
  filter_chain_reset(&a);
  MTS_ASSERT_MEM_EQ(&fresh, &a, sizeof(a));
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(designs_reject_bad_parameters),
  MTS_TEST_ENTRY(chain_capacity_is_enforced),
  MTS_TEST_ENTRY(lowpass_matches_the_butterworth_closed_form),
  MTS_TEST_ENTRY(highpass_matches_the_butterworth_closed_form),
  MTS_TEST_ENTRY(response_is_what_a_sine_comes_out_as),
  MTS_TEST_ENTRY(group_delay_of_a_lowpass_at_low_frequency),
  MTS_TEST_ENTRY(empty_chain_passes_through),
  MTS_TEST_ENTRY(reset_and_copies),
};

MTS_TEST_MAIN(tests)
