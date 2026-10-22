/* ===================================================================================================
 * core/src/separation/separation.c
 *
 * Separation v0, see separation.h.
 *
 * Integrate-and-high-pass in one section: a 2nd-order high-pass is
 *   HP(z) = k (1 - z^-1)^2 / D(z)
 * and the trapezoidal integrator T/2 (1 + z^-1) / (1 - z^-1). Their product
 *   k T/2 (1 + z^-1)(1 - z^-1) / D(z) = k T/2 (1 - z^-2) / D(z)
 * has no pole at DC left: a constant offset or rounding cannot accumulate.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <core/separation/separation.h>

#include <core/filters/filters.h>
#include <utils/logger/logger.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct Separation
{
  Separation_Config cfg;
  double gain; // band-pass normalization: 1 / |H(f_centre)|
  double disp_gain; // makes the double integration exactly 1 / w^2 at the centre
  Filter_Chain band[3]; // per axis
  Filter_Chain integrate[3]; // per axis: the two integrate-and-high-pass sections
  int64_t delay_ns;
};

Separation_Config separation_config_default(double rate_hz)
{
  Separation_Config c;
  memset(&c, 0, sizeof(c));
  c.rate_hz = rate_hz;
  c.band_lo_hz = 4.0;
  c.band_hi_hz = 12.0;
  c.filter_order = 2;
  c.estimator = SEPARATION_NEUTRAL;
  return c;
}

// One integrate-and-high-pass section, corner fc
static bool integrator(Biquad* b, double fc_hz, double rate_hz)
{
  if (!biquad_highpass(b, fc_hz, 0.7071067811865476, rate_hz))
  {
    return false;
  }
  const double k = b->b0; // HP numerator is k (1, -2, 1)
  const double half_t = 0.5 / rate_hz;
  b->b0 = k * half_t;
  b->b1 = 0.0;
  b->b2 = -k * half_t;
  return true;
}

Separation* separation_create(const Separation_Config* cfg)
{
  if (cfg->estimator != SEPARATION_NEUTRAL)
  {
    MTS_LOG_ERROR("separation: estimator %u not implemented (v0: SEPARATION_NEUTRAL only)", cfg->estimator);
    return NULL;
  }
  if (!isfinite(cfg->rate_hz) || !isfinite(cfg->band_lo_hz) || !isfinite(cfg->band_hi_hz) || !(cfg->band_lo_hz > 0.0)
      || !(cfg->band_hi_hz > cfg->band_lo_hz) || !(cfg->band_hi_hz < 0.5 * cfg->rate_hz))
  {
    MTS_LOG_ERROR("separation: band %g-%g Hz must satisfy 0 < lo < hi < rate / 2 (%g Hz)", cfg->band_lo_hz,
                  cfg->band_hi_hz, cfg->rate_hz);
    return NULL;
  }

  Separation* s = (Separation*)malloc(sizeof(Separation));
  if (!s)
  {
    MTS_LOG_ERROR("separation: out of memory");
    return NULL;
  }
  s->cfg = *cfg;
  for (int a = 0; a < 3; a++)
  {
    filter_chain_init(&s->band[a]);
    filter_chain_init(&s->integrate[a]);
    bool ok = filter_chain_add_highpass(&s->band[a], cfg->filter_order, cfg->band_lo_hz, cfg->rate_hz)
              && filter_chain_add_lowpass(&s->band[a], cfg->filter_order, cfg->band_hi_hz, cfg->rate_hz);
    s->integrate[a].n = 2;
    ok = ok && integrator(&s->integrate[a].s[0], 0.5 * cfg->band_lo_hz, cfg->rate_hz)
         && integrator(&s->integrate[a].s[1], 0.5 * cfg->band_lo_hz, cfg->rate_hz);
    if (!ok)
    {
      free(s);
      return NULL; // the filter design logged why (order, band)
    }
  }

  // Normalize at the band's geometric centre, and report the delay there
  const double centre = sqrt(cfg->band_lo_hz * cfg->band_hi_hz);
  double g, phase;
  filter_chain_response(&s->band[0], centre, cfg->rate_hz, &g, &phase);
  s->gain = 1.0 / g;
  // The trapezoidal integrator reads a little low as f rises ((wT/2) / tan(wT/2): 3 % per
  // integration at 7 Hz, 100 Hz) and the high-passes take a little more: corrected at the centre
  const double w_c = 2.0 * 3.14159265358979323846 * centre;
  double g_int;
  filter_chain_response(&s->integrate[0], centre, cfg->rate_hz, &g_int, &phase);
  s->disp_gain = 1.0 / (g_int * w_c * w_c);
  const double delay_s = filter_chain_group_delay_s(&s->band[0], centre, cfg->rate_hz)
                         + filter_chain_group_delay_s(&s->integrate[0], centre, cfg->rate_hz);
  s->delay_ns = llround(delay_s * 1e9);
  return s;
}

void separation_destroy(Separation* s)
{
  free(s);
}

void separation_reset(Separation* s)
{
  for (int a = 0; a < 3; a++)
  {
    filter_chain_reset(&s->band[a]);
    filter_chain_reset(&s->integrate[a]);
  }
}

void separation_update(Separation* s, const Vec3* lin_acc_body, Separation_Output* out)
{
  for (int a = 0; a < 3; a++)
  {
    const double acc = s->gain * filter_chain_process(&s->band[a], lin_acc_body->v[a]);
    out->tremor_acc.v[a] = acc;
    out->tremor_disp.v[a] = s->disp_gain * filter_chain_process(&s->integrate[a], acc);
  }
  out->delay_ns = s->delay_ns;
}
