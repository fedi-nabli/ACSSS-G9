/* ===================================================================================================
 * core/include/core/types/raw_sample.h
 *
 * Raw_Sample (FROZEN): one reading of all nine axes at one instant, as it
 * leaves the Hat. Produced by Hat_Sim / Hat_Real, carried by the sample
 * queue, consumed by Processing.
 *
 * Conventions (all core types):
 *  - plain C struct: fixed-width ints, fixed arrays, no pointers, no heap
 *  - SI units: rad/s, m/s^2, uT, ns (the protocol layer converts for the wire)
 *  - float where the source is quantized (raw sensor values)
 *  - axes in one fixed frame: x/y/z as the Hat reports them
 *
 * Frozen: the layout is checked below at compile time. Changing a field,
 * its type or its order is a compile error until the checks are updated
 * on purpose.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_CORE_TYPES_RAW_SAMPLE_H
#define MTS_CORE_TYPES_RAW_SAMPLE_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Raw_Sample.flags bits
#define SAMPLE_MAG_FRESH (UINT32_C(1) << 0) // mag was updated in this sample (else repeated from the last one)
#define SAMPLE_GAP (UINT32_C(1) << 1) // samples were lost just before this one
#define SAMPLE_SATURATED (UINT32_C(1) << 2) // at least one axis hit its range limit

typedef struct Raw_Sample
{
  uint64_t seq; // starts at 0 when the Hat opens, +1 per sample, never wraps
  int64_t t_ns; // CLOCK_MONOTONIC, stamped once at the Hat
  float gyro[3]; // angular rate, rad/s
  float accel[3]; // acceleration including gravity, m/s^2
  float mag[3]; // magnetic field, uT (stale unless SAMPLE_MAG_FRESH)
  uint32_t flags; // SAMPLE_* bits
} Raw_Sample;

/* Frozen layout. static_assert: a macro from <assert.h> in C11, a keyword
 * in C++, so this header checks itself in both languages. */
static_assert(sizeof(Raw_Sample) == 56, "Raw_Sample layout changed");
static_assert(offsetof(Raw_Sample, seq) == 0, "Raw_Sample layout changed");
static_assert(offsetof(Raw_Sample, t_ns) == 8, "Raw_Sample layout changed");
static_assert(offsetof(Raw_Sample, gyro) == 16, "Raw_Sample layout changed");
static_assert(offsetof(Raw_Sample, accel) == 28, "Raw_Sample layout changed");
static_assert(offsetof(Raw_Sample, mag) == 40, "Raw_Sample layout changed");
static_assert(offsetof(Raw_Sample, flags) == 52, "Raw_Sample layout changed");

#ifdef __cplusplus
}
#endif

#endif /* MTS_CORE_TYPES_RAW_SAMPLE_H */

