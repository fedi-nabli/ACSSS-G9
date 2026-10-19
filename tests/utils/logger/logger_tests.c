/* ===================================================================================================
 * tests/utils/logger/logger_tests.c
 *
 * Tests for utils/logger
 *
 * Authors: Fedi Nabli
 * Date: 19 Oct 2026
 * Last Modified: 19 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <utils/time/time.h>
#include <utils/logger/logger.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

static FILE* g_capture = NULL;

static FILE* capture_begin(void)
{
  if (g_capture)
  {
    fclose(g_capture);
  }

  g_capture = tmpfile();
  mts_log_set_stream(g_capture);
  mts_log_set_min_level(LEVEL_INFO);
  return g_capture;
}

static size_t capture_read(char* out, size_t size)
{
  fflush(g_capture);
  rewind(g_capture);
  size_t n = fread(out, 1, size - 1, g_capture);
  out[n] = '\0';
  return n;
}

static size_t count_char(const char* str, char c)
{
  size_t n = 0;
  for (; *str; str++)
  {
    if (*str == c)
    {
      n++;
    }
  }

  return n;
}

static int64_t parse_prefix(const char* line, const char** rest)
{
  if (line[0] != '[')
  {
    return -1;
  }

  char* end;
  long long sec = strtoll(line + 1, &end, 10);
  if (*end != '.')
  {
    return -1;
  }

  const char* usec_start = end + 1;
  long long usec = strtoll(usec_start, &end, 10);
  if (end - usec_start != 6 || end[0] != ']' || end[1] != ' ')
  {
    return -1;
  }

  *rest = end + 2;
  return (int64_t)sec * MTS_TIME_NS_PER_S + (int64_t)usec * MTS_TIME_NS_PER_US;
}

/* static size_t prefix_length(void)
{
  char buf[1024];
  capture_begin();
  MTS_LOG_INFO("%s", "");
  capture_read(buf, sizeof(buf));
  return strlen(buf) - 1;
} */

MTS_TEST(default_level_is_info)
{
  capture_begin();
  MTS_ASSERT_EQ(mts_log_get_level(), LEVEL_INFO);
  MTS_ASSERT(!mts_log_enabled(LEVEL_DEBUG));
  MTS_ASSERT(mts_log_enabled(LEVEL_INFO));
  MTS_ASSERT(mts_log_enabled(LEVEL_ERROR));
}

MTS_TEST(line_format)
{
  char buf[1024];
  capture_begin();

  int64_t before = mts_time_now_ns();
  MTS_LOG_INFO("x=%d", 42);
  capture_read(buf, sizeof(buf));

  const char* rest;
  int64_t time_pre = parse_prefix(buf, &rest);
  MTS_ASSERT_GE(time_pre, 0);
  MTS_ASSERT(strcmp(rest, "INFO  x=42\n") == 0);

  MTS_ASSERT_GE(time_pre, before - MTS_TIME_NS_PER_US);
  MTS_ASSERT_LT(time_pre - before, MTS_TIME_NS_PER_S);
}

MTS_TEST(level_names_padded)
{
  char buf[1024];
  capture_begin();
  mts_log_set_min_level(LEVEL_DEBUG);

  MTS_LOG_DEBUG("d");
  MTS_LOG_INFO("i");
  MTS_LOG_WARN("w");
  MTS_LOG_ERROR("e");
  capture_read(buf, sizeof(buf));

  MTS_ASSERT(strstr(buf, "] DEBUG d\n") != NULL);
  MTS_ASSERT(strstr(buf, "] INFO  i\n") != NULL);
  MTS_ASSERT(strstr(buf, "] WARN  w\n") != NULL);
  MTS_ASSERT(strstr(buf, "] ERROR e\n") != NULL);
}

MTS_TEST(filtered_levels_no_write)
{
  char buf[1024];
  capture_begin();
  mts_log_set_min_level(LEVEL_WARN);
  MTS_ASSERT_EQ(mts_log_get_level(), LEVEL_WARN);
  
  MTS_LOG_DEBUG("nej");
  MTS_LOG_INFO("nej");
  MTS_LOG_WARN("ja");
  MTS_LOG_ERROR("ja");
  capture_read(buf, sizeof(buf));

  MTS_ASSERT_EQ(count_char(buf, '\n'), 2);
  MTS_ASSERT(strstr(buf, "nej") == NULL);
}

MTS_TEST(filtered_args_not_evaluated)
{
  capture_begin();
  int counter = 0;

  MTS_LOG_DEBUG("%d", counter++);
  MTS_ASSERT_EQ(counter, 0);

  mts_log_set_min_level(LEVEL_DEBUG);
  MTS_LOG_DEBUG("%d", counter++);
  MTS_ASSERT_EQ(counter, 1);
}

#define THREADS 4
#define LINES_PER_THREAD 1000

static void* log_from_thread(void* arg)
{
  int id = *(const int*)arg;
  for (int i = 0; i < LINES_PER_THREAD; i++)
  {
    MTS_LOG_INFO("thread %d line %d", id, i);
  }

  return NULL;
}

MTS_TEST(threads_never_interleave)
{
  capture_begin();

  pthread_t threads[THREADS];
  int ids[THREADS];

  for (int i = 0; i < THREADS; i++)
  {
    ids[i] = i;
    MTS_ASSERT_EQ(pthread_create(&threads[i], NULL, log_from_thread, &ids[i]), 0);
  }

  for (int i = 0; i < THREADS; i++)
  {
    pthread_join(threads[i], NULL);
  }

  fflush(g_capture);
  rewind(g_capture);

  int per_thread[THREADS] = { 0 };
  int total = 0;
  char line[1024];
  
  while (fgets(line, sizeof(line), g_capture))
  {
    const char* rest;
    int id = -1;
    int index = -1;
    char tail = 0;

    MTS_ASSERT_GE(parse_prefix(line, &rest), 0);
    MTS_ASSERT_EQ(sscanf(rest, "INFO  thread %d line %d%c", &id, &index, &tail), 3);
    MTS_ASSERT_EQ(tail, '\n');
    MTS_ASSERT(id >= 0 && id <= THREADS);

    per_thread[id]++;
    total++;
  }

  MTS_ASSERT_EQ(total, THREADS * LINES_PER_THREAD);
  for (int i = 0; i < THREADS; i++)
  {
    MTS_ASSERT_EQ(per_thread[i], LINES_PER_THREAD);
  }
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(default_level_is_info),
  MTS_TEST_ENTRY(line_format),
  MTS_TEST_ENTRY(level_names_padded),
  MTS_TEST_ENTRY(filtered_levels_no_write),
  MTS_TEST_ENTRY(filtered_args_not_evaluated),
  MTS_TEST_ENTRY(threads_never_interleave),
};


MTS_TEST_MAIN(tests)

