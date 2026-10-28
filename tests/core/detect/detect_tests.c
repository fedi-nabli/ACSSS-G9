/* ===================================================================================================
 * tests/core/detect/detect_tests.c
 *
 * Detector v0, fed Separation_Output directly: a tremor-sized sine turns it
 * on with the right frequency and amplitude, small or broadband motion does
 * not, the hysteresis holds, and the strongest axis decides.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <core/detect/detect.h>
#include <utils/logger/logger.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static const double PI = 3.14159265358979323846;
static const double RATE = 100.0;
static const double T = 0.01;
static const double MM = 1e-3;

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

static Detector* make(void)
{
  const Detect_Config c = detect_config_default(RATE);
  return detect_create(&c);
}

static Separation_Output disp(double x, double y, double z)
{
  Separation_Output o;
  memset(&o, 0, sizeof(o));
  o.tremor_disp.v[0] = x;
  o.tremor_disp.v[1] = y;
  o.tremor_disp.v[2] = z;
  return o;
}

MTS_TEST(create_rejects_bad_configs)
{
  const Detect_Config good = detect_config_default(RATE);
  Detect_Config c;
  int refused = 0;
  quiet();
  c = good, c.off_threshold = c.on_threshold, refused += detect_create(&c) == NULL; // no hysteresis
  c = good, c.on_threshold = 1.5, refused += detect_create(&c) == NULL;
  c = good, c.window_s = 0.0, refused += detect_create(&c) == NULL;
  c = good, c.min_amp = -1.0, refused += detect_create(&c) == NULL;
  c = good, c.rate_hz = 0.0, refused += detect_create(&c) == NULL;
  loud();
  MTS_ASSERT_EQ(refused, 5);
  detect_destroy(NULL);
}

MTS_TEST(tremor_is_detected_and_measured)
{
  Detector* d = make();
  MTS_ASSERT(d != NULL);
  Detect_Output out;
  double on_at = -1.0;
  for (int i = 0; i < 500; i++)
  {
    const Separation_Output in = disp(0.35 * MM * sin(2.0 * PI * 6.0 * i * T), 0.0, 0.0);
    detect_update(d, &in, &out);
    if (on_at < 0.0 && out.active)
    {
      on_at = i * T;
    }
  }
  MTS_ASSERT(on_at >= 0.0 && on_at < 1.5);
  MTS_ASSERT(out.active);
  MTS_ASSERT_NEAR(out.hz, 6.0, 0.1);
  MTS_ASSERT_NEAR(out.amp, 0.35 * MM, 0.02 * MM);
  MTS_ASSERT(out.conf > 0.9);
  detect_destroy(d);
}

MTS_TEST(too_small_never_turns_on)
{
  // A clean 6 Hz oscillation, but 0.02 mm: below min_amp (0.05 mm)
  Detector* d = make();
  Detect_Output out;
  for (int i = 0; i < 1000; i++)
  {
    const Separation_Output in = disp(0.02 * MM * sin(2.0 * PI * 6.0 * i * T), 0.0, 0.0);
    detect_update(d, &in, &out);
    MTS_ASSERT(!out.active);
  }
  MTS_ASSERT(out.conf > 0.9); // confident it is an oscillation, just too small to matter
  detect_destroy(d);
}

MTS_TEST(broadband_motion_is_not_tremor)
{
  // White noise of tremor size: plenty of amplitude, no single oscillation
  Detector* d = make();
  Detect_Output out;
  int active = 0;
  for (int i = 0; i < 2000; i++)
  {
    const Separation_Output in = disp(0.35 * MM * gaussian(), 0.0, 0.0);
    detect_update(d, &in, &out);
    active += out.active ? 1 : 0;
  }
  MTS_ASSERT_EQ(active, 0);
  MTS_ASSERT(out.conf < 0.4);
  detect_destroy(d);
}

MTS_TEST(hysteresis_holds_then_lets_go)
{
  // On at 0.35 mm; fading to 0.035 mm (between min/2 and min) keeps it on;
  // 0.01 mm turns it off. One on, one off: no flicker in between
  Detector* d = make();
  Detect_Output out;
  int transitions = 0;
  bool was = false;
  for (int i = 0; i < 2400; i++)
  {
    const double amp = i < 800 ? 0.35 : (i < 1600 ? 0.035 : 0.01);
    const Separation_Output in = disp(amp * MM * sin(2.0 * PI * 6.0 * i * T), 0.0, 0.0);
    detect_update(d, &in, &out);
    if (out.active != was)
    {
      transitions++;
      was = out.active;
    }
    if (i == 1590)
    {
      MTS_ASSERT(out.active); // still on at 0.035 mm
    }
  }
  MTS_ASSERT(!out.active);
  MTS_ASSERT_EQ(transitions, 2);
  detect_destroy(d);
}

MTS_TEST(strongest_axis_decides)
{
  // 5 Hz small on x, 8 Hz large on y: the tremor is the one on y
  Detector* d = make();
  Detect_Output out;
  for (int i = 0; i < 600; i++)
  {
    const Separation_Output in =
      disp(0.06 * MM * sin(2.0 * PI * 5.0 * i * T), 0.4 * MM * sin(2.0 * PI * 8.0 * i * T), 0.0);
    detect_update(d, &in, &out);
  }
  MTS_ASSERT(out.active);
  MTS_ASSERT_NEAR(out.hz, 8.0, 0.1);
  MTS_ASSERT_NEAR(out.amp, 0.4 * MM, 0.03 * MM);
  detect_destroy(d);
}

MTS_TEST(reset_turns_it_off)
{
  Detector* d = make();
  Detect_Output out;
  for (int i = 0; i < 300; i++)
  {
    const Separation_Output in = disp(0.35 * MM * sin(2.0 * PI * 6.0 * i * T), 0.0, 0.0);
    detect_update(d, &in, &out);
  }
  MTS_ASSERT(out.active);
  detect_reset(d);
  const Separation_Output zero = disp(0.0, 0.0, 0.0);
  detect_update(d, &zero, &out);
  MTS_ASSERT(!out.active);
  MTS_ASSERT_NEAR(out.amp, 0.0, 0.0);
  detect_destroy(d);
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(create_rejects_bad_configs),
  MTS_TEST_ENTRY(tremor_is_detected_and_measured),
  MTS_TEST_ENTRY(too_small_never_turns_on),
  MTS_TEST_ENTRY(broadband_motion_is_not_tremor),
  MTS_TEST_ENTRY(hysteresis_holds_then_lets_go),
  MTS_TEST_ENTRY(strongest_axis_decides),
  MTS_TEST_ENTRY(reset_turns_it_off),
};

MTS_TEST_MAIN(tests)
