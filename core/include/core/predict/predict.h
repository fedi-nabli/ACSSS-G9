/* ===================================================================================================
 * core/include/core/predict/predict.h
 *
 * SEAM HEADER: interface only, implementation to come.
 *
 * Predictor ABI. Every predictor is scalar and runs PER AXIS: the
 * Prediction thread owns three instances (x, y, z) of each enabled
 * predictor. Input: one axis of Tremor_Sample::tremor_disp per sample.
 * Output: that axis k steps ahead, for k = 1..k_valid.
 *
 * A predictor type is one static const Predictor_Ops: a descriptor with
 * its identity and functions, no per-instance data. Per-instance data is
 * a state block of ops->state_size bytes that the CALLER allocates (once,
 * at startup, with malloc or anything aligned to max_align_t), so nothing
 * allocates on the hot path.
 *
 * Contract:
 *  - init once before anything else, reset at any time (clears history)
 *  - update once per sample, in order; predict any time after
 *  - one instance is used by one thread
 *
 * Adding a predictor: next PREDICTOR_* id (core/types/predictor_id.h), its
 * member in Predictor_Config.params if it has parameters, its Predictor_Ops,
 * and an entry in the registry.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_CORE_PREDICT_PREDICT_H
#define MTS_CORE_PREDICT_PREDICT_H

#include <core/types/forecast.h>
#include <core/types/predictor_id.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* params: the member named after the predictor (ops->name); inversion has
 * none. The fields are PLACEHOLDERS: the seam fixes the mechanism (one
 * union, one named member per predictor), each predictor's author sets its
 * real fields. */
typedef struct Predictor_Config
{
  double rate_hz; // measured sample rate
  uint32_t horizon_steps; // steps to predict, <= K_MAX
  uint32_t pad;
  union
  {
    struct
    {
      uint32_t n_harmonics;
      double forget;
    } harmonic;
    struct
    {
      uint32_t window;
    } fft_oscillator;
    struct
    {
      uint32_t n_harmonics;
      double mu_freq;
      double mu_amp;
    } wflc;
    struct
    {
      uint32_t n_harmonics;
      double mu_freq;
      double mu_amp;
      double q; // process noise
      double r; // measurement noise
    } wflc_kalman;
  } params;
} Predictor_Config;

typedef struct Predictor_Ops
{
  Predictor_Id id; // PREDICTOR_*: Forecast::predictor_id and the mailbox slot index
  const char* name; // wire and config name: "inversion", "wflc", ...
  size_t state_size; // bytes the caller allocates per instance

  // false (and a logged error) on a bad config
  bool (*init)(void* state, const Predictor_Config* cfg);

  void (*reset)(void* state);

  // One new sample of ONE axis, m
  void (*update)(void* state, double x);

  // Fills out[0 .. k-1] with steps 1..k, k <= k_max. Returns k (k_valid), 0 = not ready yet
  size_t (*predict)(void* state, double* out, size_t k_max);

  // Delay this predictor adds on top of stage 1
  int64_t (*delay_ns)(const void* state);
} Predictor_Ops;

// Registry. NULL if no predictor has that name / id
const Predictor_Ops* predictor_find(const char* name);
const Predictor_Ops* predictor_by_id(Predictor_Id id);

#ifdef __cplusplus
}
#endif

#endif /* MTS_CORE_PREDICT_PREDICT_H */

