/* ===================================================================================================
 * core/include/core/calibration/calibration.h
 *
 * Applies the per-device sensor corrections, the first step of stage 1:
 *   corrected = M * (raw - offset)     per sensor, per sample
 * All values SI and post-normalization (Hat_Real already converted units
 * and axes). Calibration is plain data with no state of its own: config
 * loads and saves it from the calibration file, so it is a concrete struct
 * (not opaque like the stateful stage 1 modules).
 *
 * Estimating the corrections (hold still for gyro bias, rotate for the
 * magnetometer, quick zero at every start) also lives in this module; its
 * interface is added with the implementation.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_CORE_CALIBRATION_CALIBRATION_H
#define MTS_CORE_CALIBRATION_CALIBRATION_H

#include <core/types/raw_sample.h>

#ifdef __cplusplus
extern "C" {
#endif

// corrected = m * (raw - offset). Identity: offset 0, m = I
typedef struct Sensor_Correction
{
  double offset[3]; // sensor units (m/s^2, rad/s, uT)
  double m[3][3]; // scale + misalignment (+ soft iron for mag), row-major
} Sensor_Correction;

typedef struct Calibration
{
  Sensor_Correction accel;
  Sensor_Correction gyro;
  Sensor_Correction mag;
} Calibration;

/* out = raw with all three sensors corrected. seq, t_ns and flags are
 * copied unchanged. raw and out may point to the same sample. */
/* out = raw with accel, gyro and mag corrected; seq, t_ns and flags kept.
 * raw and out may be the same sample. */
void calibration_apply(const Calibration* cal, const Raw_Sample* raw, Raw_Sample* out);

// offset 0, m = I for all three sensors: calibration_apply copies
void calibration_identity(Calibration* cal);

#ifdef __cplusplus
}
#endif

#endif /* MTS_CORE_CALIBRATION_CALIBRATION_H */
