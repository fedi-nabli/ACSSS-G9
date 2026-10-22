/* ===================================================================================================
 * tests/utils/time/time_tests.c
 *
 * Tests for utils/time. Sleep tests only assert lower bound
 *
 * Authors: Fedi Nabli
 * Date: 17 Oct 2026
 * Last Modified: 17 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <stdint.h>
#include <utils/time/time.h>

#define IMMEDIATE_NS (50 * MTS_TIME_NS_PER_MS)

MTS_TEST(constants)
{
  MTS_ASSERT_EQ(MTS_TIME_NS_PER_US, 1000);
  MTS_ASSERT_EQ(MTS_TIME_NS_PER_MS, 1000 * MTS_TIME_NS_PER_US);
  MTS_ASSERT_EQ(MTS_TIME_NS_PER_S, 1000 * MTS_TIME_NS_PER_MS);
}

MTS_TEST(now_advances_loop)
{
  int64_t prev = mts_time_now_ns();
  for (int i = 0; i < 1000000; i++)
  {
    int64_t time = mts_time_now_ns();
    MTS_ASSERT_GE(time, prev);
    prev = time;
  }
}

MTS_TEST(now_advances)
{
  int64_t start = mts_time_now_ns();
  mts_time_sleep_ns(MTS_TIME_NS_PER_MS);
  MTS_ASSERT_GE(mts_time_now_ns() - start, MTS_TIME_NS_PER_MS);
}

MTS_TEST(sleep_until_on_time)
{
  for (int i = 0; i < 20; i++)
  {
    int64_t deadline = mts_time_now_ns() + MTS_TIME_NS_PER_MS;
    mts_time_sleep_until_ns(deadline);
    MTS_ASSERT_GE(mts_time_now_ns(), deadline);
  }
}

MTS_TEST(sleep_until_past_returns_at_once)
{
  int64_t start = mts_time_now_ns();
  mts_time_sleep_until_ns(start - MTS_TIME_NS_PER_S);
  MTS_ASSERT_LT(mts_time_now_ns() - start, IMMEDIATE_NS);
}

MTS_TEST(sleep_until_negative_returns_at_once)
{
  int64_t start = mts_time_now_ns();
  mts_time_sleep_until_ns(-5);
  MTS_ASSERT_LT(mts_time_now_ns() - start, IMMEDIATE_NS);
}

MTS_TEST(sleep_ns_lower_bound)
{
  int64_t duration = 5 * MTS_TIME_NS_PER_MS;
  int64_t start = mts_time_now_ns();
  mts_time_sleep_ns(duration);
  MTS_ASSERT_GE(mts_time_now_ns() - start, duration);
}

MTS_TEST(sleep_ns_zero_and_negative_return)
{
  int64_t start = mts_time_now_ns();
  mts_time_sleep_ns(0);
  mts_time_sleep_ns(-MTS_TIME_NS_PER_S);
  MTS_ASSERT_LT(mts_time_now_ns() - start, IMMEDIATE_NS);
}

MTS_TEST(to_timespec_normalization)
{
  struct timespec ts = mts_time_to_timespec(1500000000);
  MTS_ASSERT_EQ(ts.tv_sec, 1);
  MTS_ASSERT_EQ(ts.tv_nsec, 500000000);

  ts = mts_time_to_timespec(MTS_TIME_NS_PER_S);
  MTS_ASSERT_EQ(ts.tv_sec, 1);
  MTS_ASSERT_EQ(ts.tv_nsec, 0);

  ts = mts_time_to_timespec(999999999);
  MTS_ASSERT_EQ(ts.tv_sec, 0);
  MTS_ASSERT_EQ(ts.tv_nsec, 999999999);
  
  ts = mts_time_to_timespec(-1);
  MTS_ASSERT_EQ(ts.tv_sec, -1);
  MTS_ASSERT_EQ(ts.tv_nsec, 999999999);

  ts = mts_time_to_timespec(-MTS_TIME_NS_PER_S);
  MTS_ASSERT_EQ(ts.tv_sec, -1);
  MTS_ASSERT_EQ(ts.tv_nsec, 0);
}

MTS_TEST(timespec_roundtrip)
{
  const int64_t values[] = {
    0, 1, 999999999, MTS_TIME_NS_PER_S, MTS_TIME_NS_PER_S + 1,
    -1, -999999999, -MTS_TIME_NS_PER_S - 1,
    INT64_C(123456789012345678),
  };

  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++)
  {
    struct timespec ts = mts_time_to_timespec(values[i]);
    MTS_ASSERT_GE(ts.tv_nsec, 0);
    MTS_ASSERT_LT(ts.tv_nsec, MTS_TIME_NS_PER_S);
    MTS_ASSERT_EQ(mts_time_from_timespec(ts), values[i]);
  }
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(constants),
  MTS_TEST_ENTRY(now_advances_loop),
  MTS_TEST_ENTRY(now_advances),
  MTS_TEST_ENTRY(sleep_until_on_time),
  MTS_TEST_ENTRY(sleep_until_past_returns_at_once),
  MTS_TEST_ENTRY(sleep_until_negative_returns_at_once),
  MTS_TEST_ENTRY(sleep_ns_lower_bound),
  MTS_TEST_ENTRY(sleep_ns_zero_and_negative_return),
  MTS_TEST_ENTRY(to_timespec_normalization),
  MTS_TEST_ENTRY(timespec_roundtrip),
};

MTS_TEST_MAIN(tests)

