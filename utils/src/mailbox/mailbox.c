/* ===================================================================================================
 * utils/src/mailbox/mailbox.c
 *
 * Latest-wins mailbox (seqlock over atomic words), see utils/mailbox/mailbox.h
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#include <utils/logger/logger.h>
#include <utils/mailbox/mailbox.h>

#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

_Static_assert(ATOMIC_LLONG_LOCK_FREE == 2, "64-bit atomics are not lock-free on this target");
_Static_assert(sizeof(_Atomic uint64_t) == 8, "_Atomic uint64_t is not 8 bytes");

#define MB_CACHE_LINE 128
#define MB_WORD_BYTES 8

struct Mailbox
{
  // Read only after mb_create
  size_t words_;
  char pad0_[MB_CACHE_LINE];

  // Written by the reader only, plain: no other thread touches it
  uint64_t last_seen_;
  char pad1_[MB_CACHE_LINE];

  // Written by the writer, read by the reader
  _Atomic uint64_t seq_;
  _Atomic uint64_t data_[];
};

Mailbox* mb_create(size_t elem_size)
{
  if (elem_size == 0 || elem_size % MB_WORD_BYTES != 0)
  {
    MTS_LOG_ERROR("mb_create: elem_size (%zu) must be > 0 and a multiple of %d", elem_size, MB_WORD_BYTES);
    return NULL;
  }

  size_t words = elem_size / MB_WORD_BYTES;
  if (words > (SIZE_MAX - sizeof(Mailbox)) / sizeof(_Atomic uint64_t))
  {
    MTS_LOG_ERROR("mb_create: elem_size %zu too large", elem_size);
    return NULL;
  }

  Mailbox* mb = (Mailbox*)malloc(sizeof(Mailbox) + words * sizeof(_Atomic uint64_t));
  if (!mb)
  {
    MTS_LOG_ERROR("mb_create: out of memory (%zu bytes)", elem_size);
    return NULL;
  }

  mb->words_ = words;
  mb->last_seen_ = 0;
  atomic_init(&mb->seq_, 0);
  for (size_t i = 0; i < words; i++)
  {
    atomic_init(&mb->data_[i], 0);
  }

  return mb;
}

void mb_destroy(Mailbox* mb)
{
  free(mb);
}

void mb_write(Mailbox* mb, const void* elem)
{
  const unsigned char* src = (const unsigned char*)elem;

  // Own counter: no other thread writes it
  uint64_t seq = atomic_load_explicit(&mb->seq_, memory_order_relaxed);

  // Odd: write in progress. The release fence keeps this store before every
  // payload store below, so a reader that sees new data also sees the odd seq
  atomic_store_explicit(&mb->seq_, seq + 1, memory_order_relaxed);
  atomic_thread_fence(memory_order_release);

  for (size_t i = 0; i < mb->words_; i++)
  {
    // memcpy: elem has no alignment guarantee
    uint64_t word;
    memcpy(&word, src + i * MB_WORD_BYTES, MB_WORD_BYTES);
    atomic_store_explicit(&mb->data_[i], word, memory_order_relaxed);
  }

  // Even again. Release: every payload store is visible before this one
  atomic_store_explicit(&mb->seq_, seq + 2, memory_order_release);
}

bool mb_read(Mailbox* mb, void* out)
{
  unsigned char* dst = (unsigned char*)out;

  for (;;)
  {
    // Acquire: pairs with the writer's final release, so a stable (even)
    // seq comes with the payload it published
    uint64_t seq_before = atomic_load_explicit(&mb->seq_, memory_order_acquire);
    if (seq_before & 1)
    {
      // Write in progress: let the writer finish (it may have been preempted)
      sched_yield();
      continue;
    }

    // Nothing new since the last read (also covers "never written": 0 == 0)
    uint64_t version = seq_before >> 1;
    if (version == mb->last_seen_)
    {
      return false;
    }

    for (size_t i = 0; i < mb->words_; i++)
    {
      uint64_t word = atomic_load_explicit(&mb->data_[i], memory_order_relaxed);
      memcpy(dst + i * MB_WORD_BYTES, &word, MB_WORD_BYTES);
    }

    // Acquire fence: the payload loads above happen before the reload
    // below, so if a write overlapped them the reload sees its new seq
    atomic_thread_fence(memory_order_acquire);
    uint64_t seq_after = atomic_load_explicit(&mb->seq_, memory_order_relaxed);

    if (seq_after == seq_before)
    {
      mb->last_seen_ = version;
      return true;
    }

    // Torn: a write overlapped the copy. out holds a mix, overwritten next pass
    sched_yield();
  }
}

