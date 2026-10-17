/* ===================================================================================================
 * utils/include/utils/time/time.h
 *
 * Monotinic time helpers
 *
 * Authors: Fedi Nabli
 * Date: 17 Oct 2026
 * Last Modified: 17 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_UTILS_TIME_TIME_H
#define MTS_UTILS_TIME_TIME_H

#include <time.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define MTS_TIME_NS_PER_US  INT64_C(1000)
#define MTS_TIME_NS_PER_MS  INT64_C(1000000)
#define MTS_TIME_NS_PER_S   INT64_C(1000000000)

// Current CLOCK_MONOTONIC time in nanoseconds
int64_t mts_time_now_ns(void);

// Sleep until mts_time_now_ns() >= deadline_ns
void mts_time_sleep_until_ns(int64_t deadline_ns);

// Sleep for at least duration_ns. Zero or negative return at once
void mts_time_sleep_ns(int64_t duration_ns);

struct timespec mts_time_to_timespec(int64_t ns);
int64_t mts_time_from_timespec(struct timespec ts);

#ifdef __cplusplus
}
#endif

#endif

