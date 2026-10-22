/* ===================================================================================================
 * core/src/fusion/fusion.c
 *
 * Fusion v0: Mahony's complementary filter (Mahony, Hamel, Pflimlin 2008),
 * 9 DOF, see fusion.h. q is Hamilton, w first, body -> world (NWU: x north,
 * y west, z up). R(q) maps body vectors to world; its rows are the world
 * axes seen in the body frame.
 *
 * Per sample, dt = 1 / rate:
 *   v  = R' e_z                      where the filter thinks "up" is, in the body
 *   e  = a_hat x v                   tilt error (accel gated)
 *      - psi v                       heading error only (mag fresh): psi = angle of
 *                                    h = R m_hat off north in the horizontal plane
 *   bi += ki e dt                    gyro bias, learned
 *   g' = gyro + kp e + bi
 *   q  = q (x) exp(g' dt / 2)        exact rotation by |g'| dt
 *   lin_acc = a - G v                gravity removed with the updated attitude
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <core/fusion/fusion.h>

#include <utils/logger/logger.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const double G = 9.80665; // standard gravity, m/s^2

struct Fusion
{
  Fusion_Config cfg;
  double dt;
  double q[4]; // w, x, y, z
  double bias[3]; // integral term, rad/s
  bool started; // attitude set from a first sample
  uint64_t samples; // since started
  uint64_t settle_samples;
};

Fusion_Config fusion_config_default(double rate_hz)
{
  Fusion_Config c;
  memset(&c, 0, sizeof(c));
  c.rate_hz = rate_hz;
  c.kp = 1.0;
  c.ki = 0.05;
  c.accel_gate = 0.03;
  c.settle_s = 2.0;
  return c;
}

Fusion* fusion_create(const Fusion_Config* cfg)
{
  if (!isfinite(cfg->rate_hz) || !(cfg->rate_hz > 0.0) || !isfinite(cfg->kp) || !(cfg->kp >= 0.0)
      || !isfinite(cfg->ki) || !(cfg->ki >= 0.0) || !isfinite(cfg->accel_gate) || !(cfg->accel_gate > 0.0)
      || !isfinite(cfg->settle_s) || !(cfg->settle_s >= 0.0))
  {
    MTS_LOG_ERROR("fusion: bad config (rate %g Hz, kp %g, ki %g, gate %g, settle %g s)", cfg->rate_hz, cfg->kp,
                  cfg->ki, cfg->accel_gate, cfg->settle_s);
    return NULL;
  }
  Fusion* f = (Fusion*)malloc(sizeof(Fusion));
  if (!f)
  {
    MTS_LOG_ERROR("fusion: out of memory");
    return NULL;
  }
  f->cfg = *cfg;
  f->dt = 1.0 / cfg->rate_hz;
  f->settle_samples = (uint64_t)llround(cfg->settle_s * cfg->rate_hz);
  fusion_reset(f);
  return f;
}

void fusion_destroy(Fusion* f)
{
  free(f);
}

void fusion_reset(Fusion* f)
{
  f->q[0] = 1.0;
  f->q[1] = f->q[2] = f->q[3] = 0.0;
  f->bias[0] = f->bias[1] = f->bias[2] = 0.0;
  f->started = false;
  f->samples = 0;
}

/* ---------------------------------------------------------------------------------------------------
 * Small vector and quaternion helpers
 * ------------------------------------------------------------------------------------------------- */

