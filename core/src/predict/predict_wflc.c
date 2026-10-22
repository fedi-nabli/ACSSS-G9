/* ===================================================================================================
 * core/src/predict/predict_wflc.c
 *
 * The wflc predictor: core/wflc behind the predictor ABI. It fits the
 * tremor as harmonics of one adaptive frequency and forecasts by
 * extrapolating that fit forward, frequency and weights frozen.
 *
 * No forecast until the fit has locked: an unlocked WFLC extrapolates
 * nonsense, and the contract's warm-up null exists for this. Locked when,
 * after half a second of samples, error_ratio < 0.3; unlocked again above
 * 0.5 (hysteresis: no flicker at the edge).
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include "registry.h"

#include <core/wflc/wflc.h>
#include <utils/logger/logger.h>

#define LOCK_RATIO 0.3
#define UNLOCK_RATIO 0.5
#define WARMUP_S 0.5

typedef struct Wflc_State
{
  Wflc wflc;
  uint64_t warmup; // samples before a lock can be declared
  uint32_t horizon; // steps
  uint32_t locked;
} Wflc_State;

static bool wflc_predictor_init(void* state, const Predictor_Config* cfg)
{
  Wflc_State* s = (Wflc_State*)state;
  if (cfg->horizon_steps == 0 || cfg->horizon_steps > K_MAX)
  {
    MTS_LOG_ERROR("wflc: horizon of %u steps, must be 1 .. %d", cfg->horizon_steps, K_MAX);
    return false;
  }
  Wflc_Config w = wflc_config_default(cfg->rate_hz);
  w.n_harmonics = cfg->params.wflc.n_harmonics;
  w.mu_freq = cfg->params.wflc.mu_freq;
  w.mu_amp = cfg->params.wflc.mu_amp;
  if (!wflc_init(&s->wflc, &w))
  {
    return false; // logged by wflc_init
  }
  s->warmup = (uint64_t)(WARMUP_S * cfg->rate_hz);
  s->horizon = cfg->horizon_steps;
  s->locked = 0;
  return true;
}

static void wflc_predictor_reset(void* state)
{
  Wflc_State* s = (Wflc_State*)state;
  wflc_reset(&s->wflc);
  s->locked = 0;
}

static void wflc_predictor_update(void* state, double x)
{
  Wflc_State* s = (Wflc_State*)state;
  wflc_update(&s->wflc, x);
  const double ratio = wflc_error_ratio(&s->wflc);
  if (!s->locked && wflc_updates(&s->wflc) >= s->warmup && ratio < LOCK_RATIO)
  {
    s->locked = 1;
  }
  else if (s->locked && ratio > UNLOCK_RATIO)
  {
    s->locked = 0;
  }
}

static size_t wflc_predictor_predict(void* state, double* out, size_t k_max)
{
  Wflc_State* s = (Wflc_State*)state;
  if (!s->locked)
  {
    return 0;
  }
  const size_t k = s->horizon < k_max ? s->horizon : k_max;
  wflc_project(&s->wflc, (uint32_t)k, out);
  return k;
}

static int64_t wflc_predictor_delay_ns(const void* state)
{
  (void)state;
  return 0; // no filtering of its own
}

const Predictor_Ops PREDICTOR_OPS_WFLC = {
  .id = PREDICTOR_WFLC,
  .name = "wflc",
  .state_size = sizeof(Wflc_State),
  .init = wflc_predictor_init,
  .reset = wflc_predictor_reset,
  .update = wflc_predictor_update,
  .predict = wflc_predictor_predict,
  .delay_ns = wflc_predictor_delay_ns,
};
