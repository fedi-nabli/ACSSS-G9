/* ===================================================================================================
 * utils/src/ring_buffer/ring_buffer.c
 *
 * SPSC ring buffer
 *
 * Authors: Fedi Nabli
 * Date: 19 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#include <utils/time/time.h>
#include <utils/logger/logger.h>
#include <utils/ring_buffer/ring_buffer.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>

_Static_assert(ATOMIC_LLONG_LOCK_FREE == 2, "64-bit atomics are not lock-free on this target");
_Static_assert(sizeof(uint64_t) == sizeof(long long), "uint64_t is not long long sized");

#define RB_CACHE_LINE 128

/* Longer timeouts are cut to this: keeps now + timeout far from overflow and
 * the deadline's tv_sec inside a 32-bit time_t */
#define RB_MAX_TIMEOUT_NS  (INT64_C(24) * 3600 * MTS_TIME_NS_PER_S)

struct Ring_Buffer
{
  // Read only after rb_create
  unsigned char* slots_;
  size_t capacity_;
  size_t mask_;
  size_t elem_size_;
  char pad0_[RB_CACHE_LINE];

  // Written by the producer only
  _Atomic uint64_t head_;
  _Atomic uint64_t dropped_;
  char pad1_[RB_CACHE_LINE];

  // Written by the consumer only
  _Atomic uint64_t tail_;
  _Atomic int sleeping_;
  char pad2_[RB_CACHE_LINE];

  // Used by both only while the consumer sleeps
  pthread_mutex_t mutex_;
  pthread_cond_t cond_;
  // Keeps slot 0 off the consumer's line
  char pad3_[RB_CACHE_LINE];
};

static unsigned char* slot_at(const Ring_Buffer* rb, uint64_t index)
{
  return rb->slots_ + (size_t)(index & rb->mask_) * rb->elem_size_;
}

/* Mutex + condvar. On Linux the condvar measures its deadline on
 * CLOCK_MONOTONIC. macOS has no pthread_condattr_setclock: rb_pop waits with
 * relative timeouts there instead. */
static bool init_sync(Ring_Buffer* rb)
{
  if (pthread_mutex_init(&rb->mutex_, NULL) != 0)
  {
    return false;
  }

#if defined(__APPLE__)
  int rc = pthread_cond_init(&rb->cond_, NULL);
#else
  pthread_condattr_t attr;
  int rc = pthread_condattr_init(&attr);
  if (rc == 0)
  {
    rc = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (rc == 0)
    {
      rc = pthread_cond_init(&rb->cond_, &attr);
    }
    pthread_condattr_destroy(&attr);
  }
#endif

  if (rc != 0)
  {
    pthread_mutex_destroy(&rb->mutex_);
    return false;
  }

  return true;
}

static bool rb_try_pop(Ring_Buffer* rb, void* out)
{
  uint64_t tail = atomic_load_explicit(&rb->tail_, memory_order_relaxed);
  uint64_t head = atomic_load_explicit(&rb->head_, memory_order_acquire);

  if (head == tail)
  {
    return false;
  }

  memcpy(out, slot_at(rb, tail), rb->elem_size_);

  atomic_store_explicit(&rb->tail_, tail + 1, memory_order_release);
  return true;
}

static void wait_once(Ring_Buffer* rb, int64_t deadline_ns, int64_t now_ns)
{
#if defined(__APPLE__)
  struct timespec rel = mts_time_to_timespec(deadline_ns - now_ns);
  pthread_cond_timedwait_relative_np(&rb->cond_, &rb->mutex_, &rel);
#else
  (void)now_ns;
  struct timespec abs = mts_time_to_timespec(deadline_ns);
  pthread_cond_timedwait(&rb->cond_, &rb->mutex_, &abs);
#endif
}

// Sleep until queue is non empty
static void wait_for_data(Ring_Buffer* rb, int64_t deadline_ns)
{
  pthread_mutex_lock(&rb->mutex_);

  // Announce the sleep before rechecking head
  atomic_store_explicit(&rb->sleeping_, 1, memory_order_seq_cst);

  for (;;)
  {
    uint64_t head = atomic_load_explicit(&rb->head_, memory_order_seq_cst);
    uint64_t tail = atomic_load_explicit(&rb->tail_, memory_order_relaxed);
    if (head != tail)
    {
      break;
    }

    int64_t now = mts_time_now_ns();
    if (now >= deadline_ns)
    {
      break;
    }

    // Signal timeout or spurious wakeup
    wait_once(rb, deadline_ns, now);
  }

  atomic_store_explicit(&rb->sleeping_, 0, memory_order_relaxed);
  pthread_mutex_unlock(&rb->mutex_);
}

