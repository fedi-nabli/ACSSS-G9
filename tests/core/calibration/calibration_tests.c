/* ===================================================================================================
 * tests/core/calibration/calibration_tests.c
 *
 * calibration_apply: corrected = m (raw - offset), per sensor.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <core/calibration/calibration.h>

#include <string.h>

static Raw_Sample sample(void)
{
  Raw_Sample s;
  memset(&s, 0, sizeof(s));
  s.seq = 42;
  s.t_ns = 123456789;
  s.flags = SAMPLE_MAG_FRESH | SAMPLE_GAP;
  for (int i = 0; i < 3; i++)
  {
    s.accel[i] = 1.0f + i;
    s.gyro[i] = 0.1f * (i + 1);
    s.mag[i] = 10.0f * (i + 1);
  }
  return s;
}

MTS_TEST(identity_copies)
{
  Calibration cal;
  calibration_identity(&cal);
  const Raw_Sample in = sample();
  Raw_Sample out;
  calibration_apply(&cal, &in, &out);
  MTS_ASSERT_MEM_EQ(&in, &out, sizeof(in));
}

MTS_TEST(offset_then_matrix)
{
  Calibration cal;
  calibration_identity(&cal);
  cal.gyro.offset[0] = 0.1; // a bias on x
  cal.accel.m[0][0] = 2.0; // scale x
  cal.accel.m[1][0] = 0.5; // y picks up half of x (misalignment)
  cal.mag.offset[2] = 5.0; // hard iron on z
  const Raw_Sample in = sample();
  Raw_Sample out;
  calibration_apply(&cal, &in, &out);
  MTS_ASSERT_NEAR(out.gyro[0], 0.0, 1e-7);
  MTS_ASSERT_NEAR(out.accel[0], 2.0, 1e-6);
  MTS_ASSERT_NEAR(out.accel[1], 2.0 + 0.5 * 1.0, 1e-6);
  MTS_ASSERT_NEAR(out.mag[2], 25.0, 1e-5);
  MTS_ASSERT_EQ(out.seq, 42);
  MTS_ASSERT_EQ(out.t_ns, 123456789);
  MTS_ASSERT_EQ(out.flags, SAMPLE_MAG_FRESH | SAMPLE_GAP);
}

MTS_TEST(in_place)
{
  Calibration cal;
  calibration_identity(&cal);
  cal.accel.m[0][1] = 1.0; // x picks up y: must read y before x is overwritten
  Raw_Sample s = sample();
  calibration_apply(&cal, &s, &s);
  MTS_ASSERT_NEAR(s.accel[0], 1.0 + 2.0, 1e-6);
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(identity_copies),
  MTS_TEST_ENTRY(offset_then_matrix),
  MTS_TEST_ENTRY(in_place),
};

MTS_TEST_MAIN(tests)
