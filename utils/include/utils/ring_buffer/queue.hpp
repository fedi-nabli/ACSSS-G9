/* ===================================================================================================
 * utils/include/utils/ring_buffer/queue.hpp
 *
 * Typed C++ wrapper over the SPSC ring buffer: Queue<T> owns one
 * Ring_Buffer of sizeof(T) elements. Same contract as ring_buffer.h (one
 * producer thread, one consumer thread, drop-newest on overflow); this file
 * only adds the type and the ownership.
 *
 * No exceptions: if rb_create fails (already logged) the queue is invalid.
 * Check valid() once after construction. Every other method requires a
 * valid queue.
 *
 * Movable, not copyable: a copy would be two owners of one ring buffer.
 * A moved-from queue is invalid.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_UTILS_RING_BUFFER_QUEUE_HPP
#define MTS_UTILS_RING_BUFFER_QUEUE_HPP

#include <utils/ring_buffer/ring_buffer.h>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <type_traits>

namespace mts
{

template <typename T>
class Queue
{
  static_assert(std::is_trivially_copyable_v<T>, "Queue<T> copies T with memcpy: T must be trivially copyable");

public:
  // capacity is rounded up to a power of two
  explicit Queue(size_t capacity)
    : rb_(rb_create(capacity, sizeof(T)))
  {
  }

  ~Queue()
  {
    rb_destroy(rb_);
  }

  Queue(Queue&& other) noexcept
    : rb_(std::exchange(other.rb_, nullptr))
  {
  }

  Queue& operator=(Queue&& other) noexcept
  {
    if (this != &other)
    {
      rb_destroy(rb_);
      rb_ = std::exchange(other.rb_, nullptr);
    }
    return *this;
  }

  Queue(const Queue&) = delete;
  Queue& operator=(const Queue&) = delete;

  // false if creation failed or the queue was moved from
  bool valid() const
  {
    return rb_ != nullptr;
  }

  // Producer only. false = full, value dropped
  bool try_push(const T& value)
  {
    return rb_push(rb_, &value);
  }

  // Consumer only. timeout_ns <= 0 polls, > 0 waits up to timeout_ns
  bool pop(T& out, int64_t timeout_ns = 0)
  {
    return rb_pop(rb_, &out, timeout_ns);
  }

  size_t capacity() const
  {
    return rb_capacity(rb_);
  }

  size_t count() const
  {
    return rb_count(rb_);
  }

  uint64_t dropped() const
  {
    return rb_dropped(rb_);
  }

private:
  Ring_Buffer* rb_;
};

} // namespace mts

#endif /* MTS_UTILS_RING_BUFFER_QUEUE_HPP */
