/* ===================================================================================================
 * tests/utils/mailbox/test_mailbox.c
 *
 * Tests for the latest-wins mailbox. The stress test is the one that
 * matters: run it under ./build.sh --tsan as well.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <utils/logger/logger.h>
#include <utils/mailbox/mailbox.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>

// TSan slows everything 5-15x: shorter stress run under it
#if defined(__SANITIZE_THREAD__)
#define UNDER_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define UNDER_TSAN 1
#endif
#endif

#ifdef UNDER_TSAN
#define STRESS_WRITES 100000
#else
#define STRESS_WRITES 2000000
#endif

/* ---------------------------------------------------------------------------------------------------
 * Single thread
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(create_rejects_bad_sizes)
{
  // Expected errors: keep them out of the test output
  FILE* sink = tmpfile();
  mts_log_set_stream(sink);

  Mailbox* zero = mb_create(0);
  Mailbox* four = mb_create(4);
  Mailbox* twelve = mb_create(12);
  Mailbox* huge = mb_create(SIZE_MAX - 7);

  mts_log_set_stream(NULL);
  fclose(sink);

  MTS_ASSERT(zero == NULL);
  MTS_ASSERT(four == NULL);
  MTS_ASSERT(twelve == NULL);
  MTS_ASSERT(huge == NULL);
}

MTS_TEST(create_accepts_multiples_of_8)
{
  Mailbox* a = mb_create(8);
  Mailbox* b = mb_create(800);
  bool ok = a != NULL && b != NULL;
  mb_destroy(a);
  mb_destroy(b);

  MTS_ASSERT(ok);
}

MTS_TEST(destroy_null_is_safe)
{
  mb_destroy(NULL);
}

MTS_TEST(read_before_write_returns_false)
{
  Mailbox* mb = mb_create(sizeof(uint64_t));
  MTS_ASSERT(mb != NULL);

  uint64_t out = 1234;
  bool got = mb_read(mb, &out);
  mb_destroy(mb);

  MTS_ASSERT(!got);
  MTS_ASSERT_EQ(out, 1234); // untouched
}

MTS_TEST(write_then_read_once)
{
  Mailbox* mb = mb_create(sizeof(uint64_t));
  MTS_ASSERT(mb != NULL);

  uint64_t in = 42;
  uint64_t out = 0;
  mb_write(mb, &in);

  bool first = mb_read(mb, &out);
  uint64_t first_value = out;
  out = 7;
  bool second = mb_read(mb, &out);
  mb_destroy(mb);

  MTS_ASSERT(first);
  MTS_ASSERT_EQ(first_value, 42);
  MTS_ASSERT(!second);   // nothing new
  MTS_ASSERT_EQ(out, 7); // untouched by the second read
}

MTS_TEST(latest_wins)
{
  Mailbox* mb = mb_create(sizeof(uint64_t));
  MTS_ASSERT(mb != NULL);

  for (uint64_t v = 1; v <= 3; v++)
  {
    mb_write(mb, &v);
  }

  uint64_t out = 0;
  bool got = mb_read(mb, &out);
  bool again = mb_read(mb, &out);
  mb_destroy(mb);

  MTS_ASSERT(got);
  MTS_ASSERT_EQ(out, 3); // 1 and 2 were overwritten before the read
  MTS_ASSERT(!again);
}

MTS_TEST(same_value_written_twice_is_new_twice)
{
  Mailbox* mb = mb_create(sizeof(uint64_t));
  MTS_ASSERT(mb != NULL);

  uint64_t v = 5;
  uint64_t out = 0;

  mb_write(mb, &v);
  bool first = mb_read(mb, &out);
  mb_write(mb, &v);
  bool second = mb_read(mb, &out);
  mb_destroy(mb);

  // "New" means a new write, not a different value
  MTS_ASSERT(first);
  MTS_ASSERT(second);
}

MTS_TEST(struct_roundtrip)
{
  // Shaped like Led_Command: mixed fields, padded to a multiple of 8
  typedef struct
  {
    uint32_t mode;
    float level;
    char text[16];
    uint32_t msg_id;
    uint32_t pad;
  } Cmd;
  _Static_assert(sizeof(Cmd) % 8 == 0, "Cmd must be a multiple of 8 bytes");

  Mailbox* mb = mb_create(sizeof(Cmd));
  MTS_ASSERT(mb != NULL);

  Cmd in;
  memset(&in, 0, sizeof(in));
  in.mode = 2;
  in.level = 0.75f;
  strcpy(in.text, "HOLD STILL");
  in.msg_id = 9;

  Cmd out;
  memset(&out, 0xFF, sizeof(out));
  mb_write(mb, &in);
  bool got = mb_read(mb, &out);
  mb_destroy(mb);

  MTS_ASSERT(got);
  MTS_ASSERT_MEM_EQ(&in, &out, sizeof(Cmd));
}

/* ---------------------------------------------------------------------------------------------------
 * Two threads. The reader runs in the test thread (assertions stay there),
 * the writer in a spawned thread.
 *
 * Each value is 512 bytes with the sequence number in all 64 words: a torn
 * read (part of one write, part of another) shows up as words that disagree.
 * A large payload widens the window in which a write can overlap a copy.
 * ------------------------------------------------------------------------------------------------- */

