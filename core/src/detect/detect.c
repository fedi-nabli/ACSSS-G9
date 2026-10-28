/* ===================================================================================================
 * core/src/detect/detect.c
 *
 * Detector v0, see detect.h.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <core/detect/detect.h>

#include <core/wflc/wflc.h>
#include <utils/logger/logger.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct Detector
{
  Detect_Config cfg;
  double alpha; // EWMA weight per sample: 1 / (window * rate)
  double power[3]; // EWMA of disp^2 per axis, bias-corrected via decay
  double decay;
  Wflc wflc[3];
  bool active;
};

Detect_Config detect_config_default(double rate_hz)
{
  Detect_Config c;
  memset(&c, 0, sizeof(c));
  c.rate_hz = rate_hz;
  c.on_threshold = 0.6;
  c.off_threshold = 0.4;
  c.min_amp = 0.05e-3;
  c.window_s = 1.0;
  return c;
}

Detector* detect_create(const Detect_Config* cfg)
{
  if (!isfinite(cfg->rate_hz) || !(cfg->rate_hz > 0.0) || !isfinite(cfg->window_s) || !(cfg->window_s > 0.0)
      || !isfinite(cfg->min_amp) || !(cfg->min_amp >= 0.0) || !isfinite(cfg->on_threshold)
      || !isfinite(cfg->off_threshold) || !(cfg->off_threshold < cfg->on_threshold) || cfg->on_threshold > 1.0
      || cfg->off_threshold < 0.0)
  {
    MTS_LOG_ERROR("detect: bad config (rate %g Hz, window %g s, thresholds on %g off %g, min amp %g m)",
                  cfg->rate_hz, cfg->window_s, cfg->on_threshold, cfg->off_threshold, cfg->min_amp);
    return NULL;
  }
  Detector* d = (Detector*)malloc(sizeof(Detector));
  if (!d)
  {
    MTS_LOG_ERROR("detect: out of memory");
    return NULL;
  }
  d->cfg = *cfg;
  const double a = 1.0 / (cfg->window_s * cfg->rate_hz);
  d->alpha = a < 1.0 ? a : 1.0;
  const Wflc_Config wc = wflc_config_default(cfg->rate_hz);
  for (int i = 0; i < 3; i++)
  {
    if (!wflc_init(&d->wflc[i], &wc))
    {
      free(d);
      return NULL;
    }
  }
  detect_reset(d);
  return d;
}

void detect_destroy(Detector* d)
{
  free(d);
}

void detect_reset(Detector* d)
{
  for (int i = 0; i < 3; i++)
  {
    d->power[i] = 0.0;
    wflc_reset(&d->wflc[i]);
  }
  d->decay = 1.0;
  d->active = false;
}

void detect_update(Detector* d, const Separation_Output* in, Detect_Output* out)
{
  d->decay *= 1.0 - d->alpha;
  const double correction = 1.0 / (1.0 - d->decay);
  int dominant = 0;
  for (int i = 0; i < 3; i++)
  {
    const double x = in->tremor_disp.v[i];
    d->power[i] += d->alpha * (x * x - d->power[i]);
    wflc_update(&d->wflc[i], x);
    if (d->power[i] > d->power[dominant])
    {
      dominant = i;
    }
  }

  const double amp = sqrt(2.0 * d->power[dominant] * correction);
  double conf = 1.0 - wflc_error_ratio(&d->wflc[dominant]);
  conf = conf < 0.0 ? 0.0 : (conf > 1.0 ? 1.0 : conf);

  if (!d->active && conf >= d->cfg.on_threshold && amp >= d->cfg.min_amp)
  {
    d->active = true;
  }
  else if (d->active && (conf < d->cfg.off_threshold || amp < 0.5 * d->cfg.min_amp))
  {
    d->active = false;
  }

  out->active = d->active;
  out->hz = wflc_frequency_hz(&d->wflc[dominant]);
  out->amp = amp;
  out->conf = conf;
}
