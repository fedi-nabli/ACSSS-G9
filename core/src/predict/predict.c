/* ===================================================================================================
 * core/src/predict/predict.c
 *
 * The registry: every predictor, by id and by name. Indexed by id, so
 * predictor_by_id is a lookup; a NULL entry is an id without an
 * implementation yet.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include "registry.h"

#include <core/wflc/wflc.h>

#include <string.h>

static const Predictor_Ops* const REGISTRY[PREDICTOR_COUNT] = {
  [PREDICTOR_INVERSION] = &PREDICTOR_OPS_INVERSION,
  [PREDICTOR_WFLC] = &PREDICTOR_OPS_WFLC,
};

Predictor_Config predictor_config_default(Predictor_Id id, double rate_hz, uint32_t horizon_steps)
{
  Predictor_Config c;
  memset(&c, 0, sizeof(c));
  c.rate_hz = rate_hz;
  c.horizon_steps = horizon_steps;
  if (id == PREDICTOR_WFLC)
  {
    const Wflc_Config w = wflc_config_default(rate_hz);
    c.params.wflc.n_harmonics = w.n_harmonics;
    c.params.wflc.mu_freq = w.mu_freq;
    c.params.wflc.mu_amp = w.mu_amp;
  }
  return c;
}

const Predictor_Ops* predictor_by_id(Predictor_Id id)
{
  return id < PREDICTOR_COUNT ? REGISTRY[id] : NULL;
}

const Predictor_Ops* predictor_find(const char* name)
{
  if (!name)
  {
    return NULL;
  }
  for (Predictor_Id id = 0; id < PREDICTOR_COUNT; id++)
  {
    if (REGISTRY[id] && strcmp(REGISTRY[id]->name, name) == 0)
    {
      return REGISTRY[id];
    }
  }
  return NULL;
}
