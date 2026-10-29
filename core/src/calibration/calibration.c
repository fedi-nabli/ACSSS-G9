/* ===================================================================================================
 * core/src/calibration/calibration.c
 *
 * calibration_apply, see calibration.h. Estimation comes later.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <core/calibration/calibration.h>

#include <string.h>

static void correct(const Sensor_Correction* c, const float in[3], float out[3])
{
  const double d[3] = { in[0] - c->offset[0], in[1] - c->offset[1], in[2] - c->offset[2] };
  for (int i = 0; i < 3; i++)
  {
    out[i] = (float)(c->m[i][0] * d[0] + c->m[i][1] * d[1] + c->m[i][2] * d[2]);
  }
}

void calibration_apply(const Calibration* cal, const Raw_Sample* raw, Raw_Sample* out)
{
  Raw_Sample r = *raw; // raw and out may alias
  correct(&cal->accel, raw->accel, r.accel);
  correct(&cal->gyro, raw->gyro, r.gyro);
  correct(&cal->mag, raw->mag, r.mag);
  *out = r;
}

void calibration_identity(Calibration* cal)
{
  memset(cal, 0, sizeof(*cal));
  for (int i = 0; i < 3; i++)
  {
    cal->accel.m[i][i] = 1.0;
    cal->gyro.m[i][i] = 1.0;
    cal->mag.m[i][i] = 1.0;
  }
}
