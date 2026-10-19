/* ===================================================================================================
 * tools/test/tests/self_pass.c
 *
 * Self test, every assertion with a true condition passed and a failure
 * returns from the current function only
 *
 * Authors: Fedi Nabli
 * Date: 15 Oct 2026
 * Last Modified: 15 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>

#include <stdint.h>

MTS_TEST(assert_true)
{
  MTS_ASSERT(1 + 1 == 2);
}

MTS_TEST(integer_comparison)
{
  MTS_ASSERT_EQ(42, 42);
  MTS_ASSERT_NE(1, 2);
  MTS_ASSERT_LT(-1, 0);
  MTS_ASSERT_LE(3, 3);
  MTS_ASSERT_GT(5, 4);
  MTS_ASSERT_GE(5, 5);
  MTS_ASSERT_EQ((uint64_t)1 << 40, (int64_t)1 << 40);
  MTS_ASSERT_EQ(sizeof(uint64_t), 8);
}

MTS_TEST(near)
{
  MTS_ASSERT_NEAR(0.1 + 0.2, 0.3, 1e-12);
  MTS_ASSERT_NEAR(-1.0, -1.0 - 1e-9, 1e-6);
}

MTS_TEST(mem_eq)
{
  const uint8_t a[4] = { 1, 2, 3, 4 };
  const uint8_t b[4] = { 1, 2, 3, 4 };
  MTS_ASSERT_MEM_EQ(a, b, sizeof(a));
}

MTS_TEST(each_side_evaluated_once)
{
  int calls = 0;
  MTS_ASSERT_EQ(++calls, 1);
  MTS_ASSERT_EQ(calls, 1);
}

// A failing assertion inside a helper returns from the helper only
static int reached_after_failure = 0;

static void failing_helper(void)
{
  MTS_ASSERT_EQ(1, 2);
  reached_after_failure = 1;
}

MTS_TEST(failure_returns_from_current_function)
{
  failing_helper();
  int failed = mts_test_failed;
  mts_test_failed = 0; // Swallow the expected failure so this test passes
  MTS_ASSERT_EQ(failed, 1);
  MTS_ASSERT_EQ(reached_after_failure, 0);
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(assert_true),
  MTS_TEST_ENTRY(integer_comparison),
  MTS_TEST_ENTRY(near),
  MTS_TEST_ENTRY(mem_eq),
  MTS_TEST_ENTRY(each_side_evaluated_once),
  MTS_TEST_ENTRY(failure_returns_from_current_function),
};

MTS_TEST_MAIN(tests)

