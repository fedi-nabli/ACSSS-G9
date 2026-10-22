/* ===================================================================================================
 * core/src/predict/inversion.c
 *
 * The inversion predictor: no prediction at all. The forecast for every
 * step ahead is the displacement right now, so the counter signal is just
 * the current tremor turned over. It is the baseline: whatever the
 * pipeline delay plus the horizon is, inversion arrives that late, and a
 * real predictor has to beat it.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include "registry.h"

#include <utils/logger/logger.h>

typedef struct Inversion_State
{
  double last; // m
  uint32_t horizon; // steps
  uint32_t has_sample;
} Inversion_State;

static bool inversion_init(void* state, const Predictor_Config* cfg)
{
  Inversion_State* s = (Inversion_State*)state;
  if (cfg->horizon_steps == 0 || cfg->horizon_steps > K_MAX)
  {
    MTS_LOG_ERROR("inversion: horizon of %u steps, must be 1 .. %d", cfg->horizon_steps, K_MAX);
    return false;
  }
  s->last = 0.0;
  s->horizon = cfg->horizon_steps;
  s->has_sample = 0;
  return true;
}

static void inversion_reset(void* state)
{
  Inversion_State* s = (Inversion_State*)state;
  s->last = 0.0;
  s->has_sample = 0;
}

static void inversion_update(void* state, double x)
{
  Inversion_State* s = (Inversion_State*)state;
  s->last = x;
  s->has_sample = 1;
}

static size_t inversion_predict(void* state, double* out, size_t k_max)
{
  Inversion_State* s = (Inversion_State*)state;
  if (!s->has_sample)
  {
    return 0;
  }
  const size_t k = s->horizon < k_max ? s->horizon : k_max;
  for (size_t i = 0; i < k; i++)
  {
    out[i] = s->last;
  }
  return k;
}

static int64_t inversion_delay_ns(const void* state)
{
  (void)state;
  return 0;
}

const Predictor_Ops PREDICTOR_OPS_INVERSION = {
  .id = PREDICTOR_INVERSION,
  .name = "inversion",
  .state_size = sizeof(Inversion_State),
  .init = inversion_init,
  .reset = inversion_reset,
  .update = inversion_update,
  .predict = inversion_predict,
  .delay_ns = inversion_delay_ns,
};
