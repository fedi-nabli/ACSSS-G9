/* ===================================================================================================
 * tests/utils/ring_buffer/test_ring_buffer.c
 *
 * Tests for the SPSC ring buffer (non-blocking part). The stress tests only
 * prove something under ThreadSanitizer: run ./build.sh --tsan.
 *
 * Authors: Fedi Nabli
 * Date: 19 Oct 2026
 * Last Modified: 19 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <utils/logger/logger.h>
#include <utils/ring_buffer/ring_buffer.h>

#include <sched.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>

/* TSan slows everything 5-15x: shorter stress runs under it */
#if defined(__SANITIZE_THREAD__)
#define UNDER_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define UNDER_TSAN 1
#endif
#endif

#ifdef UNDER_TSAN
#define STRESS_N 300000
#else
#define STRESS_N 3000000
#endif

/* ---------------------------------------------------------------------------------------------------
 * Single thread
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(create_rejects_zero)
{
  /* Expected errors: keep them out of the test output */
  FILE *sink = tmpfile();
  mts_log_set_stream(sink);

  Ring_Buffer* a = rb_create(0, 8);
  Ring_Buffer* b = rb_create(8, 0);
  Ring_Buffer* c = rb_create(SIZE_MAX, 8);

  mts_log_set_stream(NULL);
  fclose(sink);

  MTS_ASSERT(a == NULL);
  MTS_ASSERT(b == NULL);
  MTS_ASSERT(c == NULL);
}

MTS_TEST(create_rounds_to_power_of_two)
{
  const size_t requested[] = { 1, 2, 3, 5, 8, 9, 1000 };
  const size_t expected[] = { 1, 2, 4, 8, 8, 16, 1024 };

  for (size_t i = 0; i < sizeof(requested) / sizeof(requested[0]); i++)
  {
    Ring_Buffer* rb = rb_create(requested[i], 4);
    MTS_ASSERT(rb != NULL);
    size_t cap = rb_capacity(rb);
    rb_destroy(rb);
    MTS_ASSERT_EQ(cap, expected[i]);
  }
}

MTS_TEST(destroy_null_is_safe)
{
  rb_destroy(NULL);
}

MTS_TEST(new_queue_is_empty)
{
  Ring_Buffer* rb = rb_create(4, sizeof(int));
  MTS_ASSERT(rb != NULL);

  int out = 1234;
  bool popped = rb_pop(rb, &out, 0);
  size_t count = rb_count(rb);
  uint64_t dropped = rb_dropped(rb);
  rb_destroy(rb);

  MTS_ASSERT(!popped);
  MTS_ASSERT_EQ(out, 1234); /* untouched */
  MTS_ASSERT_EQ(count, 0);
  MTS_ASSERT_EQ(dropped, 0);
}

MTS_TEST(fifo_order)
{
  Ring_Buffer* rb = rb_create(8, sizeof(int));
  MTS_ASSERT(rb != NULL);

  for (int i = 0; i < 5; i++)
    MTS_ASSERT(rb_push(rb, &i));

  int out;
  for (int i = 0; i < 5; i++)
  {
    MTS_ASSERT(rb_pop(rb, &out, 0));
    MTS_ASSERT_EQ(out, i);
  }
  MTS_ASSERT(!rb_pop(rb, &out, 0));
  rb_destroy(rb);
}

MTS_TEST(full_drops_newest)
{
  Ring_Buffer* rb = rb_create(4, sizeof(int));
  MTS_ASSERT(rb != NULL);

  for (int i = 0; i < 4; i++)
    MTS_ASSERT(rb_push(rb, &i));
  MTS_ASSERT_EQ(rb_count(rb), 4);

  /* Full: 100 and 101 are dropped, the queued 0..3 are kept */
  int extra = 100;
  MTS_ASSERT(!rb_push(rb, &extra));
  extra = 101;
  MTS_ASSERT(!rb_push(rb, &extra));
  MTS_ASSERT_EQ(rb_dropped(rb), 2);
  MTS_ASSERT_EQ(rb_count(rb), 4);

  int out;
  for (int i = 0; i < 4; i++)
  {
    MTS_ASSERT(rb_pop(rb, &out, 0));
    MTS_ASSERT_EQ(out, i);
  }
  MTS_ASSERT(!rb_pop(rb, &out, 0));

  /* Room again: pushing works and the counter keeps its value */
  extra = 7;
  MTS_ASSERT(rb_push(rb, &extra));
  MTS_ASSERT_EQ(rb_dropped(rb), 2);
  rb_destroy(rb);
}

