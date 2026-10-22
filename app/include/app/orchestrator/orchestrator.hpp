/* ===================================================================================================
 * app/include/app/orchestrator/orchestrator.hpp
 *
 * The program's state machine (ui_contract.md, section 5), as plain logic:
 * no threads, no queues, no clock. Time comes in as now_ns, so every cell
 * of the contract's table can be tested deterministically. Pipeline runs it
 * on the main thread and does the plumbing.
 *
 *   boot -> needs_calibration (first tick; no calibration is stored yet)
 *   calibrate: calibrating (hold_still, rotate) -> ready
 *   start:     calibrating (quick_zero)         -> running
 *   stop:      running -> stopped
 *
 * Slice 1: calibration is simulated. The steps are timers and always
 * succeed. core/calibration replaces the timers later; the states, the
 * replies and the status stay.
 *
 * Status is due on every change, every calibrating_status_ns while
 * calibrating (the progress bar moves), and every heartbeat_ns otherwise.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_APP_ORCHESTRATOR_ORCHESTRATOR_HPP
#define MTS_APP_ORCHESTRATOR_ORCHESTRATOR_HPP

#include <app/link/command.h>
#include <app/link/pipeline_control.h>
#include <app/link/server_message.h>
#include <utils/time/time.h>

#include <cstdint>

namespace mts
{

// Contract defaults: inversion and wflc enabled, wflc active, 30 ms, 2 Hz, 5 Hz
Runtime_Settings default_runtime_settings();

struct Orchestrator_Config
{
  Runtime_Settings settings = default_runtime_settings(); // at boot
  int64_t quick_zero_ns = 500 * MTS_TIME_NS_PER_MS; // simulated calibration steps
  int64_t hold_still_ns = 2 * MTS_TIME_NS_PER_S;
  int64_t rotate_ns = 3 * MTS_TIME_NS_PER_S;
  int64_t heartbeat_ns = MTS_TIME_NS_PER_S;
  int64_t calibrating_status_ns = 100 * MTS_TIME_NS_PER_MS;
};

class Orchestrator
{
public:
  // sample_rate_hz: Hat_Info::rate_hz, reported in status
  Orchestrator(const Orchestrator_Config& cfg, double sample_rate_hz, int64_t now_ns);

  // Advances time: leaves boot, walks calibration steps
  void tick(int64_t now_ns);

  // Applies cmd if the state allows it. The reply goes back to cmd.client
  Command_Reply handle(const Command& cmd, int64_t now_ns);

  // The Hat gave up: stopped, and start/calibrate answer hat_error from now on
  void hat_failed(int64_t now_ns);

  /* true when a status is due (change, progress, heartbeat): out filled.
   * The drop counters are left 0: the caller knows the queues. */
  bool take_status(Status_Message& out, int64_t now_ns);

  // What Processing must do. cfg_gen changes with every new value
  Pipeline_Control control() const;

  uint32_t state() const;

private:
  struct Step
  {
    uint32_t id; // CAL_STEP_*
    int64_t duration_ns;
  };

  void set_state(uint32_t state);
  void begin_calibration(const Step* steps, uint32_t n, uint32_t then, int64_t now_ns);
  Command_Reply apply_setup(const Command& cmd, Command_Reply reply);
  uint32_t check(uint32_t kind) const;

  Orchestrator_Config cfg_;
  double sample_rate_hz_;
  uint32_t state_;
  Runtime_Settings settings_;
  uint32_t cfg_gen_;

  bool calibrated_; // a calibration exists
  int64_t calibrated_at_ns_;
  bool hat_failed_;

  // Calibrating
  Step steps_[2];
  uint32_t n_steps_;
  uint32_t step_; // index into steps_
  int64_t step_started_ns_;
  uint32_t after_calibration_; // state once the steps are done

  bool status_dirty_;
  int64_t last_status_ns_;
};

} // namespace mts

#endif /* MTS_APP_ORCHESTRATOR_ORCHESTRATOR_HPP */
