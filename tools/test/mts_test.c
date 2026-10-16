/* ===================================================================================================
 * tools/test/mts_test.c
 *
 * Test runner for mts_test: the failure flag and mts_test_run()
 *
 * Authors: Fedi Nabli
 * Date: 15 Oct 2026
 * Last Modified: 15 Oct 2026
 * ===================================================================================================
 */

#include "mts_test.h"

#include <stdio.h>
#include <string.h>

int mts_test_failed = 0;

int mts_test_run(const Mts_Test* tests, size_t count, int argc, char** argv)
{
  const char* filter = (argc > 1) ? argv[1] : NULL;
  int passed = 0;
  int failed = 0;
  int skipped = 0;

  printf("\n");

  for (size_t i = 0; i < count; i++)
  {
    const Mts_Test* test = &tests[i];

    if (filter && !strstr(test->name, filter))
    {
      skipped++;
      continue;
    }

    // Name first and flushed
    printf("  %-50s ", test->name);
    fflush(stdout);

    mts_test_failed = 0;
    test->fn();

    if (mts_test_failed)
    {
      printf("  %-50s FAIL\n", test->name);
      failed++;
    }
    else
    {
      printf("PASS\n");
      passed++;
    }
  }

  printf("\n  Results: %d passed, %d failed", passed, failed);
  if (skipped > 0)
  {
    printf(", %d skipped (filter: '%s')", skipped, filter);
  }
  printf(", %d total\n\n", passed + failed);

  if (passed + failed == 0)
  {
    printf("  No test matched\n\n");
    return 1;
  }

  return (failed > 0) ? 1 : 0;
}