static double dot(const double a[3], const double b[3])
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void cross(const double a[3], const double b[3], double out[3])
{
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

static bool normalize(double v[3])
{
  const double n = sqrt(dot(v, v));
  if (!(n > 0.0) || !isfinite(n))
  {
    return false;
  }
  v[0] /= n;
  v[1] /= n;
  v[2] /= n;
  return true;
}

// R(q) v: body -> world
static void rotate(const double q[4], const double v[3], double out[3])
{
  const double w = q[0], x = q[1], y = q[2], z = q[3];
  out[0] = (1 - 2 * (y * y + z * z)) * v[0] + 2 * (x * y - w * z) * v[1] + 2 * (x * z + w * y) * v[2];
  out[1] = 2 * (x * y + w * z) * v[0] + (1 - 2 * (x * x + z * z)) * v[1] + 2 * (y * z - w * x) * v[2];
  out[2] = 2 * (x * z - w * y) * v[0] + 2 * (y * z + w * x) * v[1] + (1 - 2 * (x * x + y * y)) * v[2];
}

// R(q)' v: world -> body
static void rotate_back(const double q[4], const double v[3], double out[3])
{
  const double w = q[0], x = q[1], y = q[2], z = q[3];
  out[0] = (1 - 2 * (y * y + z * z)) * v[0] + 2 * (x * y + w * z) * v[1] + 2 * (x * z - w * y) * v[2];
  out[1] = 2 * (x * y - w * z) * v[0] + (1 - 2 * (x * x + z * z)) * v[1] + 2 * (y * z + w * x) * v[2];
  out[2] = 2 * (x * z + w * y) * v[0] + 2 * (y * z - w * x) * v[1] + (1 - 2 * (x * x + y * y)) * v[2];
}

static void quat_normalize(double q[4])
{
  const double n = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  for (int i = 0; i < 4; i++)
  {
    q[i] /= n;
  }
}

/* Attitude from the world axes seen in the body: rows of R. Shepperd's
 * method: the largest of the four candidates, never a small divisor. */
static void quat_from_rows(const double xw[3], const double yw[3], const double zw[3], double q[4])
{
  const double m00 = xw[0], m01 = xw[1], m02 = xw[2];
  const double m10 = yw[0], m11 = yw[1], m12 = yw[2];
  const double m20 = zw[0], m21 = zw[1], m22 = zw[2];
  const double tr = m00 + m11 + m22;
  if (tr > 0.0)
  {
    const double s = 2.0 * sqrt(tr + 1.0);
    q[0] = 0.25 * s;
    q[1] = (m21 - m12) / s;
    q[2] = (m02 - m20) / s;
    q[3] = (m10 - m01) / s;
  }
  else if (m00 > m11 && m00 > m22)
  {
    const double s = 2.0 * sqrt(1.0 + m00 - m11 - m22);
    q[0] = (m21 - m12) / s;
    q[1] = 0.25 * s;
    q[2] = (m01 + m10) / s;
    q[3] = (m02 + m20) / s;
  }
  else if (m11 > m22)
  {
    const double s = 2.0 * sqrt(1.0 + m11 - m00 - m22);
    q[0] = (m02 - m20) / s;
    q[1] = (m01 + m10) / s;
    q[2] = 0.25 * s;
    q[3] = (m12 + m21) / s;
  }
  else
  {
    const double s = 2.0 * sqrt(1.0 + m22 - m00 - m11);
    q[0] = (m10 - m01) / s;
    q[1] = (m02 + m20) / s;
    q[2] = (m12 + m21) / s;
    q[3] = 0.25 * s;
  }
  quat_normalize(q);
}

/* First sample: up from accel, north from the horizontal part of mag (or,
 * without a usable mag, from the body x axis: yaw 0 then means "where x
 * pointed at start"). false: accel unusable, try again next sample. */
static bool start(Fusion* f, const double a[3], const double m[3], bool mag_ok)
{
  double zw[3] = { a[0], a[1], a[2] };
  if (!normalize(zw))
  {
    return false;
  }
  double xw[3];
  const double* ref = m;
  const double body_x[3] = { 1.0, 0.0, 0.0 };
  const double body_y[3] = { 0.0, 1.0, 0.0 };
  for (int attempt = 0; attempt < 3; attempt++)
  {
    if (attempt == 1 || !mag_ok)
    {
      ref = body_x;
    }
    if (attempt == 2)
    {
      ref = body_y; // board pointing x straight up or down
    }
    // The part of ref perpendicular to up. Too short (ref nearly vertical): unreliable, next reference
    const double k = dot(ref, zw);
    xw[0] = ref[0] - k * zw[0];
    xw[1] = ref[1] - k * zw[1];
    xw[2] = ref[2] - k * zw[2];
    if (sqrt(dot(xw, xw)) > 0.1)
    {
      normalize(xw);
      break;
    }
  }
  double yw[3];
  cross(zw, xw, yw); // z x x = y: right-handed
  quat_from_rows(xw, yw, zw, f->q);
  f->started = true;
  f->samples = 0;
  return true;
}

void fusion_update(Fusion* f, const Raw_Sample* s, Fusion_Output* out)
{
  const double a[3] = { s->accel[0], s->accel[1], s->accel[2] };
  const double g[3] = { s->gyro[0], s->gyro[1], s->gyro[2] };
  double m[3] = { s->mag[0], s->mag[1], s->mag[2] };
  const bool mag_fresh = (s->flags & SAMPLE_MAG_FRESH) != 0;
  const bool mag_ok = mag_fresh && normalize(m);

  if (!f->started)
  {
    start(f, a, m, mag_ok);
  }
  else
  {
    const double up[3] = { 0.0, 0.0, 1.0 };
    double v[3];
    rotate_back(f->q, up, v);

    double e[3] = { 0.0, 0.0, 0.0 };

    // Tilt: only while the accelerometer is measuring (mostly) gravity
    double a_hat[3] = { a[0], a[1], a[2] };
    const double a_norm = sqrt(dot(a, a));
    if (fabs(a_norm - G) < f->cfg.accel_gate * G && normalize(a_hat))
    {
      cross(a_hat, v, e);
    }

    /* Heading: the measured field taken to the world by the estimate
     * should point north (+x) in the horizontal plane. Its angle psi off x
     * is the yaw error, corrected by turning about "up" only, so the field
     * can never tilt the board. As an angle, not Mahony's cross product:
     * that one shrinks with the horizontal share of the field (a third of
     * it in Uppsala), and the heading would settle slowly and off. Skipped
     * where the field is nearly vertical: no heading in it */
    if (mag_ok)
    {
      double h[3];
      rotate(f->q, m, h);
      const double horizontal = sqrt(h[0] * h[0] + h[1] * h[1]);
      if (horizontal > 0.1)
      {
        const double psi = atan2(h[1], h[0]);
        for (int i = 0; i < 3; i++)
        {
          e[i] -= psi * v[i];
        }
      }
    }

    double rate[3];
    for (int i = 0; i < 3; i++)
    {
      f->bias[i] += f->cfg.ki * e[i] * f->dt;
      rate[i] = g[i] + f->cfg.kp * e[i] + f->bias[i];
    }

    // q = q (x) [cos(th/2), axis sin(th/2)], th = |rate| dt: exact for a constant rate over the step
    const double th = sqrt(dot(rate, rate)) * f->dt;
    if (th > 0.0)
    {
      const double k = sin(0.5 * th) / (th / f->dt);
      const double dq[4] = { cos(0.5 * th), rate[0] * k, rate[1] * k, rate[2] * k };
      const double q0 = f->q[0], q1 = f->q[1], q2 = f->q[2], q3 = f->q[3];
      f->q[0] = q0 * dq[0] - q1 * dq[1] - q2 * dq[2] - q3 * dq[3];
      f->q[1] = q0 * dq[1] + q1 * dq[0] + q2 * dq[3] - q3 * dq[2];
      f->q[2] = q0 * dq[2] - q1 * dq[3] + q2 * dq[0] + q3 * dq[1];
      f->q[3] = q0 * dq[3] + q1 * dq[2] - q2 * dq[1] + q3 * dq[0];
      quat_normalize(f->q);
    }
    f->samples++;
  }

  // Gravity removed with the attitude after this sample's correction
  const double up[3] = { 0.0, 0.0, 1.0 };
  double v[3];
  rotate_back(f->q, up, v);
  for (int i = 0; i < 3; i++)
  {
    out->q.q[i] = f->q[i];
    out->lin_acc_body.v[i] = a[i] - G * v[i];
  }
  out->q.q[3] = f->q[3];
  out->flags = f->started && f->samples >= f->settle_samples ? FUSION_CONVERGED : 0;
  out->pad = 0;
  out->delay_ns = 0; // instantaneous: the attitude only lags in what it removes as gravity
}
