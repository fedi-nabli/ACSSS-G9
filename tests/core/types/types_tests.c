/* ===================================================================================================
 * tests/core/types/types_tests.c
 *
 * Tests for core/types from C. The layout itself is checked at compile
 * time by the header; these check the flags and that a sample survives
 * the queue it will travel through.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <core/types/raw_sample.h>
#include <utils/ring_buffer/ring_buffer.h>

#include <string.h>

MTS_TEST(flags_are_distinct_single_bits)
{
  const uint32_t flags[] = { SAMPLE_MAG_FRESH, SAMPLE_GAP, SAMPLE_SATURATED };
  const size_t n = sizeof(flags) / sizeof(flags[0]);

  for (size_t i = 0; i < n; i++)
  {
    // Exactly one bit set
    MTS_ASSERT_NE(flags[i], 0);
    MTS_ASSERT_EQ(flags[i] & (flags[i] - 1), 0);

    for (size_t j = i + 1; j < n; j++)
    {
      MTS_ASSERT_EQ(flags[i] & flags[j], 0);
    }
  }
}

MTS_TEST(zero_initialized_has_no_flags)
{
  Raw_Sample s = { 0 };
  MTS_ASSERT_EQ(s.flags, 0);
  MTS_ASSERT_EQ(s.seq, 0);
  MTS_ASSERT_EQ(s.t_ns, 0);
}

MTS_TEST(flags_set_and_clear)
{
  Raw_Sample s = { 0 };
  s.flags |= SAMPLE_GAP | SAMPLE_MAG_FRESH;
  MTS_ASSERT(s.flags & SAMPLE_GAP);
  MTS_ASSERT(s.flags & SAMPLE_MAG_FRESH);
  MTS_ASSERT(!(s.flags & SAMPLE_SATURATED));

  // Unsigned constants: ~ on them stays a uint32_t, no sign mixing
  s.flags &= ~SAMPLE_GAP;
  MTS_ASSERT(!(s.flags & SAMPLE_GAP));
  MTS_ASSERT(s.flags & SAMPLE_MAG_FRESH);
}

MTS_TEST(survives_ring_buffer)
{
  Ring_Buffer* rb = rb_create(4, sizeof(Raw_Sample));
  MTS_ASSERT(rb != NULL);

  Raw_Sample in;
  memset(&in, 0, sizeof(in));
  in.seq = 123456789012ULL;
  in.t_ns = -42;
  in.gyro[0] = 0.01f;
  in.gyro[1] = -0.02f;
  in.gyro[2] = 0.03f;
  in.accel[0] = 0.1f;
  in.accel[1] = 0.2f;
  in.accel[2] = 9.81f;
  in.mag[0] = 20.0f;
  in.mag[1] = -5.0f;
  in.mag[2] = 45.0f;
  in.flags = SAMPLE_MAG_FRESH | SAMPLE_SATURATED;

  Raw_Sample out;
  memset(&out, 0xFF, sizeof(out));
  bool pushed = rb_push(rb, &in);
  bool popped = rb_pop(rb, &out, 0);
  rb_destroy(rb);

  MTS_ASSERT(pushed);
  MTS_ASSERT(popped);
  MTS_ASSERT_MEM_EQ(&in, &out, sizeof(Raw_Sample));
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(flags_are_distinct_single_bits),
  MTS_TEST_ENTRY(zero_initialized_has_no_flags),
  MTS_TEST_ENTRY(flags_set_and_clear),
  MTS_TEST_ENTRY(survives_ring_buffer),
};

MTS_TEST_MAIN(tests)

