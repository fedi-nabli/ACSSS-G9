/* ===================================================================================================
 * app/src/processing/processing.cc
 *
 * Processing_Core and Processing, see processing.hpp
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <app/processing/processing.hpp>

#include <core/detect/detect.h>
#include <core/fusion/fusion.h>
#include <core/separation/separation.h>
#include <utils/logger/logger.h>
#include <utils/time/time.h>

#include <cmath>
#include <cstring>

namespace mts
{

/* ---------------------------------------------------------------------------------------------------
 * Processing_Core
 * ------------------------------------------------------------------------------------------------- */

Processing_Core::Processing_Core(double rate_hz)
  : rate_hz_(rate_hz)
  , cal_{}
  , fusion_(nullptr)
  , separation_(nullptr)
  , detect_(nullptr)
  , cfg_gen_(0)
  , horizon_(0)
  , active_(PREDICTOR_COUNT)
  , n_slots_(0)
  , slots_(new Slot[P_MAX])
  , since_reset_(0)
{
  calibration_identity(&cal_);
  const Fusion_Config fc = fusion_config_default(rate_hz);
  const Separation_Config sc = separation_config_default(rate_hz);
  const Detect_Config dc = detect_config_default(rate_hz);
  fusion_ = fusion_create(&fc);
  separation_ = separation_create(&sc);
  detect_ = detect_create(&dc);
}

Processing_Core::~Processing_Core()
{
  fusion_destroy(fusion_);
  separation_destroy(separation_);
  detect_destroy(detect_);
}

bool Processing_Core::valid() const
{
  return fusion_ && separation_ && detect_;
}

uint32_t Processing_Core::horizon_steps() const
{
  return horizon_;
}

void Processing_Core::apply(const Pipeline_Control& ctl)
{
  if (ctl.cfg_gen == cfg_gen_)
  {
    return;
  }
  cfg_gen_ = ctl.cfg_gen;
  const Runtime_Settings& rs = ctl.settings;

  // h: the horizon in whole samples, at least one, at most what a forecast holds
  const double steps = std::llround((double)rs.horizon_ns * rate_hz_ / 1e9);
  horizon_ = steps < 1.0 ? 1 : (steps > K_MAX ? K_MAX : (uint32_t)steps);
  active_ = rs.active_predictor_id;

  // One slot per enabled predictor, in id order (the order the UI gets them in)
  n_slots_ = 0;
  for (Predictor_Id id = 0; id < PREDICTOR_COUNT && n_slots_ < P_MAX; id++)
  {
    if (!(rs.enabled_mask & (UINT32_C(1) << id)))
    {
      continue;
    }
    Slot& slot = slots_[n_slots_++];
    slot.id = id;
    slot.ops = predictor_by_id(id);
    const Predictor_Config pc = predictor_config_default(id, rate_hz_, horizon_);
    for (int a = 0; a < 3 && slot.ops; a++)
    {
      if (!slot.ops->init(slot.state[a], &pc))
      {
        slot.ops = nullptr; // logged by the predictor: the entry stays, all null
      }
    }
  }
  reset_predictors();
}

void Processing_Core::reset_predictors()
{
  for (uint32_t i = 0; i < n_slots_; i++)
  {
    Slot& slot = slots_[i];
    for (int a = 0; a < 3 && slot.ops; a++)
    {
      slot.ops->reset(slot.state[a]);
    }
    std::memset(slot.made_valid, 0, sizeof(slot.made_valid));
  }
  since_reset_ = 0;
}

