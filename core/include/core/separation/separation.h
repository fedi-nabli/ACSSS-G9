/* ===================================================================================================
 * core/include/core/separation/separation.h
 *
 * SEAM HEADER: interface only, implementation to come.
 *
 * Splits tremor from voluntary motion AND integrates to tremor
 * displacement. Band-pass before or after integrating is equivalent (both
 * linear); only the tremor-band displacement is computed, voluntary
 * displacement would just drift. Two selectable estimators: neutral
 * (band-pass + Kalman) or WFLC.
 *
 * Opaque: the internals use filters / kalman / wflc, which this header must
 * not depend on. Same contract as every stage 1 module (see fusion.h).
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_CORE_SEPARATION_SEPARATION_H
#define MTS_CORE_SEPARATION_SEPARATION_H

#include <core/types/vec.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Separation_Config.estimator values
#define SEPARATION_NEUTRAL 0u // band-pass + Kalman
#define SEPARATION_WFLC 1u

typedef struct Separation Separation;

typedef struct Separation_Config
{
  double rate_hz; // measured sample rate
  double band_lo_hz; // tremor band, 4 Hz by default
  double band_hi_hz; // 12 Hz by default
  uint32_t filter_order;
  uint32_t estimator; // SEPARATION_*
} Separation_Config;

typedef struct Separation_Output
{
  Vec3 tremor_acc; // m/s^2, body frame, band-passed
  Vec3 tremor_disp; // m, body frame, band-passed displacement
  int64_t delay_ns;
} Separation_Output;

// NULL (and a logged error) on a bad config or out of memory
Separation* separation_create(const Separation_Config* cfg);

// NULL is fine
void separation_destroy(Separation* s);

void separation_reset(Separation* s);

// lin_acc_body: Fusion_Output::lin_acc_body of the same sample
void separation_update(Separation* s, const Vec3* lin_acc_body, Separation_Output* out);

#ifdef __cplusplus
}
#endif

#endif /* MTS_CORE_SEPARATION_SEPARATION_H */

