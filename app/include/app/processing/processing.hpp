/* ===================================================================================================
 * app/include/app/processing/processing.hpp
 *
 * Processing: samples in, frames out, in 6 DOF: two chains.
 *
 *   Raw_Sample -> calibration_apply -> fusion -> separation (2 integrations) -> detect    translation, m
 *                                   -> gyro   -> separation (1 integration)  -> detect    rotation, rad
 *              -> the enabled predictors, per axis, on all 6 axes                       (core/predict)
 *              -> Output_Frame
 *
 * The rotation chain needs no fusion: the gyro has no gravity in it, and
 * the band-pass removes its bias. Each chain has its own tremor gate.
 *
 * Processing_Core is that chain for one sample, without threads, so tests
 * drive it sample by sample. Processing is the thread around it:
 *
 *   until stopping:
 *     next Raw_Sample (waits for it)
 *     latest Pipeline_Control (a new cfg_gen applies from this sample on)
 *     the chain, always: filters and predictors stay warm while stopped
 *     running -> the frame to the server
 *
 * Forecasts are compared with what actually happened: a forecast made at
 * sample n for n + h waits h samples in a small ring, then becomes that
 * sample's forecast_mm, and residual = displacement - forecast. A gap
 * (SAMPLE_GAP) or a new cfg_gen resets the predictors and the rings: across
 * a gap, "h samples later" is no longer h * 1/rate later. Predictors run
 * per axis (0-2 translation, 3-5 rotation): an axis whose predictor has no
 * forecast (not locked, e.g. no tremor on it) forecasts 0, and the frame has
 * a forecast once any of the 6 axes does.
 *
 * Calibration: the identity for now (calibration is simulated). The LED is
 * set off once at start.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_APP_PROCESSING_PROCESSING_HPP
#define MTS_APP_PROCESSING_PROCESSING_HPP

#include <app/link/led_command.h>
#include <app/link/output_frame.h>
#include <app/link/pipeline_control.h>
#include <app/link/sample_queue.hpp>
#include <app/link/server_message.h>
#include <core/calibration/calibration.h>
#include <core/predict/predict.h>
#include <core/types/forecast.h>
#include <core/types/raw_sample.h>
#include <utils/mailbox/mailbox.hpp>
#include <utils/ring_buffer/queue.hpp>
#include <utils/time/time.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>

struct Fusion;
struct Separation;
struct Detector;

namespace mts
{

// The longest the thread waits for a sample before checking whether to stop
constexpr int64_t PROCESSING_POLL_NS = 50 * MTS_TIME_NS_PER_MS;

// Rotation tremor smaller than this (peak) never turns the rotation gate on: 1 mrad, 0.06 deg
constexpr double ROTATION_MIN_AMP_RAD = 1e-3;

class Processing_Core
{
public:
  // rate_hz: Hat_Info::rate_hz. Builds stage 1; see valid()
  explicit Processing_Core(double rate_hz);
  ~Processing_Core();

  Processing_Core(const Processing_Core&) = delete;
  Processing_Core& operator=(const Processing_Core&) = delete;

  // false: a stage 1 module could not be created (logged by it)
  bool valid() const;

  /* Takes the settings of ctl if its cfg_gen is new: rebuilds the
   * predictor slots (enabled set, horizon) and resets them. */
  void apply(const Pipeline_Control& ctl);

  // One sample through the whole chain. now_ns: for the processing part of pipeline_delay
  void process(const Raw_Sample& s, int64_t now_ns, Output_Frame& out);

  // Forecast horizon in samples, from the last apply (0 before any)
  uint32_t horizon_steps() const;

private:
  // Predictor axes: 0-2 translation x y z, 3-5 rotation x y z
  static constexpr int AXES = 6;

  struct Slot
  {
    Predictor_Id id;
    const Predictor_Ops* ops; // nullptr: no implementation yet (the entry still goes out, all null)
    alignas(std::max_align_t) unsigned char state[AXES][PREDICTOR_STATE_MAX];
    double made[K_MAX + 1][AXES]; // forecast made at sample n for n + h, at n % (K_MAX + 1)
    bool made_valid[K_MAX + 1];
  };

  void reset_predictors();

  double rate_hz_;
  Calibration cal_;
  Fusion* fusion_;
  Separation* separation_; // translation
  Detector* detect_;
  Separation* separation_rot_; // rotation
  Detector* detect_rot_;

  uint32_t cfg_gen_; // last applied, 0 = none
  uint32_t horizon_; // h, steps
  Predictor_Id active_;
  uint32_t n_slots_;
  std::unique_ptr<Slot[]> slots_; // P_MAX of them, allocated once
  uint64_t since_reset_; // samples since the predictors were last reset
};

class Processing
{
public:
  /* Consumer of samples and control, producer of frames and led. All four
   * must outlive the Processing. */
  Processing(double rate_hz, Sample_Queue& samples, Mailbox<Pipeline_Control>& control,
             Queue<Server_Message>& frames, Mailbox<Led_Command>& led);

  // Calls stop()
  ~Processing();

  Processing(const Processing&) = delete;
  Processing& operator=(const Processing&) = delete;

  // false (logged): stage 1 could not be built, or already started
  bool start();

  // Flag + join: within PROCESSING_POLL_NS. Safe to call twice
  void stop();

private:
  void run();

  Processing_Core core_;
  Sample_Queue& samples_;
  Mailbox<Pipeline_Control>& control_;
  Queue<Server_Message>& frames_;
  Mailbox<Led_Command>& led_;

  Server_Message msg_; // ~3 KB: a member, not on the thread's stack every sample
  std::atomic<bool> stopping_;
  std::thread thread_;
};

} // namespace mts

#endif /* MTS_APP_PROCESSING_PROCESSING_HPP */
