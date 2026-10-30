/* ===================================================================================================
 * core/include/core/fusion/fusion.h
 *
 * Orientation estimation, magnetometer correction, online gyro bias and
 * gravity removal. Input: one calibrated Raw_Sample. Output: orientation
 * and gravity-free linear acceleration in the body frame. No displacement
 * here (that is separation's job).
 *
 * v0 estimator: Mahony's nonlinear complementary filter, 9 DOF. The gyro
 * is integrated (exact rotation per step), and corrected toward gravity
 * (accel) and magnetic north (mag) with a PI law: kp pulls the attitude,
 * ki learns the gyro bias online. Details in fusion.c. Fusion is opaque:
 * a different estimator can replace it behind this header.
 *  - accel corrects only while |a| is within accel_gate of 1 g: under
 *    strong linear acceleration it does not point at gravity
 *  - mag corrects only on fresh samples (SAMPLE_MAG_FRESH), and only the
 *    heading: a disturbed field can turn the yaw, never tilt the board
 *  - the first sample sets the attitude directly (tilt from accel, heading
 *    from mag), so there is no slow start from identity
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

#include <stdbool.h>

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
  double kp; // rad/s per unit of attitude error: the correction's bandwidth (~kp / 2 pi Hz)
  double ki; // rad/s^2 per unit of error: online gyro bias
  double accel_gate; // fraction of g: accel corrects only while ||a| - g| < accel_gate * g
  double settle_s; // FUSION_CONVERGED after this long since the first sample
} Fusion_Config;

/* kp 1 (0.16 Hz: tremor at 4-12 Hz barely moves the attitude), ki 0.05
 * (a gyro bias is learned in about kp / ki = 20 s), settle 2 s, gate 0.03 g.
 * The gate checks |a| only, and |a| grows with the square of a sideways
 * push: 0.03 g catches 3 m/s^2 sideways (|a| + 0.46) but not a gentle
 * 1 m/s^2 one (|a| + 0.05). Tremor (~0.5 m/s^2) passes when sideways and
 * is gated when along gravity, which costs nothing: the gyro carries the
 * attitude through. A better test of "is this gravity" is for the next
 * estimator. */
Fusion_Config fusion_config_default(double rate_hz);

typedef struct Fusion_Output
{
  Quat q; // body -> world
  Vec3 lin_acc_body; // m/s^2, gravity removed, body frame
  uint32_t flags; // FUSION_* bits
  uint32_t pad;
  int64_t delay_ns;
} Fusion_Output;

// NULL (and a logged error) on a bad config (rate not > 0, a gain < 0, ...) or out of memory
Fusion* fusion_create(const Fusion_Config* cfg);

// NULL is fine
void fusion_destroy(Fusion* f);

// Back to the state right after create (FUSION_CONVERGED unset)
void fusion_reset(Fusion* f);

// One calibrated sample in, one output out
void fusion_update(Fusion* f, const Raw_Sample* calibrated, Fusion_Output* out);

// Copy an existing initialized Fusion instance's state.
bool fusion_copy_state(Fusion* dst, const Fusion* src);

#ifdef __cplusplus
}
#endif

#endif /* MTS_CORE_FUSION_FUSION_H */