Ring_Buffer* rb_create(size_t capacity, size_t elem_size)
{
  if (capacity == 0 || elem_size == 0)
  {
    MTS_LOG_ERROR("rb_create: capacity (%zu) and elem_size (%zu) must be > 0", capacity, elem_size);
    return NULL;
  }

  // Round up to power of 2
  if (capacity > (SIZE_MAX >> 1) + 1)
  {
    MTS_LOG_ERROR("rb_create: capacity %zu too large", capacity);
    return NULL;
  }

  size_t rounded = 1;
  while (rounded < capacity)
  {
    rounded <<= 1;
  }

  if (rounded > (SIZE_MAX - sizeof(Ring_Buffer)) / elem_size)
  {
    MTS_LOG_ERROR("rb_create: %zu x %zu bytes overflow", rounded, elem_size);
    return NULL;
  }

  Ring_Buffer* rb = (Ring_Buffer*)malloc(sizeof(Ring_Buffer) + rounded * elem_size);
  if (!rb)
  {
    MTS_LOG_ERROR("rb_create: out of memory (%zu x %zu bytes)", rounded, elem_size);
    return NULL;
  }

  rb->slots_ = (unsigned char*)(rb + 1);
  rb->capacity_ = rounded;
  rb->mask_ = rounded - 1;
  rb->elem_size_ = elem_size;
  atomic_init(&rb->head_, 0);
  atomic_init(&rb->dropped_, 0);
  atomic_init(&rb->tail_, 0);
  atomic_init(&rb->sleeping_, 0);

  if (!init_sync(rb))
  {
    MTS_LOG_ERROR("rb_create: mutex/condvar init failed");
    free(rb);
    return NULL;
  }

  return rb;
}

void rb_destroy(Ring_Buffer* rb)
{
  if (!rb)
  {
    return;
  }

  pthread_cond_destroy(&rb->cond_);
  pthread_mutex_destroy(&rb->mutex_);
  free(rb);
}

bool rb_push(Ring_Buffer* rb, const void* elem)
{
  uint64_t head = atomic_load_explicit(&rb->head_, memory_order_relaxed);
  uint64_t tail = atomic_load_explicit(&rb->tail_, memory_order_acquire);

  if (head - tail >= rb->capacity_)
  {
    atomic_fetch_add_explicit(&rb->dropped_, 1, memory_order_relaxed);
    return false;
  }

  memcpy(slot_at(rb, head), elem, rb->elem_size_);

  atomic_store_explicit(&rb->head_, head + 1, memory_order_seq_cst);

  if (atomic_load_explicit(&rb->sleeping_, memory_order_seq_cst))
  {
    pthread_mutex_lock(&rb->mutex_);
    pthread_cond_signal(&rb->cond_);
    pthread_mutex_unlock(&rb->mutex_);
  }

  return true;
}

bool rb_pop(Ring_Buffer* rb, void* out, int64_t timeout_ns)
{
  if (rb_try_pop(rb, out))
  {
    return true;
  }

  if (timeout_ns <= 0)
  {
    return false;
  }

  if (timeout_ns > RB_MAX_TIMEOUT_NS)
  {
    timeout_ns = RB_MAX_TIMEOUT_NS;
  }

  wait_for_data(rb, mts_time_now_ns() + timeout_ns);
  return rb_try_pop(rb, out);
}

size_t rb_capacity(const Ring_Buffer* rb)
{
  return rb->capacity_;
}

size_t rb_count(const Ring_Buffer* rb)
{
  uint64_t tail = atomic_load_explicit(&rb->tail_, memory_order_acquire);
  uint64_t head = atomic_load_explicit(&rb->head_, memory_order_acquire);

  int64_t count = (int64_t)(head - tail);
  if (count < 0)
  {
    return 0;
  }

  if ((uint64_t)count > rb->capacity_)
  {
    return rb->capacity_;
  }

  return (size_t)count;
}

uint64_t rb_dropped(const Ring_Buffer* rb)
{
  return atomic_load_explicit(&rb->dropped_, memory_order_relaxed);
}

