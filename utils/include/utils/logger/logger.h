/* ===================================================================================================
 * utils/include/utils/logger/logger.h
 *
 * Authors: Fedi Nabli
 * Date: 19 Oct 2026
 * Last Modified: 19 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_UTILS_LOGGER_LOGGER_H
#define MTS_UTILS_LOGGER_LOGGER_H

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum Mts_Log_Level
{
  LEVEL_DEBUG,
  LEVEL_INFO,
  LEVEL_WARN,
  LEVEL_ERROR,
} Mts_Log_Level;

void mts_log_set_min_level(Mts_Log_Level min_level);
Mts_Log_Level mts_log_get_level(void);

bool mts_log_enabled(Mts_Log_Level level);

// Where logging goes
void mts_log_set_stream(FILE* stream);

void mts_log_at(Mts_Log_Level level, const char* file, int line, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
void mts_vlog_at(Mts_Log_Level level, const char* file, int line, const char* fmt, va_list args) __attribute__((format(printf, 4, 0)));

#define MTS_LOG_AT_(level, ...)                           \
  do                                                      \
  {                                                       \
    if (mts_log_enabled(level))                           \
    {                                                     \
      mts_log_at(level, __FILE__, __LINE__, __VA_ARGS__); \
    }                                                     \
  } while (0)

#define MTS_LOG_DEBUG(...)  MTS_LOG_AT_(LEVEL_DEBUG, __VA_ARGS__)
#define MTS_LOG_INFO(...)  MTS_LOG_AT_(LEVEL_INFO, __VA_ARGS__)
#define MTS_LOG_WARN(...)  MTS_LOG_AT_(LEVEL_WARN, __VA_ARGS__)
#define MTS_LOG_ERROR(...)  MTS_LOG_AT_(LEVEL_ERROR, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif

