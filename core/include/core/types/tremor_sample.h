/* ===================================================================================================
 * core/include/core/types/tremor_sample.h
 *
 * Tremor_Sample (PROVISIONAL): the output of stage 1, one per Raw_Sample.
 * Assembled by Processing from the calibration, fusion, separation and
 * detect outputs; carried to Prediction by a queue.
 *
 * Provisional: fields may still change during implementation, so only the
 * size is checked (multiple of 8), not the offsets.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_CORE_TYPES_TREMOR_SAMPLE_H
#define MTS_CORE_TYPES_TREMOR_SAMPLE_H

#include <core/types/vec.h>

#include <assert.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Tremor_Sample.flags bits
#define TREMOR_ON (UINT32_C(1) << 0) // detector says tremor is present
#define TREMOR_GAP (UINT32_C(1) << 1) // samples were lost before this one (from SAMPLE_GAP)
#define TREMOR_RESET (UINT32_C(1) << 2) // stage 1 state was reset: predictors should reset too

typedef struct Tremor_Sample
{
  uint64_t seq; // seq of the Raw_Sample it came from
  int64_t t_ns; // t_ns of that Raw_Sample
  uint32_t flags; // TREMOR_* bits
  uint32_t pad;
  Vec3 tremor_disp; // m, body frame, band-passed displacement: the predictor input
  Vec3 tremor_acc; // m/s^2, body frame, band-passed acceleration (for the UI)
  Quat orientation; // body -> world (for the UI 3D view)
  double tremor_hz; // dominant tremor frequency
  double tremor_amp; // m, displacement amplitude
  double confidence; // 0..1
  int64_t delay_ns; // total stage 1 delay: how far this lags the real motion
} Tremor_Sample;

static_assert(sizeof(Tremor_Sample) % 8 == 0, "Tremor_Sample must be a multiple of 8 bytes");

#ifdef __cplusplus
}
#endif

#endif /* MTS_CORE_TYPES_TREMOR_SAMPLE_H */

