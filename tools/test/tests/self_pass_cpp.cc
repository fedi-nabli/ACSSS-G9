/* ===================================================================================================
 * tools/test/tests/self_pass_cpp.cc
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

#include <array>
#include <cstdint>

MTS_TEST(cpp_comparisons)
{
  std::array<int, 3> v{ { 1, 2, 3 } };
  MTS_ASSERT(v.size() == 3);
  MTS_ASSERT_EQ(v[2], 3);
  MTS_ASSERT_LT(v[0], v[1]);
  MTS_ASSERT_NEAR(1.0 / 3.0, 0.333333333, 1e-6);
}

MTS_TEST(cpp_mem_eq)
{
  const std::uint8_t a[2] = { 7, 8 };
  const std::uint8_t b[2] = { 7, 8 };
  MTS_ASSERT_MEM_EQ(a, b, sizeof(a));
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(cpp_comparisons),
  MTS_TEST_ENTRY(cpp_mem_eq),
};

MTS_TEST_MAIN(tests)

