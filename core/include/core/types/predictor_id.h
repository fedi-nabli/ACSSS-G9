/* ===================================================================================================
 * core/include/core/types/predictor_id.h
 *
 * Compact in-code identifier of a predictor. Lives in types (not predict)
 * because Forecast stores it, and types must not depend on an algorithm.
 *
 * Ids are dense, 0 .. PREDICTOR_COUNT - 1: they index the per-predictor
 * mailbox slots and the pred[] entries of an output frame. Config and the
 * wire use the predictor's name (Predictor_Ops::name), never the number,
 * so adding a predictor is: next id, bump PREDICTOR_COUNT, register it.
 *
 * uint32_t + defines rather than an enum: fixed size inside Forecast.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_CORE_TYPES_PREDICTOR_ID_H
#define MTS_CORE_TYPES_PREDICTOR_ID_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t Predictor_Id;

#define PREDICTOR_INVERSION 0u // baseline: counter = -current displacement
#define PREDICTOR_HARMONIC 1u
#define PREDICTOR_FFT_OSCILLATOR 2u
#define PREDICTOR_WFLC 3u
#define PREDICTOR_WFLC_KALMAN 4u // Zhou et al.
#define PREDICTOR_COUNT 5u

#ifdef __cplusplus
}
#endif

#endif /* MTS_CORE_TYPES_PREDICTOR_ID_H */

