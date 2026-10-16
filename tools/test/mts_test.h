/* ===================================================================================================
 * tools/test/mts_test.h
 *
 * Minimal test framework for MTS unit tests
 *
 * Registration is an explicit table
 *
 * Usage:
 *  #include <mts_test.h>
 *
 *  MTS_TEST(push_then_pop) {
 *    MTS_ASSERT(rb_push(rb, &x));
 *    MTS_ASSERT_EQ(rb_count(rb), 1);
 *  }
 *
 *  static const Mts_Tests tests[] = {
 *    MTS_TEST_ENTRY(push_then_pop);
 *  };
 *
 *  MTS_TEST_MAIN(tests)
 *
 * Run: ./mts_utils_ring_buffer_tests         (all tests)
 *      ./mts_utils_ring_buffer_tests push    (only tests whose name contains "push")
 *
 * Authors: Fedi Nabli
 * Date: 15 Oct 2026
 * Last Modified: 15 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_TOOLS_TEST_MTS_TEST_H
#define MTS_TOOLS_TEST_MTS_TEST_H

#include <stdio.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef void (*Mts_Test_Fn)(void);

typedef struct Mts_Test
{
  const char* name;
  Mts_Test_Fn fn;
} Mts_Test;

// Set by a failing assertion, reset by the runner before each test
extern int mts_test_failed;

// Run the table, prints PASS/FAIL per test and summary
// argv[1] if given is a substring filter on test names
// Returns 0 if every test that ran passed, 1 otherwise (or if nothing matched)
int mts_test_run(const Mts_Test* tests, size_t count, int argc, char** argv);

#ifdef __cplusplus
}
#endif

#define MTS_TEST(name) static void mts_test_##name(void)

#define MTS_TEST_ENTRY(name) { #name, mts_test_##name }

#define MTS_TEST_MAIN(table)                                                      \
  int main(int argc, char** argv)                                                 \
  {                                                                               \
    return mts_test_run(table, sizeof(table) / sizeof((table)[0]), argc, argv);   \
  }

#define MTS_TEST_FAIL_(...)                           \
  do                                                  \
  {                                                   \
    printf("\n    FAIL: ");                           \
    printf(__VA_ARGS__);                              \
    printf("\n      at %s:%d\n", __FILE__, __LINE__); \
    mts_test_failed = 1;                              \
    return;                                           \
  } while (0);

#define MTS_ASSERT(expr)            \
  do                                \
  {                                 \
    if (!(expr))                    \
    {                               \
      MTS_TEST_FAIL_("%s", #expr);  \
    }                               \
    } while (0)

#define MTS_ASSERT_CMP_(a, op, b)                                                         \
  do                                                                                      \
  {                                                                                       \
    long long mts_test_a_ = (long long)(a);                                               \
    long long mts_test_b_ = (long long)(b);                                               \
    if (!(mts_test_a_ op mts_test_b_))                                                    \
    {                                                                                     \
      MTS_TEST_FAIL_("%s %s %s\n      left: %lld (0x%llx)\n      right: %lld (0x%llx)",   \
                      #a, #op, #b,                                                        \
                      mts_test_a_, (unsigned long long)mts_test_a_,                       \
                      mts_test_b_, (unsigned long long)mts_test_b_);                      \
    }                                                                                     \
  } while (0)

#define MTS_ASSERT_EQ(a, b) MTS_ASSERT_CMP_(a, ==, b)
#define MTS_ASSERT_NE(a, b) MTS_ASSERT_CMP_(a, !=, b)
#define MTS_ASSERT_LT(a, b) MTS_ASSERT_CMP_(a, <, b)
#define MTS_ASSERT_LE(a, b) MTS_ASSERT_CMP_(a, <=, b)
#define MTS_ASSERT_GT(a, b) MTS_ASSERT_CMP_(a, >, b)
#define MTS_ASSERT_GE(a, b) MTS_ASSERT_CMP_(a, >=, b)

#define MTS_ASSERT_NEAR(a, b, eps)                                                                \
  do                                                                                              \
  {                                                                                               \
    double mts_test_a_ = (double)(a);                                                             \
    double mts_test_b_ = (double)(b);                                                             \
    double mts_test_d_ = mts_test_a_ - mts_test_b_;                                               \
    if (mts_test_d_ < 0.0)                                                                        \
    {                                                                                             \
      mts_test_d_ = -mts_test_d_;                                                                 \
    }                                                                                             \
    if (!(mts_test_d_ <= (double)(eps)))                                                          \
    {                                                                                             \
      MTS_TEST_FAIL_("|%s - %s| <= %s\n      left: %.17g\n      right: %.17g\n      diff: %.17g", \
                      #a, #b, #eps, mts_test_a_, mts_test_b_, mts_test_d_);                       \
    }                                                                                             \
  } while (0)

#define MTS_ASSERT_MEM_EQ(a, b, n)                                  \
  do                                                                \
  {                                                                 \
    if (memcmp((a), (b), (n)) != 0)                                 \
    {                                                               \
      MTS_TEST_FAIL_("%s == %s (%zu bytes)", #a, #b, (size_t)(n));  \
    }                                                               \
  } while (0)

#endif

