/* ===================================================================================================
 * utils/src/ring_buffer/ring_buffer.c
 *
 * SPSC ring buffer
 *
 * Authors: Fedi Nabli
 * Date: 19 Oct 2026
 * Last Modified: 19 Oct 2026
 * ===================================================================================================
 */

#include <utils/logger/logger.h>
#include <utils/ring_buffer/ring_buffer.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

_Static_assert(ATOMIC_LLONG_LOCK_FREE == 2, "64-bit atomics are not lock-free on this target");
_Static_assert(sizeof(uint64_t) == sizeof(long long), "uint64_t is not long long sized");

#define RB_CACHE_LINE 128

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
  // Keeps slot 0 off the consumer's line
  char pad2_[RB_CACHE_LINE];
};

static unsigned char* slot_at(const Ring_Buffer* rb, uint64_t index)
{
  return rb->slots_ + (size_t)(index & rb->mask_) * rb->elem_size_;
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

  return rb;
}

void rb_destroy(Ring_Buffer* rb)
{
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

  atomic_store_explicit(&rb->head_, head + 1, memory_order_release);
  return true;
}

bool rb_pop(Ring_Buffer* rb, void* out, int64_t timeout_ns)
{
  // Blocking wait comes later
  (void)timeout_ns;

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

size_t rb_capacity(const Ring_Buffer* rb)
{
  return rb->capacity_;
}

size_t rb_count(const Ring_Buffer* rb)
{
  uint64_t tail = atomic_load_explicit(&rb->tail_, memory_order_acquire);
  uint64_t head = atomic_load_explicit(&rb->head_, memory_order_acquire);

  int count = (int64_t)(head - tail);
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

