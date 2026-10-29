/* ===================================================================================================
 * tests/core/fusion/fusion_tests.c
 *
 * Fusion against synthetic motion: a true attitude q(t) and angular rate
 * are chosen, and the three sensors are computed from them exactly as the
 * Hat would measure (specific force, field, rate + bias), in the body
 * frame. NWU world: x north, y west, z up. Field: Uppsala-like, 16 uT
 * north, 48 uT down.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <core/fusion/fusion.h>
#include <utils/logger/logger.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static const double PI = 3.14159265358979323846;
static const double G = 9.80665;
static const double RATE = 100.0;
static const double DT = 0.01;
static const double FIELD[3] = { 16.0, 0.0, -48.0 }; // world, uT
static const double DEG = 3.14159265358979323846 / 180.0;

/* ---------------------------------------------------------------------------------------------------
 * Quaternions (Hamilton, w first) and the synthetic sensors
 * ------------------------------------------------------------------------------------------------- */

static void quat_mul(const double a[4], const double b[4], double out[4])
{
  out[0] = a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3];
  out[1] = a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2];
  out[2] = a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1];
  out[3] = a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0];
}

static void quat_axis_angle(double ax, double ay, double az, double angle, double q[4])
{
  const double n = sqrt(ax * ax + ay * ay + az * az);
  q[0] = cos(0.5 * angle);
  q[1] = ax / n * sin(0.5 * angle);
  q[2] = ay / n * sin(0.5 * angle);
  q[3] = az / n * sin(0.5 * angle);
}

// world -> body: R(q)' v
static void to_body(const double q[4], const double v[3], double out[3])
{
  const double w = q[0], x = q[1], y = q[2], z = q[3];
  out[0] = (1 - 2 * (y * y + z * z)) * v[0] + 2 * (x * y + w * z) * v[1] + 2 * (x * z - w * y) * v[2];
  out[1] = 2 * (x * y - w * z) * v[0] + (1 - 2 * (x * x + z * z)) * v[1] + 2 * (y * z + w * x) * v[2];
  out[2] = 2 * (x * z + w * y) * v[0] + 2 * (y * z - w * x) * v[1] + (1 - 2 * (x * x + y * y)) * v[2];
}

// Angle between two attitudes, rad
static double angle_between(const double a[4], const Quat* b)
{
  double d = fabs(a[0] * b->q[0] + a[1] * b->q[1] + a[2] * b->q[2] + a[3] * b->q[3]);
  d = d > 1.0 ? 1.0 : d;
  return 2.0 * acos(d);
}

// Tilt error only: the angle between the true and estimated "up" in the body frame
static double tilt_error(const double q_true[4], const Quat* est)
{
  const double up[3] = { 0.0, 0.0, 1.0 };
  double a[3], b[3];
  to_body(q_true, up, a);
  const double qe[4] = { est->q[0], est->q[1], est->q[2], est->q[3] };
  to_body(qe, up, b);
  double d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  d = d > 1.0 ? 1.0 : d;
  return acos(d);
}

/* What the Hat measures at attitude q, turning at body rate w, with linear
 * acceleration lin (world), a gyro bias, and the mag fresh or not. */
static Raw_Sample sense(const double q[4], const double w_body[3], const double lin_world[3], double bias,
                        bool mag_fresh, const double field[3])
{
  Raw_Sample s;
  memset(&s, 0, sizeof(s));
  const double f_world[3] = { lin_world[0], lin_world[1], lin_world[2] + G }; // specific force
  double f[3], m[3];
  to_body(q, f_world, f);
  to_body(q, field, m);
  for (int i = 0; i < 3; i++)
  {
    s.accel[i] = (float)f[i];
    s.gyro[i] = (float)(w_body[i] + bias);
    s.mag[i] = (float)m[i];
  }
  s.flags = mag_fresh ? SAMPLE_MAG_FRESH : 0;
  return s;
}

// Advances q by a constant body rate over one step (the truth, exact)
static void advance(double q[4], const double w_body[3])
{
  const double n = sqrt(w_body[0] * w_body[0] + w_body[1] * w_body[1] + w_body[2] * w_body[2]);
  if (n == 0.0)
  {
    return;
  }
  double dq[4], out[4];
  quat_axis_angle(w_body[0], w_body[1], w_body[2], n * DT, dq);
  quat_mul(q, dq, out);
  memcpy(q, out, sizeof(out));
}

static Fusion* make(void)
{
  const Fusion_Config c = fusion_config_default(RATE);
  return fusion_create(&c);
}

static const double ZERO[3] = { 0.0, 0.0, 0.0 };

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

