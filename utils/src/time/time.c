/* ===================================================================================================
 * utils/src/time/time.c
 *
 * Monotinic time helpers
 *
 * Authors: Fedi Nabli
 * Date: 17 Oct 2026
 * Last Modified: 17 Oct 2026
 * ===================================================================================================
 */

#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#include <utils/time/time.h>

#include <time.h>
#include <errno.h>

int64_t mts_time_now_ns(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return mts_time_from_timespec(ts);
}

void mts_time_sleep_until_ns(int64_t deadline_ns)
{
  if (deadline_ns <= mts_time_now_ns())
  {
    return;
  }

#if defined(__APPLE__)
  for (;;)
  {
    int64_t remaining = deadline_ns - mts_time_now_ns();
    if (remaining <= 0)
    {
      return;
    }

    struct timespec ts = mts_time_to_timespec(remaining);
    nanosleep(&ts, NULL);
  }
#else
  struct timespec ts = mts_time_to_timespec(deadline_ns);
  while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR)
  {}
#endif
}

void mts_time_sleep_ns(int64_t duration_ns)
{
  if (duration_ns <= 0)
  {
    return;
  }

  mts_time_sleep_until_ns(mts_time_now_ns() + duration_ns);
}

struct timespec mts_time_to_timespec(int64_t ns)
{
  int64_t sec = ns / MTS_TIME_NS_PER_S;
  int64_t remaining = ns % MTS_TIME_NS_PER_S;
  if (remaining < 0)
  {
    remaining += MTS_TIME_NS_PER_S;
    sec -= 1;
  }

  struct timespec ts;
  ts.tv_sec = (time_t)sec;
  ts.tv_nsec = (long)remaining;
  return ts;
}

int64_t mts_time_from_timespec(struct timespec ts)
{
  return (int64_t)ts.tv_sec * MTS_TIME_NS_PER_S + (int64_t)ts.tv_nsec;
}

