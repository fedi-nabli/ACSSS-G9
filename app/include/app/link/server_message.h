/* ===================================================================================================
 * app/include/app/link/server_message.h
 *
 * Server_Message: one tagged message through the single Processing ->
 * server queue. One variant per periodic UI message: frame, metrics,
 * spectrum, status. (ack and error are replies to commands: Command_Reply,
 * see command.h.)
 *
 * SI units here; protocol converts. A scalar that can be absent
 * (calibration_age_s) is NaN when absent, and goes out as null.
 *
 * Size: the spectrum variant dominates (~3 KB per slot).
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_APP_LINK_SERVER_MESSAGE_H
#define MTS_APP_LINK_SERVER_MESSAGE_H

#include <app/link/output_frame.h>
#include <core/types/predictor_id.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Server_Message.kind values
#define SERVER_MSG_FRAME 1u
#define SERVER_MSG_METRICS 2u
#define SERVER_MSG_SPECTRUM 3u
#define SERVER_MSG_STATUS 4u

// Status_Message.state values (UI protocol state machine)
#define APP_STATE_BOOT 0u
#define APP_STATE_NEEDS_CALIBRATION 1u
#define APP_STATE_READY 2u
#define APP_STATE_CALIBRATING 3u
#define APP_STATE_RUNNING 4u
#define APP_STATE_STOPPED 5u

// Status_Message.calibration_step values (meaningful while calibrating)
#define CAL_STEP_NONE 0u
#define CAL_STEP_QUICK_ZERO 1u
#define CAL_STEP_HOLD_STILL 2u
#define CAL_STEP_ROTATE 3u

/* Spectrum bins: 0-25 Hz of a ~2 s window rounded to a power of two. The
 * count depends on the rate and the rounding (65 at 100 or 200 Hz, 86 at
 * 150 Hz rounding up), so the maximum leaves room. */
#define SPECTRUM_MAX_BINS 128

typedef struct Predictor_Metrics
{
  Predictor_Id predictor_id;
  uint32_t pad;
  double suppression_ratio; // residual RMS / tremor RMS
  double residual_rms; // m
} Predictor_Metrics;

// One snapshot for all enabled predictors: one "metrics" line each
typedef struct Metrics_Message
{
  int64_t t_ns; // end of the window
  int64_t window_ns;
  uint32_t count; // entries used in items[], <= P_MAX
  uint32_t pad;
  Predictor_Metrics items[P_MAX];
} Metrics_Message;

typedef struct Spectrum_Message
{
  int64_t t_ns; // end of the analysis window
  double bin_width_hz; // bin i is at i * bin_width_hz
  uint32_t n_bins; // used bins per axis, <= SPECTRUM_MAX_BINS
  uint32_t pad;
  double amp[3][SPECTRUM_MAX_BINS]; // m/s^2, amp[axis][bin]
} Spectrum_Message;

typedef struct Status_Message
{
  uint32_t state; // APP_STATE_*
  uint32_t calibration_step; // CAL_STEP_*, while calibrating
  double calibration_progress; // 0..1, while calibrating
  double calibration_age_s; // NaN = no stored calibration
  double sample_rate_hz; // measured
  int64_t horizon_ns;
  double metrics_hz;
  double spectrum_hz;
  uint32_t enabled_mask; // bit i = predictor id i enabled
  Predictor_Id active_predictor_id;
  uint64_t drops_hat_to_processing;
  uint64_t drops_processing_to_prediction;
  uint64_t drops_processing_to_server;
} Status_Message;

typedef struct Server_Message
{
  uint32_t kind; // SERVER_MSG_*: which member of the union is set
  uint32_t pad;
  union
  {
    Output_Frame frame;
    Metrics_Message metrics;
    Spectrum_Message spectrum;
    Status_Message status;
  };
} Server_Message;

#ifdef __cplusplus
}
#endif

#endif /* MTS_APP_LINK_SERVER_MESSAGE_H */
