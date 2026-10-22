/* ===================================================================================================
 * app/include/app/processing/processing.hpp
 *
 * The Processing thread: samples in, frames out.
 *
 *   until stopping:
 *     next Raw_Sample (waits for it)
 *     latest Pipeline_Control (applies from this sample on)
 *     running -> one Output_Frame to the server
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
#include <core/types/raw_sample.h>
#include <utils/mailbox/mailbox.hpp>
#include <utils/ring_buffer/queue.hpp>
#include <utils/time/time.h>

#include <atomic>
#include <cstdint>
#include <thread>

namespace mts
{

// The longest the thread waits for a sample before checking whether to stop
constexpr int64_t PROCESSING_POLL_NS = 50 * MTS_TIME_NS_PER_MS;

/* The slice 1 frame for sample s under settings, at now_ns. Pure: the
 * thread below is this plus the plumbing. */
void processing_pass_through(const Raw_Sample& s, const Runtime_Settings& settings, int64_t now_ns, Output_Frame& out);

class Processing
{
public:
  /* Consumer of samples and control, producer of frames and led. All four
   * must outlive the Processing. */
  Processing(Sample_Queue& samples, Mailbox<Pipeline_Control>& control, Queue<Server_Message>& frames,
             Mailbox<Led_Command>& led);

  // Calls stop()
  ~Processing();

  Processing(const Processing&) = delete;
  Processing& operator=(const Processing&) = delete;

  // false (logged) if already started
  bool start();

  // Flag + join: within PROCESSING_POLL_NS. Safe to call twice
  void stop();

private:
  void run();

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
