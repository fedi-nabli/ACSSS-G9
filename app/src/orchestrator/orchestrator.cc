/* ===================================================================================================
 * app/src/orchestrator/orchestrator.cc
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include "core/types/predictor_id.h"
#include <app/orchestrator/orchestrator.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace mts
{

namespace
{

const char* command_name(uint32_t kind)
{
  switch (kind)
  {
    case CMD_START:
      return "start";
    case CMD_STOP:
      return "stop";
    case CMD_CALIBRATE:
      return "calibrate";
    case CMD_SETUP:
      return "setup";
    default:
      return "?";
  }
}

const char* state_name(uint32_t state)
{
  switch (state)
  {
    case APP_STATE_BOOT:
      return "boot";
    case APP_STATE_NEEDS_CALIBRATION:
      return "needs_calibration";
    case APP_STATE_READY:
      return "ready";
    case APP_STATE_CALIBRATING:
      return "calibrating";
    case APP_STATE_RUNNING:
      return "running";
    case APP_STATE_STOPPED:
      return "stopped";
    default:
      return "?";
  }
}

} // namespace

Runtime_Settings default_runtime_settings()
{
  Runtime_Settings s;
  std::memset(&s, 0, sizeof(s));
    s.enabled_mask =
      (UINT32_C(1) << PREDICTOR_INVERSION) |
      (UINT32_C(1) << PREDICTOR_WFLC) |
      (UINT32_C(1) << PREDICTOR_LSTM);

  s.active_predictor_id = PREDICTOR_WFLC;
  s.horizon_ns = 30 * MTS_TIME_NS_PER_MS;
  s.metrics_hz = 2.0;
  s.spectrum_hz = 5.0;
  return s;
}

Orchestrator::Orchestrator(const Orchestrator_Config& cfg, double sample_rate_hz, int64_t now_ns)
  : cfg_(cfg)
  , sample_rate_hz_(sample_rate_hz)
  , state_(APP_STATE_BOOT)
  , settings_(cfg.settings)
  , cfg_gen_(1)
  , calibrated_(false)
  , calibrated_at_ns_(0)
  , hat_failed_(false)
  , steps_{}
  , n_steps_(0)
  , step_(0)
  , step_started_ns_(0)
  , after_calibration_(APP_STATE_READY)
  , status_dirty_(true)
  , last_status_ns_(now_ns)
{
}

void Orchestrator::set_state(uint32_t state)
{
  if (state == state_)
  {
    return;
  }
  // Processing only cares about running or not: a new generation when that flips
  if ((state == APP_STATE_RUNNING) != (state_ == APP_STATE_RUNNING))
  {
    cfg_gen_++;
  }
  state_ = state;
  status_dirty_ = true;
}

void Orchestrator::begin_calibration(const Step* steps, uint32_t n, uint32_t then, int64_t now_ns)
{
  for (uint32_t i = 0; i < n; i++)
  {
    steps_[i] = steps[i];
  }
  n_steps_ = n;
  step_ = 0;
  step_started_ns_ = now_ns;
  after_calibration_ = then;
  set_state(APP_STATE_CALIBRATING);
}

void Orchestrator::tick(int64_t now_ns)
{
  if (state_ == APP_STATE_BOOT)
  {
    set_state(calibrated_ ? APP_STATE_READY : APP_STATE_NEEDS_CALIBRATION);
    return;
  }
  if (state_ != APP_STATE_CALIBRATING)
  {
    return;
  }

  // Steps end on their own schedule, not on when tick happens to run
  while (step_ < n_steps_ && now_ns - step_started_ns_ >= steps_[step_].duration_ns)
  {
    step_started_ns_ += steps_[step_].duration_ns;
    step_++;
    status_dirty_ = true;
  }
  if (step_ < n_steps_)
  {
    return;
  }

  // Slice 1: always succeeds. The full calibration is a new calibration;
  // the quick zero only refreshes the existing one
  if (after_calibration_ == APP_STATE_READY)
  {
    calibrated_ = true;
    calibrated_at_ns_ = step_started_ns_;
  }
  set_state(after_calibration_);
}

uint32_t Orchestrator::check(uint32_t kind) const
{
  // The contract's table, row by row
  switch (state_)
  {
    case APP_STATE_BOOT:
      return ERR_INVALID_STATE;
    case APP_STATE_CALIBRATING:
      return ERR_BUSY;
    case APP_STATE_NEEDS_CALIBRATION:
      if (kind == CMD_START)
      {
        return ERR_NEEDS_CALIBRATION;
      }
      return kind == CMD_STOP ? ERR_INVALID_STATE : ERR_NONE;
    case APP_STATE_READY:
    case APP_STATE_STOPPED:
      return kind == CMD_STOP ? ERR_INVALID_STATE : ERR_NONE;
    case APP_STATE_RUNNING:
      return kind == CMD_STOP || kind == CMD_SETUP ? ERR_NONE : ERR_INVALID_STATE;
    default:
      return ERR_INVALID_STATE;
  }
}

Command_Reply Orchestrator::handle(const Command& cmd, int64_t now_ns)
{
  Command_Reply reply;
  std::memset(&reply, 0, sizeof(reply));
  reply.id = cmd.id;
  reply.has_id = 1;
  reply.client = cmd.client;

  if (cmd.kind < CMD_START || cmd.kind > CMD_SETUP)
  {
    reply.error = ERR_UNKNOWN_COMMAND;
    std::snprintf(reply.msg, sizeof(reply.msg), "Unknown command.");
    return reply;
  }

  reply.error = check(cmd.kind);
  if (reply.error == ERR_NONE && hat_failed_ && (cmd.kind == CMD_START || cmd.kind == CMD_CALIBRATE))
  {
    reply.error = ERR_HAT_ERROR;
  }

  switch (reply.error)
  {
    case ERR_NONE:
      break;
    case ERR_NEEDS_CALIBRATION:
      std::snprintf(reply.msg, sizeof(reply.msg), "No valid calibration; run calibrate first.");
      return reply;
    case ERR_BUSY:
      std::snprintf(reply.msg, sizeof(reply.msg), "Calibration in progress; wait for it to finish.");
      return reply;
    case ERR_HAT_ERROR:
      std::snprintf(reply.msg, sizeof(reply.msg), "The sensor failed; restart the program.");
      return reply;
    default:
      std::snprintf(reply.msg, sizeof(reply.msg), "%s is not valid in state %s.", command_name(cmd.kind),
                    state_name(state_));
      return reply;
  }

  switch (cmd.kind)
  {
    case CMD_START:
    {
      const Step quick[] = { { CAL_STEP_QUICK_ZERO, cfg_.quick_zero_ns } };
      begin_calibration(quick, 1, APP_STATE_RUNNING, now_ns);
      break;
    }
    case CMD_STOP:
      set_state(APP_STATE_STOPPED);
      break;
    case CMD_CALIBRATE:
    {
      const Step full[] = { { CAL_STEP_HOLD_STILL, cfg_.hold_still_ns }, { CAL_STEP_ROTATE, cfg_.rotate_ns } };
      begin_calibration(full, 2, APP_STATE_READY, now_ns);
      break;
    }
    case CMD_SETUP:
      return apply_setup(cmd, reply);
  }
  return reply;
}

Command_Reply Orchestrator::apply_setup(const Command& cmd, Command_Reply reply)
{
  // Only the fields the command carries; the result must hold as a whole
  Runtime_Settings next = settings_;
  const Runtime_Settings& in = cmd.settings;
  if (cmd.set_mask & SETTING_ENABLED_PREDICTORS)
  {
    next.enabled_mask = in.enabled_mask;
  }
  if (cmd.set_mask & SETTING_ACTIVE_PREDICTOR)
  {
    next.active_predictor_id = in.active_predictor_id;
  }
  if (cmd.set_mask & SETTING_HORIZON)
  {
    next.horizon_ns = in.horizon_ns;
  }
  if (cmd.set_mask & SETTING_METRICS_HZ)
  {
    next.metrics_hz = in.metrics_hz;
  }
  if (cmd.set_mask & SETTING_SPECTRUM_HZ)
  {
    next.spectrum_hz = in.spectrum_hz;
  }

  if (next.active_predictor_id >= PREDICTOR_COUNT || !(next.enabled_mask & (UINT32_C(1) << next.active_predictor_id)))
  {
    reply.error = ERR_INVALID_ARGUMENT;
    std::snprintf(reply.msg, sizeof(reply.msg), "active_predictor_id must be one of enabled_predictor_ids.");
    return reply;
  }

  if (std::memcmp(&next, &settings_, sizeof(next)) != 0)
  {
    settings_ = next;
    cfg_gen_++;
    status_dirty_ = true;
  }
  return reply;
}

void Orchestrator::hat_failed(int64_t now_ns)
{
  (void)now_ns;
  if (hat_failed_)
  {
    return;
  }
  hat_failed_ = true;
  // Nothing can run or calibrate without a sensor
  if (state_ == APP_STATE_RUNNING || state_ == APP_STATE_CALIBRATING)
  {
    set_state(APP_STATE_STOPPED);
  }
  status_dirty_ = true;
}

bool Orchestrator::take_status(Status_Message& out, int64_t now_ns)
{
  const int64_t period = state_ == APP_STATE_CALIBRATING ? cfg_.calibrating_status_ns : cfg_.heartbeat_ns;
  if (!status_dirty_ && now_ns - last_status_ns_ < period)
  {
    return false;
  }
  status_dirty_ = false;
  last_status_ns_ = now_ns;

  std::memset(&out, 0, sizeof(out));
  out.state = state_;
  if (state_ == APP_STATE_CALIBRATING && step_ < n_steps_)
  {
    const Step& s = steps_[step_];
    double p = s.duration_ns > 0 ? (double)(now_ns - step_started_ns_) / (double)s.duration_ns : 1.0;
    out.calibration_step = s.id;
    out.calibration_progress = p < 0.0 ? 0.0 : (p > 1.0 ? 1.0 : p);
  }
  out.calibration_age_s = calibrated_ ? (double)(now_ns - calibrated_at_ns_) / 1e9 : NAN;
  out.sample_rate_hz = sample_rate_hz_;
  out.horizon_ns = settings_.horizon_ns;
  out.metrics_hz = settings_.metrics_hz;
  out.spectrum_hz = settings_.spectrum_hz;
  out.enabled_mask = settings_.enabled_mask;
  out.active_predictor_id = settings_.active_predictor_id;
  return true;
}

Pipeline_Control Orchestrator::control() const
{
  Pipeline_Control c;
  std::memset(&c, 0, sizeof(c));
  c.running = state_ == APP_STATE_RUNNING ? 1 : 0;
  c.cfg_gen = cfg_gen_;
  c.settings = settings_;
  return c;
}

uint32_t Orchestrator::state() const
{
  return state_;
}

} // namespace mts
