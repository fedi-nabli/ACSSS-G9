/* ===================================================================================================
 * core/include/core/types/forecast.h
 *
 * Forecast (PROVISIONAL): one predictor's prediction of the tremor
 * displacement for the next k_valid steps, all three axes. Written by the
 * Prediction thread into one latest-wins mailbox slot per predictor id,
 * read by Processing stage 2.
 *
 * Goes through a mailbox: the size must stay a multiple of 8 (checked).
 * Provisional: only the size is checked, not the offsets.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_CORE_TYPES_FORECAST_H
#define MTS_CORE_TYPES_FORECAST_H

#include <core/types/predictor_id.h>

#include <assert.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Longest forecast: at 200 Hz, 160 ms of lookahead including filter delay
#define K_MAX 32

typedef struct Forecast
{
  uint64_t origin_seq; // seq of the last Tremor_Sample the predictor saw
  int64_t origin_t_ns; // its t_ns
  int64_t step_ns; // time between two forecast steps (1 / measured rate)
  uint32_t k_valid; // 0..K_MAX steps filled. 0 = predictor not ready
  Predictor_Id predictor_id; // PREDICTOR_*
  uint32_t cfg_gen; // config generation it was computed under: stale -> discard
  uint32_t pad;
  double y[3][K_MAX]; // m, body frame, y[axis][k-1] = displacement k steps after origin
} Forecast;

static_assert(sizeof(Forecast) % 8 == 0, "Forecast must be a multiple of 8 bytes (mailbox)");

#ifdef __cplusplus
}
#endif

#endif /* MTS_CORE_TYPES_FORECAST_H */

