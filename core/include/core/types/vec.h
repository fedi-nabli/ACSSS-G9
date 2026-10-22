/* ===================================================================================================
 * core/include/core/types/vec.h
 *
 * Small math types shared by the whole pipeline, and THE place where the
 * axis frames are defined.
 *
 * Frames:
 *  - Body frame: the Hat's x/y/z, exactly as in Raw_Sample. Everything
 *    per-axis (tremor displacement, predictors, forecasts) is in this frame.
 *  - World frame (NWU): z up, x toward magnetic north in the horizontal
 *    plane, y = z cross x (west). Right-handed.
 *  - Board flat and still: orientation is the identity quaternion and the
 *    accelerometer reads +g on body z.
 *
 * Quaternion: Hamilton convention, stored w, x, y, z, unit length, rotates
 * a vector from body to world: v_world = q * v_body * conj(q).
 *
 * Arrays rather than named fields: per-axis loops (for a = AXIS_X..AXIS_Z)
 * are the common case, the predictors run once per axis.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_CORE_TYPES_VEC_H
#define MTS_CORE_TYPES_VEC_H

#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AXIS_X 0
#define AXIS_Y 1
#define AXIS_Z 2
#define AXIS_COUNT 3

// Index with AXIS_X / AXIS_Y / AXIS_Z
typedef struct Vec3
{
  double v[AXIS_COUNT];
} Vec3;

// q[0] = w, q[1] = x, q[2] = y, q[3] = z. Rotates body -> world
typedef struct Quat
{
  double q[4];
} Quat;

#define QUAT_W 0
#define QUAT_X 1
#define QUAT_Y 2
#define QUAT_Z 3

static_assert(sizeof(Vec3) == 3 * sizeof(double), "Vec3 must have no padding");
static_assert(sizeof(Quat) == 4 * sizeof(double), "Quat must have no padding");

#ifdef __cplusplus
}
#endif

#endif /* MTS_CORE_TYPES_VEC_H */

