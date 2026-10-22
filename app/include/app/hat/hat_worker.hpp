/* ===================================================================================================
 * app/include/app/hat/hat_worker.hpp
 *
 * The Hat thread: the only thread that touches the Hat.
 *
 *   until stopping or failed:
 *     read_sample: ok    -> sample queue (to Processing)
 *                  error -> counted, wait one sample period, try again
 *     latest Led_Command (from Processing) -> hat->show()
 *
 * Takes an Hat that is already open: the orchestrator opens it first,
 * because Hat_Info (opened_t_ns, rate) is needed before any thread starts.
 * The Hat is destroyed with the worker, after the thread is joined.
 *
 * Lost data is always flagged: the first good sample after a read error
 * carries SAMPLE_GAP (the queue does the same for its own drops). A Hat
 * that only fails for fail_after_ns in a row is given up on: failed() turns
 * true and the thread ends. The orchestrator polls failed() and reports
 * hat_error.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_APP_HAT_HAT_WORKER_HPP
#define MTS_APP_HAT_HAT_WORKER_HPP

#include <app/hat/hat.hpp>
#include <app/link/led_command.h>
#include <app/link/sample_queue.hpp>
#include <utils/mailbox/mailbox.hpp>
#include <utils/time/time.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

namespace mts
{

struct Hat_Worker_Config
{
  int64_t fail_after_ns = MTS_TIME_NS_PER_S; // nothing but errors this long: give up
};

class Hat_Worker
{
public:
  /* hat: open, from hat_open(). samples: this worker is its producer.
   * led: this worker is its reader. Both must outlive the worker. */
  Hat_Worker(std::unique_ptr<Hat> hat, Sample_Queue& samples, Mailbox<Led_Command>& led,
             const Hat_Worker_Config& cfg = Hat_Worker_Config{});

  // stop(), then the Hat is destroyed: after the join, as hat.hpp requires
  ~Hat_Worker();

  Hat_Worker(const Hat_Worker&) = delete;
  Hat_Worker& operator=(const Hat_Worker&) = delete;

  // Starts the thread. false (logged) without a Hat or if already started
  bool start();

  // Flag + join: within about one sample period, the longest read_sample blocks. Safe to call twice
  void stop();

  // Any thread: the Hat failed for fail_after_ns in a row and the thread ended
  bool failed() const;

  // Any thread: read errors since start
  uint64_t errors() const;

private:
  void run();

  std::unique_ptr<Hat> hat_;
  Sample_Queue& samples_;
  Mailbox<Led_Command>& led_;
  Hat_Worker_Config cfg_;
  int64_t period_ns_; // one sample period at the Hat's rate

  std::atomic<bool> stopping_;
  std::atomic<bool> failed_;
  std::atomic<uint64_t> errors_;
  std::thread thread_;
};

} // namespace mts

#endif /* MTS_APP_HAT_HAT_WORKER_HPP */