MTS_TEST(wraparound_keeps_order)
{
  Ring_Buffer* rb = rb_create(4, sizeof(int));
  MTS_ASSERT(rb != NULL);

  /* 40 elements through 4 slots, the queue holding 0 to 3 at a time */
  int next_in = 0;
  int next_out = 0;
  int out;
  for (int round = 0; round < 10; round++)
  {
    int batch = 1 + round % 4;
    for (int i = 0; i < batch; i++, next_in++)
      MTS_ASSERT(rb_push(rb, &next_in));
    for (int i = 0; i < batch; i++, next_out++)
    {
      MTS_ASSERT(rb_pop(rb, &out, 0));
      MTS_ASSERT_EQ(out, next_out);
    }
  }
  MTS_ASSERT_EQ(rb_dropped(rb), 0);
  rb_destroy(rb);
}

MTS_TEST(count_follows_push_and_pop)
{
  Ring_Buffer* rb = rb_create(8, sizeof(int));
  MTS_ASSERT(rb != NULL);

  int v = 0;
  for (size_t i = 1; i <= 8; i++)
  {
    MTS_ASSERT(rb_push(rb, &v));
    MTS_ASSERT_EQ(rb_count(rb), i);
  }
  for (size_t i = 8; i > 0; i--)
  {
    MTS_ASSERT(rb_pop(rb, &v, 0));
    MTS_ASSERT_EQ(rb_count(rb), i - 1);
  }
  rb_destroy(rb);
}

MTS_TEST(odd_element_sizes)
{
  /* 3 bytes: slots are packed, not aligned */
  Ring_Buffer* rb = rb_create(4, 3);
  MTS_ASSERT(rb != NULL);

  for (unsigned char i = 0; i < 10; i++)
  {
    unsigned char in[3] = { i, (unsigned char)(i + 1), (unsigned char)(i + 2) };
    unsigned char out[3] = { 0 };
    MTS_ASSERT(rb_push(rb, in));
    MTS_ASSERT(rb_pop(rb, out, 0));
    MTS_ASSERT_MEM_EQ(in, out, sizeof(in));
  }
  rb_destroy(rb);

  /* A struct like the real messages */
  typedef struct { uint64_t seq; int64_t t_ns; float v[3]; uint32_t flags; } Msg;
  rb = rb_create(2, sizeof(Msg));
  MTS_ASSERT(rb != NULL);

  Msg in = { 42, -7, { 1.5f, -2.25f, 3.0f }, 0xABCDu };
  Msg out;
  memset(&out, 0, sizeof(out));
  MTS_ASSERT(rb_push(rb, &in));
  MTS_ASSERT(rb_pop(rb, &out, 0));
  MTS_ASSERT_MEM_EQ(&in, &out, sizeof(Msg));
  rb_destroy(rb);
}

/* ---------------------------------------------------------------------------------------------------
 * Two threads. The consumer runs in the test thread (assertions must stay
 * there), the producer in a spawned thread.
 *
 * Each element is 64 bytes with the sequence number in every word: a torn
 * copy (part old, part new element) shows up as words that disagree.
 * ------------------------------------------------------------------------------------------------- */

#define WORDS 8

typedef struct Stress_Elem
{
  uint64_t w[WORDS];
} Stress_Elem;

typedef struct Producer
{
  Ring_Buffer* rb;
  uint64_t n;
  bool retry_when_full; /* true: lossless, spins until there is room */
  atomic_bool done;
} Producer;

