/* ===================================================================================================
 * tests/utils/mailbox/test_mailbox_cpp.cc
 *
 * Tests for mts::Mailbox<T>. Thin on purpose: the C tests cover the
 * seqlock itself, these cover the type, the ownership and the moves.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <utils/mailbox/mailbox.hpp>

#include <thread>
#include <cstdint>
#include <cstring>
#include <utility>

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

// Shaped like Led_Command, padded to a multiple of 8.
// Without `pad` it is 28 bytes and Mailbox<Led> does not compile.
struct Led
{
  uint32_t mode;
  float level;
  char text[16];
  uint32_t msg_id;
  uint32_t pad;
};

MTS_TEST(valid_after_construction)
{
  mts::Mailbox<Led> mb;
  MTS_ASSERT(mb.valid());
}

MTS_TEST(read_before_write_returns_false)
{
  mts::Mailbox<uint64_t> mb;
  uint64_t out = 1234;
  MTS_ASSERT(!mb.read(out));
  MTS_ASSERT_EQ(out, 1234);
}

MTS_TEST(typed_write_read)
{
  mts::Mailbox<Led> mb;

  Led in{};
  in.mode = 1;
  in.level = 0.25f;
  std::strcpy(in.text, "HOLD STILL");
  in.msg_id = 3;
  mb.write(in);

  Led out{};
  MTS_ASSERT(mb.read(out));
  MTS_ASSERT_MEM_EQ(&in, &out, sizeof(Led));
  MTS_ASSERT(!mb.read(out));
}

MTS_TEST(latest_wins)
{
  mts::Mailbox<uint64_t> mb;
  for (uint64_t v = 1; v <= 5; v++)
  {
    mb.write(v);
  }

  uint64_t out = 0;
  MTS_ASSERT(mb.read(out));
  MTS_ASSERT_EQ(out, 5);
}

MTS_TEST(move_transfers_ownership)
{
  mts::Mailbox<uint64_t> a;
  uint64_t v = 8;
  a.write(v);

  mts::Mailbox<uint64_t> b(std::move(a));
  MTS_ASSERT(!a.valid());
  MTS_ASSERT(b.valid());

  mts::Mailbox<uint64_t> c;
  c = std::move(b); // c's own slot is freed here
  MTS_ASSERT(!b.valid());

  uint64_t out = 0;
  MTS_ASSERT(c.read(out));
  MTS_ASSERT_EQ(out, 8);
}

struct Wide
{
  uint64_t w[8];
};

MTS_TEST(two_threads_typed)
{
  mts::Mailbox<Wide> mb;
  MTS_ASSERT(mb.valid());

  std::thread writer([&mb] {
    Wide v{};
    for (uint64_t seq = 1; seq <= TYPED_N; seq++)
    {
      for (uint64_t& word : v.w)
      {
        word = seq;
      }
      mb.write(v);
    }
  });

  uint64_t last = 0;
  bool whole = true;
  bool increasing = true;
  while (last != TYPED_N && whole && increasing)
  {
    Wide v{};
    if (!mb.read(v))
    {
      continue;
    }
    for (uint64_t word : v.w)
    {
      whole = whole && word == v.w[0];
    }
    increasing = increasing && v.w[0] > last;
    last = v.w[0];
  }

  writer.join();

  MTS_ASSERT(whole);
  MTS_ASSERT(increasing);
  MTS_ASSERT_EQ(last, TYPED_N);
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(valid_after_construction),
  MTS_TEST_ENTRY(read_before_write_returns_false),
  MTS_TEST_ENTRY(typed_write_read),
  MTS_TEST_ENTRY(latest_wins),
  MTS_TEST_ENTRY(move_transfers_ownership),
  MTS_TEST_ENTRY(two_threads_typed),
};

MTS_TEST_MAIN(tests)
