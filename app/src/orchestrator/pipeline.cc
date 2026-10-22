/* ===================================================================================================
 * app/src/orchestrator/pipeline.cc
 *
 * Pipeline, see pipeline.hpp
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <app/orchestrator/pipeline.hpp>

#include <app/processing/processing.hpp>
#include <utils/logger/logger.h>
#include <utils/time/time.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace mts
{

namespace
{

size_t sample_queue_capacity(const Pipeline_Config& cfg)
{
  const double n = std::ceil(cfg.hat.sample_rate_hz * cfg.sample_queue_s);
  return n < 8.0 ? 8 : (size_t)n;
}

} // namespace

Pipeline::Pipeline(const Pipeline_Config& cfg)
  : cfg_(cfg)
  , samples_(sample_queue_capacity(cfg))
  , frames_(cfg.frame_queue)
  , status_(cfg.status_queue)
  , commands_(cfg.command_queue)
  , replies_(cfg.reply_queue)
  , control_gen_(0)
  , hat_failure_reported_(false)
  , port_(0)
{
}

Pipeline::~Pipeline()
{
  stop();
}

bool Pipeline::start()
{
  if (orch_)
  {
    MTS_LOG_ERROR("pipeline: already started");
    return false;
  }
  if (!samples_.valid() || !frames_.valid() || !status_.valid() || !commands_.valid() || !replies_.valid()
      || !control_.valid() || !led_.valid())
  {
    MTS_LOG_ERROR("pipeline: could not allocate the queues");
    return false;
  }

  std::unique_ptr<Hat> hat = hat_open(cfg_.hat);
  if (!hat)
  {
    return false; // hat_open logged why
  }
  const Hat_Info info = hat->info();

  orch_ = std::make_unique<Orchestrator>(cfg_.orchestrator, info.rate_hz, mts_time_now_ns());

  // Processing must know what to do before its first sample
  const Pipeline_Control ctl = orch_->control();
  control_.write(ctl);
  control_gen_ = ctl.cfg_gen;

  server_ = std::make_unique<Server>(cfg_.server, info.opened_t_ns, frames_, status_, commands_, replies_);
  processing_ = std::make_unique<Processing>(samples_, control_, frames_, led_);
  worker_ = std::make_unique<Hat_Worker>(std::move(hat), samples_, led_, cfg_.worker);

  if (!server_->start() || !processing_->start() || !worker_->start())
  {
    stop();
    return false;
  }
  port_ = server_->port();

  // Everything is up: boot is over (needs_calibration, or ready once a
  // calibration can be stored). No command is read before this
  orch_->tick(mts_time_now_ns());
  return true;
}

void Pipeline::step(int64_t max_wait_ns)
{
  if (!orch_)
  {
    return;
  }

  Command cmd;
  bool got = commands_.pop(cmd, max_wait_ns);
  while (got)
  {
    push_reply(orch_->handle(cmd, mts_time_now_ns()));
    got = commands_.pop(cmd);
  }

  const int64_t now = mts_time_now_ns();
  orch_->tick(now);

  if (!hat_failure_reported_ && worker_->failed())
  {
    hat_failure_reported_ = true;
    orch_->hat_failed(now);

    // Answers no command: to every client
    Command_Reply r;
    std::memset(&r, 0, sizeof(r));
    r.error = ERR_HAT_ERROR;
    r.client = 0;
    std::snprintf(r.msg, sizeof(r.msg), "The sensor stopped responding.");
    push_reply(r);
  }

  const Pipeline_Control ctl = orch_->control();
  if (ctl.cfg_gen != control_gen_)
  {
    control_.write(ctl);
    control_gen_ = ctl.cfg_gen;
  }

  Status_Message st;
  if (orch_->take_status(st, now))
  {
    st.drops_hat_to_processing = samples_.dropped();
    st.drops_processing_to_prediction = 0; // no prediction thread yet
    // drops_processing_to_server: filled by the server, it owns that queue's far end
    if (!status_.try_push(st))
    {
      MTS_LOG_WARN("pipeline: status queue full, status dropped");
    }
  }
}

void Pipeline::push_reply(const Command_Reply& reply)
{
  if (!replies_.try_push(reply))
  {
    MTS_LOG_WARN("pipeline: reply queue full, reply to command %u dropped", (unsigned)reply.id);
  }
}

void Pipeline::stop()
{
  // Producer first, so nothing pushes into a queue whose consumer is gone
  if (worker_)
  {
    worker_->stop();
  }
  if (processing_)
  {
    processing_->stop();
  }
  if (server_)
  {
    server_->stop();
  }
}

uint16_t Pipeline::port() const
{
  return port_;
}

} // namespace mts
