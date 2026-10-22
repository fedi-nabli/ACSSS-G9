/* ===================================================================================================
 * app/src/hat/hat_worker.cc
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <app/hat/hat_worker.hpp>

#include <utils/logger/logger.h>

#include <cmath>
#include <utility>

namespace mts
{

Hat_Worker::Hat_Worker(std::unique_ptr<Hat> hat, Sample_Queue& samples, Mailbox<Led_Command>& led,
                       const Hat_Worker_Config& cfg)
  : hat_(std::move(hat))
  , samples_(samples)
  , led_(led)
  , cfg_(cfg)
  , period_ns_(0)
  , stopping_(false)
  , failed_(false)
  , errors_(0)
{
  if (hat_)
  {
    period_ns_ = std::llround(1e9 / hat_->info().rate_hz);
  }
}

Hat_Worker::~Hat_Worker()
{
  stop();
  // hat_ is destroyed after this body: the thread is joined by then
}

bool Hat_Worker::start()
{
  if (!hat_)
  {
    MTS_LOG_ERROR("hat worker: no Hat");
    return false;
  }
  if (thread_.joinable())
  {
    MTS_LOG_ERROR("hat worker: already started");
    return false;
  }
  thread_ = std::thread(&Hat_Worker::run, this);
  return true;
}

void Hat_Worker::stop()
{
  if (!thread_.joinable())
  {
    return;
  }
  stopping_.store(true, std::memory_order_relaxed);
  thread_.join();
}

bool Hat_Worker::failed() const
{
  return failed_.load(std::memory_order_acquire);
}

uint64_t Hat_Worker::errors() const
{
  return errors_.load(std::memory_order_relaxed);
}

void Hat_Worker::run()
{
  bool gap_pending = false; // a read failed: the next good sample says so
  int64_t failing_since_ns = 0; // start of the current error streak, 0 = none
  Led_Command led{};
  bool have_led = false;

  while (!stopping_.load(std::memory_order_relaxed))
  {
    Raw_Sample s;
    if (hat_->read_sample(s) == Hat_Status::ok)
    {
      if (gap_pending)
      {
        s.flags |= SAMPLE_GAP;
        gap_pending = false;
      }
      if (failing_since_ns != 0)
      {
        MTS_LOG_INFO("hat worker: Hat back after %.3f s of errors",
                     (double)(mts_time_now_ns() - failing_since_ns) / 1e9);
        failing_since_ns = 0;
      }
      samples_.push(s); // full: dropped and flagged by the queue, counted in dropped()
    }
    else
    {
      errors_.fetch_add(1, std::memory_order_relaxed);
      gap_pending = true;
      const int64_t now = mts_time_now_ns();
      if (failing_since_ns == 0)
      {
        failing_since_ns = now;
        MTS_LOG_WARN("hat worker: read error, retrying");
      }
      else if (now - failing_since_ns >= cfg_.fail_after_ns)
      {
        MTS_LOG_ERROR("hat worker: no sample for %.3f s, giving up on the Hat",
                      (double)(now - failing_since_ns) / 1e9);
        failed_.store(true, std::memory_order_release);
        return;
      }
      // An error may come back instantly: do not spin on it
      mts_time_sleep_ns(period_ns_);
    }

    // The Hat wants show() at sample rate (it animates and rate-limits
    // itself), so the latest command is shown every turn, not only when new
    if (led_.read(led))
    {
      have_led = true;
    }
    if (have_led)
    {
      hat_->show(led);
    }
  }
}

} // namespace mts
