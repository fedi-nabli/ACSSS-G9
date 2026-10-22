/* ===================================================================================================
 * core/src/wflc/wflc.c
 *
 * Wflc, see wflc.h
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <core/wflc/wflc.h>

#include <utils/logger/logger.h>

#include <math.h>
#include <string.h>

static const double TWO_PI = 6.283185307179586;

Wflc_Config wflc_config_default(double rate_hz)
{
  Wflc_Config c;
  memset(&c, 0, sizeof(c));
  c.rate_hz = rate_hz;
  c.n_harmonics = 1;
  c.mu_freq = 0.002;
  c.mu_amp = 0.05;
  c.f0_hz = 6.0;
  c.f_min_hz = 3.0;
  c.f_max_hz = 14.0;
  c.power_tau_s = 1.0;
  c.power_floor = 1e-18; // (1 nm)^2
  return c;
}

static bool finite_config(const Wflc_Config* c)
{
  return isfinite(c->rate_hz) && isfinite(c->mu_freq) && isfinite(c->mu_amp) && isfinite(c->f0_hz)
         && isfinite(c->f_min_hz) && isfinite(c->f_max_hz) && isfinite(c->power_tau_s) && isfinite(c->power_floor);
}

bool wflc_init(Wflc* w, const Wflc_Config* cfg)
{
  if (!finite_config(cfg))
  {
    MTS_LOG_ERROR("wflc: a config value is not finite");
    return false;
  }
  if (!(cfg->rate_hz > 0.0))
  {
    MTS_LOG_ERROR("wflc: rate %g Hz must be > 0", cfg->rate_hz);
    return false;
  }
  if (cfg->n_harmonics < 1 || cfg->n_harmonics > WFLC_HARMONICS_MAX)
  {
    MTS_LOG_ERROR("wflc: %u harmonics, must be 1 .. %d", cfg->n_harmonics, WFLC_HARMONICS_MAX);
    return false;
  }
  if (!(cfg->mu_freq > 0.0) || !(cfg->mu_amp > 0.0))
  {
    MTS_LOG_ERROR("wflc: step sizes must be > 0 (mu_freq %g, mu_amp %g)", cfg->mu_freq, cfg->mu_amp);
    return false;
  }
  if (!(cfg->f_min_hz > 0.0) || cfg->f0_hz < cfg->f_min_hz || cfg->f0_hz > cfg->f_max_hz)
  {
    MTS_LOG_ERROR("wflc: need 0 < f_min <= f0 <= f_max (%g, %g, %g Hz)", cfg->f_min_hz, cfg->f0_hz, cfg->f_max_hz);
    return false;
  }
  if (cfg->n_harmonics * cfg->f_max_hz >= 0.5 * cfg->rate_hz)
  {
    MTS_LOG_ERROR("wflc: harmonic %u of %g Hz reaches Nyquist (%g Hz)", cfg->n_harmonics, cfg->f_max_hz,
                  0.5 * cfg->rate_hz);
    return false;
  }
  if (!(cfg->power_tau_s > 0.0) || cfg->power_floor < 0.0)
  {
    MTS_LOG_ERROR("wflc: need tau > 0 and floor >= 0 (%g s, %g)", cfg->power_tau_s, cfg->power_floor);
    return false;
  }

  memset(w, 0, sizeof(*w));
  w->m = cfg->n_harmonics;
  w->rate_hz = cfg->rate_hz;
  w->omega0 = TWO_PI * cfg->f0_hz / cfg->rate_hz;
  w->omega_min = TWO_PI * cfg->f_min_hz / cfg->rate_hz;
  w->omega_max = TWO_PI * cfg->f_max_hz / cfg->rate_hz;
  w->mu_freq = cfg->mu_freq;
  w->mu_amp = cfg->mu_amp;
  w->power_floor = cfg->power_floor;
  const double a = 1.0 / (cfg->power_tau_s * cfg->rate_hz);
  w->alpha = a < 1.0 ? a : 1.0;
  wflc_reset(w);
  return true;
}

void wflc_reset(Wflc* w)
{
  memset(w->w, 0, sizeof(w->w));
  w->omega = w->omega0;
  w->phase = 0.0;
  w->power = 0.0;
  w->norm_power = 0.0;
  w->err_power = 0.0;
  w->decay = 1.0;
  w->scale = sqrt(w->power_floor);
  w->updates = 0;
}

double wflc_update(Wflc* w, double s)
{
  if (!isfinite(s))
  {
    return 0.0;
  }
  const uint32_t m = w->m;
  const double a = w->alpha;

  // Running power, bias-corrected: the very first sample normalizes to unit size, not 1/sqrt(alpha)
  w->decay *= 1.0 - a;
  const double correction = 1.0 / (1.0 - w->decay);
  w->power += a * (s * s - w->power);
  w->scale = sqrt(w->power * correction + w->power_floor);
  const double sn = w->scale > 0.0 ? s / w->scale : 0.0;

  // Advance the phase by this sample's frequency, kept in [0, 2 pi)
  w->phase += w->omega;
  if (w->phase >= TWO_PI)
  {
    w->phase -= TWO_PI;
  }

  // Fit, error, and the frequency gradient, in one pass over the harmonics
  double x[2 * WFLC_HARMONICS_MAX];
  double fit = 0.0;
  double grad = 0.0;
  for (uint32_t r = 1; r <= m; r++)
  {
    const double sr = sin(r * w->phase);
    const double cr = cos(r * w->phase);
    x[r - 1] = sr;
    x[m + r - 1] = cr;
    fit += w->w[r - 1] * sr + w->w[m + r - 1] * cr;
    grad += r * (w->w[r - 1] * cr - w->w[m + r - 1] * sr);
  }
  const double e = sn - fit;

  // Frequency first (it used the weights the fit used), then the weights
  double omega = w->omega + 2.0 * w->mu_freq * e * grad;
  omega = omega < w->omega_min ? w->omega_min : (omega > w->omega_max ? w->omega_max : omega);
  w->omega = omega;
  for (uint32_t i = 0; i < 2 * m; i++)
  {
    w->w[i] += 2.0 * w->mu_amp * e * x[i];
  }

  w->norm_power += a * (sn * sn - w->norm_power);
  w->err_power += a * (e * e - w->err_power);
  w->updates++;
  return e * w->scale;
}

void wflc_project(const Wflc* w, uint32_t steps, double* out)
{
  const uint32_t m = w->m;
  for (uint32_t j = 1; j <= steps; j++)
  {
    const double phi = w->phase + j * w->omega;
    double y = 0.0;
    for (uint32_t r = 1; r <= m; r++)
    {
      y += w->w[r - 1] * sin(r * phi) + w->w[m + r - 1] * cos(r * phi);
    }
    out[j - 1] = y * w->scale;
  }
}

double wflc_frequency_hz(const Wflc* w)
{
  return w->omega * w->rate_hz / TWO_PI;
}

double wflc_error_ratio(const Wflc* w)
{
  // Both averages share the same bias correction: it cancels in the ratio
  return w->norm_power > 0.0 ? w->err_power / w->norm_power : 1.0;
}

uint64_t wflc_updates(const Wflc* w)
{
  return w->updates;
}
