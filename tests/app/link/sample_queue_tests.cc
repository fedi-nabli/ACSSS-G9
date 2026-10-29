/* ===================================================================================================
 * tests/app/link/sample_queue_tests.cc
 *
 * Sample_Queue: the gap rule single-threaded, then the invariant it exists
 * for under a real producer and a jittery consumer:
 *
 *   in what Processing pops, SAMPLE_GAP is set exactly when seq jumps
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <app/link/sample_queue.hpp>

#include <atomic>
#include <cstring>
#include <thread>

namespace
{

Raw_Sample sample(uint64_t seq, uint32_t flags = 0)
{
  Raw_Sample s;
  std::memset(&s, 0, sizeof(s));
  s.seq = seq;
  s.t_ns = (int64_t)seq * 10000000;
  s.flags = flags;
  return s;
}

bool has_gap(const Raw_Sample& s)
{
  return (s.flags & SAMPLE_GAP) != 0;
}

// xorshift: cheap jitter that does not lock (rand() may)
uint32_t next_random(uint32_t& state)
{
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

void spin(uint32_t iterations)
{
  for (volatile uint32_t i = 0; i < iterations; i++)
  {
  }
}

} // namespace

MTS_TEST(no_drop_no_gap)
{
  mts::Sample_Queue q(4);
  MTS_ASSERT(q.valid());
  MTS_ASSERT_EQ(q.capacity(), 4);

  for (uint64_t i = 0; i < 4; i++)
  {
    MTS_ASSERT(q.push(sample(i, SAMPLE_MAG_FRESH)));
  }
  Raw_Sample out;
  for (uint64_t i = 0; i < 4; i++)
  {
    MTS_ASSERT(q.pop(out));
    MTS_ASSERT_EQ(out.seq, i);
    MTS_ASSERT_EQ(out.flags, SAMPLE_MAG_FRESH); // untouched
  }
  MTS_ASSERT_EQ(q.dropped(), 0);
}

MTS_TEST(drop_flags_the_next_accepted_sample_only)
{
  mts::Sample_Queue q(2);
  MTS_ASSERT(q.push(sample(0)));
  MTS_ASSERT(q.push(sample(1)));
  MTS_ASSERT(!q.push(sample(2))); // full: dropped
  MTS_ASSERT_EQ(q.dropped(), 1);

  Raw_Sample out;
  MTS_ASSERT(q.pop(out));
  MTS_ASSERT(!has_gap(out)); // already queued before the drop: not affected
  MTS_ASSERT(q.pop(out));
  MTS_ASSERT(!has_gap(out));

  MTS_ASSERT(q.push(sample(3)));
  MTS_ASSERT(q.push(sample(4)));
  MTS_ASSERT(q.pop(out));
  MTS_ASSERT_EQ(out.seq, 3);
  MTS_ASSERT(has_gap(out));
  MTS_ASSERT(q.pop(out));
  MTS_ASSERT_EQ(out.seq, 4);
  MTS_ASSERT(!has_gap(out));
}

MTS_TEST(drops_in_a_row_give_one_gap)
{
  mts::Sample_Queue q(2);
  MTS_ASSERT(q.push(sample(0)));
  MTS_ASSERT(q.push(sample(1)));
  for (uint64_t i = 2; i < 7; i++)
  {
    MTS_ASSERT(!q.push(sample(i)));
  }
  MTS_ASSERT_EQ(q.dropped(), 5);

  Raw_Sample out;
  MTS_ASSERT(q.pop(out));
  MTS_ASSERT(q.pop(out));
  MTS_ASSERT(q.push(sample(7)));
  MTS_ASSERT(q.push(sample(8)));
  MTS_ASSERT(q.pop(out));
  MTS_ASSERT(has_gap(out));
  MTS_ASSERT(q.pop(out));
  MTS_ASSERT(!has_gap(out));
}

MTS_TEST(hat_gap_is_kept)
{
  // The Hat lost samples itself: its flag goes through, and it does not
  // make the queue flag the next one
  mts::Sample_Queue q(4);
  MTS_ASSERT(q.push(sample(5, SAMPLE_GAP | SAMPLE_SATURATED)));
  MTS_ASSERT(q.push(sample(6)));

  Raw_Sample out;
  MTS_ASSERT(q.pop(out));
  MTS_ASSERT_EQ(out.flags, SAMPLE_GAP | SAMPLE_SATURATED);
  MTS_ASSERT(q.pop(out));
  MTS_ASSERT_EQ(out.flags, 0);
}

MTS_TEST(gap_iff_seq_jumps_under_load)
{
  // Small queue, a producer that never waits and a consumer that dawdles at
  // random: plenty of drops, at random places
  const uint64_t n = 200000;
  mts::Sample_Queue q(8);
  std::atomic<bool> done(false);

  std::thread producer([&] {
    uint32_t rng = 0x9e3779b9u;
    for (uint64_t i = 0; i < n; i++)
    {
      q.push(sample(i));
      spin(next_random(rng) % 64);
    }
    done.store(true, std::memory_order_release);
  });

  uint32_t rng = 0x85ebca6bu;
  uint64_t popped = 0;
  uint64_t gaps = 0;
  uint64_t wrong = 0; // assertions stay on this thread: count, check after the join
  int64_t prev = -1;
  for (;;)
  {
    // Read before popping: done, then an empty queue, means nothing is left
    const bool finished = done.load(std::memory_order_acquire);
    Raw_Sample out;
    if (!q.pop(out))
    {
      if (finished)
      {
        break;
      }
      continue;
    }
    popped++;
    const bool jumped = (int64_t)out.seq != prev + 1;
    gaps += has_gap(out) ? 1 : 0;
    wrong += jumped != has_gap(out) ? 1 : 0;
    prev = (int64_t)out.seq;
    spin(next_random(rng) % 128);
  }
  producer.join();

  MTS_ASSERT_EQ(wrong, 0);
  MTS_ASSERT_EQ(popped + q.dropped(), n); // every sample is either delivered or counted
  MTS_ASSERT_GT(gaps, 0); // the test really exercised drops
  MTS_ASSERT_LT(gaps, n / 2); // and really delivered runs between them
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(no_drop_no_gap),
  MTS_TEST_ENTRY(drop_flags_the_next_accepted_sample_only),
  MTS_TEST_ENTRY(drops_in_a_row_give_one_gap),
  MTS_TEST_ENTRY(hat_gap_is_kept),
  MTS_TEST_ENTRY(gap_iff_seq_jumps_under_load),
};

MTS_TEST_MAIN(tests)
