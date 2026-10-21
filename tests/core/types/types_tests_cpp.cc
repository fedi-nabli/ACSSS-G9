/* ===================================================================================================
 * tests/core/types/types_tests_cpp.cc
 *
 * Tests for core/types from C++. The type traits are the point: Queue<T>
 * needs trivially copyable, and standard layout guarantees C and C++ see
 * the same struct. Both are checked at compile time.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <core/types/raw_sample.h>
#include <utils/ring_buffer/queue.hpp>

#include <cstring>
#include <type_traits>

static_assert(std::is_trivially_copyable_v<Raw_Sample>, "Raw_Sample must be trivially copyable (Queue<T>, memcpy)");
static_assert(std::is_standard_layout_v<Raw_Sample>, "Raw_Sample must be standard layout (same struct in C and C++)");

MTS_TEST(aggregate_init_from_cpp)
{
  Raw_Sample s{ 7, 1000, { 0.0f, 0.0f, 0.1f }, { 0.0f, 0.0f, 9.81f }, { 20.0f, 0.0f, 45.0f }, SAMPLE_MAG_FRESH };

  MTS_ASSERT_EQ(s.seq, 7);
  MTS_ASSERT_EQ(s.t_ns, 1000);
  MTS_ASSERT_NEAR(s.accel[2], 9.81, 1e-6);
  MTS_ASSERT_EQ(s.flags, SAMPLE_MAG_FRESH);
}

MTS_TEST(survives_typed_queue)
{
  mts::Queue<Raw_Sample> q(4);
  MTS_ASSERT(q.valid());

  Raw_Sample in{ 99, -5, { 1.0f, 2.0f, 3.0f }, { 4.0f, 5.0f, 6.0f }, { 7.0f, 8.0f, 9.0f }, SAMPLE_GAP };
  Raw_Sample out{};

  MTS_ASSERT(q.try_push(in));
  MTS_ASSERT(q.pop(out));
  MTS_ASSERT(std::memcmp(&in, &out, sizeof(Raw_Sample)) == 0);
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(aggregate_init_from_cpp),
  MTS_TEST_ENTRY(survives_typed_queue),
};

MTS_TEST_MAIN(tests)