static void* produce(void *arg)
{
  Producer* p = arg;
  Stress_Elem e;

  for (uint64_t seq = 0; seq < p->n; seq++)
  {
    for (int i = 0; i < WORDS; i++)
      e.w[i] = seq;

    while (!rb_push(p->rb, &e) && p->retry_when_full)
      sched_yield();
  }

  /* Release: every push above happens before the consumer sees done */
  atomic_store_explicit(&p->done, true, memory_order_release);
  return NULL;
}

static bool elem_is_whole(const Stress_Elem *e)
{
  for (int i = 1; i < WORDS; i++)
    if (e->w[i] != e->w[0])
      return false;
  return true;
}

/* Lossless: the producer retries, so every element must arrive, in order */
MTS_TEST(stress_lossless_in_order)
{
  Producer p = { .rb = rb_create(64, sizeof(Stress_Elem)), .n = STRESS_N, .retry_when_full = true };
  MTS_ASSERT(p.rb != NULL);
  atomic_init(&p.done, false);

  pthread_t thread;
  MTS_ASSERT_EQ(pthread_create(&thread, NULL, produce, &p), 0);

  uint64_t expected = 0;
  bool whole = true;
  bool ordered = true;
  Stress_Elem e;

  for (;;)
  {
    /* Read done BEFORE popping: if it was already true, every push happened
     * before this pop, so an empty pop really means nothing is left */
    bool done = atomic_load_explicit(&p.done, memory_order_acquire);
    if (rb_pop(p.rb, &e, 0))
    {
      whole = whole && elem_is_whole(&e);
      ordered = ordered && e.w[0] == expected;
      expected++;
      continue;
    }
    if (done)
      break;
    sched_yield();
  }

  pthread_join(thread, NULL);
  rb_destroy(p.rb);

  MTS_ASSERT(whole);
  MTS_ASSERT(ordered);
  MTS_ASSERT_EQ(expected, STRESS_N);
}

/* Lossy: the producer never retries. What arrives is strictly increasing,
 * and the holes in the sequence are exactly the dropped elements. */
MTS_TEST(stress_lossy_gaps_match_dropped)
{
  Producer p = { .rb = rb_create(16, sizeof(Stress_Elem)), .n = STRESS_N, .retry_when_full = false };
  MTS_ASSERT(p.rb != NULL);
  atomic_init(&p.done, false);

  pthread_t thread;
  MTS_ASSERT_EQ(pthread_create(&thread, NULL, produce, &p), 0);

  uint64_t received = 0;
  uint64_t gaps = 0;
  uint64_t next = 0; /* sequence number expected if nothing was dropped */
  bool whole = true;
  bool increasing = true;
  Stress_Elem e;

  for (;;)
  {
    bool done = atomic_load_explicit(&p.done, memory_order_acquire);
    if (rb_pop(p.rb, &e, 0))
    {
      whole = whole && elem_is_whole(&e);
      if (e.w[0] < next)
        increasing = false;
      else
        gaps += e.w[0] - next;
      next = e.w[0] + 1;
      received++;
      continue;
    }
    /* done was read BEFORE this empty pop: everything was pushed by then */
    if (done)
      break;
    sched_yield();
  }

  pthread_join(thread, NULL);
  gaps += STRESS_N - next; /* drops after the last element received */
  uint64_t dropped = rb_dropped(p.rb);
  rb_destroy(p.rb);

  MTS_ASSERT(whole);
  MTS_ASSERT(increasing);
  MTS_ASSERT_EQ(received + dropped, STRESS_N);
  MTS_ASSERT_EQ(gaps, dropped);
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(create_rejects_zero),
  MTS_TEST_ENTRY(create_rounds_to_power_of_two),
  MTS_TEST_ENTRY(destroy_null_is_safe),
  MTS_TEST_ENTRY(new_queue_is_empty),
  MTS_TEST_ENTRY(fifo_order),
  MTS_TEST_ENTRY(full_drops_newest),
  MTS_TEST_ENTRY(wraparound_keeps_order),
  MTS_TEST_ENTRY(count_follows_push_and_pop),
  MTS_TEST_ENTRY(odd_element_sizes),
  MTS_TEST_ENTRY(stress_lossless_in_order),
  MTS_TEST_ENTRY(stress_lossy_gaps_match_dropped),
};

MTS_TEST_MAIN(tests)

