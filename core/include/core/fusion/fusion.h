/* ===================================================================================================
 * core/include/core/fusion/fusion.h
 *
 * SEAM HEADER: interface only, implementation to come.
 *
 * Orientation estimation, magnetometer correction, online gyro bias and
 * gravity removal. Input: one calibrated Raw_Sample. Output: orientation
 * and gravity-free linear acceleration in the body frame. No displacement
 * here (that is separation's job).
 *
 * Estimator still to choose (Mahony / Madgwick / error-state KF): Fusion is
 * opaque so the header does not depend on that choice. Fusion_Config gets
 * the estimator's gains once it is chosen.
 *
 * Contract (all core stage 1 modules):
 *  - create allocates (startup only), update never allocates
 *  - one update per sample, in sample order
 *  - one instance is used by one thread
 *  - every output reports the delay it adds (delay_ns)
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_CORE_FUSION_FUSION_H
#define MTS_CORE_FUSION_FUSION_H

#include <core/types/raw_sample.h>
#include <core/types/vec.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fusion_Output.flags bits
#define FUSION_CONVERGED (UINT32_C(1) << 0) // unset while the estimate settles after create/reset

typedef struct Fusion Fusion;

typedef struct Fusion_Config
{
  double rate_hz; // measured sample rate (Hat_Info::rate_hz)
  // + estimator gains once the estimator is chosen
} Fusion_Config;

typedef struct Fusion_Output
{
  Quat q; // body -> world
  Vec3 lin_acc_body; // m/s^2, gravity removed, body frame
  uint32_t flags; // FUSION_* bits
  uint32_t pad;
  int64_t delay_ns;
} Fusion_Output;

// NULL (and a logged error) on a bad config or out of memory
Fusion* fusion_create(const Fusion_Config* cfg);

// NULL is fine
void fusion_destroy(Fusion* f);

// Back to the state right after create (FUSION_CONVERGED unset)
void fusion_reset(Fusion* f);

// One calibrated sample in, one output out
void fusion_update(Fusion* f, const Raw_Sample* calibrated, Fusion_Output* out);

#ifdef __cplusplus
}
#endif

#endif /* MTS_CORE_FUSION_FUSION_H */

