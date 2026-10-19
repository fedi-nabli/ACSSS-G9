/* ===================================================================================================
 * utils/include/utils/ring_buffer/ring_buffer.h
 *
 * Bounded single-producer / single-consumer queue of fixed-size elements.
 *
 * Threads:
 *  - rb_push:    ONE producer thread only
 *  - rb_pop:     ONE consumer thread only
 *  - rb_count, rb_dropped, rb_capacity: any thread
 *  - rb_destroy: only once both sides have stopped using it
 *
 * Overflow: the producer never blocks. When the queue is full the NEW element
 * is dropped, rb_push returns false and the dropped counter increments. The
 * queue knows nothing about what the elements mean (no GAP flag here): a
 * caller that cares compares rb_dropped() between its pushes.
 *
 * Memory ordering contract (C11 <stdatomic.h>):
 *  - head (next slot to write) is written only by the producer, tail (next
 *    slot to read) only by the consumer. Both only ever increase, the slot is
 *    index & (capacity - 1). Empty: head == tail. Full: head - tail == capacity.
 *  - push: copy into the slot, then release-store head. pop: acquire-load
 *    head, copy out of the slot, then release-store tail. push acquire-loads
 *    tail before reusing a slot.
 *  - So an element is fully written before the consumer can see it, and fully
 *    read before the producer can overwrite it. The slots themselves are plain
 *    memory, ordered by those two release/acquire pairs.
 *  - head, tail and the read-only fields sit on separate 128-byte blocks
 *    (64-byte lines on the Pi's Cortex-A, 128 on Apple M), so the producer and
 *    consumer never write to the same cache line. *
 * Authors: Fedi Nabli
 *
 * Date: 19 Oct 2026
 * Last Modified: 19 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_UTILS_RING_BUFFER_H
#define MTS_UTILS_RING_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Ring_Buffer Ring_Buffer;

Ring_Buffer* rb_create(size_t capacity, size_t elem_size);
void rb_destroy(Ring_Buffer* rb);

bool rb_push(Ring_Buffer* rb, const void* elem);
bool rb_pop(Ring_Buffer* rb, void* out, int64_t timeout_ns);

size_t rb_capacity(const Ring_Buffer* rb);
size_t rb_count(const Ring_Buffer* rb);
uint64_t rb_dropped(const Ring_Buffer* rb);

#ifdef __cplusplus
}
#endif

#endif

