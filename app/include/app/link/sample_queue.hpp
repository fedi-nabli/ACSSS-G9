/* ===================================================================================================
 * app/include/app/link/sample_queue.hpp
 *
 * Sample_Queue: Raw_Samples from the Hat_Worker (the one producer) to
 * Processing (the one consumer), with the gap rule on top of Queue:
 *
 *   a sample that cannot be pushed is dropped (drop-newest), and the next
 *   sample that does get in carries SAMPLE_GAP
 *
 * so Processing learns about lost samples from the flag alone, whether the
 * Hat lost them (it sets SAMPLE_GAP itself, kept as is) or this queue did.
 * Several drops in a row give one flagged sample.
 *
 * Sizing: the queue absorbs jitter, it is not storage. With drop-newest a
 * full queue is a stale backlog that Processing has to chew through, and
 * the delay shows in pipeline_delay_ms. About 0.25 s of samples is plenty.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_APP_LINK_SAMPLE_QUEUE_HPP
#define MTS_APP_LINK_SAMPLE_QUEUE_HPP

#include <core/types/raw_sample.h>
#include <utils/ring_buffer/queue.hpp>

#include <cstddef>
#include <cstdint>

namespace mts
{

class Sample_Queue
{
public:
  // capacity is rounded up to a power of two
  explicit Sample_Queue(size_t capacity)
    : q_(capacity)
    , gap_pending_(false)
  {
  }

  Sample_Queue(const Sample_Queue&) = delete;
  Sample_Queue& operator=(const Sample_Queue&) = delete;

  // false if the queue could not be created
  bool valid() const
  {
    return q_.valid();
  }

  /* Producer only. false: queue full, s dropped, and the next sample that
   * gets in carries SAMPLE_GAP. */
  bool push(const Raw_Sample& s)
  {
    Raw_Sample out = s;
    if (gap_pending_)
    {
      out.flags |= SAMPLE_GAP;
    }
    if (!q_.try_push(out))
    {
      gap_pending_ = true;
      return false;
    }
    gap_pending_ = false;
    return true;
  }

  // Consumer only. timeout_ns <= 0 polls, > 0 waits up to timeout_ns
  bool pop(Raw_Sample& out, int64_t timeout_ns = 0)
  {
    return q_.pop(out, timeout_ns);
  }

  // Any thread: samples dropped here since creation (status drops.hat_to_processing)
  uint64_t dropped() const
  {
    return q_.dropped();
  }

  size_t capacity() const
  {
    return q_.capacity();
  }

private:
  Queue<Raw_Sample> q_;
  bool gap_pending_; // producer only: a plain bool, the consumer never reads it
};

} // namespace mts

#endif /* MTS_APP_LINK_SAMPLE_QUEUE_HPP */