/* ---------------------------------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(create_rejects_bad_configs)
{
  const Fusion_Config good = fusion_config_default(RATE);
  Fusion_Config c;
  int refused = 0;
  quiet();
  c = good, c.rate_hz = 0.0, refused += fusion_create(&c) == NULL;
  c = good, c.kp = -1.0, refused += fusion_create(&c) == NULL;
  c = good, c.ki = NAN, refused += fusion_create(&c) == NULL;
  c = good, c.accel_gate = 0.0, refused += fusion_create(&c) == NULL;
  c = good, c.settle_s = -1.0, refused += fusion_create(&c) == NULL;
  loud();
  MTS_ASSERT_EQ(refused, 5);
  fusion_destroy(NULL); // fine
}

MTS_TEST(flat_board_facing_north_is_identity)
{
  Fusion* f = make();
  MTS_ASSERT(f != NULL);
  const double q[4] = { 1.0, 0.0, 0.0, 0.0 };
  Fusion_Output out;
  for (int i = 0; i < 500; i++)
  {
    Raw_Sample s = sense(q, ZERO, ZERO, 0.0, i % 2 == 0, FIELD);
    fusion_update(f, &s, &out);
    if (i == 0)
    {
      MTS_ASSERT(angle_between(q, &out.q) < 0.1 * DEG); // set straight from the first sample
      MTS_ASSERT_EQ(out.flags & FUSION_CONVERGED, 0); // but not called settled yet
    }
  }
  MTS_ASSERT(angle_between(q, &out.q) < 0.1 * DEG);
  MTS_ASSERT_NE(out.flags & FUSION_CONVERGED, 0);
  for (int a = 0; a < 3; a++)
  {
    MTS_ASSERT_NEAR(out.lin_acc_body.v[a], 0.0, 1e-4);
  }
  MTS_ASSERT_EQ(out.delay_ns, 0);
  fusion_destroy(f);
}

MTS_TEST(any_attitude_from_the_first_sample)
{
  // Tilted 30 deg about x, pitched 20 deg, yawed 70 deg: found at once, and held
  double qx[4], qy[4], qz[4], t[4], q[4];
  quat_axis_angle(1, 0, 0, 30 * DEG, qx);
  quat_axis_angle(0, 1, 0, 20 * DEG, qy);
  quat_axis_angle(0, 0, 1, 70 * DEG, qz);
  quat_mul(qz, qy, t);
  quat_mul(t, qx, q);

  Fusion* f = make();
  Fusion_Output out;
  for (int i = 0; i < 1000; i++)
  {
    Raw_Sample s = sense(q, ZERO, ZERO, 0.0, i % 2 == 0, FIELD);
    fusion_update(f, &s, &out);
    MTS_ASSERT(angle_between(q, &out.q) < 0.2 * DEG);
  }
  fusion_destroy(f);
}

MTS_TEST(follows_a_turn)
{
  // 90 deg/s about a slanted body axis for 4 s: a full turn, tracked within a degree
  Fusion* f = make();
  double q[4] = { 1.0, 0.0, 0.0, 0.0 };
  const double w[3] = { 0.3 * PI / 2, 0.2 * PI / 2, 0.93 * PI / 2 };
  Fusion_Output out;
  double worst = 0.0;
  for (int i = 0; i < 400; i++)
  {
    Raw_Sample s = sense(q, w, ZERO, 0.0, i % 2 == 0, FIELD);
    fusion_update(f, &s, &out);
    const double err = angle_between(q, &out.q);
    worst = err > worst ? err : worst;
    advance(q, w);
  }
  MTS_ASSERT(worst < 1.0 * DEG);
  fusion_destroy(f);
}

MTS_TEST(gyro_bias_is_learned)
{
  // 0.02 rad/s on every axis (twice Hat_Sim's). Proportional only, the tilt
  // would sit about bias / kp = 1.1 deg off; the integral removes it
  Fusion* f = make();
  const double q[4] = { 1.0, 0.0, 0.0, 0.0 };
  Fusion_Output out;
  for (int i = 0; i < 12000; i++) // 2 min
  {
    Raw_Sample s = sense(q, ZERO, ZERO, 0.02, i % 2 == 0, FIELD);
    fusion_update(f, &s, &out);
  }
  MTS_ASSERT(angle_between(q, &out.q) < 0.1 * DEG);
  fusion_destroy(f);
}

MTS_TEST(disturbed_field_turns_the_heading_never_the_tilt)
{
  // A steel desk: the field swings 40 deg in the vertical north-up plane.
  // Mahony without the projection would tilt the board toward it
  double qx[4];
  quat_axis_angle(1, 0, 0, 25 * DEG, qx);
  double bent[3];
  const double c = cos(40 * DEG), s40 = sin(40 * DEG);
  bent[0] = c * FIELD[0] - s40 * FIELD[2];
  bent[1] = 0.0;
  bent[2] = s40 * FIELD[0] + c * FIELD[2];

  Fusion* f = make();
  Fusion_Output out;
  double worst = 0.0;
  for (int i = 0; i < 3000; i++)
  {
    Raw_Sample s = sense(qx, ZERO, ZERO, 0.0, i % 2 == 0, i < 100 ? FIELD : bent);
    fusion_update(f, &s, &out);
    const double e = tilt_error(qx, &out.q);
    worst = e > worst ? e : worst;
  }
  MTS_ASSERT(worst < 0.1 * DEG);
  fusion_destroy(f);
}

MTS_TEST(tremor_comes_out_as_linear_acceleration)
{
  // 0.5 m/s^2 at 6 Hz along north, board tilted: lin_acc carries it in the
  // body frame, the attitude barely notices
  double q[4];
  quat_axis_angle(0, 1, 0, 15 * DEG, q);
  Fusion* f = make();
  Fusion_Output out;
  double worst_tilt = 0.0, worst_lin = 0.0;
  for (int i = 0; i < 1000; i++)
  {
    const double a = 0.5 * sin(2 * PI * 6.0 * i * DT);
    const double lin_world[3] = { a, 0.0, 0.0 };
    Raw_Sample s = sense(q, ZERO, lin_world, 0.0, i % 2 == 0, FIELD);
    fusion_update(f, &s, &out);
    if (i >= 200)
    {
      double expected[3];
      to_body(q, lin_world, expected);
      for (int k = 0; k < 3; k++)
      {
        const double d = fabs(out.lin_acc_body.v[k] - expected[k]);
        worst_lin = d > worst_lin ? d : worst_lin;
      }
      const double t = tilt_error(q, &out.q);
      worst_tilt = t > worst_tilt ? t : worst_tilt;
    }
  }
  MTS_ASSERT(worst_tilt < 0.3 * DEG);
  MTS_ASSERT(worst_lin < 0.02 * 0.5 * 10); // within 10 % of the tremor at its worst
  fusion_destroy(f);
}

MTS_TEST(strong_acceleration_does_not_tip_the_estimate)
{
  // 3 m/s^2 sideways for 2 s (0.3 g, past the gate): the accelerometer is
  // ignored meanwhile; trusting it would tip the board by ~17 deg
  const double q[4] = { 1.0, 0.0, 0.0, 0.0 };
  Fusion* f = make();
  Fusion_Output out;
  double worst = 0.0;
  for (int i = 0; i < 600; i++)
  {
    const double lin_world[3] = { 0.0, i >= 200 && i < 400 ? 3.0 : 0.0, 0.0 };
    Raw_Sample s = sense(q, ZERO, lin_world, 0.0, i % 2 == 0, FIELD);
    fusion_update(f, &s, &out);
    const double t = tilt_error(q, &out.q);
    worst = t > worst ? t : worst;
  }
  MTS_ASSERT(worst < 0.5 * DEG);
  fusion_destroy(f);
}

MTS_TEST(starts_without_a_magnetometer)
{
  // No fresh mag at first: tilt from accel, heading from the body x axis.
  // The flat board facing north then still reads identity
  const double q[4] = { 1.0, 0.0, 0.0, 0.0 };
  Fusion* f = make();
  Fusion_Output out;
  Raw_Sample s = sense(q, ZERO, ZERO, 0.0, false, FIELD);
  fusion_update(f, &s, &out);
  MTS_ASSERT(angle_between(q, &out.q) < 0.1 * DEG);
  fusion_destroy(f);
}

MTS_TEST(reset_starts_over)
{
  Fusion* f = make();
  Fusion_Output out;
  double q[4];
  quat_axis_angle(0, 0, 1, 90 * DEG, q);
  for (int i = 0; i < 300; i++)
  {
    Raw_Sample s = sense(q, ZERO, ZERO, 0.0, true, FIELD);
    fusion_update(f, &s, &out);
  }
  MTS_ASSERT_NE(out.flags & FUSION_CONVERGED, 0);

  fusion_reset(f);
  const double flat[4] = { 1.0, 0.0, 0.0, 0.0 };
  Raw_Sample s = sense(flat, ZERO, ZERO, 0.0, true, FIELD);
  fusion_update(f, &s, &out);
  MTS_ASSERT_EQ(out.flags & FUSION_CONVERGED, 0);
  MTS_ASSERT(angle_between(flat, &out.q) < 0.1 * DEG); // started over from this sample
  fusion_destroy(f);
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(create_rejects_bad_configs),
  MTS_TEST_ENTRY(flat_board_facing_north_is_identity),
  MTS_TEST_ENTRY(any_attitude_from_the_first_sample),
  MTS_TEST_ENTRY(follows_a_turn),
  MTS_TEST_ENTRY(gyro_bias_is_learned),
  MTS_TEST_ENTRY(disturbed_field_turns_the_heading_never_the_tilt),
  MTS_TEST_ENTRY(tremor_comes_out_as_linear_acceleration),
  MTS_TEST_ENTRY(strong_acceleration_does_not_tip_the_estimate),
  MTS_TEST_ENTRY(starts_without_a_magnetometer),
  MTS_TEST_ENTRY(reset_starts_over),
};

MTS_TEST_MAIN(tests)
