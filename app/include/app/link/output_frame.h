/* ===================================================================================================
 * app/include/app/link/output_frame.h
 *
 * Output_Frame: everything the UI shows for one sample (the "frame" line of
 * the UI protocol). Filled by Processing stage 2, carried to the server
 * thread inside a Server_Message, turned into NDJSON by protocol.
 *
 * SI units here (m, m/s^2, ns): protocol converts to the wire units.
 * Absent values are flagged, never encoded as magic numbers: a vector whose
 * valid flag is off goes out as null.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_APP_LINK_OUTPUT_FRAME_H
#define MTS_APP_LINK_OUTPUT_FRAME_H

#include <core/types/vec.h>
#include <core/types/predictor_id.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Most predictors enabled at once (pred[] entries, mailbox slots)
#define P_MAX 8

// Output_Frame.flags bits
#define FRAME_GAP_BEFORE (UINT32_C(1) << 0) // samples were lost just before this one
#define FRAME_TREMOR_ACTIVE (UINT32_C(1) << 1) // detector flag
#define FRAME_COUNTER_VALID (UINT32_C(1) << 2) // counter is set (else the tremor gate is closed: null)

// Frame_Prediction.flags bits
#define PRED_FORECAST_VALID (UINT32_C(1) << 0) // else warm-up: null
#define PRED_RESIDUAL_VALID (UINT32_C(1) << 1) // else tremor inactive or warm-up: null

typedef struct Frame_Prediction
{
  Predictor_Id predictor_id; // PREDICTOR_*
  uint32_t flags; // PRED_* bits
  Vec3 forecast; // m: what it predicted, horizon earlier, for this instant
  Vec3 residual; // m: actual minus forecast
} Frame_Prediction;

typedef struct Output_Frame
{
  uint64_t seq; // Raw_Sample seq
  int64_t t_ns; // Raw_Sample t_ns (absolute monotonic; protocol makes it relative to the Hat opening)
  uint32_t flags; // FRAME_* bits
  uint32_t n_predictions; // entries used in predictions[], <= P_MAX
  Vec3 tremor_disp; // m, body frame
  Vec3 tremor_acc; // m/s^2, body frame
  Vec3 imu_acc;   // Raw acceleration, m/s²
  Vec3 imu_gyro;  // Raw angular velocity, rad/s
  Quat orientation; // body -> world
  double tremor_hz;
  double tremor_amp; // m, peak
  double confidence; // 0..1
  int64_t pipeline_delay_ns;
  Vec3 counter; // m, negative of the active forecast (FRAME_COUNTER_VALID)
  Frame_Prediction predictions[P_MAX];
} Output_Frame;

#ifdef __cplusplus
}
#endif

#endif /* MTS_APP_LINK_OUTPUT_FRAME_H */
