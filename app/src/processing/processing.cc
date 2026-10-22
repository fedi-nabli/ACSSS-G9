/* ===================================================================================================
 * app/src/processing/processing.cc
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <app/processing/processing.hpp>

#include <app/hat/hat.hpp>
#include <utils/logger/logger.h>
#include <utils/time/time.h>

#include <cstring>

namespace mts
{

void processing_pass_through(const Raw_Sample& s, const Runtime_Settings& settings, int64_t now_ns, Output_Frame& out)
{
  std::memset(&out, 0, sizeof(out));
  out.seq = s.seq;
  out.t_ns = s.t_ns;
  if (s.flags & SAMPLE_GAP)
  {
    out.flags |= FRAME_GAP_BEFORE;
  }

  out.tremor_acc.v[AXIS_X] = s.accel[0];
  out.tremor_acc.v[AXIS_Y] = s.accel[1];
  out.tremor_acc.v[AXIS_Z] = (double)s.accel[2] - STANDARD_GRAVITY_MS2;
  out.orientation.q[QUAT_W] = 1.0;

  // Nothing estimates these yet: null on the wire, not a made-up number
  out.tremor_hz = 0.0;
  out.tremor_amp = 0.0;
  out.confidence = 0.0;
  out.pipeline_delay_ns = now_ns - s.t_ns;

  // The final shape: an entry per enabled predictor, in warm-up (flags 0: both null)
  for (Predictor_Id id = 0; id < PREDICTOR_COUNT && out.n_predictions < P_MAX; id++)
  {
    if (settings.enabled_mask & (UINT32_C(1) << id))
    {
      out.predictions[out.n_predictions].predictor_id = id;
      out.n_predictions++;
    }
  }
}

Processing::Processing(Sample_Queue& samples, Mailbox<Pipeline_Control>& control, Queue<Server_Message>& frames,
                       Mailbox<Led_Command>& led)
  : samples_(samples)
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
    control_.read(ctl); // false: nothing new, the last one stays

    if (!ctl.running)
    {
      continue; // read and dropped: the Hat keeps measuring, nobody wants frames
    }

    msg_.kind = SERVER_MSG_FRAME;
    processing_pass_through(s, ctl.settings, mts_time_now_ns(), msg_.frame);
    frames_.try_push(msg_); // full: dropped, counted by the queue (drops.processing_to_server)
  }
}

} // namespace mts