void Processing_Core::process(const Raw_Sample& raw, int64_t now_ns, Output_Frame& out)
{
  std::memset(&out, 0, sizeof(out));
  out.seq = raw.seq;
  out.t_ns = raw.t_ns;

  // Stage 1
  Raw_Sample s;
  calibration_apply(&cal_, &raw, &s);
  Fusion_Output fo;
  fusion_update(fusion_, &s, &fo);
  Separation_Output so;
  separation_update(separation_, &fo.lin_acc_body, &so);
  Detect_Output d;
  detect_update(detect_, &so, &d);

  if (raw.flags & SAMPLE_GAP)
  {
    out.flags |= FRAME_GAP_BEFORE;
    reset_predictors(); // "h samples ago" would not be h periods ago any more
  }
  if (d.active)
  {
    out.flags |= FRAME_TREMOR_ACTIVE;
  }
  out.tremor_disp = so.tremor_disp;
  out.tremor_acc = so.tremor_acc;
  out.orientation = fo.q;
  out.tremor_hz = d.hz;
  out.tremor_amp = d.amp;
  out.confidence = d.conf;

  // Predictors: update with this sample, then match forecasts to the present
  const uint32_t ring = K_MAX + 1;
  const uint32_t now_slot = (uint32_t)(since_reset_ % ring);
  const bool old_enough = since_reset_ >= horizon_;
  const uint32_t then_slot = old_enough ? (uint32_t)((since_reset_ - horizon_) % ring) : 0;
  int64_t predictor_delay = 0;

  for (uint32_t i = 0; i < n_slots_; i++)
  {
    Slot& slot = slots_[i];
    Frame_Prediction& p = out.predictions[i];
    p.predictor_id = slot.id;
    if (!slot.ops)
    {
      continue; // flags 0: forecast and residual null
    }

    // The forecast made h samples ago for this instant
    if (old_enough && slot.made_valid[then_slot])
    {
      p.flags |= PRED_FORECAST_VALID;
      for (int a = 0; a < 3; a++)
      {
        p.forecast.v[a] = slot.made[then_slot][a];
        p.residual.v[a] = so.tremor_disp.v[a] - p.forecast.v[a];
      }
      if (d.active)
      {
        p.flags |= PRED_RESIDUAL_VALID;
      }
    }

    /* This sample in, the forecast for h samples ahead out (into the slot just read: h <= K_MAX < ring).
     * An axis whose predictor has nothing to say (a WFLC that never locks
     * on an axis without tremor) forecasts 0: no motion to cancel there.
     * The forecast exists once at least one axis has one */
    bool made = false;
    for (int a = 0; a < 3; a++)
    {
      double ahead[K_MAX];
      slot.ops->update(slot.state[a], so.tremor_disp.v[a]);
      const size_t k = slot.ops->predict(slot.state[a], ahead, horizon_);
      const bool axis = k >= horizon_;
      slot.made[now_slot][a] = axis ? ahead[horizon_ - 1] : 0.0;
      made = made || axis;
    }
    slot.made_valid[now_slot] = made;

    if (slot.id == active_)
    {
      predictor_delay = slot.ops->delay_ns(slot.state[0]);
      if ((p.flags & PRED_FORECAST_VALID) && d.active)
      {
        out.flags |= FRAME_COUNTER_VALID;
        for (int a = 0; a < 3; a++)
        {
          out.counter.v[a] = -p.forecast.v[a];
        }
      }
    }
  }
  out.n_predictions = n_slots_;
  since_reset_++;

  // Causal delay of everything: the filters' group delay, the predictor's, and the time spent getting here
  out.pipeline_delay_ns = fo.delay_ns + so.delay_ns + predictor_delay + (now_ns - raw.t_ns);
}

/* ---------------------------------------------------------------------------------------------------
 * Processing (the thread)
 * ------------------------------------------------------------------------------------------------- */

Processing::Processing(double rate_hz, Sample_Queue& samples, Mailbox<Pipeline_Control>& control,
                       Queue<Server_Message>& frames, Mailbox<Led_Command>& led)
  : core_(rate_hz)
  , samples_(samples)
  , control_(control)
  , frames_(frames)
  , led_(led)
  , msg_{}
  , stopping_(false)
{
}

Processing::~Processing()
{
  stop();
}

bool Processing::start()
{
  if (!core_.valid())
  {
    MTS_LOG_ERROR("processing: stage 1 could not be built");
    return false;
  }
  if (thread_.joinable())
  {
    MTS_LOG_ERROR("processing: already started");
    return false;
  }
  thread_ = std::thread(&Processing::run, this);
  return true;
}

void Processing::stop()
{
  if (!thread_.joinable())
  {
    return;
  }
  stopping_.store(true, std::memory_order_relaxed);
  thread_.join();
}

void Processing::run()
{
  Led_Command off;
  std::memset(&off, 0, sizeof(off));
  off.mode = LED_MODE_OFF;
  led_.write(off);

  // Not running until told otherwise: the orchestrator writes control before starting us
  Pipeline_Control ctl;
  std::memset(&ctl, 0, sizeof(ctl));

  while (!stopping_.load(std::memory_order_relaxed))
  {
    Raw_Sample s;
    if (!samples_.pop(s, PROCESSING_POLL_NS))
    {
      continue;
    }
    if (control_.read(ctl)) // false: nothing new, the last one stays
    {
      core_.apply(ctl);
    }

    msg_.kind = SERVER_MSG_FRAME;
    core_.process(s, mts_time_now_ns(), msg_.frame);
    if (ctl.running)
    {
      frames_.try_push(msg_); // full: dropped, counted by the queue (drops.processing_to_server)
    }
  }
}

} // namespace mts
