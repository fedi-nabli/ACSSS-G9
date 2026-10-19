/* ===================================================================================================
 * tools/test/tests/self_fail.c
 *
 * Self test, every assertion with a false condition passed 
 *
 * Authors: Fedi Nabli
 * Date: 15 Oct 2026
 * Last Modified: 15 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>

#include <math.h>
#include <stdint.h>

MTS_TEST(fail_assert)
{
  MTS_ASSERT(1 == 2);
}

MTS_TEST(fail_eq)
{
  MTS_ASSERT_EQ(1, 2);
}

MTS_TEST(fail_ne)
{
  MTS_ASSERT_NE(3, 3);
}

MTS_TEST(fail_lt)
{
  MTS_ASSERT_LT(0, -1);
}

MTS_TEST(fail_ge)
{
  MTS_ASSERT_GE(-5, 5);
}

MTS_TEST(fail_near)
{
  MTS_ASSERT_NEAR(1.0, 1.1, 1e-3);
}

MTS_TEST(fail_near_nan)
{
  MTS_ASSERT_NEAR(NAN, 0.0, 1e9);
}

MTS_TEST(fail_mem_eq)
{
  const uint8_t a[4] = { 1, 2, 3, 4 };
  const uint8_t b[4] = { 1, 2, 3, 5 };
  MTS_ASSERT_MEM_EQ(a, b, sizeof(a));
}

MTS_TEST(fail_stops_at_first)
{
  MTS_ASSERT_EQ(0, 1);
  MTS_ASSERT(!"unreachable: tets did not return after the first failure");
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(fail_assert),
  MTS_TEST_ENTRY(fail_eq),
  MTS_TEST_ENTRY(fail_ne),
  MTS_TEST_ENTRY(fail_lt),
  MTS_TEST_ENTRY(fail_ge),
  MTS_TEST_ENTRY(fail_near),
  MTS_TEST_ENTRY(fail_near_nan),
  MTS_TEST_ENTRY(fail_mem_eq),
  MTS_TEST_ENTRY(fail_stops_at_first),
};

MTS_TEST_MAIN(tests)