#define STRESS_WORDS 64

typedef struct Stress_Value
{
  uint64_t w[STRESS_WORDS];
} Stress_Value;

typedef struct Writer
{
  Mailbox* mb;
  uint64_t n;
} Writer;

static void* write_sequence(void* arg)
{
  Writer* wr = (Writer*)arg;
  Stress_Value v;

  // 1..n: 0 never appears, so "nothing read yet" is distinguishable
  for (uint64_t seq = 1; seq <= wr->n; seq++)
  {
    for (int i = 0; i < STRESS_WORDS; i++)
    {
      v.w[i] = seq;
    }
    mb_write(wr->mb, &v);
  }
  return NULL;
}

MTS_TEST(stress_reads_never_torn)
{
  Writer wr = { .mb = mb_create(sizeof(Stress_Value)), .n = STRESS_WRITES };
  MTS_ASSERT(wr.mb != NULL);

  pthread_t thread;
  MTS_ASSERT_EQ(pthread_create(&thread, NULL, write_sequence, &wr), 0);

  uint64_t last = 0;
  uint64_t reads = 0;
  bool whole = true;
  bool increasing = true;
  Stress_Value v;

  // Until the final value is seen: the writer always finishes, and the
  // last write stays in the slot, so this terminates
  while (last != STRESS_WRITES)
  {
    if (!mb_read(wr.mb, &v))
    {
      continue;
    }

    for (int i = 1; i < STRESS_WORDS; i++)
    {
      whole = whole && v.w[i] == v.w[0];
    }
    increasing = increasing && v.w[0] > last;
    last = v.w[0];
    reads++;

    if (!whole || !increasing)
    {
      break;
    }
  }

  pthread_join(thread, NULL);
  mb_destroy(wr.mb);

  MTS_ASSERT(whole);
  MTS_ASSERT(increasing);
  MTS_ASSERT_EQ(last, STRESS_WRITES);
  MTS_ASSERT_GT(reads, 1); // the reader really ran alongside the writer
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(create_rejects_bad_sizes),
  MTS_TEST_ENTRY(create_accepts_multiples_of_8),
  MTS_TEST_ENTRY(destroy_null_is_safe),
  MTS_TEST_ENTRY(read_before_write_returns_false),
  MTS_TEST_ENTRY(write_then_read_once),
  MTS_TEST_ENTRY(latest_wins),
  MTS_TEST_ENTRY(same_value_written_twice_is_new_twice),
  MTS_TEST_ENTRY(struct_roundtrip),
  MTS_TEST_ENTRY(stress_reads_never_torn),
};

MTS_TEST_MAIN(tests)

