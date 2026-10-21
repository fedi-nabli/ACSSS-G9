/* ===================================================================================================
 * tests/utils/ring_buffer/test_queue.cc
 *
 * Tests for mts::Queue<T>. Thin on purpose: the C tests cover the ring
 * buffer itself, these cover the type, the ownership and the moves.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <utils/time/time.h>
#include <utils/logger/logger.h>
#include <utils/ring_buffer/queue.hpp>

#include <cstdio>
#include <thread>
#include <utility>
#include <cstdint>

#if defined(__SANITIZE_THREAD__)
#define UNDER_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define UNDER_TSAN 1
#endif
#endif

#ifdef UNDER_TSAN
#define TYPED_N 50000
#else
#define TYPED_N 500000
#endif

// Shaped like a sample: mixed types, not a multiple of 8 (Queue does not care)
struct Sample
{
  uint64_t seq;
  float accel[3];
};

MTS_TEST(invalid_when_create_fails)
{
  // Expected error: keep it out of the test output
  FILE* sink = tmpfile();
  mts_log_set_stream(sink);
  mts::Queue<int> q(0);
  mts_log_set_stream(nullptr);
  fclose(sink);

  MTS_ASSERT(!q.valid());
}

MTS_TEST(typed_push_pop)
{
  mts::Queue<Sample> q(3);
  MTS_ASSERT(q.valid());
  MTS_ASSERT_EQ(q.capacity(), 4);

  for (uint64_t i = 0; i < 3; i++)
  {
    Sample s{ i, { 1.0f * i, 2.0f * i, 3.0f * i } };
    MTS_ASSERT(q.try_push(s));
  }
  MTS_ASSERT_EQ(q.count(), 3);

  for (uint64_t i = 0; i < 3; i++)
  {
    Sample s{};
    MTS_ASSERT(q.pop(s));
    MTS_ASSERT_EQ(s.seq, i);
    MTS_ASSERT_NEAR(s.accel[2], 3.0 * i, 0.0);
  }

  Sample none{};
  MTS_ASSERT(!q.pop(none));
}

MTS_TEST(full_drops_newest)
{
  mts::Queue<int> q(2);
  MTS_ASSERT(q.valid());

  MTS_ASSERT(q.try_push(1));
  MTS_ASSERT(q.try_push(2));
  MTS_ASSERT(!q.try_push(3));
  MTS_ASSERT_EQ(q.dropped(), 1);

  int out = 0;
  MTS_ASSERT(q.pop(out));
  MTS_ASSERT_EQ(out, 1);
}

MTS_TEST(pop_timeout)
{
  mts::Queue<int> q(2);
  MTS_ASSERT(q.valid());

  int out = 5;
  int64_t timeout = 10 * MTS_TIME_NS_PER_MS;
  int64_t start = mts_time_now_ns();
  bool popped = q.pop(out, timeout);
  int64_t elapsed = mts_time_now_ns() - start;

  MTS_ASSERT(!popped);
  MTS_ASSERT_EQ(out, 5);
  MTS_ASSERT_GE(elapsed, timeout);
}

MTS_TEST(move_construct_transfers_ownership)
{
  mts::Queue<int> a(4);
  MTS_ASSERT(a.try_push(11));

  mts::Queue<int> b(std::move(a));
  MTS_ASSERT(!a.valid());
  MTS_ASSERT(b.valid());

  int out = 0;
  MTS_ASSERT(b.pop(out));
  MTS_ASSERT_EQ(out, 11);
  // a is destroyed at scope end holding nullptr: rb_destroy(NULL) is a no-op
}

MTS_TEST(move_assign_frees_old_and_takes_new)
{
  mts::Queue<int> a(4);
  mts::Queue<int> b(16);
  MTS_ASSERT(a.try_push(22));

  // b's own 16-slot buffer is freed here (ASan reports it if not)
  b = std::move(a);
  MTS_ASSERT(!a.valid());
  MTS_ASSERT(b.valid());
  MTS_ASSERT_EQ(b.capacity(), 4);

  int out = 0;
  MTS_ASSERT(b.pop(out));
  MTS_ASSERT_EQ(out, 22);
}

MTS_TEST(two_threads_typed)
{
  mts::Queue<Sample> q(64);
  MTS_ASSERT(q.valid());

  std::thread producer([&q] {
    for (uint64_t i = 0; i < TYPED_N; i++)
    {
      Sample s{ i, { 0.5f, -0.5f, 9.81f } };
      while (!q.try_push(s))
      {
        std::this_thread::yield();
      }
    }
  });

  uint64_t expected = 0;
  bool ordered = true;
  while (expected < TYPED_N)
  {
    Sample s{};
    if (!q.pop(s, MTS_TIME_NS_PER_S))
    {
      break;
    }
    ordered = ordered && s.seq == expected && s.accel[2] == 9.81f;
    expected++;
  }

  producer.join();

  MTS_ASSERT(ordered);
  MTS_ASSERT_EQ(expected, TYPED_N);
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(invalid_when_create_fails),
  MTS_TEST_ENTRY(typed_push_pop),
  MTS_TEST_ENTRY(full_drops_newest),
  MTS_TEST_ENTRY(pop_timeout),
  MTS_TEST_ENTRY(move_construct_transfers_ownership),
  MTS_TEST_ENTRY(move_assign_frees_old_and_takes_new),
  MTS_TEST_ENTRY(two_threads_typed),
};

MTS_TEST_MAIN(tests)
