/* ===================================================================================================
 * core/src/filters/filters.c
 *
 * Filters, see filters.h
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <core/filters/filters.h>

#include <utils/logger/logger.h>

#include <math.h>
#include <string.h>

static const double PI = 3.14159265358979323846;

static bool valid_design(double fc_hz, double q, double rate_hz)
{
  if (!isfinite(fc_hz) || !isfinite(q) || !isfinite(rate_hz) || !(rate_hz > 0.0) || !(fc_hz > 0.0)
      || !(fc_hz < 0.5 * rate_hz) || !(q > 0.0))
  {
    MTS_LOG_ERROR("filters: cutoff %g Hz must be in (0, %g), q %g > 0", fc_hz, 0.5 * rate_hz, q);
    return false;
  }
  return true;
}

// RBJ cookbook, bilinear with prewarping: exact -3 dB at fc for q = 1/sqrt(2)
static void design(Biquad* b, double fc_hz, double q, double rate_hz, bool high)
{
  const double w0 = 2.0 * PI * fc_hz / rate_hz;
  const double cw = cos(w0);
  const double alpha = sin(w0) / (2.0 * q);
  const double a0 = 1.0 + alpha;
  if (high)
  {
    b->b0 = (1.0 + cw) / 2.0 / a0;
    b->b1 = -(1.0 + cw) / a0;
    b->b2 = (1.0 + cw) / 2.0 / a0;
  }
  else
  {
    b->b0 = (1.0 - cw) / 2.0 / a0;
    b->b1 = (1.0 - cw) / a0;
    b->b2 = (1.0 - cw) / 2.0 / a0;
  }
  b->a1 = -2.0 * cw / a0;
  b->a2 = (1.0 - alpha) / a0;
  b->z1 = 0.0;
  b->z2 = 0.0;
}

bool biquad_lowpass(Biquad* b, double fc_hz, double q, double rate_hz)
{
  if (!valid_design(fc_hz, q, rate_hz))
  {
    return false;
  }
  design(b, fc_hz, q, rate_hz, false);
  return true;
}

bool biquad_highpass(Biquad* b, double fc_hz, double q, double rate_hz)
{
  if (!valid_design(fc_hz, q, rate_hz))
  {
    return false;
  }
  design(b, fc_hz, q, rate_hz, true);
  return true;
}

double biquad_process(Biquad* b, double x)
{
  const double y = b->b0 * x + b->z1;
  b->z1 = b->b1 * x - b->a1 * y + b->z2;
  b->z2 = b->b2 * x - b->a2 * y;
  return y;
}

void biquad_reset(Biquad* b)
{
  b->z1 = 0.0;
  b->z2 = 0.0;
}

void biquad_response(const Biquad* b, double f_hz, double rate_hz, double* gain, double* phase_rad)
{
  // H(z) at z = e^{jw}: (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2)
  const double w = 2.0 * PI * f_hz / rate_hz;
  const double c1 = cos(w), s1 = sin(w), c2 = cos(2.0 * w), s2 = sin(2.0 * w);
  const double nr = b->b0 + b->b1 * c1 + b->b2 * c2;
  const double ni = -(b->b1 * s1 + b->b2 * s2);
  const double dr = 1.0 + b->a1 * c1 + b->a2 * c2;
  const double di = -(b->a1 * s1 + b->a2 * s2);
  *gain = sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
  *phase_rad = atan2(ni, nr) - atan2(di, dr);
}

void filter_chain_init(Filter_Chain* c)
{
  memset(c, 0, sizeof(*c));
}

static bool add_butterworth(Filter_Chain* c, uint32_t order, double fc_hz, double rate_hz, bool high)
{
  if (order < 2 || order > FILTER_ORDER_MAX || order % 2 != 0)
  {
    MTS_LOG_ERROR("filters: Butterworth order %u, must be even, 2 .. %d", order, FILTER_ORDER_MAX);
    return false;
  }
  if (c->n + order / 2 > FILTER_SECTIONS_MAX)
  {
    MTS_LOG_ERROR("filters: chain full (%u + %u sections > %d)", c->n, order / 2, FILTER_SECTIONS_MAX);
    return false;
  }
  if (!valid_design(fc_hz, 1.0, rate_hz))
  {
    return false;
  }
  for (uint32_t k = 1; k <= order / 2; k++)
  {
    const double q = 1.0 / (2.0 * cos(PI * (2.0 * k - 1.0) / (2.0 * order)));
    design(&c->s[c->n++], fc_hz, q, rate_hz, high);
  }
  return true;
}

bool filter_chain_add_lowpass(Filter_Chain* c, uint32_t order, double fc_hz, double rate_hz)
{
  return add_butterworth(c, order, fc_hz, rate_hz, false);
}

bool filter_chain_add_highpass(Filter_Chain* c, uint32_t order, double fc_hz, double rate_hz)
{
  return add_butterworth(c, order, fc_hz, rate_hz, true);
}

double filter_chain_process(Filter_Chain* c, double x)
{
  for (uint32_t i = 0; i < c->n; i++)
  {
    x = biquad_process(&c->s[i], x);
  }
  return x;
}

void filter_chain_reset(Filter_Chain* c)
{
  for (uint32_t i = 0; i < c->n; i++)
  {
    biquad_reset(&c->s[i]);
  }
}

void filter_chain_response(const Filter_Chain* c, double f_hz, double rate_hz, double* gain, double* phase_rad)
{
  double g = 1.0, p = 0.0;
  for (uint32_t i = 0; i < c->n; i++)
  {
    double gi, pi;
    biquad_response(&c->s[i], f_hz, rate_hz, &gi, &pi);
    g *= gi;
    p += pi;
  }
  *gain = g;
  *phase_rad = p;
}

double filter_chain_group_delay_s(const Filter_Chain* c, double f_hz, double rate_hz)
{
  // Central difference, per section, each phase step wrapped: sections never jump by pi in df
  const double df = 1e-4 * f_hz;
  double dphi = 0.0;
  for (uint32_t i = 0; i < c->n; i++)
  {
    double g, p1, p2;
    biquad_response(&c->s[i], f_hz - df, rate_hz, &g, &p1);
    biquad_response(&c->s[i], f_hz + df, rate_hz, &g, &p2);
    double d = p2 - p1;
    while (d > PI)
    {
      d -= 2.0 * PI;
    }
    while (d < -PI)
    {
      d += 2.0 * PI;
    }
    dphi += d;
  }
  return -dphi / (2.0 * PI * 2.0 * df);
}
